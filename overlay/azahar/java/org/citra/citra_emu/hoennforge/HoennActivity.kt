// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge

import android.graphics.Color
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat

/**
 * Base for Hoenn shell screens.
 *
 *  - Routes gamepad keys / left-stick to [HoennFocus] so D-pad, stick and A work
 *    without touch.
 *  - Runs every screen edge-to-edge and pads the content out of the system bars
 *    and the display cutout ([HoennInsets]). Without this the shell inherited
 *    Theme.Citra's transparent bars over a light window background, which is what
 *    produced the white band at the screen edges on a phone.
 *
 * AppCompatActivity.setContentView is final in the AndroidX version used here,
 * so the insets are applied by posting to the decor view after onCreate returns;
 * by then subclasses have called setContentView.
 */
open class HoennActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Draw behind the bars on every supported API level; Theme.Hoenn.Forge
        // supplies the dark ground and light bar icons.
        WindowCompat.setDecorFitsSystemWindows(window, false)
        @Suppress("DEPRECATION")
        window.statusBarColor = Color.TRANSPARENT
        @Suppress("DEPRECATION")
        window.navigationBarColor = Color.TRANSPARENT

        window.decorView.post { HoennInsets.applyToContent(this) }
    }

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
