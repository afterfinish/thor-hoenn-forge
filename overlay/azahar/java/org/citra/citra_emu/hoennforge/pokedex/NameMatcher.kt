// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import java.text.Normalizer
import java.util.Locale

/**
 * Match OCR tokens against offline species names (exact, alias, fuzzy).
 */
object NameMatcher {
    private val UI_STOP = setOf(
        "lv", "hp", "pp", "male", "female", "ot", "id", "exp", "next", "ok",
        "no", "yes", "back", "bag", "run", "fight", "pokemon", "pokémon",
        "wild", "appeared", "fainted", "used", "super", "effective", "not",
        "very", "but", "nothing", "happened", "got", "away", "safely",
        "route", "city", "town", "gym", "center", "mart", "move", "moves",
        "item", "items", "status", "summary", "switch", "cancel",
    )

    private val SHORT_EXACT = setOf(
        "abra", "onix", "mew", "muk", "jynx", "natu", "xatu", "absol", "azurill",
        "beldum", "deino", "eevee", "ekans", "entei", "gible", "golett", "hoopa",
        "inkay", "klink", "klang", "klinklang", "latias", "latios", "lugia",
        "munna", "nincada", "noibat", "pichu", "pinsir", "ralts", "relicanth",
        "shinx", "snorlax", "spinda", "unown", "uxie", "mesprit", "azelf",
    )

    /** Extra OCR / forme aliases → normalized species name key. */
    private val ALIASES = mapOf(
        "nidoranf" to "nidoran♀",
        "nidoranfemale" to "nidoran♀",
        "nidoran-f" to "nidoran♀",
        "nidoranm" to "nidoran♂",
        "nidoranmale" to "nidoran♂",
        "nidoran-m" to "nidoran♂",
        "mrmime" to "mr. mime",
        "mr mime" to "mr. mime",
        "mimejr" to "mime jr.",
        "mimejr." to "mime jr.",
        "farfetchd" to "farfetch'd",
        "farfetch" to "farfetch'd",
        "hooh" to "ho-oh",
        "ho oh" to "ho-oh",
        "porygonz" to "porygon-z",
        "porygon z" to "porygon-z",
        "type null" to "type: null",
        "typenull" to "type: null",
        "flabébé" to "flabebe",
        "flabebe" to "flabébé",
        "megacharizardx" to "charizard",
        "megacharizardy" to "charizard",
        "megablaziken" to "blaziken",
        "megaswampert" to "swampert",
        "megasceptile" to "sceptile",
        "primalkyogre" to "kyogre",
        "primalgroudon" to "groudon",
    )

    data class Hit(
        val species: Species,
        val token: String,
        val score: Float,
    )

    fun normalize(raw: String): String {
        var s = Normalizer.normalize(raw, Normalizer.Form.NFKC)
            .lowercase(Locale.US)
            .trim()
        // Curly quotes / fancy apostrophes → ASCII
        s = s.replace('\u2019', '\'').replace('\u2018', '\'').replace('`', '\'')
        s = s.replace('♀', 'f').replace('♂', 'm')
        s = s.replace(Regex("""^(mega|primal)\s+"""), "")
        s = s.replace(Regex("""[^a-z0-9♀♂'.\-\s]"""), "")
        s = s.replace(Regex("""\s+"""), " ").trim()
        return s
    }

    fun matchTokens(tokens: List<String>, repo: PokedexRepository): List<Hit> {
        val hits = LinkedHashMap<Int, Hit>()
        for (raw in tokens) {
            val norm = normalize(raw)
            if (norm.length < 3 && norm !in SHORT_EXACT) continue
            if (norm in UI_STOP) continue
            if (norm.all { it.isDigit() }) continue

            val resolved = resolveOne(norm, repo) ?: continue
            val existing = hits[resolved.species.id]
            if (existing == null || resolved.score > existing.score) {
                hits[resolved.species.id] = resolved
            }
        }
        return hits.values.sortedByDescending { it.score }
    }

    private fun resolveOne(norm: String, repo: PokedexRepository): Hit? {
        val aliasTarget = ALIASES[norm.replace(" ", "")]
            ?: ALIASES[norm]
        val key = when {
            aliasTarget != null -> normalize(aliasTarget)
            else -> norm
        }

        repo.byNormName[key]?.let {
            return Hit(it, norm, 1f)
        }
        // Compact key without spaces/punctuation
        val compact = key.replace(Regex("""[\s'.\-]"""), "")
        for ((n, sp) in repo.byNormName) {
            if (n.replace(Regex("""[\s'.\-]"""), "") == compact) {
                return Hit(sp, norm, 0.98f)
            }
        }

        // Short names: exact only
        if (key.length <= 4 && key !in SHORT_EXACT) {
            return null
        }

        // Prefix unique (len >= 5)
        if (key.length >= 5) {
            val prefs = repo.byNormName.filter { (n, _) -> n.startsWith(key) }
            if (prefs.size == 1) {
                val sp = prefs.values.first()
                return Hit(sp, norm, 0.9f)
            }
        }

        // Fuzzy
        var best: Species? = null
        var bestDist = Int.MAX_VALUE
        val maxDist = when {
            key.length <= 5 -> 1
            key.length <= 10 -> 2
            else -> 3
        }
        for ((n, sp) in repo.byNormName) {
            if (kotlin.math.abs(n.length - key.length) > maxDist) continue
            val d = levenshtein(key, n)
            if (d < bestDist && d <= maxDist) {
                bestDist = d
                best = sp
            }
        }
        val sp = best ?: return null
        val score = 1f - bestDist.toFloat() / key.length.coerceAtLeast(1)
        if (score < 0.75f) return null
        // Prefer unique-ish
        return Hit(sp, norm, score)
    }

    private fun levenshtein(a: String, b: String): Int {
        if (a == b) return 0
        if (a.isEmpty()) return b.length
        if (b.isEmpty()) return a.length
        val prev = IntArray(b.length + 1) { it }
        val cur = IntArray(b.length + 1)
        for (i in a.indices) {
            cur[0] = i + 1
            for (j in b.indices) {
                val cost = if (a[i] == b[j]) 0 else 1
                cur[j + 1] = minOf(cur[j] + 1, prev[j + 1] + 1, prev[j] + cost)
            }
            for (j in prev.indices) prev[j] = cur[j]
        }
        return prev[b.length]
    }
}
