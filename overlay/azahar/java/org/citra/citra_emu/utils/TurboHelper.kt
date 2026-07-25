// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// Hoenn Forge: stronger turbo apply (re-push limit + native FrameLimiter::Reset).

package org.citra.citra_emu.utils

import android.widget.Toast
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.IntSetting

object TurboHelper {
    private var turboSpeedEnabled = false

    fun isTurboSpeedEnabled(): Boolean = turboSpeedEnabled

    fun reloadTurbo(showToast: Boolean) {
        val context = CitraApplication.appContext
        val toastMessage: String

        if (turboSpeedEnabled) {
            // Percent speed (200 = 2×). Floor at 200 so a stale 0/100 setting never "turboes" to 1×.
            val limit = IntSetting.TURBO_LIMIT.int.coerceIn(200, 1000).toDouble()
            NativeLibrary.setTemporaryFrameLimit(limit)
            // Second Reset after a short delay covers the case where the first races a heavy frame.
            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                if (turboSpeedEnabled) {
                    NativeLibrary.setTemporaryFrameLimit(limit)
                }
            }, 50)
            toastMessage = context.getString(R.string.turbo_enabled_toast)
            android.util.Log.i("HoennForge", "Turbo ON limit=$limit%")
        } else {
            NativeLibrary.disableTemporaryFrameLimit()
            toastMessage = context.getString(R.string.turbo_disabled_toast)
            android.util.Log.i("HoennForge", "Turbo OFF")
        }

        if (showToast) {
            Toast.makeText(context, toastMessage, Toast.LENGTH_SHORT).show()
        }
    }

    fun setTurboEnabled(state: Boolean, showToast: Boolean) {
        turboSpeedEnabled = state
        reloadTurbo(showToast)
    }

    fun toggleTurbo(showToast: Boolean) {
        setTurboEnabled(!isTurboSpeedEnabled(), showToast)
    }
}
