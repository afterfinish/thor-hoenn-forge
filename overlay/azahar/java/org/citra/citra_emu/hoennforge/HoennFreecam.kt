// Copyright Hoenn Forge — interim free-camera via ORAS camera Gateway cheats
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.features.cheats.model.Cheat
import org.citra.citra_emu.features.cheats.model.CheatEngine

/**
 * Camera **zoom assist** for Omega Ruby / Alpha Sapphire (not right-stick freelook).
 *
 * Right-stick free look is [NativeLibrary.setHoennFreelook] / core FreeCam.
 * This object only toggles Gateway codes for:
 *  - wider FOV
 *  - L/R shoulder zoom
 *
 * Does not work in every map (dynamic cameras: Mauville, some gyms, cutscenes).
 * Enter/exit a building after toggling for best results.
 */
object HoennFreecam {
    private const val TAG = "HoennForgeCamZoom"

    /** Marker name so we can find our entry among user cheats. */
    const val CHEAT_NAME = "Hoenn Camera Zoom Assist"

    /**
     * Wide FOV + free zoom (L out / R in).
     * Source: community ORAS v1.4 camera codes (Citra/Azahar Gateway format).
     * Addresses are process-relative pointer walks used by Gateway/Citra.
     */
    private val CODE = """
        685F67DC 00000000
        B85F67DC 00000000
        000000B0 44551CCD
        D2000000 00000000
        685F67DC 00000000
        B85F67DC 00000000
        DA000000 000000B2
        DFFFFFFE 00000001
        D4000000 00000020
        DD000000 00000200
        D7000000 000000B2
        D2000000 00000000
        685F67DC 00000000
        B85F67DC 00000000
        DA000000 000000B2
        DFFFFFFE 00000001
        D4000000 FFFFFFEC
        DD000000 00000100
        D7000000 000000B2
        D2000000 00000000
    """.trimIndent().lines().joinToString("\n") { it.trim() }

    private val NOTES =
        "Hoenn Forge free-camera assist (wide FOV + L/R zoom). " +
            "Not full stick freecam. Skip Mauville/gyms/cutscenes if locked. " +
            "Enter/exit a building after toggling to refresh."

    fun isSupportedTitle(titleId: Long): Boolean =
        OrasTitles.find(titleId) != null

    /**
     * Apply prefs state to the live cheat engine for [titleId].
     * Safe to call after emulation is running.
     * @return true if freecam is now active
     */
    fun apply(titleId: Long, enabled: Boolean): Boolean {
        if (titleId == 0L) {
            Log.w(TAG, "apply: no titleId")
            return false
        }
        if (!isSupportedTitle(titleId)) {
            Log.w(TAG, "apply: unsupported title ${"%016X".format(titleId)}")
            return false
        }
        return try {
            CheatEngine.loadCheatFile(titleId)
            val cheats = CheatEngine.getCheats()
            // Prefer new name; also match legacy "Hoenn Free Camera"
            val idx = cheats.indexOfFirst {
                it.getName() == CHEAT_NAME || it.getName() == "Hoenn Free Camera"
            }
            if (idx >= 0) {
                cheats[idx].setEnabled(enabled)
                // Refresh code body / rename to current marker
                if (enabled) {
                    val updated = Cheat.createGatewayCode(CHEAT_NAME, NOTES, CODE)
                    updated.setEnabled(true)
                    CheatEngine.updateCheat(idx, updated)
                }
            } else if (enabled) {
                val validity = Cheat.isValidGatewayCode(CODE)
                if (validity != 0) {
                    Log.e(TAG, "invalid freecam code at line $validity")
                    return false
                }
                val cheat = Cheat.createGatewayCode(CHEAT_NAME, NOTES, CODE)
                cheat.setEnabled(true)
                CheatEngine.addCheat(cheat)
            }
            CheatEngine.saveCheatFile(titleId)
            Log.i(TAG, "freecam enabled=$enabled title=${"%016X".format(titleId)}")
            enabled
        } catch (e: Exception) {
            Log.e(TAG, "apply failed", e)
            false
        }
    }

    /** Read live enabled state (false if missing). */
    fun isActive(titleId: Long): Boolean {
        if (titleId == 0L) return false
        return try {
            CheatEngine.loadCheatFile(titleId)
            CheatEngine.getCheats().any { it.getName() == CHEAT_NAME && it.getEnabled() }
        } catch (_: Exception) {
            false
        }
    }
}
