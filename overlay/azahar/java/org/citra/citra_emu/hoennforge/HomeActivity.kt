// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.GameHelper

class HomeActivity : HoennActivity() {
    private lateinit var prefs: HoennPrefs
    /** Prevents double-tap / slow-load double launch of EmulationActivity (crash). */
    private var playLaunchInFlight = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = HoennPrefs(this)

        if (!Onboarding.hasDataDirectory(this)) {
            startActivity(Onboarding.intentTo(this, DataDirActivity::class.java))
            finish()
            return
        }
        if (!prefs.hasDump) {
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
            return
        }
        if (!prefs.preparedReady) {
            startActivity(Onboarding.intentTo(this, PlayModeActivity::class.java))
            finish()
            return
        }

        Onboarding.ensureDirectoriesInitialized(this)

        setContentView(R.layout.activity_hoenn_home)
        val config = prefs.randomizerConfig
        val label = prefs.dumpGameLabel ?: getString(R.string.app_name)
        findViewById<TextView>(R.id.textGame).text =
            if (config.enabled) "$label — forged" else label
        findViewById<TextView>(R.id.chipMode).text =
            if (config.enabled) {
                getString(R.string.hoenn_chip_randomized)
            } else {
                getString(R.string.hoenn_chip_vanilla)
            }
        findViewById<TextView>(R.id.textMode).text =
            if (config.enabled) {
                getString(R.string.hoenn_home_mode_random)
            } else {
                getString(R.string.hoenn_home_mode_vanilla)
            }

        val chipSeed = findViewById<TextView>(R.id.chipSeed)
        val chipModules = findViewById<TextView>(R.id.chipModules)
        if (config.enabled) {
            chipSeed.visibility = View.VISIBLE
            chipSeed.text = config.seedDisplay()
            chipSeed.setOnClickListener {
                val cm = getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager
                cm.setPrimaryClip(
                    android.content.ClipData.newPlainText("seed", config.seedDisplay()),
                )
                Toast.makeText(this, "Seed copied", Toast.LENGTH_SHORT).show()
            }
            chipModules.visibility = View.VISIBLE
            chipModules.text = getString(R.string.hoenn_modules_count, config.modulesOnCount())
        } else {
            chipSeed.visibility = View.GONE
            chipModules.visibility = View.GONE
        }

        findViewById<TextView>(R.id.textMeta).text = getString(R.string.hoenn_home_snapshot)
        try {
            val free = android.os.StatFs(filesDir.absolutePath).availableBytes
            val gb = free / (1024.0 * 1024.0 * 1024.0)
            findViewById<TextView>(R.id.textFreeSpace).text =
                getString(R.string.hoenn_free_space, String.format("%.1f", gb))
        } catch (_: Exception) {
            findViewById<TextView>(R.id.textFreeSpace).text = ""
        }

        val play = findViewById<Button>(R.id.buttonPlay)
        play.setOnClickListener { playDump(play) }
        findViewById<Button>(R.id.buttonNewRun).setOnClickListener { confirmNewRun() }
        findViewById<Button>(R.id.buttonVanilla).setOnClickListener { confirmVanilla() }
        findViewById<Button>(R.id.buttonSettings).setOnClickListener {
            startActivity(Onboarding.intentTo(this, SettingsActivity::class.java))
        }
        val root = findViewById<View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        play.post { play.requestFocus() }
    }

    override fun onResume() {
        super.onResume()
        // Returning from a failed/cancelled play path re-enables Continue/Play
        playLaunchInFlight = false
        findViewById<Button?>(R.id.buttonPlay)?.isEnabled = true
    }

    private fun confirmNewRun() {
        AlertDialog.Builder(this)
            .setTitle(R.string.hoenn_new_run)
            .setMessage(R.string.hoenn_new_run_confirm)
            .setPositiveButton(R.string.hoenn_continue) { _, _ ->
                prefs.clearPreparedRun()
                startActivity(Onboarding.intentTo(this, PlayModeActivity::class.java))
                finish()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun confirmVanilla() {
        AlertDialog.Builder(this)
            .setTitle(R.string.hoenn_play_vanilla)
            .setMessage(R.string.hoenn_play_vanilla_confirm)
            .setPositiveButton(R.string.hoenn_continue) { _, _ ->
                prefs.randomizerConfig = RandomizerConfig.vanilla()
                prefs.preparedReady = false
                startActivity(Onboarding.intentTo(this, PrepareActivity::class.java))
                finish()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun playDump(playButton: Button? = null) {
        if (playLaunchInFlight) {
            Log.w(TAG, "playDump ignored — launch already in flight")
            return
        }
        val uriString = prefs.dumpUri
        if (uriString.isNullOrBlank()) {
            Toast.makeText(this, R.string.hoenn_game_invalid, Toast.LENGTH_LONG).show()
            return
        }
        playLaunchInFlight = true
        playButton?.isEnabled = false
        val uri = uriString.toUri()
        try {
            if (!Onboarding.hasDataDirectory(this)) {
                playLaunchInFlight = false
                playButton?.isEnabled = true
                startActivity(Onboarding.intentTo(this, DataDirActivity::class.java))
                finish()
                return
            }
            Onboarding.ensureDirectoriesInitialized(this)

            try {
                contentResolver.takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION,
                )
            } catch (_: SecurityException) {
            }

            try {
                ThorProfile.applyIfNeeded(prefs)
            } catch (e: Exception) {
                Log.w(TAG, "Thor profile apply failed (continuing)", e)
            }

            val game = GameHelper.getGame(
                uri,
                isInstalled = false,
                addedToLibrary = false,
                mediaType = Game.MediaType.GAME_CARD,
            )
            Log.i(
                TAG,
                "Game valid=${game.valid} title=${game.title} path=${game.path} " +
                    "titleId=${game.titleId} regions=${game.regions} " +
                    "randomizer=${prefs.randomizerConfig.enabled} seed=${prefs.randomizerConfig.seed}",
            )

            if (!game.valid) {
                playLaunchInFlight = false
                playButton?.isEnabled = true
                val exists = DocumentFile.fromSingleUri(this, uri)?.exists() == true
                Toast.makeText(
                    this,
                    getString(
                        R.string.hoenn_game_invalid_detail,
                        exists.toString(),
                        uri.toString().take(80),
                    ),
                    Toast.LENGTH_LONG,
                ).show()
                return
            }

            startActivity(
                Intent(this, EmulationActivity::class.java).apply {
                    action = Intent.ACTION_VIEW
                    data = uri
                    putExtra("game", game)
                    // Single-top style: avoid stacking duplicate emulators on double-tap
                    addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                },
            )
        } catch (e: Exception) {
            playLaunchInFlight = false
            playButton?.isEnabled = true
            Log.e(TAG, "playDump failed", e)
            Toast.makeText(
                this,
                getString(
                    R.string.hoenn_play_failed,
                    e.javaClass.simpleName + ": " + (e.message ?: ""),
                ),
                Toast.LENGTH_LONG,
            ).show()
        }
    }

    companion object {
        private const val TAG = "HoennForge"
    }
}
