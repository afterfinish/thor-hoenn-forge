// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.Difficulty
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.Preset
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.StarterMode
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.TypeTheme

/**
 * pk3DS-class option builder: presets, seed, module toggles.
 */
class RandomizerActivity : AppCompatActivity() {
    private var seed: Long = RandomizerConfig.newSeed()
    private var activePreset: Preset = Preset.STANDARD

    private lateinit var textSeed: TextView
    private lateinit var editSeed: EditText
    private lateinit var textSummary: TextView
    private lateinit var textWarning: TextView

    private lateinit var cbWildSpecies: CheckBox
    private lateinit var cbWildLevels: CheckBox
    private lateinit var cbWildLegends: CheckBox
    private lateinit var rgWildTheme: RadioGroup

    private lateinit var cbTrainerParties: CheckBox
    private lateinit var cbTrainerItems: CheckBox
    private lateinit var cbTrainerMoves: CheckBox
    private lateinit var cbTrainerAbilities: CheckBox
    private lateinit var rgDifficulty: RadioGroup

    private lateinit var rgStarters: RadioGroup

    private lateinit var cbPersonalTypes: CheckBox
    private lateinit var cbPersonalStats: CheckBox
    private lateinit var cbPersonalAbilities: CheckBox
    private lateinit var cbPersonalTm: CheckBox

    private lateinit var cbMoveTypes: CheckBox
    private lateinit var cbMoveCats: CheckBox
    private lateinit var cbLevelUp: CheckBox
    private lateinit var cbEggMoves: CheckBox
    private lateinit var cbTmList: CheckBox

    private lateinit var cbEvolutions: CheckBox
    private lateinit var cbMarts: CheckBox
    private lateinit var cbStaticGifts: CheckBox

