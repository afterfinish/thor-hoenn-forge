package dev.tzigdon.hoennforge.dump

/**
 * Known Omega Ruby / Alpha Sapphire title IDs (cartridge / eShop program IDs).
 * Identifiers only — no game data.
 */
object OrasTitles {
    data class Entry(
        val titleId: Long,
        val game: Game,
        val region: String,
        val label: String,
    )

    enum class Game { OMEGA_RUBY, ALPHA_SAPPHIRE }

    private val entries: List<Entry> = listOf(
        // USA
        Entry(0x000400000011C400L, Game.OMEGA_RUBY, "USA", "Pokémon Omega Ruby (USA)"),
        Entry(0x000400000011C500L, Game.ALPHA_SAPPHIRE, "USA", "Pokémon Alpha Sapphire (USA)"),
        // Europe
        Entry(0x000400000011C600L, Game.OMEGA_RUBY, "EUR", "Pokémon Omega Ruby (EUR)"),
        Entry(0x000400000011C700L, Game.ALPHA_SAPPHIRE, "EUR", "Pokémon Alpha Sapphire (EUR)"),
        // Japan
        Entry(0x000400000011C000L, Game.OMEGA_RUBY, "JPN", "Pocket Monsters Omega Ruby (JPN)"),
        Entry(0x000400000011C100L, Game.ALPHA_SAPPHIRE, "JPN", "Pocket Monsters Alpha Sapphire (JPN)"),
    )

    private val byId: Map<Long, Entry> = entries.associateBy { it.titleId }

    fun find(titleId: Long): Entry? = byId[titleId]

    fun isSupported(titleId: Long): Boolean = byId.containsKey(titleId)

    fun formatTitleId(titleId: Long): String = "%016X".format(titleId)
}
