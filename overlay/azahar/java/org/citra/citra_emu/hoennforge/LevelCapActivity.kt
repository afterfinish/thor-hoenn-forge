// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.TextView
import org.citra.citra_emu.R

/**
 * Onboarding step: hardcore-nuzlocke level cap, on or off.
 *
 * Sits between play mode and whatever comes next so it applies to a vanilla run and a
 * randomized one alike — the cap is a rule about how you play, not a randomizer setting.
 * The run always starts at stage 0; advancing as you beat each boss is done from the
 * START quick menu, because the Elite Four are stages too and no badge count can say
 * which one you are on.
 */
class LevelCapActivity : HoennActivity() {
    companion object {
        const val EXTRA_NEXT = "hoenn_level_cap_next"
        const val NEXT_PREPARE = "prepare"
        const val NEXT_RANDOMIZER = "randomizer"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)
        if (!prefs.hasDump) {
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_level_cap)
        findViewById<TextView>(R.id.textCapLadder).text = LevelCapLadder.summary()

        val off = findViewById<Button>(R.id.buttonCapOff)
        val on = findViewById<Button>(R.id.buttonCapOn)
        val cardOff = findViewById<View>(R.id.cardCapOff)
        val cardOn = findViewById<View>(R.id.cardCapOn)

        fun choose(enabled: Boolean) {
            if (!off.isEnabled) return
            off.isEnabled = false
            on.isEnabled = false
            prefs.levelCapEnabled = enabled
            prefs.levelCapStage = 0
            val next = when (intent.getStringExtra(EXTRA_NEXT)) {
                NEXT_RANDOMIZER -> RandomizerActivity::class.java
                else -> PrepareActivity::class.java
            }
            startActivity(Onboarding.intentTo(this, next))
            finish()
        }

        off.setOnClickListener { choose(false) }
        on.setOnClickListener { choose(true) }
        cardOff.setOnClickListener { choose(false) }
        cardOn.setOnClickListener { choose(true) }

        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        off.post { off.requestFocus() }
    }
}
