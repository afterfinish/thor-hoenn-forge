// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.app.Activity
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.util.WeakHashMap

/**
 * Edge-to-edge insets for the Hoenn shell.
 *
 * Every Hoenn screen is a full-window surface painted with the dark
 * `hoenn_bg_screen` ground. Drawing behind the bars is what lets that ground
 * reach the physical edges (no light band beside a dark screen), but the content
 * still has to stay clear of the status bar, the navigation bar and — in
 * landscape — the punch-hole cutout on a vertical edge.
 *
 * [apply] remembers the padding the layout declared and adds the live
 * `systemBars() or displayCutout()` insets on top of it, so the 12 dp (phone) or
 * 32 dp (Thor) outer margin from `hoenn_dimens.xml` is preserved inside the safe
 * area on every device and after every configuration change.
 *
 * The base padding is captured once per view: inset dispatch runs on every
 * layout/resize, and re-capturing would compound the padding.
 */
object HoennInsets {
    private val basePadding = WeakHashMap<View, IntArray>()

    /** Attach the listener to the root view a Hoenn activity just inflated. */
    fun applyToContent(activity: Activity) {
        val content = activity.findViewById<ViewGroup>(android.R.id.content) ?: return
        if (content.childCount == 0) return
        apply(content.getChildAt(0))
    }

    fun apply(root: View) {
        if (basePadding.containsKey(root)) return
        val base = intArrayOf(
            root.paddingLeft,
            root.paddingTop,
            root.paddingRight,
            root.paddingBottom,
        )
        basePadding[root] = base

        ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bars = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout(),
            )
            view.setPadding(
                base[0] + bars.left,
                base[1] + bars.top,
                base[2] + bars.right,
                base[3] + bars.bottom,
            )
            insets
        }
        ViewCompat.requestApplyInsets(root)
    }
}
