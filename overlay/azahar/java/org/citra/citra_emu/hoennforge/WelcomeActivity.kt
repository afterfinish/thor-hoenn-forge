// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.widget.Button
import android.widget.CheckBox
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
        val checkbox = findViewById<CheckBox>(R.id.checkboxLegal)
        val button = findViewById<Button>(R.id.buttonContinue)
        button.isEnabled = false
        checkbox.setOnCheckedChangeListener { _, checked -> button.isEnabled = checked }
        button.setOnClickListener {
            prefs.legalAccepted = true
            // Always go through onboarding router (data dir → dump → home)
            startActivity(Onboarding.intentTo(this, Onboarding.nextAfterLegal(this, prefs)))
            finish()
        }
    }
}
