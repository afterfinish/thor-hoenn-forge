// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R

class DumpPickerActivity : AppCompatActivity() {
    private var pendingUri: Uri? = null
    private var pendingSuccess: DumpValidator.Result.Success? = null

    private lateinit var textStatus: TextView
    private lateinit var progress: ProgressBar
    private lateinit var buttonContinue: Button

    private val openDump = registerForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri == null) return@registerForActivityResult
        try {
            contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
        } catch (_: SecurityException) {
        }
        validateUri(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Data folder must come first in onboarding
        if (!Onboarding.hasDataDirectory(this)) {
            startActivity(Onboarding.intentTo(this, DataDirActivity::class.java))
            finish()
            return
        }
        Onboarding.ensureDirectoriesInitialized(this)

        setContentView(R.layout.activity_hoenn_dump_picker)
        textStatus = findViewById(R.id.textStatus)
        progress = findViewById(R.id.progress)
        buttonContinue = findViewById(R.id.buttonContinue)
        buttonContinue.isEnabled = false
        val buttonPick = findViewById<Button>(R.id.buttonPick)
        buttonPick.setOnClickListener {
            openDump.launch(arrayOf("application/octet-stream", "*/*"))
        }
        findViewById<TextView?>(R.id.buttonWhereDump)?.setOnClickListener {
            androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.hoenn_dump_where)
                .setMessage(R.string.hoenn_dump_where_body)
                .setPositiveButton(android.R.string.ok, null)
                .show()
        }
        buttonContinue.setOnClickListener {
            if (!buttonContinue.isEnabled) return@setOnClickListener
            val success = pendingSuccess ?: return@setOnClickListener
            val uri = pendingUri ?: return@setOnClickListener
            // Guard double-tap while next screen is slow to load
            buttonContinue.isEnabled = false
            val prefs = HoennPrefs(this)
            prefs.dumpUri = uri.toString()
            prefs.dumpDisplayName = success.displayName
            prefs.dumpTitleId = OrasTitles.formatTitleId(success.titleId)
            prefs.dumpGameLabel = success.entry.label
            prefs.dumpRegion = success.entry.region
            // New dump → choose play mode (vanilla / randomizer), not straight home
            prefs.clearPreparedRun()
            startActivity(Onboarding.intentTo(this, PlayModeActivity::class.java))
            finish()
        }
        val root = findViewById<android.view.View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        buttonPick.post { buttonPick.requestFocus() }
    }

    private fun validateUri(uri: Uri) {
        progress.visibility = View.VISIBLE
        textStatus.setText(R.string.hoenn_dump_checking)
        buttonContinue.isEnabled = false
        pendingUri = uri
        pendingSuccess = null

        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                DumpValidator.validate(this@DumpPickerActivity, uri)
            }
            progress.visibility = View.GONE
            when (result) {
                is DumpValidator.Result.Success -> {
                    pendingSuccess = result
                    val gb = result.sizeBytes / (1024.0 * 1024.0 * 1024.0)
                    textStatus.text = getString(
                        R.string.hoenn_dump_ok,
                        result.entry.label,
                        OrasTitles.formatTitleId(result.titleId),
                        String.format("%.2f", gb),
                    )
                    buttonContinue.isEnabled = true
                }
                is DumpValidator.Result.Failure -> {
                    textStatus.text = result.message
                    buttonContinue.isEnabled = false
                }
            }
        }
    }
}
