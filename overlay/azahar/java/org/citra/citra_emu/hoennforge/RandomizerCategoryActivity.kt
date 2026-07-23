// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import androidx.core.widget.CompoundButtonCompat
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.Difficulty
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.StarterMode
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.TypeTheme

/**
 * Category fine-tune screen (design 13–18 pattern).
 */
class RandomizerCategoryActivity : HoennActivity() {
    private lateinit var prefs: HoennPrefs
    private lateinit var config: RandomizerConfig
    private lateinit var category: String
    private lateinit var optionsPanel: LinearLayout

    private val checks = mutableListOf<CheckBox>()
    private var rgTheme: RadioGroup? = null
    private var rgDiff: RadioGroup? = null
    private var rgStarter: RadioGroup? = null

    // Dynamic radio ids (not in R — hub layouts no longer define them)
    private var idThemeNone = View.NO_ID
    private var idThemeMono = View.NO_ID
    private var idThemeDual = View.NO_ID
    private var idDiffWeaker = View.NO_ID
    private var idDiffSimilar = View.NO_ID
    private var idDiffStronger = View.NO_ID
    private var idDiffRival = View.NO_ID
    private var idStarterVanilla = View.NO_ID
    private var idStarterFull = View.NO_ID
    private var idStarterGen = View.NO_ID
    private var idStarterType = View.NO_ID

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = HoennPrefs(this)
        category = intent.getStringExtra(EXTRA_CATEGORY) ?: CAT_WILDS
        config = prefs.randomizerConfig
        if (!config.enabled) {
            config = RandomizerConfig.fromPreset(RandomizerConfig.Preset.STANDARD)
        }

        setContentView(R.layout.activity_hoenn_randomizer_category)
        optionsPanel = findViewById(R.id.optionsPanel)
        findViewById<TextView>(R.id.textCatTitle).text = titleFor(category)

        buildOptions()

