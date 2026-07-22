// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge

import android.app.Activity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.widget.CheckBox
import android.widget.CompoundButton

/**
 * Controller-friendly focus for Thor / Odin-style gamepads.
 * D-pad / left stick move focus; A toggles checkboxes or clicks; B finishes activity.
 */
object HoennFocus {
    fun enable(root: View) {
        root.isFocusable = false
        walk(root) { v ->
            when (v) {
                is android.widget.Button,
                is CheckBox,
                is android.widget.RadioButton,
                is android.widget.EditText,
                -> {
                    v.isFocusable = true
                    v.isFocusableInTouchMode = false
                    v.isClickable = true
                }
            }
        }
        // Prefer first button/checkbox
        val first = findFirstFocusable(root)
        first?.post { first.requestFocus() }
    }

    fun installKeyRouting(activity: Activity, root: View) {
        root.isFocusableInTouchMode = true
        root.setOnKeyListener { _, keyCode, event ->
            if (event.action != KeyEvent.ACTION_DOWN) return@setOnKeyListener false
            when (keyCode) {
                KeyEvent.KEYCODE_BUTTON_B,
                KeyEvent.KEYCODE_BACK,
                -> {
                    // Let system back work; don't finish unless no focus handler
                    false
                }
                KeyEvent.KEYCODE_BUTTON_A,
                KeyEvent.KEYCODE_DPAD_CENTER,
                -> {
                    val focused = activity.currentFocus
                    when (focused) {
                        is CheckBox -> {
                            focused.toggle()
                            true
                        }
                        is CompoundButton -> {
                            focused.toggle()
                            true
                        }
                        is View -> {
                            focused.performClick()
                            true
                        }
                        else -> false
                    }
                }
                else -> false
            }
        }
    }

    private fun walk(v: View, fn: (View) -> Unit) {
        fn(v)
        if (v is ViewGroup) {
            for (i in 0 until v.childCount) walk(v.getChildAt(i), fn)
        }
    }

    private fun findFirstFocusable(root: View): View? {
        var found: View? = null
        walk(root) { v ->
            if (found == null && v.isFocusable && (v is android.widget.Button || v is CheckBox)) {
                found = v
            }
        }
        return found
    }
}
