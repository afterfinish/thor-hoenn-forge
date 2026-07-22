package dev.tzigdon.hoennforge.dump

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class OrasTitlesTest {
    @Test
    fun usaAlphaSapphire_isSupported() {
        val id = 0x000400000011C500L
        assertTrue(OrasTitles.isSupported(id))
        val entry = OrasTitles.find(id)
        assertNotNull(entry)
        assertEquals(OrasTitles.Game.ALPHA_SAPPHIRE, entry!!.game)
        assertEquals("USA", entry.region)
    }

    @Test
    fun usaOmegaRuby_isSupported() {
        val id = 0x000400000011C400L
        val entry = OrasTitles.find(id)
        assertNotNull(entry)
        assertEquals(OrasTitles.Game.OMEGA_RUBY, entry!!.game)
    }

    @Test
    fun formatTitleId_paddedHex() {
        assertEquals("000400000011C500", OrasTitles.formatTitleId(0x000400000011C500L))
    }
}
