// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.randomizer

import kotlin.random.Random

/**
 * Species lists for Gen 6 (ORAS). IDs 1..721.
 */
object SpeciesPool {
    const val MAX_SPECIES = 721

    /** Gen 1–6 basic starters (first stages only). */
    val BASIC_STARTERS = intArrayOf(
        1, 4, 7, // Kanto
        152, 155, 158, // Johto
        252, 255, 258, // Hoenn
        387, 390, 393, // Sinnoh
        495, 498, 501, // Unova
        650, 653, 656, // Kalos
    )

    // Classic starter type pools (first stage only)
    // Grass: Bulbasaur, Chikorita, Treecko, Turtwig, Snivy, Chespin
    val STARTER_GRASS = intArrayOf(1, 152, 252, 387, 495, 650)
    // Fire: Charmander, Cyndaquil, Torchic, Chimchar, Tepig, Fennekin
    val STARTER_FIRE = intArrayOf(4, 155, 255, 390, 498, 653)
    // Water: Squirtle, Totodile, Mudkip, Piplup, Oshawott, Froakie
    val STARTER_WATER = intArrayOf(7, 158, 258, 393, 501, 656)

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
            RandomizerConfig.StarterMode.TYPE_BALANCED -> {
                // True type balance: one Grass, one Fire, one Water (shuffled into bag slots)
                val trio = intArrayOf(
                    pick(rng, STARTER_GRASS),
                    pick(rng, STARTER_FIRE),
                    pick(rng, STARTER_WATER),
                )
                // Shuffle so left/middle/right aren't always G/F/W order
                for (i in trio.lastIndex downTo 1) {
                    val j = rng.nextInt(i + 1)
                    val tmp = trio[i]
                    trio[i] = trio[j]
                    trio[j] = tmp
                }
                trio
            }
            RandomizerConfig.StarterMode.FULL_RANDOM -> {
                // Only first-stage starters — bag/select models are reliable
                IntArray(3) { pick(rng, BASIC_STARTERS) }
                    .also { ensureDistinct(it, rng, BASIC_STARTERS) }
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
