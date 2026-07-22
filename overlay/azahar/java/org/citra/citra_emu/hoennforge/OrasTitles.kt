// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

object OrasTitles {
    data class Entry(
        val titleId: Long,
        val game: Game,
        val region: String,
        val label: String,
    )

    enum class Game { OMEGA_RUBY, ALPHA_SAPPHIRE }

    private val entries = listOf(
        Entry(0x000400000011C400L, Game.OMEGA_RUBY, "USA", "Pokémon Omega Ruby (USA)"),
        Entry(0x000400000011C500L, Game.ALPHA_SAPPHIRE, "USA", "Pokémon Alpha Sapphire (USA)"),
        Entry(0x000400000011C600L, Game.OMEGA_RUBY, "EUR", "Pokémon Omega Ruby (EUR)"),
        Entry(0x000400000011C700L, Game.ALPHA_SAPPHIRE, "EUR", "Pokémon Alpha Sapphire (EUR)"),
        Entry(0x000400000011C000L, Game.OMEGA_RUBY, "JPN", "Pocket Monsters Omega Ruby (JPN)"),
        Entry(0x000400000011C100L, Game.ALPHA_SAPPHIRE, "JPN", "Pocket Monsters Alpha Sapphire (JPN)"),
    )

    private val byId = entries.associateBy { it.titleId }

    fun find(titleId: Long): Entry? = byId[titleId]

    fun formatTitleId(titleId: Long): String = "%016X".format(titleId)
}
