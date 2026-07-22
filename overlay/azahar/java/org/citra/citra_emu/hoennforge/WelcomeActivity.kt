// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.CheckBox
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import org.citra.citra_emu.R

class WelcomeActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)

        if (prefs.legalAccepted && prefs.hasDump) {
            startActivity(Intent(this, HomeActivity::class.java))
            finish()
            return
        }
        if (prefs.legalAccepted) {
            startActivity(Intent(this, DumpPickerActivity::class.java))
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
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
        }
    }
}
