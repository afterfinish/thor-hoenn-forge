// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge

import android.app.Activity
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.CompoundButton
import android.widget.EditText
import android.widget.ImageButton
import kotlin.math.abs

/**
 * Controller-friendly focus for Thor / Odin-style gamepads.
 *
 * Android’s default focus only moves on DPAD *key* events, and AppCompat buttons
 * do not treat [KeyEvent.KEYCODE_BUTTON_A] as a click. Handheld left sticks often
 * report as axes, not DPAD keys — so without this, stick “highlights” nothing useful
 * or focus lands on a full-screen container and A does nothing.
 *
 * Use from [HoennActivity] via [dispatchKey] / [dispatchMotion].
 */
object HoennFocus {
    private const val STICK_DEAD = 0.55f
    private const val STICK_RESET = 0.35f
    private const val HAT_DEAD = 0.5f
    private const val NAV_DEBOUNCE_MS = 160L

    /** Last stick/hat direction we acted on (edge-trigger focus moves). */
    private var lastNavDir = 0
    private var lastNavAtMs = 0L

    fun enable(root: View) {
        // Never let the content root steal focus — that paints a full-screen ring.
        root.isFocusable = false
        root.isFocusableInTouchMode = false
        if (root is ViewGroup) {
            root.descendantFocusability = ViewGroup.FOCUS_AFTER_DESCENDANTS
        }

        walk(root) { v ->
            when (v) {
                is Button,
                is ImageButton,
                is CheckBox,
                is android.widget.RadioButton,
                is EditText,
                -> {
                    v.isFocusable = true
                    v.isFocusableInTouchMode = false
                    v.isClickable = true
                }
                else -> {
                    // Containers with card backgrounds must not take focus (screen ring).
                    if (v !is EditText) {
                        v.isFocusable = false
                        v.isFocusableInTouchMode = false
                    }
                }
            }
        }

        val first = findFirstFocusable(root)
        first?.post {
            if (first.isAttachedToWindow) first.requestFocus()
        }
    }

    /**
     * @deprecated Prefer [HoennActivity] + [dispatchKey]. Kept so call sites stay stable.
     */
    fun installKeyRouting(activity: Activity, root: View) {
        // Do not make root focusable — that was the “highlights the whole screen” bug.
        root.isFocusable = false
        root.isFocusableInTouchMode = false
        // No-op listener: real routing is dispatchKey on HoennActivity.
        root.setOnKeyListener(null)
    }