        findViewById<Button>(R.id.buttonBack).setOnClickListener { finish() }
        findViewById<Button>(R.id.buttonSave).setOnClickListener {
            prefs.randomizerConfig = collect().copy(enabled = true, preset = RandomizerConfig.Preset.NONE)
            finish()
        }

        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
    }

    private fun titleFor(cat: String): String = when (cat) {
        CAT_WILDS -> getString(R.string.hoenn_cat_wilds)
        CAT_TRAINERS -> getString(R.string.hoenn_cat_trainers)
        CAT_STARTERS -> getString(R.string.hoenn_cat_starters)
        CAT_PERSONAL -> getString(R.string.hoenn_cat_personal_short)
        CAT_MOVES -> getString(R.string.hoenn_cat_moves_short)
        CAT_EVOLUTIONS -> getString(R.string.hoenn_cat_evolutions)
        CAT_MISC -> getString(R.string.hoenn_cat_misc_short)
        else -> cat
    }

    private fun buildOptions() {
        optionsPanel.removeAllViews()
        checks.clear()
        when (category) {
            CAT_WILDS -> {
                addCheck("wildSpecies", R.string.hoenn_opt_wild_species, config.wildSpecies)
                addCheck("wildLevels", R.string.hoenn_opt_wild_levels, config.wildLevels)
                addCheck("wildLegendaries", R.string.hoenn_opt_wild_legends, config.wildLegendaries)
                addLabel(R.string.hoenn_opt_wild_theme)
                idThemeNone = View.generateViewId()
                idThemeMono = View.generateViewId()
                idThemeDual = View.generateViewId()
                rgTheme = addRadio(
                    listOf(
                        idThemeNone to R.string.hoenn_theme_none,
                        idThemeMono to R.string.hoenn_theme_mono,
                        idThemeDual to R.string.hoenn_theme_dual,
                    ),
                    when (config.wildTypeTheme) {
                        TypeTheme.MONO -> idThemeMono
                        TypeTheme.DUAL -> idThemeDual
                        else -> idThemeNone
                    },
                )
            }
            CAT_TRAINERS -> {
                addCheck("trainerParties", R.string.hoenn_opt_trainer_parties, config.trainerParties)
                addCheck("trainerItems", R.string.hoenn_opt_trainer_items, config.trainerItems)
                addCheck("trainerMoves", R.string.hoenn_opt_trainer_moves, config.trainerMoves)
                addCheck("trainerAbilities", R.string.hoenn_opt_trainer_abilities, config.trainerAbilities)
                addLabel(R.string.hoenn_opt_trainer_difficulty)
                idDiffWeaker = View.generateViewId()
                idDiffSimilar = View.generateViewId()
                idDiffStronger = View.generateViewId()
                idDiffRival = View.generateViewId()
                rgDiff = addRadio(
                    listOf(
                        idDiffWeaker to R.string.hoenn_diff_weaker,
                        idDiffSimilar to R.string.hoenn_diff_similar,
                        idDiffStronger to R.string.hoenn_diff_stronger,
                        idDiffRival to R.string.hoenn_diff_rival,
                    ),
                    when (config.trainerDifficulty) {
                        Difficulty.WEAKER -> idDiffWeaker
                        Difficulty.STRONGER -> idDiffStronger
                        Difficulty.RIVAL_PLUS -> idDiffRival
                        else -> idDiffSimilar
                    },
                )
            }
            CAT_STARTERS -> {
                addLabel(R.string.hoenn_cat_starters)
                idStarterVanilla = View.generateViewId()
                idStarterFull = View.generateViewId()
                idStarterGen = View.generateViewId()
                idStarterType = View.generateViewId()
                rgStarter = addRadio(
                    listOf(
                        idStarterVanilla to R.string.hoenn_starter_vanilla,
                        idStarterFull to R.string.hoenn_starter_full,
                        idStarterGen to R.string.hoenn_starter_gen,
                        idStarterType to R.string.hoenn_starter_type,
                    ),
                    when (config.starterMode) {
                        StarterMode.FULL_RANDOM -> idStarterFull
                        StarterMode.GEN_LIMITED -> idStarterGen
                        StarterMode.TYPE_BALANCED -> idStarterType
                        else -> idStarterVanilla
                    },
                )
            }
            CAT_PERSONAL -> {
                addCheck("personalTypes", R.string.hoenn_opt_personal_types, config.personalTypes)
                addCheck("personalBaseStats", R.string.hoenn_opt_personal_stats, config.personalBaseStats)
                addCheck("personalAbilities", R.string.hoenn_opt_personal_abilities, config.personalAbilities)
                addCheck("personalTmCompat", R.string.hoenn_opt_personal_tm, config.personalTmCompat)
            }
            CAT_MOVES -> {
                addCheck("moveTypes", R.string.hoenn_opt_move_types, config.moveTypes)
                addCheck("moveCategories", R.string.hoenn_opt_move_cats, config.moveCategories)
                addCheck("levelUpLearnsets", R.string.hoenn_opt_level_up, config.levelUpLearnsets)
                addCheck("eggMoves", R.string.hoenn_opt_egg_moves, config.eggMoves)
                addCheck("tmList", R.string.hoenn_opt_tm_list, config.tmList)
            }
            CAT_EVOLUTIONS -> {
                addCheck("evolutions", R.string.hoenn_opt_evolutions, config.evolutions)
            }
            CAT_MISC -> {
                addCheck("specialMarts", R.string.hoenn_opt_marts, config.specialMarts)
                addCheck("staticGifts", R.string.hoenn_opt_static, config.staticGifts)
            }
        }
    }

    private fun addLabel(res: Int) {
        val tv = TextView(this)
        tv.setText(res)
        tv.setTextAppearance(R.style.Hoenn_Text_Muted)
        tv.setPadding(0, 16, 0, 8)
        optionsPanel.addView(tv)
    }

    private fun addCheck(tag: String, label: Int, checked: Boolean): CheckBox {
        val cb = CheckBox(this)
        cb.tag = tag
        cb.setText(label)
        cb.isChecked = checked
        cb.minHeight = resources.getDimensionPixelSize(R.dimen.hoenn_row_min)
        cb.setTextAppearance(R.style.Hoenn_Text_Body)
        val d = getDrawable(R.drawable.hoenn_checkbox)?.constantState?.newDrawable()?.mutate()
        cb.buttonDrawable = d
        CompoundButtonCompat.setButtonTintList(cb, null)
        optionsPanel.addView(
            cb,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ),
        )
        checks += cb
        return cb
    }

    private fun addRadio(options: List<Pair<Int, Int>>, checkedId: Int): RadioGroup {
        val rg = RadioGroup(this)
        rg.orientation = LinearLayout.VERTICAL
        for ((id, label) in options) {
            val rb = RadioButton(this)
            rb.id = id
            rb.setText(label)
            rb.setTextColor(getColor(R.color.hoenn_text))
            rb.minHeight = resources.getDimensionPixelSize(R.dimen.hoenn_row_min)
            rg.addView(rb)
        }
        rg.check(checkedId)
        optionsPanel.addView(rg)
        return rg
    }

    private fun checked(tag: String): Boolean =
        checks.firstOrNull { it.tag == tag }?.isChecked == true

    private fun collect(): RandomizerConfig {
        var c = config
        when (category) {
            CAT_WILDS -> {
                val theme = when (rgTheme?.checkedRadioButtonId) {
                    idThemeMono -> TypeTheme.MONO
                    idThemeDual -> TypeTheme.DUAL
                    else -> TypeTheme.NONE
                }
                c = c.copy(
                    wildSpecies = checked("wildSpecies"),
                    wildLevels = checked("wildLevels"),
                    wildLegendaries = checked("wildLegendaries"),
                    wildTypeTheme = theme,
                )
            }
            CAT_TRAINERS -> {
                val diff = when (rgDiff?.checkedRadioButtonId) {
                    idDiffWeaker -> Difficulty.WEAKER
                    idDiffStronger -> Difficulty.STRONGER
                    idDiffRival -> Difficulty.RIVAL_PLUS
                    else -> Difficulty.SIMILAR
                }
                c = c.copy(
                    trainerParties = checked("trainerParties"),
                    trainerItems = checked("trainerItems"),
                    trainerMoves = checked("trainerMoves"),
                    trainerAbilities = checked("trainerAbilities"),
                    trainerDifficulty = diff,
                )
            }
            CAT_STARTERS -> {
                val mode = when (rgStarter?.checkedRadioButtonId) {
                    idStarterFull -> StarterMode.FULL_RANDOM
                    idStarterGen -> StarterMode.GEN_LIMITED
                    idStarterType -> StarterMode.TYPE_BALANCED
                    else -> StarterMode.VANILLA
                }
                c = c.copy(starterMode = mode)
            }
            CAT_PERSONAL -> c = c.copy(
                personalTypes = checked("personalTypes"),
                personalBaseStats = checked("personalBaseStats"),
                personalAbilities = checked("personalAbilities"),
                personalTmCompat = checked("personalTmCompat"),
            )
            CAT_MOVES -> c = c.copy(
                moveTypes = checked("moveTypes"),
                moveCategories = checked("moveCategories"),
                levelUpLearnsets = checked("levelUpLearnsets"),
                eggMoves = checked("eggMoves"),
                tmList = checked("tmList"),
            )
            CAT_EVOLUTIONS -> c = c.copy(evolutions = checked("evolutions"))
            CAT_MISC -> c = c.copy(
                specialMarts = checked("specialMarts"),
                staticGifts = checked("staticGifts"),
            )
        }
        return c
    }

    companion object {
        const val EXTRA_CATEGORY = "category"
        const val CAT_WILDS = "wilds"
        const val CAT_TRAINERS = "trainers"
        const val CAT_STARTERS = "starters"
        const val CAT_PERSONAL = "personal"
        const val CAT_MOVES = "moves"
        const val CAT_EVOLUTIONS = "evolutions"
        const val CAT_MISC = "misc"
    }
}
