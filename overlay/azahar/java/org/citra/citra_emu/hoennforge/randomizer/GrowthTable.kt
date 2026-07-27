// Copyright Hoenn Forge — experience-growth curves for the level cap
package org.citra.citra_emu.hoennforge.randomizer

import android.content.Context
import android.net.Uri
import android.util.Log
import java.io.File

/**
 * The experience-growth curve of every species, pulled out of the personal table during
 * prepare and handed to the native level cap.
 *
 * The cap holds a Pokemon at level N by writing the first experience value of level N,
 * and that value depends on which of the six curves the species is on. Guessing it wrong
 * would put the Pokemon at the wrong level, so the cap refuses to write anything at all
 * until this table is loaded.
 *
 * Read out of the player's own dump rather than hardcoded, so it cannot drift from the
 * game actually running. It reads the dump's personal table, not the LayeredFS copy,
 * which is correct only because [RandomizerEngine.randomizePersonal] leaves the growth
 * byte alone — if that ever changes, this has to read the deployed mod instead.
 */
object GrowthTable {
    private const val TAG = "HoennGrowthTable"

    /** National dex plus the unused index 0, which the personal table also reserves. */
    const val SPECIES_COUNT = 722

    /** Personal entry offset of the EXP-growth id (0 medium fast … 5 slow). */
    private const val OFF_EXP_GROWTH = 0x15

    private const val FILE_NAME = "growth_rates.bin"

    fun file(context: Context): File = File(context.filesDir, "prepared/$FILE_NAME")

    /**
     * Reads the personal GARC out of the dump and writes the table beside the other
     * prepare output. Returns null and logs on any failure: the level cap fails open, so
     * a missing table costs the feature, not the run.
     */
    fun extract(context: Context, dumpUri: Uri): ByteArray? = try {
        val romfs = RomfsReader.open(context, dumpUri)
        try {
            val rates = parse(romfs.read(OrasPaths.PERSONAL))
            val out = file(context)
            out.parentFile?.mkdirs()
            out.writeBytes(rates)
            Log.i(TAG, "growth table written: ${rates.size} species -> ${out.path}")
            rates
        } finally {
            romfs.close()
        }
    } catch (e: Exception) {
        Log.e(TAG, "growth table extraction failed", e)
        null
    }

    fun load(context: Context): ByteArray? {
        val f = file(context)
        if (!f.isFile) return null
        return try {
            val bytes = f.readBytes()
            if (bytes.size < SPECIES_COUNT) {
                Log.w(TAG, "growth table too short (${bytes.size}), ignoring")
                null
            } else {
                bytes
            }
        } catch (e: Exception) {
            Log.e(TAG, "growth table read failed", e)
            null
        }
    }

    fun clear(context: Context) {
        file(context).delete()
    }

    /** Personal table lives in the last GARC entry, LZ11-compressed in a retail dump. */
    internal fun parse(personalGarc: ByteArray): ByteArray {
        val garc = GarcArchive.open(personalGarc)
        val table = Lz11.maybeDecompress(garc.getFile(garc.fileCount - 1))
        val size = OrasPaths.PERSONAL_SIZE
        val entries = table.size / size
        require(entries > 1) { "personal table has $entries entries" }

        val rates = ByteArray(SPECIES_COUNT)
        // Entries past the dex are alternate forms and share the base species' curve, so
        // stopping at the dex count loses nothing the cap needs.
        val last = minOf(entries, SPECIES_COUNT)
        for (i in 1 until last) {
            val v = table[i * size + OFF_EXP_GROWTH].toInt() and 0xFF
            // Six curves. Anything else is a table we do not understand; leave it at 0
            // (medium fast) and let the native validator reject the slot instead.
            rates[i] = if (v <= 5) v.toByte() else 0
        }
        return rates
    }
}