    /** @return true if the event was handled for UI navigation / activate. */
    fun dispatchKey(activity: Activity, event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return false
        // Ignore repeats for nav; allow A repeat? no
        if (event.repeatCount > 0 && isNavKey(event.keyCode)) return true

        return when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_DPAD_CENTER,
            KeyEvent.KEYCODE_ENTER,
            -> activateFocused(activity)

            KeyEvent.KEYCODE_BUTTON_B,
            -> {
                // Let system back handle finish; only consume if we want custom later.
                false
            }

            KeyEvent.KEYCODE_DPAD_UP -> moveFocus(activity, View.FOCUS_UP)
            KeyEvent.KEYCODE_DPAD_DOWN -> moveFocus(activity, View.FOCUS_DOWN)
            KeyEvent.KEYCODE_DPAD_LEFT -> moveFocus(activity, View.FOCUS_LEFT)
            KeyEvent.KEYCODE_DPAD_RIGHT -> moveFocus(activity, View.FOCUS_RIGHT)

            else -> false
        }
    }

    /**
     * Map left stick / hat to D-pad focus moves (edge-triggered).
     * Right stick (AXIS_Z/RZ) is ignored — that belongs in-game for freecam.
     */
    fun dispatchMotion(activity: Activity, event: MotionEvent): Boolean {
        val sources = event.source
        val isPad =
            (sources and InputDevice.SOURCE_JOYSTICK) != 0 ||
                (sources and InputDevice.SOURCE_GAMEPAD) != 0 ||
                (sources and InputDevice.SOURCE_DPAD) != 0
        if (!isPad) return false
        if (event.actionMasked != MotionEvent.ACTION_MOVE &&
            event.actionMasked != MotionEvent.ACTION_HOVER_MOVE
        ) {
            return false
        }

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        // Left stick only (AXIS_X/Y). Do not use Z/RZ (C-stick / freecam).
        val sx = event.getAxisValue(MotionEvent.AXIS_X)
        val sy = event.getAxisValue(MotionEvent.AXIS_Y)

        val dir = when {
            abs(hatY) >= HAT_DEAD || abs(hatX) >= HAT_DEAD ->
                axisToDir(hatX, hatY, HAT_DEAD)
            abs(sy) >= STICK_DEAD || abs(sx) >= STICK_DEAD ->
                axisToDir(sx, sy, STICK_DEAD)
            else -> 0
        }

        if (dir == 0) {
            // Reset edge trigger when stick returns to center
            if (abs(hatX) < STICK_RESET && abs(hatY) < STICK_RESET &&
                abs(sx) < STICK_RESET && abs(sy) < STICK_RESET
            ) {
                lastNavDir = 0
            }
            return false
        }

        if (dir == lastNavDir) {
            // Already moved this way — hold does not spam focus
            return true
        }
        lastNavDir = dir
        return moveFocus(activity, dir)
    }

    fun ensureFocus(activity: Activity) {
        val focused = activity.currentFocus
        if (focused != null && focused.isFocusable && isActionable(focused)) return
        val root = activity.findViewById<View>(android.R.id.content) ?: return
        findFirstFocusable(root)?.requestFocus()
    }

    private fun isNavKey(code: Int): Boolean =
        code == KeyEvent.KEYCODE_DPAD_UP ||
            code == KeyEvent.KEYCODE_DPAD_DOWN ||
            code == KeyEvent.KEYCODE_DPAD_LEFT ||
            code == KeyEvent.KEYCODE_DPAD_RIGHT

    private fun axisToDir(x: Float, y: Float, dead: Float): Int {
        // Prefer the stronger axis so diagonals pick one direction
        return if (abs(y) >= abs(x) && abs(y) >= dead) {
            if (y < 0) View.FOCUS_UP else View.FOCUS_DOWN
        } else if (abs(x) >= dead) {
            if (x < 0) View.FOCUS_LEFT else View.FOCUS_RIGHT
        } else {
            0
        }
    }

    private fun moveFocus(activity: Activity, direction: Int): Boolean {
        val now = SystemClock.uptimeMillis()
        // Stick often also synthesizes DPAD keys — debounce so focus doesn't double-step
        if (now - lastNavAtMs < NAV_DEBOUNCE_MS) return true
        lastNavAtMs = now

        ensureFocus(activity)
        val focused = activity.currentFocus ?: return false
        val next = focused.focusSearch(direction)
        if (next != null && next !== focused && isActionable(next)) {
            next.requestFocus()
            return true
        }
        // Fallback: walk actionable siblings in document order for up/down
        if (direction == View.FOCUS_DOWN || direction == View.FOCUS_UP ||
            direction == View.FOCUS_LEFT || direction == View.FOCUS_RIGHT
        ) {
            val list = collectActionable(activity)
            if (list.isEmpty()) return false
            val idx = list.indexOf(focused).let { if (it < 0) 0 else it }
            val target = when (direction) {
                View.FOCUS_DOWN, View.FOCUS_RIGHT -> list.getOrNull((idx + 1) % list.size)
                else -> list.getOrNull(if (idx <= 0) list.lastIndex else idx - 1)
            }
            if (target != null && target !== focused) {
                target.requestFocus()
                return true
            }
        }
        return false
    }

    private fun activateFocused(activity: Activity): Boolean {
        ensureFocus(activity)
        val focused = activity.currentFocus ?: return false
        return when (focused) {
            is CheckBox -> {
                focused.toggle()
                true
            }
            is CompoundButton -> {
                focused.toggle()
                true
            }
            else -> {
                if (focused.isClickable || focused.hasOnClickListeners()) {
                    focused.performClick()
                    true
                } else {
                    false
                }
            }
        }
    }

    private fun isActionable(v: View): Boolean =
        v is Button || v is ImageButton || v is CheckBox ||
            v is CompoundButton || v is EditText ||
            (v.isClickable && v.isFocusable)

    private fun collectActionable(activity: Activity): List<View> {
        val root = activity.findViewById<View>(android.R.id.content) ?: return emptyList()
        val out = ArrayList<View>()
        walk(root) { v ->
            if (v.isShown && v.isEnabled && isActionable(v) && v.isFocusable) {
                out.add(v)
            }
        }
        return out
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
            if (found == null && v.isShown && v.isEnabled && isActionable(v) && v.isFocusable) {
                found = v
            }
        }
        return found
    }
}
