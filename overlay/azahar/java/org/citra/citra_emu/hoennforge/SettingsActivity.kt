// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.TextView
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity

/** Design 25 — Hoenn settings hub (Thor defaults + escape hatch to Azahar). */
class SettingsActivity : HoennActivity() {
    private lateinit var textTitle: TextView
    private lateinit var textBody: TextView
    private val navIds = listOf(
        R.id.navDisplay to (R.string.hoenn_settings_display to R.string.hoenn_settings_display_body),
        R.id.navControls to (R.string.hoenn_settings_controls to R.string.hoenn_settings_controls_body),
        R.id.navGraphics to (R.string.hoenn_settings_graphics to R.string.hoenn_settings_graphics_body),
        R.id.navRandomizer to (R.string.hoenn_settings_randomizer to R.string.hoenn_settings_randomizer_body),
        R.id.navData to (R.string.hoenn_settings_data to R.string.hoenn_settings_data_body),
        R.id.navAbout to (R.string.hoenn_settings_about to R.string.hoenn_settings_about_body),
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_hoenn_settings)
        textTitle = findViewById(R.id.textSectionTitle)
        textBody = findViewById(R.id.textSectionBody)

        for ((id, pair) in navIds) {
            val (titleRes, bodyRes) = pair
            findViewById<View>(id).setOnClickListener {
                selectNav(id, titleRes, bodyRes)
            }
        }
        selectNav(R.id.navDisplay, R.string.hoenn_settings_display, R.string.hoenn_settings_display_body)

        findViewById<Button>(R.id.buttonAdvanced).setOnClickListener {
            startActivity(Intent(this, MainActivity::class.java))
        }
        findViewById<Button>(R.id.buttonBack).setOnClickListener { finish() }

        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        findViewById<View>(R.id.navDisplay).post {
            findViewById<View>(R.id.navDisplay).requestFocus()
        }
    }

    private fun selectNav(activeId: Int, titleRes: Int, bodyRes: Int) {
        for ((id, _) in navIds) {
            findViewById<View>(id).setBackgroundResource(
                if (id == activeId) R.drawable.hoenn_bg_card_selected else R.drawable.hoenn_bg_card,
            )
        }
        textTitle.setText(titleRes)
        textBody.setText(bodyRes)
    }
}
