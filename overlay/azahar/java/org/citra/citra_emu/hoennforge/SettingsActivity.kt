// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.TextView
import androidx.core.content.ContextCompat
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity

/** Design 25 — Hoenn settings hub (Thor defaults + escape hatch to Azahar). */
class SettingsActivity : HoennActivity() {
    private lateinit var textTitle: TextView
    private lateinit var textBody: TextView

    private data class NavItem(
        val id: Int,
        val label: String,
        val titleRes: Int,
        val bodyRes: Int,
    )

    private val navItems by lazy {
        listOf(
            NavItem(R.id.navDisplay, "Display", R.string.hoenn_settings_display, R.string.hoenn_settings_display_body),
            NavItem(R.id.navControls, "Controls", R.string.hoenn_settings_controls, R.string.hoenn_settings_controls_body),
            NavItem(R.id.navGraphics, "Graphics performance", R.string.hoenn_settings_graphics, R.string.hoenn_settings_graphics_body),
            NavItem(R.id.navRandomizer, "Randomizer", R.string.hoenn_settings_randomizer, R.string.hoenn_settings_randomizer_body),
            NavItem(R.id.navData, "Data", R.string.hoenn_settings_data, R.string.hoenn_settings_data_body),
            NavItem(R.id.navAbout, "About", R.string.hoenn_settings_about, R.string.hoenn_settings_about_body),
        )
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_hoenn_settings)
        textTitle = findViewById(R.id.textSectionTitle)
        textBody = findViewById(R.id.textSectionBody)

        val labelColor = ContextCompat.getColorStateList(this, R.color.hoenn_nav_text)
        for (item in navItems) {
            val tv = findViewById<TextView>(item.id)
            // Literal labels — never depend on string resolve for the chip text.
            tv.text = item.label
            tv.setTextColor(labelColor)
            tv.setOnClickListener {
                selectNav(item.id, item.titleRes, item.bodyRes)
            }
        }
        selectNav(
            R.id.navDisplay,
            R.string.hoenn_settings_display,
            R.string.hoenn_settings_display_body,
        )

        findViewById<Button>(R.id.buttonAdvanced).setOnClickListener {
            startActivity(Intent(this, MainActivity::class.java))
        }
        findViewById<Button>(R.id.buttonBack).setOnClickListener { finish() }

        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        // Re-assert labels + focusability after HoennFocus (it clears focus on plain TextViews).
        for (item in navItems) {
            val tv = findViewById<TextView>(item.id)
            tv.text = item.label
            tv.setTextColor(labelColor)
            tv.isFocusable = true
            tv.isFocusableInTouchMode = false
            tv.isClickable = true
        }
        HoennFocus.installKeyRouting(this, root)
        findViewById<View>(R.id.navDisplay).post {
            findViewById<View>(R.id.navDisplay).requestFocus()
        }
    }

    private fun selectNav(activeId: Int, titleRes: Int, bodyRes: Int) {
        for (item in navItems) {
            findViewById<View>(item.id).setBackgroundResource(
                if (item.id == activeId) {
                    R.drawable.hoenn_bg_card_selected
                } else {
                    R.drawable.hoenn_bg_card
                },
            )
        }
        textTitle.setText(titleRes)
        textBody.setText(bodyRes)
    }
}
