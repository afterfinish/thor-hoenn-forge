// Copyright Hoenn Forge — GPU-path free look controls
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary

/**
 * Runtime knobs for the outdoor free look, which rotates the view transform inside the
 * emulator's vertex-shader uniforms rather than writing the game's camera object.
 *
 * Everything here is a debug/tuning surface: the shipping camera needs none of it, but
 * the row detector has to be verifiable on device without a rebuild, so the row index,
 * matrix layout and orbit radius are all reachable from the START menu.
 *
 * Param ids mirror `Hoenn::GpuCam::Param` in video_core/hoenn_gpu_cam.h — keep in sync.
 */
object HoennGpuCam {
    private const val TAG = "HoennForgeGpuCam"

    const val PARAM_PROBE = 0
    const val PARAM_ROW_MODE = 1
    const val PARAM_TRANSPOSE = 2
    const val PARAM_RADIUS = 3
    const val PARAM_DETECTED_ROW = 4
    const val PARAM_QUALIFY_COUNT = 5
    const val PARAM_ACTIVE = 6
    const val PARAM_YAW = 7
    const val PARAM_PITCH = 8

    const val ROW_MODE_AUTO = -1
    const val ROW_MODE_ALL = -2

    /** Highest valid start of a three-row triple in the 96-row uniform block. */
    const val MAX_ROW = 93

    private fun put(param: Int, value: Float) {
        try {
            NativeLibrary.hoennGpuCamSet(param, value)
        } catch (e: Throwable) {
            Log.e(TAG, "set param $param failed", e)
        }
    }

    private fun take(param: Int, fallback: Float = 0f): Float =
        try {
            NativeLibrary.hoennGpuCamGet(param)
        } catch (e: Throwable) {
            Log.e(TAG, "get param $param failed", e)
            fallback
        }

    var probe: Boolean
        get() = take(PARAM_PROBE) != 0f
        set(value) = put(PARAM_PROBE, if (value) 1f else 0f)

    var rowMode: Int
        get() = take(PARAM_ROW_MODE, ROW_MODE_AUTO.toFloat()).toInt()
        set(value) = put(PARAM_ROW_MODE, value.toFloat())

    var transpose: Boolean
        get() = take(PARAM_TRANSPOSE) != 0f
        set(value) = put(PARAM_TRANSPOSE, if (value) 1f else 0f)

    var radius: Float
        get() = take(PARAM_RADIUS, 2300f)
        set(value) = put(PARAM_RADIUS, value)

    val detectedRow: Int
        get() = take(PARAM_DETECTED_ROW, -1f).toInt()

    val qualifyingTriples: Int
        get() = take(PARAM_QUALIFY_COUNT).toInt()

    val isDrivingView: Boolean
        get() = take(PARAM_ACTIVE) != 0f

    /** Push the persisted debug settings into the native side after a boot. */
    fun restore(prefs: HoennPrefs) {
        probe = prefs.gpuCamProbe
        rowMode = prefs.gpuCamRowMode
        transpose = prefs.gpuCamTranspose
        radius = prefs.gpuCamRadius
    }

    fun rowModeLabel(mode: Int): String = when (mode) {
        ROW_MODE_AUTO -> "Auto"
        ROW_MODE_ALL -> "All"
        else -> "Row $mode"
    }
}
