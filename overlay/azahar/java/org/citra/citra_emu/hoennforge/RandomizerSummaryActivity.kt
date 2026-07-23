// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig

/**
 * Design screen 19 — review run before prepare.
 */
class RandomizerSummaryActivity : HoennActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)
        val config = prefs.randomizerConfig
        if (!config.enabled) {
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_randomizer_summary)

        findViewById<TextView>(R.id.textSeed).text = config.seedDisplay()
        findViewById<TextView>(R.id.chipGame).text =
            prefs.dumpGameLabel ?: getString(R.string.hoenn_brand)
        findViewById<TextView>(R.id.chipModules).text =
            getString(R.string.hoenn_modules_count, config.modulesOnCount())

        val rows = findViewById<LinearLayout>(R.id.summaryRows)
        rows.removeAllViews()
        addRow(rows, getString(R.string.hoenn_cat_wilds), config.wildsBlurb())
        addRow(rows, getString(R.string.hoenn_cat_trainers), config.trainersBlurb())
        addRow(rows, getString(R.string.hoenn_cat_starters), config.startersBlurb())
        addRow(
            rows,
            getString(R.string.hoenn_cat_personal_short) + ", " +
                getString(R.string.hoenn_cat_moves_short).lowercase() + ", " +
                getString(R.string.hoenn_cat_evolutions).lowercase() + ", " +
                getString(R.string.hoenn_cat_misc_short).lowercase(),
            when {
                config.personalOn() || config.movesOn() || config.evolutionsOn() || config.miscOn() ->
                    listOfNotNull(
                        if (config.personalOn()) "personal" else null,
                        if (config.movesOn()) "moves" else null,
                        if (config.evolutionsOn()) "evolutions" else null,
                        if (config.miscOn()) "misc" else null,
                    ).joinToString(" · ")
                else -> getString(R.string.hoenn_status_vanilla)
            },
        )

        val warn = findViewById<TextView>(R.id.textWarning)
        warn.visibility = if (config.hasSoftlockWarnings()) View.VISIBLE else View.GONE

        findViewById<Button>(R.id.buttonBack).setOnClickListener { finish() }
        findViewById<Button>(R.id.buttonForge).setOnClickListener {
            prefs.randomizerConfig = config
            startActivity(Onboarding.intentTo(this, PrepareActivity::class.java))
            finish()
        }

        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        findViewById<Button>(R.id.buttonForge).post {
            findViewById<Button>(R.id.buttonForge).requestFocus()
        }
    }

    private fun addRow(parent: LinearLayout, left: String, right: String) {
        val row = LinearLayout(this)
        row.orientation = LinearLayout.HORIZONTAL
        row.setPadding(0, 14, 0, 14)
        val l = TextView(this)
        l.layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        l.text = left
        l.setTextAppearance(this, R.style.Hoenn_Text_Body)
        val r = TextView(this)
        r.layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT,
        )
        r.text = right
        r.setTextAppearance(this, R.style.Hoenn_Text_Muted)
        r.gravity = android.view.Gravity.END
        row.addView(l)
        row.addView(r)
        parent.addView(row)
        val rule = View(this)
        rule.layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            1,
        )
        rule.setBackgroundColor(0x22FFFFFF)
        parent.addView(rule)
    }
}
