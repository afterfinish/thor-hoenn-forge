// Copyright Hoenn Forge — experimental overworld follower probe
package org.citra.citra_emu.hoennforge

import android.app.Activity
import android.util.Log
import org.citra.citra_emu.NativeLibrary

/**
 * Rough follower experiment.
 *
 * 1) Read party lead from save (PK6) — dogfood party e.g. 83,391,161
 * 2) Show sprite ghost on bottom screen lagging with stick
 * 3) Native path-lag thrash (budgeted) still runs in case any field object moves
 *
 * Not HG/SS-quality in-engine mon model (needs MdlAdd / BehindWalk RE).
 */
object HoennFollower {
    private const val TAG = "HoennForgeFollower"

    @Volatile
    var lastParty: List<Int> = emptyList()
        private set

    @Volatile
    var lastLead: Int = 0
        private set

    fun isSupportedTitle(titleId: Long): Boolean =
        OrasTitles.find(titleId) != null

    fun apply(activity: Activity?, titleId: Long, enabled: Boolean): Boolean {
        if (titleId == 0L || !isSupportedTitle(titleId)) {
            Log.w(TAG, "follower: unsupported title")
            return false
        }
        return try {
            if (enabled) {
                val party = OrasPartyReader.readParty(titleId)
                val lead = party?.lead ?: 0
                lastParty = party?.species ?: emptyList()
                lastLead = lead
                if (lead in 1..721) {
                    try {
                        NativeLibrary.setHoennFollowerPartySpecies(lead)
                    } catch (e: Exception) {
                        Log.w(TAG, "set party species jni missing?", e)
                    }
                }
                NativeLibrary.setHoennFollowerProbe(true)
                if (activity != null && lead in 1..721) {
                    FollowerGhostOverlay.show(activity, lead, lastParty)
                } else if (activity != null) {
                    // Still enable native; ghost needs a species — try 83 if empty (won't match all saves)
                    Log.w(TAG, "party lead unknown — native only")
                }
                Log.i(TAG, "follower ON lead=$lead party=$lastParty")
            } else {
                NativeLibrary.setHoennFollowerProbe(false)
                FollowerGhostOverlay.hide(activity)
                Log.i(TAG, "follower OFF")
            }
            true
        } catch (e: Exception) {
            Log.e(TAG, "follower probe failed", e)
            false
        }
    }

    /** Backward-compatible (no activity → no ghost overlay). */
    fun apply(titleId: Long, enabled: Boolean): Boolean = apply(null, titleId, enabled)

    fun statusLine(): String {
        return try {
            val native = NativeLibrary.hoennFollowerStatus()
            if (lastLead > 0) "lead=$lastLead $native" else native
        } catch (e: Exception) {
            Log.e(TAG, "status failed", e)
            if (lastLead > 0) "lead=$lastLead fol=?" else "fol=?"
        }
    }
}
