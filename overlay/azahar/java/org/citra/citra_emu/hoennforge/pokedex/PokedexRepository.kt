// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.content.Context
import org.json.JSONArray
import java.util.concurrent.atomic.AtomicReference

/**
 * Loads `assets/pokedex/species.json` once and serves species by id / name.
 */
class PokedexRepository private constructor(
    val all: List<Species>,
    val byId: Map<Int, Species>,
    val byNormName: Map<String, Species>,
) {
    fun get(id: Int): Species? = byId[id]

    fun resolveNames(ids: Collection<Int>): List<Species> =
        ids.mapNotNull { byId[it] }

    companion object {
        private val cache = AtomicReference<PokedexRepository?>(null)

        fun get(context: Context): PokedexRepository {
            cache.get()?.let { return it }
            synchronized(this) {
                cache.get()?.let { return it }
                val repo = load(context.applicationContext)
                cache.set(repo)
                return repo
            }
        }

        private fun load(context: Context): PokedexRepository {
            val json = context.assets.open("pokedex/species.json").bufferedReader().use { it.readText() }
            val arr = JSONArray(json)
            val list = ArrayList<Species>(arr.length())
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val abilities = ArrayList<String>()
                val ab = o.optJSONArray("abilities")
                if (ab != null) {
                    for (j in 0 until ab.length()) {
                        abilities.add(ab.getString(j))
                    }
                }
                val evoLine = ArrayList<Int>()
                val el = o.optJSONArray("evo_line")
                if (el != null) {
                    for (j in 0 until el.length()) {
                        evoLine.add(el.getInt(j))
                    }
                }
                val learnset = ArrayList<LearnMove>()
                val ls = o.optJSONArray("learnset")
                if (ls != null) {
                    for (j in 0 until ls.length()) {
                        val m = ls.getJSONObject(j)
                        learnset.add(LearnMove(m.getInt("level"), m.getString("name")))
                    }
                }
                val type2Raw = when {
                    !o.has("type2") || o.isNull("type2") -> ""
                    else -> o.optString("type2", "")
                }
                val type2 = type2Raw.trim().takeIf {
                    it.isNotEmpty() && !it.equals("null", ignoreCase = true)
                }
                list.add(
                    Species(
                        id = o.getInt("id"),
                        name = o.getString("name"),
                        type1 = o.getString("type1"),
                        type2 = type2,
                        hp = o.optInt("hp"),
                        atk = o.optInt("atk"),
                        def = o.optInt("def"),
                        spa = o.optInt("spa"),
                        spd = o.optInt("spd"),
                        spe = o.optInt("spe"),
                        abilities = abilities,
                        hidden = o.optString("hidden", "").ifBlank { null },
                        evolvesFrom = if (o.isNull("evolves_from")) null else o.optInt("evolves_from"),
                        evoMethod = o.optString("evo_method", "").ifBlank { null },
                        evoLine = evoLine,
                        learnset = learnset,
                    ),
                )
            }
            val byId = list.associateBy { it.id }
            val byNorm = HashMap<String, Species>(list.size * 2)
            for (s in list) {
                byNorm[NameMatcher.normalize(s.name)] = s
            }
            return PokedexRepository(list, byId, byNorm)
        }
    }
}
