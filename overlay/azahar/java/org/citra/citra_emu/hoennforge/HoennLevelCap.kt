// Copyright Hoenn Forge — hardcore-nuzlocke level cap (native driver)
package org.citra.citra_emu.hoennforge

import android.content.Context
import android.util.Log
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.hoennforge.randomizer.GrowthTable

/**
 * Kotlin side of `Hoenn::LevelCap`. Native state is per-session, so everything here is
 * pushed again on every boot.
 */
object HoennLevelCap {
    private const val TAG = "HoennLevelCap"

    /**
     * Enforcement kill switch.
     *
     * True runs the whole pipeline — locate the party, validate it, work out what would be
     * clamped — and logs the result without writing a single guest byte. It stays true
     * until the party address has been proved against a real save on hardware, because
     * the one failure mode that matters here is writing to something that is not a
     * Pokemon, and that failure lands in somebody's save file.
     *
     * Flip to false once `docs/level-cap.md`'s device checklist passes. Grep the log for
     * "Hoenn level cap" — you want "party found at …" followed by observe lines whose
     * levels match what the party screen shows.
     */
    private const val OBSERVE_ONLY = true

    fun isSupportedTitle(titleId: Long): Boolean = OrasTitles.find(titleId) != null

    /** Push the persisted choice into the freshly started core. */
    fun restore(context: Context, prefs: HoennPrefs, titleId: Long) {
        if (titleId == 0L || !isSupportedTitle(titleId)) return
        try {
            if (!prefs.levelCapEnabled) {
                NativeLibrary.setHoennLevelCap(false)
                return
            }
            val rates = GrowthTable.load(context)
            if (rates == null) {
                // No curves means no way to know what experience value a level starts at,
                // and the native side refuses to write without them. Say so once rather
                // than leaving a cap that quietly never fires.
                Log.w(TAG, "level cap on but no growth table; re-run prepare to build one")
            } else {
                NativeLibrary.setHoennLevelCapGrowth(rates)
            }
            NativeLibrary.setHoennLevelCapStage(prefs.levelCapStage)
            NativeLibrary.setHoennLevelCapEnforce(!OBSERVE_ONLY)
            NativeLibrary.setHoennLevelCap(true)
            Log.i(
                TAG,
                "level cap on, stage ${prefs.levelCapStage} (${LevelCapLadder.label(prefs.levelCapStage)})" +
                    if (OBSERVE_ONLY) " [observe only]" else "",
            )
        } catch (e: Exception) {
            Log.e(TAG, "level cap restore failed", e)
        }
    }

    fun setEnabled(prefs: HoennPrefs, enabled: Boolean): Boolean = try {
        prefs.levelCapEnabled = enabled
        NativeLibrary.setHoennLevelCap(enabled)
        true
    } catch (e: Exception) {
        Log.e(TAG, "level cap toggle failed", e)
        false
    }

    fun setStage(prefs: HoennPrefs, stage: Int): Boolean = try {
        val clamped = LevelCapLadder.clamp(stage)
        prefs.levelCapStage = clamped
        NativeLibrary.setHoennLevelCapStage(clamped)
        Log.i(TAG, "stage -> $clamped (${LevelCapLadder.label(clamped)})")
        true
    } catch (e: Exception) {
        Log.e(TAG, "level cap stage failed", e)
        false
    }

    fun advance(prefs: HoennPrefs): Boolean = setStage(prefs, prefs.levelCapStage + 1)

    /** Live state for the quick menu. Null when the core is not answering. */
    data class Status(val cap: Int, val stage: Int, val partyCount: Int, val clamps: Int)

    fun status(): Status? = try {
        Status(
            cap = NativeLibrary.hoennLevelCapActive(),
            stage = NativeLibrary.hoennLevelCapStage(),
            partyCount = NativeLibrary.hoennLevelCapPartyCount(),
            clamps = NativeLibrary.hoennLevelCapClamps(),
        )
    } catch (e: Exception) {
        Log.w(TAG, "level cap status unavailable", e)
        null
    }
}
