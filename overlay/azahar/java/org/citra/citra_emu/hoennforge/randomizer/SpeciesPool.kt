// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.randomizer

import kotlin.random.Random

/**
 * Species lists for Gen 6 (ORAS). IDs 1..721.
 */
object SpeciesPool {
    const val MAX_SPECIES = 721

    /** Gen 1–5 basic starters (first stages only). */
    val BASIC_STARTERS = intArrayOf(
        1, 4, 7, // Kanto
        152, 155, 158, // Johto
        252, 255, 258, // Hoenn
        387, 390, 393, // Sinnoh
        495, 498, 501, // Unova
        650, 653, 656, // Kalos
    )

    val LEGENDARY = intArrayOf(
        144, 145, 146, 150, 151,
        243, 244, 245, 249, 250, 251,
        377, 378, 379, 380, 381, 382, 383, 384, 385, 386,
        480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493,
        494, 638, 639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649,
        716, 717, 718, 719, 720, 721,
    )

    private val legendarySet = LEGENDARY.toSet()

    fun isLegendary(id: Int): Boolean = id in legendarySet

    fun allSpecies(includeLegendaries: Boolean): IntArray {
        val list = ArrayList<Int>(MAX_SPECIES)
        for (i in 1..MAX_SPECIES) {
            if (!includeLegendaries && isLegendary(i)) continue
            // Skip eggs / glitch placeholders if any — 1..721 are valid in Gen 6
            list.add(i)
        }
        return list.toIntArray()
    }

    fun pick(rng: Random, pool: IntArray): Int {
        require(pool.isNotEmpty())
        return pool[rng.nextInt(pool.size)]
    }

    /** Three distinct species for a starter set. */
    fun pickStarterTrio(
        rng: Random,
        mode: RandomizerConfig.StarterMode,
        includeLegendaries: Boolean,
    ): IntArray {
        return when (mode) {
            RandomizerConfig.StarterMode.VANILLA ->
                intArrayOf(252, 255, 258)
            RandomizerConfig.StarterMode.GEN_LIMITED -> {
                // Random basic starters (may repeat gens)
                IntArray(3) { pick(rng, BASIC_STARTERS) }.also { ensureDistinct(it, rng, BASIC_STARTERS) }
            }
            RandomizerConfig.StarterMode.TYPE_BALANCED,
            RandomizerConfig.StarterMode.FULL_RANDOM,
            -> {
                val pool = if (mode == RandomizerConfig.StarterMode.TYPE_BALANCED) {
                    BASIC_STARTERS
                } else {
                    allSpecies(includeLegendaries)
                }
                IntArray(3) { pick(rng, pool) }.also { ensureDistinct(it, rng, pool) }
            }
        }
    }

    private fun ensureDistinct(out: IntArray, rng: Random, pool: IntArray) {
        var guard = 0
        while (out.toSet().size < out.size && guard++ < 50) {
            for (i in out.indices) {
                if (out.count { it == out[i] } > 1) {
                    out[i] = pick(rng, pool)
                }
            }
        }
    }
}
