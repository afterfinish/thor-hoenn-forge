// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.GameHelper

class HomeActivity : AppCompatActivity() {
    private lateinit var prefs: HoennPrefs

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
        findViewById<TextView>(R.id.textGame).text =
            prefs.dumpGameLabel ?: getString(R.string.app_name)
        findViewById<TextView>(R.id.textMode).text = if (config.enabled) {
            getString(
                R.string.hoenn_home_mode_random,
                config.modeLabel(),
                config.seedDisplay(),
            ) + "\n" + getString(R.string.hoenn_home_randomizer_wip)
        } else {
            getString(R.string.hoenn_home_mode_vanilla)
        }
        findViewById<TextView>(R.id.textMeta).text = getString(
            R.string.hoenn_home_meta,
            prefs.dumpDisplayName ?: "—",
            prefs.dumpTitleId ?: "—",
            prefs.dumpRegion ?: "—",
        )

        findViewById<Button>(R.id.buttonPlay).setOnClickListener { playDump() }
        findViewById<Button>(R.id.buttonNewRun).setOnClickListener { confirmNewRun() }
        findViewById<Button>(R.id.buttonChangeDump).setOnClickListener {
            prefs.clearDump()
            startActivity(Onboarding.intentTo(this, DumpPickerActivity::class.java))
            finish()
        }
        findViewById<Button>(R.id.buttonAdvanced).setOnClickListener {
            startActivity(Intent(this, MainActivity::class.java))
        }
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

    private fun playDump() {
        val uriString = prefs.dumpUri
        if (uriString.isNullOrBlank()) {
            Toast.makeText(this, R.string.hoenn_game_invalid, Toast.LENGTH_LONG).show()
            return
        }
        val uri = uriString.toUri()
        try {
            if (!Onboarding.hasDataDirectory(this)) {
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

            // addedToLibrary=false avoids GameHelper lateinit crash (prefs only set in getGames())
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

            // Boot original dump; Azahar LayeredFS applies load/mods/{titleId}/romfs/
            startActivity(
                Intent(this, EmulationActivity::class.java).apply {
                    action = Intent.ACTION_VIEW
                    data = uri
                    putExtra("game", game)
                },
            )
        } catch (e: Exception) {
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
