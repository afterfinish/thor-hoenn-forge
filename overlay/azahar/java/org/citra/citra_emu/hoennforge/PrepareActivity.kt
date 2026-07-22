// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig
import java.io.File

/**
 * Prepare pipeline shell: copy → extract → randomize → finalize.
 *
 * v1: UI stages + config persistence. Real RomFS extract / pk3DS modules
 * land in later engine work; vanilla skips randomize stage quickly.
 * Original dump URI is never written.
 */
class PrepareActivity : AppCompatActivity() {
    private lateinit var prefs: HoennPrefs
    private lateinit var textStage: TextView
    private lateinit var textDetail: TextView
    private lateinit var progress: ProgressBar
    private lateinit var buttonCancel: Button
    private var cancelled = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = HoennPrefs(this)
        if (!prefs.hasDump) {
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_prepare)
        textStage = findViewById(R.id.textStage)
        textDetail = findViewById(R.id.textDetail)
        progress = findViewById(R.id.progress)
        buttonCancel = findViewById(R.id.buttonCancel)

        val config = prefs.randomizerConfig
        textDetail.text = if (config.enabled) {
            // Honest: options are saved; game-data rewrite engine not shipped yet
            getString(
                R.string.hoenn_prepare_detail_random,
                config.modeLabel(),
                config.seedDisplay(),
            ) + "\n\n" + getString(R.string.hoenn_prepare_engine_wip)
        } else {
            getString(R.string.hoenn_prepare_detail_vanilla)
        }

        buttonCancel.setOnClickListener {
            cancelled = true
            prefs.preparedReady = false
            startActivity(Onboarding.intentTo(this, PlayModeActivity::class.java))
            finish()
        }

        lifecycleScope.launch {
            runPipeline(config)
        }
    }

    private suspend fun runPipeline(config: RandomizerConfig) {
        val stages = if (config.enabled) {
            listOf(
                Stage(R.string.hoenn_prepare_stage_copy, 15),
                Stage(R.string.hoenn_prepare_stage_extract, 35),
                Stage(R.string.hoenn_prepare_stage_randomize, 70),
                Stage(R.string.hoenn_prepare_stage_finalize, 100),
            )
        } else {
            listOf(
                Stage(R.string.hoenn_prepare_stage_copy, 40),
                Stage(R.string.hoenn_prepare_stage_finalize, 100),
            )
        }

        try {
            withContext(Dispatchers.IO) {
                ensureWorkDirs()
                // Persist config for Home + future engine
                prefs.randomizerConfig = config
            }

            for (stage in stages) {
                if (cancelled) return
                textStage.setText(stage.labelRes)
                progress.isIndeterminate = false
                progress.progress = stage.progressTarget.coerceAtMost(95)
                // Shell delay — real work replaces this
                delay(if (config.enabled) 450L else 250L)
            }

            if (cancelled) return

            // Write a marker so we know prepare completed (engine will expand this)
            withContext(Dispatchers.IO) {
                val marker = File(filesDir, "prepared/last_run.json")
                marker.parentFile?.mkdirs()
                marker.writeText(
                    """
                    {
                      "titleId": "${prefs.dumpTitleId}",
                      "game": "${prefs.dumpGameLabel}",
                      "dumpUri": "${prefs.dumpUri}",
                      "config": ${config.toJsonString()}
                    }
                    """.trimIndent(),
                )
                prefs.preparedReady = true
            }

            progress.progress = 100
            textStage.setText(R.string.hoenn_prepare_done)
            delay(300)
            if (!cancelled) {
                startActivity(Onboarding.intentTo(this@PrepareActivity, HomeActivity::class.java))
                finish()
            }
        } catch (e: Exception) {
            textStage.text = getString(R.string.hoenn_prepare_failed, e.message ?: e.javaClass.simpleName)
            progress.progress = 0
            buttonCancel.setText(R.string.hoenn_prepare_back)
            prefs.preparedReady = false
        }
    }

    private fun ensureWorkDirs() {
        File(filesDir, "prepared").mkdirs()
        File(filesDir, "work").mkdirs()
    }

    private data class Stage(val labelRes: Int, val progressTarget: Int)
}
