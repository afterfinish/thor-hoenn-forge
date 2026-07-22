// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toUri
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig
import org.citra.citra_emu.hoennforge.randomizer.RandomizerEngine
import java.io.File

/**
 * Prepare pipeline: extract needed RomFS files → apply randomizer → LayeredFS deploy.
 * Original dump is never modified.
 */
class PrepareActivity : AppCompatActivity() {
    private lateinit var prefs: HoennPrefs
    private lateinit var textStage: TextView
    private lateinit var textDetail: TextView
    private lateinit var progress: ProgressBar
    private lateinit var buttonCancel: Button
    @Volatile private var cancelled = false

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
            getString(
                R.string.hoenn_prepare_detail_random,
                config.modeLabel(),
                config.seedDisplay(),
            )
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
        try {
            progress.isIndeterminate = false
            progress.progress = 0
            if (cancelled) return

            val titleId = prefs.dumpTitleId?.replace("0x", "")?.uppercase()
                ?: error("Missing title id")
            val uri = prefs.dumpUri?.toUri() ?: error("Missing dump URI")

            val outcome = withContext(Dispatchers.IO) {
                File(filesDir, "prepared").mkdirs()
                prefs.randomizerConfig = config
                val engine = RandomizerEngine(
                    this@PrepareActivity,
                    config,
                    titleId,
                    uri,
                ) { stage, pct ->
                    runOnUiThread {
                        if (!isFinishing) {
                            textStage.text = stage
                            progress.progress = pct.coerceIn(0, 100)
                        }
                    }
                }
                engine.run()
            }

            if (cancelled || isFinishing) return

            if (outcome.ok) {
                Log.i(TAG, "Prepare ok: ${outcome.message}")
                withContext(Dispatchers.IO) {
                    val marker = File(filesDir, "prepared/last_run.json")
                    marker.parentFile?.mkdirs()
                    marker.writeText(
                        """
                        {
                          "titleId": "${prefs.dumpTitleId}",
                          "game": "${prefs.dumpGameLabel}",
                          "dumpUri": "${prefs.dumpUri}",
                          "config": ${config.toJsonString()},
                          "engine": ${if (config.enabled) "\"layeredfs\"" else "\"vanilla\""}
                        }
                        """.trimIndent(),
                    )
                    prefs.preparedReady = true
                }
                progress.progress = 100
                textStage.setText(R.string.hoenn_prepare_done)
                startActivity(Onboarding.intentTo(this@PrepareActivity, HomeActivity::class.java))
                finish()
            } else {
                Log.e(TAG, "Prepare failed", outcome.error)
                prefs.preparedReady = false
                textStage.text = getString(R.string.hoenn_prepare_failed, outcome.message)
                progress.progress = 0
                buttonCancel.setText(R.string.hoenn_prepare_back)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Prepare crashed", e)
            textStage.text = getString(
                R.string.hoenn_prepare_failed,
                e.message ?: e.javaClass.simpleName,
            )
            progress.progress = 0
            buttonCancel.setText(R.string.hoenn_prepare_back)
            prefs.preparedReady = false
        }
    }

    companion object {
        private const val TAG = "HoennForge"
    }
}
