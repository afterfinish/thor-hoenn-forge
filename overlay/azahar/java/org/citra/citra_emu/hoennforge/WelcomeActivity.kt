// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.widget.Button
import android.widget.CheckBox
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import org.citra.citra_emu.R

class WelcomeActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)

        if (prefs.legalAccepted) {
            startActivity(Onboarding.intentTo(this, Onboarding.nextAfterLegal(this, prefs)))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_welcome)
        val root = findViewById<android.view.View>(android.R.id.content)
        val checkbox = findViewById<CheckBox>(R.id.checkboxLegal)
        val button = findViewById<Button>(R.id.buttonContinue)
        checkbox.buttonDrawable = getDrawable(R.drawable.hoenn_checkbox)
        androidx.core.widget.CompoundButtonCompat.setButtonTintList(checkbox, null)
        button.isEnabled = false
        checkbox.setOnCheckedChangeListener { _, checked -> button.isEnabled = checked }
        button.setOnClickListener {
            prefs.legalAccepted = true
            startActivity(Onboarding.intentTo(this, Onboarding.nextAfterLegal(this, prefs)))
            finish()
        }
        findViewById<TextView>(R.id.linkWhatsDump).setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle(R.string.hoenn_whats_dump)
                .setMessage(R.string.hoenn_whats_dump_body)
                .setPositiveButton(android.R.string.ok, null)
                .show()
        }
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        checkbox.post { checkbox.requestFocus() }
    }
}
