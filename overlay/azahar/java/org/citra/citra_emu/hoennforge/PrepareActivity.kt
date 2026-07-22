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
    private lateinit var stageCopy: TextView
    private lateinit var stageExtract: TextView
    private lateinit var stageRandom: TextView
    private lateinit var stageFinalize: TextView
    private lateinit var stageCopyMeta: TextView
    private lateinit var stageExtractMeta: TextView
    private lateinit var stageRandomMeta: TextView
    private lateinit var stageFinalizeMeta: TextView
    private lateinit var textFree: TextView
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
        stageCopy = findViewById(R.id.stageCopy)
        stageExtract = findViewById(R.id.stageExtract)
        stageRandom = findViewById(R.id.stageRandom)
        stageFinalize = findViewById(R.id.stageFinalize)
        stageCopyMeta = findViewById(R.id.stageCopyMeta)
        stageExtractMeta = findViewById(R.id.stageExtractMeta)
        stageRandomMeta = findViewById(R.id.stageRandomMeta)
        stageFinalizeMeta = findViewById(R.id.stageFinalizeMeta)
        textFree = findViewById(R.id.textFree)

        val config = prefs.randomizerConfig
        textDetail.setText(R.string.hoenn_prepare_subtitle)
        try {
            val free = android.os.StatFs(filesDir.absolutePath).availableBytes
            val gb = free / (1024.0 * 1024.0 * 1024.0)
            textFree.text = getString(R.string.hoenn_free_space, String.format("%.1f", gb))
        } catch (_: Exception) {
            textFree.text = ""
        }

        buttonCancel.setOnClickListener {
            cancelled = true
            prefs.preparedReady = false
            startActivity(Onboarding.intentTo(this, PlayModeActivity::class.java))
            finish()
        }
        val root = findViewById<android.view.View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        buttonCancel.post { buttonCancel.requestFocus() }

        lifecycleScope.launch {
            runPipeline(config)
        }
    }

    private fun highlightStage(pct: Int, stage: String) {
        val accent = 0xFFE9E9ED.toInt()
        val muted = 0xFF9397AB.toInt()
        val active = 0xFFB8AEF0.toInt()
        fun style(tv: TextView, on: Boolean, done: Boolean) {
            tv.setTextColor(
                when {
                    on -> active
                    done -> accent
                    else -> muted
                },
            )
        }
        style(stageCopy, pct in 1..25, pct > 25)
        style(stageExtract, pct in 26..45, pct > 45)
        style(stageRandom, pct in 46..85, pct > 85)
        style(stageFinalize, pct in 86..100, pct >= 100)
        when {
            pct <= 25 -> stageCopyMeta.text = stage.take(24)
            pct <= 45 -> stageExtractMeta.text = "…"
            pct <= 85 -> stageRandomMeta.text = "$pct%"
            else -> stageFinalizeMeta.text = if (pct >= 100) "OK" else "…"
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
                            highlightStage(pct, stage)
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
                highlightStage(100, "done")
                textStage.setText(R.string.hoenn_prepare_done)
                // Design 22 — ready tips before Home
                startActivity(Onboarding.intentTo(this@PrepareActivity, ReadyTipsActivity::class.java))
                finish()
            } else {
                Log.e(TAG, "Prepare failed", outcome.error)
                prefs.preparedReady = false
                textStage.visibility = android.view.View.VISIBLE
                textStage.text = getString(R.string.hoenn_prepare_failed, outcome.message)
                progress.progress = 0
                buttonCancel.setText(R.string.hoenn_prepare_back)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Prepare crashed", e)
            textStage.visibility = android.view.View.VISIBLE
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
