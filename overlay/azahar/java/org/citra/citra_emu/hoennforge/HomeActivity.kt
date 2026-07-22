// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toUri
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.GameHelper

class HomeActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = HoennPrefs(this)
        if (!prefs.hasDump) {
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_hoenn_home)
        findViewById<TextView>(R.id.textGame).text =
            prefs.dumpGameLabel ?: getString(R.string.app_name)
        findViewById<TextView>(R.id.textMeta).text = getString(
            R.string.hoenn_home_meta,
            prefs.dumpDisplayName ?: "—",
            prefs.dumpTitleId ?: "—",
            prefs.dumpRegion ?: "—",
        )

        findViewById<Button>(R.id.buttonPlay).setOnClickListener {
            playDump(prefs)
        }
        findViewById<Button>(R.id.buttonChangeDump).setOnClickListener {
            prefs.clearDump()
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
        }
        findViewById<Button>(R.id.buttonAdvanced).setOnClickListener {
            // Full Azahar UI for power users
            startActivity(Intent(this, MainActivity::class.java))
        }
    }

    private fun playDump(prefs: HoennPrefs) {
        val uriString = prefs.dumpUri ?: return
        val uri = uriString.toUri()
        try {
            ThorProfile.applyIfNeeded(prefs)
            val game = GameHelper.getGame(
                uri,
                isInstalled = false,
                addedToLibrary = true,
                mediaType = Game.MediaType.GAME_CARD,
            )
            if (!game.valid) {
                Toast.makeText(this, R.string.hoenn_game_invalid, Toast.LENGTH_LONG).show()
                return
            }
            val intent = Intent(this, EmulationActivity::class.java).apply {
                action = Intent.ACTION_VIEW
                data = uri
                putExtra("game", game)
            }
            startActivity(intent)
        } catch (e: Exception) {
            Toast.makeText(
                this,
                getString(R.string.hoenn_play_failed, e.message ?: "error"),
                Toast.LENGTH_LONG,
            ).show()
        }
    }
}
