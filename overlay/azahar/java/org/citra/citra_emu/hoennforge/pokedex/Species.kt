// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

data class LearnMove(
    val level: Int,
    val name: String,
)

/**
 * Offline species entry (Gen 1–6 National Dex 1–721).
 * Data built from PokeAPI CSVs (BSD-3-Clause); names are Nintendo trademarks.
 */
data class Species(
    val id: Int,
    val name: String,
    val type1: String,
    val type2: String?,
    val hp: Int,
    val atk: Int,
    val def: Int,
    val spa: Int,
    val spd: Int,
    val spe: Int,
    val abilities: List<String>,
    val hidden: String?,
    val evolvesFrom: Int?,
    val evoMethod: String?,
    val evoLine: List<Int>,
    val learnset: List<LearnMove>,
) {
    val typesLabel: String
        get() = if (type2.isNullOrBlank()) type1 else "$type1 / $type2"

    val bst: Int
        get() = hp + atk + def + spa + spd + spe
}
