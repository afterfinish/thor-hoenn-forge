// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import java.text.Normalizer
import java.util.Locale

/**
 * Match OCR tokens against offline species names (exact, alias, OCR confusions, fuzzy).
 */
object NameMatcher {
    private val UI_STOP = setOf(
        "lv", "hp", "pp", "male", "female", "ot", "id", "exp", "next", "ok",
        "no", "yes", "back", "bag", "run", "fight", "pokemon", "pokémon",
        "wild", "appeared", "fainted", "used", "super", "effective", "not",
        "very", "but", "nothing", "happened", "got", "away", "safely",
        "route", "city", "town", "gym", "center", "mart", "move", "moves",
        "item", "items", "status", "summary", "switch", "cancel", "ability",
        "nature", "level", "type", "types", "stats", "attack", "defense",
        "special", "speed", "accuracy", "evasion", "critical", "hit",
    )

    private val SHORT_EXACT = setOf(
        "abra", "onix", "mew", "muk", "jynx", "natu", "xatu", "absol", "azurill",
        "beldum", "deino", "eevee", "ekans", "entei", "gible", "golett", "hoopa",
        "inkay", "klink", "klang", "klinklang", "latias", "latios", "lugia",
        "munna", "nincada", "noibat", "pichu", "pinsir", "ralts", "relicanth",
        "shinx", "snorlax", "spinda", "unown", "uxie", "mesprit", "azelf",
        "axew", "aron", "aipom", "absol", "bidoof", "buizel", "budew", "cleffa",
    )

