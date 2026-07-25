#!/usr/bin/env python3
"""Build offline Gen 1–6 species JSON from PokeAPI CSVs (BSD-3 data)."""
from __future__ import annotations

import csv
import io
import json
import os
import urllib.request
from collections import defaultdict

BASE = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "overlay", "azahar", "assets", "pokedex", "species.json")


def fetch(name: str) -> list[dict[str, str]]:
    print("fetch", name)
    data = urllib.request.urlopen(BASE + name, timeout=90).read().decode("utf-8")
    return list(csv.DictReader(io.StringIO(data)))


def main() -> None:
    names = fetch("pokemon_species_names.csv")
    en_names = {
        int(r["pokemon_species_id"]): r["name"]
        for r in names
        if r["local_language_id"] == "9" and int(r["pokemon_species_id"]) <= 721
    }

    stats_rows = fetch("pokemon_stats.csv")
    stat_map = {1: "hp", 2: "atk", 3: "def", 4: "spa", 5: "spd", 6: "spe"}
    base_stats: dict[int, dict[str, int]] = defaultdict(dict)
    for r in stats_rows:
        pid = int(r["pokemon_id"])
        if pid > 721:
            continue
        sid = int(r["stat_id"])
        if sid in stat_map:
            base_stats[pid][stat_map[sid]] = int(r["base_stat"])

    type_name_rows = fetch("type_names.csv")
    type_en = {
        int(r["type_id"]): r["name"] for r in type_name_rows if r["local_language_id"] == "9"
    }
    types_rows = fetch("pokemon_types.csv")
    species_types: dict[int, dict[int, str]] = defaultdict(dict)
    for r in types_rows:
        pid = int(r["pokemon_id"])
        if pid > 721:
            continue
        species_types[pid][int(r["slot"])] = type_en.get(int(r["type_id"]), str(r["type_id"]))

    an = fetch("ability_names.csv")
    ab_en = {int(r["ability_id"]): r["name"] for r in an if r["local_language_id"] == "9"}
    pa = fetch("pokemon_abilities.csv")
    species_abs: dict[int, list[tuple[int, str]]] = defaultdict(list)
    species_hidden: dict[int, str] = {}
    for r in pa:
        pid = int(r["pokemon_id"])
        if pid > 721:
            continue
        name = ab_en.get(int(r["ability_id"]), "?")
        if r["is_hidden"] == "1":
            species_hidden[pid] = name
        else:
            species_abs[pid].append((int(r["slot"]), name))
    abilities = {pid: [n for _, n in sorted(v)] for pid, v in species_abs.items()}

    species = fetch("pokemon_species.csv")
    from_map: dict[int, int | None] = {}
    for r in species:
        sid = int(r["id"])
        if sid > 721:
            continue
        efs = r["evolves_from_species_id"]
        from_map[sid] = int(efs) if efs else None

    items = fetch("item_names.csv")
    item_en = {int(r["item_id"]): r["name"] for r in items if r["local_language_id"] == "9"}
    trigger = {
        1: "Level up",
        2: "Trade",
        3: "Use item",
        4: "Shed",
        5: "Spin",
    }
    evo_methods: dict[int, str] = {}
    for r in fetch("pokemon_evolution.csv"):
        to_id = int(r["evolved_species_id"])
        if to_id > 721:
            continue
        parts: list[str] = []
        tr = int(r["evolution_trigger_id"]) if r["evolution_trigger_id"] else 0
        parts.append(trigger.get(tr, f"Trigger {tr}"))
        if r.get("minimum_level"):
            parts.append(f"Lv {r['minimum_level']}")
        if r.get("trigger_item_id"):
            parts.append(item_en.get(int(r["trigger_item_id"]), f"item {r['trigger_item_id']}"))
        if r.get("held_item_id"):
            parts.append("hold " + item_en.get(int(r["held_item_id"]), r["held_item_id"]))
        if r.get("time_of_day"):
            parts.append(r["time_of_day"])
        if r.get("minimum_happiness"):
            parts.append(f"happiness ≥{r['minimum_happiness']}")
        if r.get("known_move_id"):
            parts.append("know move")
        if r.get("location_id"):
            parts.append("at location")
        if r.get("gender_id"):
            parts.append("gender-locked")
        if r.get("needs_overworld_rain") == "1":
            parts.append("rain")
        if r.get("turn_upside_down") == "1":
            parts.append("upside-down")
        evo_methods[to_id] = " · ".join(parts)

    mn = fetch("move_names.csv")
    move_en = {int(r["move_id"]): r["name"] for r in mn if r["local_language_id"] == "9"}
    learnsets: dict[int, list[tuple[int, str]]] = defaultdict(list)
    for r in fetch("pokemon_moves.csv"):
        pid = int(r["pokemon_id"])
        if pid > 721:
            continue
        # XY=14, ORAS=16 in pokeapi version_groups
        if int(r["version_group_id"]) not in (14, 15, 16):
            continue
        if r["pokemon_move_method_id"] != "1":
            continue
        learnsets[pid].append((int(r["level"]), move_en.get(int(r["move_id"]), str(r["move_id"]))))

    def clean_learnset(lst: list[tuple[int, str]]) -> list[dict]:
        best: dict[str, tuple[int, str]] = {}
        for level, name in lst:
            if name not in best or level < best[name][0]:
                best[name] = (level, name)
        return [
            {"level": lv, "name": n}
            for lv, n in sorted(best.values(), key=lambda x: (x[0], x[1]))
        ][:40]

    children: dict[int, list[int]] = defaultdict(list)
    for sid, parent in from_map.items():
        if parent:
            children[parent].append(sid)

    def evo_line(sid: int) -> list[int]:
        root = sid
        while from_map.get(root):
            root = from_map[root]  # type: ignore
        line: list[int] = []
        q = [root]
        seen: set[int] = set()
        while q:
            n = q.pop(0)
            if n in seen:
                continue
            seen.add(n)
            line.append(n)
            q.extend(sorted(children.get(n, [])))
        return line

    out = []
    for sid in range(1, 722):
        st = base_stats.get(sid, {})
        ty = species_types.get(sid, {})
        out.append(
            {
                "id": sid,
                "name": en_names.get(sid, f"#{sid}"),
                "type1": ty.get(1, "???"),
                "type2": ty.get(2),
                "hp": st.get("hp", 0),
                "atk": st.get("atk", 0),
                "def": st.get("def", 0),
                "spa": st.get("spa", 0),
                "spd": st.get("spd", 0),
                "spe": st.get("spe", 0),
                "abilities": abilities.get(sid, []),
                "hidden": species_hidden.get(sid),
                "evolves_from": from_map.get(sid),
                "evo_method": evo_methods.get(sid),
                "evo_line": evo_line(sid),
                "learnset": clean_learnset(learnsets.get(sid, [])),
            }
        )

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, separators=(",", ":"))
    print("wrote", OUT, "bytes", os.path.getsize(OUT), "count", len(out))
    print("sample Mudkip", out[257]["name"], out[257]["type1"], out[257]["learnset"][:3])


if __name__ == "__main__":
    main()
