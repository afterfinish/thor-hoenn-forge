// Copyright Hoenn Forge — experimental overworld follower probe
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary

/**
 * Rough path-lag follower experiment (native [Hoenn::FollowerProbe]).
 * Not HG/SS-quality. START menu toggle only.
 */
object HoennFollower {
    private const val TAG = "HoennForgeFollower"

    fun isSupportedTitle(titleId: Long): Boolean =
        OrasTitles.find(titleId) != null

    fun apply(titleId: Long, enabled: Boolean): Boolean {
        if (titleId == 0L || !isSupportedTitle(titleId)) {
            Log.w(TAG, "follower: unsupported title")
            return false
        }
        return try {
            NativeLibrary.setHoennFollowerProbe(enabled)
            Log.i(TAG, "follower probe=$enabled")
            true
        } catch (e: Exception) {
            Log.e(TAG, "follower probe failed", e)
            false
        }
    }

    fun statusLine(): String {
        return try {
            NativeLibrary.hoennFollowerStatus()
        } catch (e: Exception) {
            Log.e(TAG, "status failed", e)
            "fol=?"
        }
    }
}
