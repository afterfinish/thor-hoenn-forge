// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig

/**
 * After dump validation: Vanilla vs Randomized.
 */
class PlayModeActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)
        if (!prefs.hasDump) {
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_play_mode)
        findViewById<TextView>(R.id.textGame).text =
            prefs.dumpGameLabel ?: getString(R.string.app_name)
        findViewById<TextView>(R.id.textBody).setText(R.string.hoenn_play_mode_body)

        findViewById<Button>(R.id.buttonVanilla).setOnClickListener {
            prefs.randomizerConfig = RandomizerConfig.vanilla()
            startActivity(Onboarding.intentTo(this, PrepareActivity::class.java))
            finish()
        }
        findViewById<Button>(R.id.buttonRandomized).setOnClickListener {
            startActivity(Onboarding.intentTo(this, RandomizerActivity::class.java))
            // Keep this activity in back stack for return from builder
        }
    }
}
