// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig.Preset

/**
 * Design screen 12 — randomizer hub: presets + category cards → fine-tune / review.
 */
class RandomizerActivity : AppCompatActivity() {
    private lateinit var prefs: HoennPrefs
    private var config: RandomizerConfig = RandomizerConfig.fromPreset(Preset.STANDARD)

    private lateinit var textSeed: TextView
    private lateinit var chipGame: TextView
    private lateinit var cardLight: View
    private lateinit var cardStandard: View
    private lateinit var cardChaos: View

    private data class CatCard(
        val root: View,
        val title: TextView,
        val blurb: TextView,
        val chip: TextView,
    )

    private lateinit var catWilds: CatCard
    private lateinit var catTrainers: CatCard
    private lateinit var catStarters: CatCard
    private lateinit var catPersonal: CatCard
    private lateinit var catMoves: CatCard
    private lateinit var catEvolutions: CatCard
    private lateinit var catMisc: CatCard

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = HoennPrefs(this)
        if (!prefs.hasDump) {
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_randomizer)

        // Resume draft if returning from category/summary
        val existing = prefs.randomizerConfig
        config = if (existing.enabled) existing else RandomizerConfig.fromPreset(Preset.STANDARD)

        textSeed = findViewById(R.id.textSeed)
        chipGame = findViewById(R.id.chipGame)
        chipGame.text = prefs.dumpGameLabel ?: getString(R.string.hoenn_brand)

        cardLight = findViewById(R.id.cardPresetLight)
        cardStandard = findViewById(R.id.cardPresetStandard)
        cardChaos = findViewById(R.id.cardPresetChaos)

        catWilds = bindCard(R.id.catWilds)
        catTrainers = bindCard(R.id.catTrainers)
        catStarters = bindCard(R.id.catStarters)
        catPersonal = bindCard(R.id.catPersonal)
        catMoves = bindCard(R.id.catMoves)
        catEvolutions = bindCard(R.id.catEvolutions)
        catMisc = bindCard(R.id.catMisc)

        cardLight.setOnClickListener { applyPreset(Preset.LIGHT) }
        cardStandard.setOnClickListener { applyPreset(Preset.STANDARD) }
        cardChaos.setOnClickListener { applyPreset(Preset.CHAOS) }

        findViewById<Button>(R.id.buttonRerollSeed).setOnClickListener {
            config = config.copy(seed = RandomizerConfig.newSeed())
            persistAndRefresh()
        }
        findViewById<Button>(R.id.buttonBack).setOnClickListener { finish() }
        findViewById<Button>(R.id.buttonReview).setOnClickListener {
            prefs.randomizerConfig = config.copy(enabled = true)
            startActivity(Onboarding.intentTo(this, RandomizerSummaryActivity::class.java))
        }

        openCategory(catWilds, RandomizerCategoryActivity.CAT_WILDS)
        openCategory(catTrainers, RandomizerCategoryActivity.CAT_TRAINERS)
        openCategory(catStarters, RandomizerCategoryActivity.CAT_STARTERS)
        openCategory(catPersonal, RandomizerCategoryActivity.CAT_PERSONAL)
        openCategory(catMoves, RandomizerCategoryActivity.CAT_MOVES)
        openCategory(catEvolutions, RandomizerCategoryActivity.CAT_EVOLUTIONS)
        openCategory(catMisc, RandomizerCategoryActivity.CAT_MISC)

        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        cardStandard.post { cardStandard.requestFocus() }
        refreshUi()
    }

    override fun onResume() {
        super.onResume()
        if (::prefs.isInitialized) {
            val c = prefs.randomizerConfig
            if (c.enabled) config = c
            refreshUi()
        }
    }

    private fun bindCard(includeId: Int): CatCard {
        val root = findViewById<View>(includeId)
        return CatCard(
            root = root,
            title = root.findViewById(R.id.textCatTitle),
            blurb = root.findViewById(R.id.textCatBlurb),
            chip = root.findViewById(R.id.chipStatus),
        )
    }

    private fun openCategory(card: CatCard, category: String) {
        card.root.setOnClickListener {
            prefs.randomizerConfig = config.copy(enabled = true)
            startActivity(
                Onboarding.intentTo(this, RandomizerCategoryActivity::class.java).putExtra(
                    RandomizerCategoryActivity.EXTRA_CATEGORY,
                    category,
                ),
            )
        }
    }

    private fun applyPreset(preset: Preset) {
        config = RandomizerConfig.fromPreset(preset, config.seed)
        persistAndRefresh()
    }

    private fun persistAndRefresh() {
        prefs.randomizerConfig = config.copy(enabled = true)
        refreshUi()
    }

    private fun refreshUi() {
        textSeed.text = config.seedDisplay()
        highlightPreset(config.preset)
        fillCard(
            catWilds,
            R.string.hoenn_cat_wilds,
            config.wildsBlurb(),
            config.wildsOn(),
        )
        fillCard(
            catTrainers,
            R.string.hoenn_cat_trainers,
            config.trainersBlurb(),
            config.trainersOn(),
        )
        fillCard(
            catStarters,
            R.string.hoenn_cat_starters,
            config.startersBlurb(),
            config.startersOn(),
        )
        fillCard(
            catPersonal,
            R.string.hoenn_cat_personal_short,
            config.personalBlurb(),
            config.personalOn(),
        )
        fillCard(
            catMoves,
            R.string.hoenn_cat_moves_short,
            config.movesBlurb(),
            config.movesOn(),
        )
        fillCard(
            catEvolutions,
            R.string.hoenn_cat_evolutions,
            config.evolutionsBlurb(),
            config.evolutionsOn(),
        )
        fillCard(
            catMisc,
            R.string.hoenn_cat_misc_short,
            config.miscBlurb(),
            config.miscOn(),
        )
    }

    private fun fillCard(card: CatCard, titleRes: Int, blurb: String, on: Boolean) {
        card.title.setText(titleRes)
        card.blurb.text = blurb
        if (on) {
            card.chip.setText(R.string.hoenn_status_on)
            card.chip.setBackgroundResource(R.drawable.hoenn_chip_on)
            card.root.setBackgroundResource(R.drawable.hoenn_bg_card_selected)
        } else {
            card.chip.setText(R.string.hoenn_status_vanilla)
            card.chip.setBackgroundResource(R.drawable.hoenn_chip_muted)
            card.root.setBackgroundResource(R.drawable.hoenn_bg_card)
        }
    }

    private fun highlightPreset(preset: Preset) {
        fun style(v: View, selected: Boolean) {
            v.setBackgroundResource(
                if (selected) R.drawable.hoenn_bg_card_selected else R.drawable.hoenn_bg_card,
            )
        }
        style(cardLight, preset == Preset.LIGHT)
        style(cardStandard, preset == Preset.STANDARD)
        style(cardChaos, preset == Preset.CHAOS)
    }
}