    /** Extra OCR / forme aliases → display / lookup name. */
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
        "flabebe" to "flabébé",
        "gogoat" to "gogoat",
        "megacharizardx" to "charizard",
        "megacharizardy" to "charizard",
        "megablaziken" to "blaziken",
        "megaswampert" to "swampert",
        "megasceptile" to "sceptile",
        "primalkyogre" to "kyogre",
        "primalgroudon" to "groudon",
    )

    /**
     * Common ML Kit confusions on ORAS pixel fonts.
     * Each entry: wrong OCR char(s) → possible true char(s) for variant expansion.
     */
    private val CONFUSABLES: Map<Char, CharArray> = mapOf(
        // W often becomes VV, UU, N, H, M
        'w' to charArrayOf('w', 'm', 'n', 'h', 'u'),
        'm' to charArrayOf('m', 'w', 'n', 'r', 'h'),
        'n' to charArrayOf('n', 'm', 'h', 'r', 'u'),
        'h' to charArrayOf('h', 'n', 'b', 'k'),
        'r' to charArrayOf('r', 'n', 'k', 'v'),
        'v' to charArrayOf('v', 'y', 'u', 'w'),
        'u' to charArrayOf('u', 'v', 'n', 'w'),
        'i' to charArrayOf('i', 'l', '1', 't', 'j'),
        'l' to charArrayOf('l', 'i', '1', 't'),
        '1' to charArrayOf('1', 'l', 'i', 't'),
        'o' to charArrayOf('o', '0', 'd', 'q', 'a'),
        '0' to charArrayOf('0', 'o', 'd', 'q'),
        'c' to charArrayOf('c', 'e', 'o', 'g'),
        'e' to charArrayOf('e', 'c', 'o', 'a'),
        'g' to charArrayOf('g', 'q', 'o', 'c', '9'),
        'q' to charArrayOf('q', 'g', 'o', '9'),
        'a' to charArrayOf('a', 'e', 'o', 'd'),
        's' to charArrayOf('s', '5', 'z', '8'),
        '5' to charArrayOf('5', 's', 'z'),
        'z' to charArrayOf('z', 's', '2'),
        'b' to charArrayOf('b', 'h', '8', 'd'),
        'd' to charArrayOf('d', 'o', 'a', 'b'),
        't' to charArrayOf('t', 'f', 'l', 'i'),
        'f' to charArrayOf('f', 't', 'p'),
        'p' to charArrayOf('p', 'f', 'r'),
        'y' to charArrayOf('y', 'v', 'g'),
        'k' to charArrayOf('k', 'h', 'r', 'x'),
        'x' to charArrayOf('x', 'k', 'n'),
    )

    // Multi-char OCR glitches → single letter (applied as regex replacements)
    private val MULTI_CHAR_FIXES = listOf(
        Regex("vv") to "w",
        Regex("uu") to "w",
        Regex("rn") to "m", // common: m → rn
        Regex("nn") to "m",
        Regex("cl") to "d",
        Regex("ii") to "n",
        Regex("lI") to "m",
        Regex("\\|") to "l",
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
        s = s.replace('\u2019', '\'').replace('\u2018', '\'').replace('`', '\'')
        s = s.replace('♀', 'f').replace('♂', 'm')
        s = s.replace(Regex("""^(mega|primal)\s+"""), "")
        s = s.replace(Regex("""[^a-z0-9♀♂'.\-\s]"""), "")
        s = s.replace(Regex("""\s+"""), " ").trim()
        return s
    }

    /** Compact form for matching (no spaces / punctuation). */
    fun compact(norm: String): String =
        norm.replace(Regex("""[\s'.\-]"""), "")

    fun matchTokens(tokens: List<String>, repo: PokedexRepository): List<Hit> {
        val hits = LinkedHashMap<Int, Hit>()
        val expanded = expandTokens(tokens)
        for (raw in expanded) {
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

    /**
     * Manual / typed search: match a single user string (exact, prefix, contains, fuzzy).
     */
    fun matchQuery(query: String, repo: PokedexRepository, limit: Int = 12): List<Hit> {
        val norm = normalize(query)
        if (norm.isEmpty()) return emptyList()
        val hits = LinkedHashMap<Int, Hit>()
        resolveOne(norm, repo)?.let { hits[it.species.id] = it }

        val c = compact(norm)
        if (c.length >= 2) {
            for ((n, sp) in repo.byNormName) {
                val nc = compact(n)
                val score = when {
                    nc == c -> 1f
                    nc.startsWith(c) -> 0.92f
                    c.startsWith(nc) && nc.length >= 4 -> 0.85f
                    nc.contains(c) && c.length >= 3 -> 0.8f
                    else -> {
                        val d = levenshtein(c, nc)
                        val maxD = when {
                            c.length <= 5 -> 1
                            c.length <= 10 -> 2
                            else -> 3
                        }
                        if (d <= maxD) 1f - d.toFloat() / c.length.coerceAtLeast(1) else continue
                    }
                }
                if (score < 0.72f) continue
                val prev = hits[sp.id]
                if (prev == null || score > prev.score) {
                    hits[sp.id] = Hit(sp, query, score)
                }
            }
        }
        return hits.values.sortedByDescending { it.score }.take(limit)
    }

    /** Expand OCR tokens with confusable-letter variants (bounded). */
    private fun expandTokens(tokens: List<String>): List<String> {
        val out = LinkedHashSet<String>()
        for (t in tokens) {
            out.add(t)
            val n = normalize(t)
            if (n.isEmpty()) continue
            out.add(n)
            // Multi-char fixes
            var fixed = n
            for ((re, rep) in MULTI_CHAR_FIXES) {
                fixed = re.replace(fixed, rep)
            }
            out.add(fixed)
            // Single-char confusable expansion (limit explosion)
            for (v in confusableVariants(compact(fixed), maxVariants = 48)) {
                out.add(v)
            }
            // Joined adjacent tokens already come as lines from OcrService
        }
        // Also try concatenating consecutive short tokens (e.g. "Go" + "goat")
        val norms = tokens.map { normalize(it) }.filter { it.isNotEmpty() }
        for (i in 0 until norms.size - 1) {
            val a = norms[i]
            val b = norms[i + 1]
            if (a.length + b.length in 4..14) {
                out.add(a + b)
                out.add("$a $b")
            }
        }
        return out.toList()
    }

    private fun confusableVariants(compact: String, maxVariants: Int): List<String> {
        if (compact.length !in 3..14) return emptyList()
        val results = LinkedHashSet<String>()
        results.add(compact)
        // Positions that have confusable chars
        val positions = compact.indices.filter { CONFUSABLES.containsKey(compact[it]) }
        if (positions.isEmpty()) return results.toList()

        // 1-edit variants
        for (pos in positions) {
            val alts = CONFUSABLES[compact[pos]] ?: continue
            for (ch in alts) {
                if (ch == compact[pos] || ch.toString().length != 1) continue
                val arr = compact.toCharArray()
                arr[pos] = ch
                results.add(String(arr))
                if (results.size >= maxVariants) return results.toList()
            }
        }
        // 2-edit variants only for longer names (Gogoat etc.)
        if (compact.length >= 5 && positions.size >= 2) {
            val p0 = positions[0]
            val p1 = positions.getOrNull(1) ?: return results.toList()
            for (c0 in CONFUSABLES[compact[p0]] ?: charArrayOf()) {
                for (c1 in CONFUSABLES[compact[p1]] ?: charArrayOf()) {
                    val arr = compact.toCharArray()
                    arr[p0] = c0
                    arr[p1] = c1
                    results.add(String(arr))
                    if (results.size >= maxVariants) return results.toList()
                }
            }
        }
        return results.toList()
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
        val compactKey = compact(key)
        for ((n, sp) in repo.byNormName) {
            if (compact(n) == compactKey) {
                return Hit(sp, norm, 0.98f)
            }
        }

        // Short names: exact / compact only
        if (key.length <= 4 && key !in SHORT_EXACT) {
            return null
        }

        // Prefix unique (len >= 4)
        if (compactKey.length >= 4) {
            val prefs = repo.byNormName.filter { (n, _) -> compact(n).startsWith(compactKey) }
            if (prefs.size == 1) {
                return Hit(prefs.values.first(), norm, 0.9f)
            }
            // Unique suffix (OCR sometimes drops first letter)
            val suf = repo.byNormName.filter { (n, _) ->
                val c = compact(n)
                c.endsWith(compactKey) && c.length - compactKey.length <= 2
            }
            if (suf.size == 1) {
                return Hit(suf.values.first(), norm, 0.88f)
            }
        }

        // Fuzzy on compact names
        var best: Species? = null
        var bestDist = Int.MAX_VALUE
        val maxDist = when {
            compactKey.length <= 5 -> 1
            compactKey.length <= 8 -> 2
            compactKey.length <= 12 -> 3
            else -> 3
        }
        for ((n, sp) in repo.byNormName) {
            val c = compact(n)
            if (kotlin.math.abs(c.length - compactKey.length) > maxDist) continue
            val d = levenshtein(compactKey, c)
            if (d < bestDist && d <= maxDist) {
                bestDist = d
                best = sp
            }
        }
        val sp = best ?: return null
        val score = 1f - bestDist.toFloat() / compactKey.length.coerceAtLeast(1)
        if (score < 0.72f) return null
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
