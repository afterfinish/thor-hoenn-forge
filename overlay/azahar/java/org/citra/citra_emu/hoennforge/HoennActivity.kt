// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge

import android.view.KeyEvent
import android.view.MotionEvent
import androidx.appcompat.app.AppCompatActivity

/**
 * Base for Hoenn shell screens: routes gamepad keys / left-stick to [HoennFocus]
 * so D-pad, stick, and A work without touch.
 */
open class HoennActivity : AppCompatActivity() {
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (HoennFocus.dispatchKey(this, event)) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (HoennFocus.dispatchMotion(this, event)) return true
        return super.dispatchGenericMotionEvent(event)
    }

    override fun onResume() {
        super.onResume()
        // After dialogs / activity returns, put focus back on a button
        window.decorView.post { HoennFocus.ensureFocus(this) }
    }
}
