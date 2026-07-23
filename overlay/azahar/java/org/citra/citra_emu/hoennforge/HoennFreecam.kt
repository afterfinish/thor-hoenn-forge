// Copyright Hoenn Forge — ORAS camera tools (native freelook + zoom)
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary

/**
 * Camera tools for Omega Ruby / Alpha Sapphire.
 *
 * Zoom assist and free-look are implemented in native [Hoenn::FreeCam]
 * (see core/hoenn_freecam.cpp). START menu façade only.
 *
 * Free look: stick Y = pitch (+0x98), stick X = yaw (+0x9C, probe #14 locked).
 */
object HoennFreecam {
    private const val TAG = "HoennForgeCam"

    fun isSupportedTitle(titleId: Long): Boolean =
        OrasTitles.find(titleId) != null

    /** Enable/disable continuous L/R zoom assist (native). */
    fun applyZoomAssist(titleId: Long, enabled: Boolean): Boolean {
        if (titleId == 0L || !isSupportedTitle(titleId)) {
            Log.w(TAG, "zoom: unsupported title")
            return false
        }
        return try {
            NativeLibrary.setHoennZoomAssist(enabled)
            Log.i(TAG, "zoom assist=$enabled")
            true
        } catch (e: Exception) {
            Log.e(TAG, "zoom assist failed", e)
            false
        }
    }

    /** Enable/disable right-stick free-look (native). */
    fun applyFreelook(titleId: Long, enabled: Boolean): Boolean {
        if (titleId == 0L || !isSupportedTitle(titleId)) {
            Log.w(TAG, "freelook: unsupported title")
            return false
        }
        return try {
            NativeLibrary.setHoennFreelook(enabled)
            Log.i(TAG, "freelook=$enabled")
            true
        } catch (e: Exception) {
            Log.e(TAG, "freelook failed", e)
            false
        }
    }

    /** @deprecated use applyZoomAssist */
    fun apply(titleId: Long, enabled: Boolean): Boolean = applyZoomAssist(titleId, enabled)
}
