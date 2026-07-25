// Copyright Hoenn Forge — ORAS camera tools (native freelook + zoom)
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary

/**
 * Camera tools for Omega Ruby / Alpha Sapphire.
 *
 * Free look: right stick dual GOLD eulers (works in houses / many interiors).
 * Zoom assist: L/R continuous FOV on primary GOLD.
 */
object HoennFreecam {
    private const val TAG = "HoennForgeCam"

    fun isSupportedTitle(titleId: Long): Boolean =
        OrasTitles.find(titleId) != null

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