    private lateinit var advancedPanel: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)
        if (!prefs.hasDump) {
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_randomizer)
        bindViews()

        // Start from Standard preset
        applyPreset(Preset.STANDARD, keepSeed = false)
        updateSummary()

        findViewById<Button>(R.id.buttonPresetLight).setOnClickListener {
            applyPreset(Preset.LIGHT)
        }
        findViewById<Button>(R.id.buttonPresetStandard).setOnClickListener {
            applyPreset(Preset.STANDARD)
        }
        findViewById<Button>(R.id.buttonPresetChaos).setOnClickListener {
            applyPreset(Preset.CHAOS)
        }
        findViewById<Button>(R.id.buttonRerollSeed).setOnClickListener {
            seed = RandomizerConfig.newSeed()
            textSeed.text = seed.toString()
            editSeed.setText(seed.toString())
            updateSummary()
        }
        findViewById<Button>(R.id.buttonApplySeed).setOnClickListener {
            val parsed = editSeed.text?.toString()?.trim()?.toLongOrNull()
            if (parsed == null || parsed < 0) {
                Toast.makeText(this, R.string.hoenn_seed_invalid, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            seed = parsed
            textSeed.text = seed.toString()
            updateSummary()
        }
        findViewById<Button>(R.id.buttonToggleAdvanced).setOnClickListener {
            val show = advancedPanel.visibility != View.VISIBLE
            advancedPanel.visibility = if (show) View.VISIBLE else View.GONE
            (it as Button).setText(
                if (show) R.string.hoenn_hide_advanced else R.string.hoenn_show_advanced,
            )
        }

        val refresh = View.OnClickListener {
            activePreset = Preset.NONE // custom once user touches options
            updateSummary()
        }
        listOf(
            cbWildSpecies, cbWildLevels, cbWildLegends,
            cbTrainerParties, cbTrainerItems, cbTrainerMoves, cbTrainerAbilities,
            cbPersonalTypes, cbPersonalStats, cbPersonalAbilities, cbPersonalTm,
            cbMoveTypes, cbMoveCats, cbLevelUp, cbEggMoves, cbTmList,
            cbEvolutions, cbMarts, cbStaticGifts,
        ).forEach { it.setOnClickListener(refresh) }
        rgWildTheme.setOnCheckedChangeListener { _, _ -> refresh.onClick(rgWildTheme) }
        rgDifficulty.setOnCheckedChangeListener { _, _ -> refresh.onClick(rgDifficulty) }
        rgStarters.setOnCheckedChangeListener { _, _ -> refresh.onClick(rgStarters) }

        findViewById<Button>(R.id.buttonBack).setOnClickListener { finish() }
        findViewById<Button>(R.id.buttonForge).setOnClickListener {
            val config = buildConfig()
            prefs.randomizerConfig = config
            startActivity(Onboarding.intentTo(this, PrepareActivity::class.java))
            finish()
        }
    }

    private fun bindViews() {
        textSeed = findViewById(R.id.textSeed)
        editSeed = findViewById(R.id.editSeed)
        textSummary = findViewById(R.id.textSummary)
        textWarning = findViewById(R.id.textWarning)
        advancedPanel = findViewById(R.id.advancedPanel)

        cbWildSpecies = findViewById(R.id.cbWildSpecies)
        cbWildLevels = findViewById(R.id.cbWildLevels)
        cbWildLegends = findViewById(R.id.cbWildLegends)
        rgWildTheme = findViewById(R.id.rgWildTheme)

        cbTrainerParties = findViewById(R.id.cbTrainerParties)
        cbTrainerItems = findViewById(R.id.cbTrainerItems)
        cbTrainerMoves = findViewById(R.id.cbTrainerMoves)
        cbTrainerAbilities = findViewById(R.id.cbTrainerAbilities)
        rgDifficulty = findViewById(R.id.rgDifficulty)

        rgStarters = findViewById(R.id.rgStarters)

        cbPersonalTypes = findViewById(R.id.cbPersonalTypes)
        cbPersonalStats = findViewById(R.id.cbPersonalStats)
        cbPersonalAbilities = findViewById(R.id.cbPersonalAbilities)
        cbPersonalTm = findViewById(R.id.cbPersonalTm)

        cbMoveTypes = findViewById(R.id.cbMoveTypes)
        cbMoveCats = findViewById(R.id.cbMoveCats)
        cbLevelUp = findViewById(R.id.cbLevelUp)
        cbEggMoves = findViewById(R.id.cbEggMoves)
        cbTmList = findViewById(R.id.cbTmList)

        cbEvolutions = findViewById(R.id.cbEvolutions)
        cbMarts = findViewById(R.id.cbMarts)
        cbStaticGifts = findViewById(R.id.cbStaticGifts)
    }

    private fun applyPreset(preset: Preset, keepSeed: Boolean = true) {
        val s = if (keepSeed) seed else RandomizerConfig.newSeed()
        val c = RandomizerConfig.fromPreset(preset, s)
        seed = c.seed
        activePreset = preset
        textSeed.text = seed.toString()
        editSeed.setText(seed.toString())

        cbWildSpecies.isChecked = c.wildSpecies
        cbWildLevels.isChecked = c.wildLevels
        cbWildLegends.isChecked = c.wildLegendaries
        when (c.wildTypeTheme) {
            TypeTheme.NONE -> rgWildTheme.check(R.id.rbThemeNone)
            TypeTheme.MONO -> rgWildTheme.check(R.id.rbThemeMono)
            TypeTheme.DUAL -> rgWildTheme.check(R.id.rbThemeDual)
        }

        cbTrainerParties.isChecked = c.trainerParties
        cbTrainerItems.isChecked = c.trainerItems
        cbTrainerMoves.isChecked = c.trainerMoves
        cbTrainerAbilities.isChecked = c.trainerAbilities
        when (c.trainerDifficulty) {
            Difficulty.WEAKER -> rgDifficulty.check(R.id.rbDiffWeaker)
            Difficulty.SIMILAR -> rgDifficulty.check(R.id.rbDiffSimilar)
            Difficulty.STRONGER -> rgDifficulty.check(R.id.rbDiffStronger)
            Difficulty.RIVAL_PLUS -> rgDifficulty.check(R.id.rbDiffRival)
        }

        when (c.starterMode) {
            StarterMode.VANILLA -> rgStarters.check(R.id.rbStarterVanilla)
            StarterMode.FULL_RANDOM -> rgStarters.check(R.id.rbStarterFull)
            StarterMode.GEN_LIMITED -> rgStarters.check(R.id.rbStarterGen)
            StarterMode.TYPE_BALANCED -> rgStarters.check(R.id.rbStarterType)
        }

        cbPersonalTypes.isChecked = c.personalTypes
        cbPersonalStats.isChecked = c.personalBaseStats
        cbPersonalAbilities.isChecked = c.personalAbilities
        cbPersonalTm.isChecked = c.personalTmCompat

        cbMoveTypes.isChecked = c.moveTypes
        cbMoveCats.isChecked = c.moveCategories
        cbLevelUp.isChecked = c.levelUpLearnsets
        cbEggMoves.isChecked = c.eggMoves
        cbTmList.isChecked = c.tmList

        cbEvolutions.isChecked = c.evolutions
        cbMarts.isChecked = c.specialMarts
        cbStaticGifts.isChecked = c.staticGifts

        updateSummary()
    }

    private fun wildTheme(): TypeTheme = when (rgWildTheme.checkedRadioButtonId) {
        R.id.rbThemeMono -> TypeTheme.MONO
        R.id.rbThemeDual -> TypeTheme.DUAL
        else -> TypeTheme.NONE
    }

    private fun difficulty(): Difficulty = when (rgDifficulty.checkedRadioButtonId) {
        R.id.rbDiffWeaker -> Difficulty.WEAKER
        R.id.rbDiffStronger -> Difficulty.STRONGER
        R.id.rbDiffRival -> Difficulty.RIVAL_PLUS
        else -> Difficulty.SIMILAR
    }

    private fun starterMode(): StarterMode = when (rgStarters.checkedRadioButtonId) {
        R.id.rbStarterFull -> StarterMode.FULL_RANDOM
        R.id.rbStarterGen -> StarterMode.GEN_LIMITED
        R.id.rbStarterType -> StarterMode.TYPE_BALANCED
        else -> StarterMode.VANILLA
    }

    private fun buildConfig(): RandomizerConfig {
        // If user only picked a preset and didn't customize, keep preset name
        val base = RandomizerConfig(
            enabled = true,
            seed = seed,
            preset = activePreset,
            wildSpecies = cbWildSpecies.isChecked,
            wildLevels = cbWildLevels.isChecked,
            wildLegendaries = cbWildLegends.isChecked,
            wildTypeTheme = wildTheme(),
            trainerParties = cbTrainerParties.isChecked,
            trainerItems = cbTrainerItems.isChecked,
            trainerMoves = cbTrainerMoves.isChecked,
            trainerAbilities = cbTrainerAbilities.isChecked,
            trainerDifficulty = difficulty(),
            starterMode = starterMode(),
            personalTypes = cbPersonalTypes.isChecked,
            personalBaseStats = cbPersonalStats.isChecked,
            personalAbilities = cbPersonalAbilities.isChecked,
            personalTmCompat = cbPersonalTm.isChecked,
            moveTypes = cbMoveTypes.isChecked,
            moveCategories = cbMoveCats.isChecked,
            levelUpLearnsets = cbLevelUp.isChecked,
            eggMoves = cbEggMoves.isChecked,
            tmList = cbTmList.isChecked,
            evolutions = cbEvolutions.isChecked,
            specialMarts = cbMarts.isChecked,
            staticGifts = cbStaticGifts.isChecked,
        )
        // Detect custom: differs from preset defaults
        if (activePreset != Preset.NONE) {
            val expected = RandomizerConfig.fromPreset(activePreset, seed)
            if (base.copy(preset = activePreset) != expected) {
                return base.copy(preset = Preset.NONE)
            }
        }
        return base
    }

    private fun updateSummary() {
        val c = buildConfig()
        textSummary.text = c.summaryLines().joinToString("\n")
        textWarning.visibility = if (c.hasSoftlockWarnings()) View.VISIBLE else View.GONE
        if (c.hasSoftlockWarnings()) {
            textWarning.setText(R.string.hoenn_randomizer_warning)
        }
    }
}
