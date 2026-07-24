// Copyright Hoenn Forge — ORAS camera tools (native freelook + zoom)
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary

/**
 * Camera tools for Omega Ruby / Alpha Sapphire.
 *
 * Zoom assist and free-look are implemented in native [Hoenn::FreeCam].
 * START menu selects freelook **experiment modes** so dogfood can A/B without rebuilds.
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

    fun setExperimentMode(mode: Int): Boolean {
        return try {
            NativeLibrary.setHoennFreelookExperiment(mode)
            Log.i(TAG, "freelook exp=$mode")
            true
        } catch (e: Exception) {
            Log.e(TAG, "set experiment failed", e)
            false
        }
    }

    fun getExperimentMode(): Int {
        return try {
            NativeLibrary.getHoennFreelookExperiment()
        } catch (e: Exception) {
            Log.e(TAG, "get experiment failed", e)
            0
        }
    }

    fun getExperimentCount(): Int {
        return try {
            NativeLibrary.getHoennFreelookExperimentCount()
        } catch (e: Exception) {
            Log.e(TAG, "get experiment count failed", e)
            0
        }
    }

    fun getExperimentLabel(mode: Int): String {
        return try {
            NativeLibrary.getHoennFreelookExperimentLabel(mode)
        } catch (e: Exception) {
            Log.e(TAG, "get experiment label failed", e)
            "#$mode"
        }
    }

    fun experimentLabels(): Array<String> {
        val n = getExperimentCount().coerceAtLeast(0)
        return Array(n) { i -> getExperimentLabel(i) }
    }

    fun scanCamProbeLabels(): Array<String> {
        return try {
            val n = NativeLibrary.hoennScanCamCandidates()
            Log.i(TAG, "camProbe scan n=$n")
            if (n <= 0) emptyArray() else NativeLibrary.hoennGetCamCandidateLabels()
        } catch (e: Exception) {
            Log.e(TAG, "camProbe scan failed", e)
            emptyArray()
        }
    }

    fun setCamProbeIndex(index: Int) {
        try {
            NativeLibrary.hoennSetCamProbeIndex(index)
            Log.i(TAG, "camProbe active #$index")
        } catch (e: Exception) {
            Log.e(TAG, "camProbe set failed", e)
        }
    }

    fun getCamProbeIndex(): Int {
        return try {
            NativeLibrary.hoennGetCamProbeIndex()
        } catch (e: Exception) {
            Log.e(TAG, "camProbe get failed", e)
            0
        }
    }

    fun dumpCamRE(tag: String): String {
        return try {
            val msg = NativeLibrary.hoennDumpCamRE(tag)
            Log.i(TAG, "cam RE dump: $msg")
            msg
        } catch (e: Exception) {
            Log.e(TAG, "cam RE dump failed", e)
            "dump failed"
        }
    }

    /** @deprecated use applyZoomAssist */
    fun apply(titleId: Long, enabled: Boolean): Boolean = applyZoomAssist(titleId, enabled)
}
