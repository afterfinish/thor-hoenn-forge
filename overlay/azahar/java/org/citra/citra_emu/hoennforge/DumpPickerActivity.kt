// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R

class DumpPickerActivity : HoennActivity() {
    private var pendingUri: Uri? = null
    private var pendingSuccess: DumpValidator.Result.Success? = null

    private lateinit var textStatus: TextView
    private lateinit var progress: ProgressBar
    private lateinit var buttonContinue: Button

    private val openDump = registerForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        Log.i(TAG, "dump picker result: $uri")
        if (uri == null) {
            textStatus.text = "No file selected"
            textStatus.setTextColor(ContextCompat.getColor(this, R.color.hoenn_danger))
            return@registerForActivityResult
        }
        try {
            contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
        } catch (e: SecurityException) {
            Log.w(TAG, "persistable URI permission unavailable: ${e.message}")
        }
        validateUri(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "DumpPickerActivity onCreate, savedState=$savedInstanceState")

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
        // Force label + color: Material Button theming left an empty outline on Thor
        applyContinueLabel(enabled = false)
        val buttonPick = findViewById<Button>(R.id.buttonPick)
        buttonPick.setOnClickListener {
            Log.i(TAG, "Browse files tapped")
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
            applyContinueLabel(enabled = false)
            val prefs = HoennPrefs(this)
            prefs.dumpUri = uri.toString()
            prefs.dumpDisplayName = success.displayName
            prefs.dumpTitleId = OrasTitles.formatTitleId(success.titleId)
            prefs.dumpGameLabel = success.entry.label
            prefs.dumpRegion = success.entry.region
            // New dump → choose play mode (vanilla / randomizer), not straight home
            prefs.clearPreparedRun()
            Log.i(TAG, "Continue: saved dump ${prefs.dumpUri}")
            startActivity(Onboarding.intentTo(this, PlayModeActivity::class.java))
            finish()
        }
        val root = findViewById<android.view.View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        buttonPick.post { buttonPick.requestFocus() }
    }

    override fun onDestroy() {
        Log.i(TAG, "DumpPickerActivity onDestroy")
        super.onDestroy()
    }

    private fun applyContinueLabel(enabled: Boolean) {
        val label = getString(R.string.hoenn_dump_continue).ifBlank { "Continue" }
        buttonContinue.text = label
        buttonContinue.contentDescription = label
        // Solid accent — avoid color-state lists that can go invisible on some skins
        buttonContinue.setTextColor(ContextCompat.getColor(this, R.color.hoenn_accent))
        // Cancel any stuck press-scale animator from stateListAnimator
        buttonContinue.scaleX = 1f
        buttonContinue.scaleY = 1f
        buttonContinue.isEnabled = enabled
        buttonContinue.alpha = if (enabled) 1f else 0.5f
    }

    private fun validateUri(uri: Uri) {
        Log.i(TAG, "validateUri: $uri")
        progress.visibility = View.VISIBLE
        textStatus.setText(R.string.hoenn_dump_checking)
        textStatus.setTextColor(ContextCompat.getColor(this, R.color.hoenn_neutral_400))
        applyContinueLabel(enabled = false)
        pendingUri = uri
        pendingSuccess = null

        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                DumpValidator.validate(this@DumpPickerActivity, uri)
            }
            Log.i(TAG, "validation result: $result")
            progress.visibility = View.GONE
            when (result) {
                is DumpValidator.Result.Success -> {
                    pendingSuccess = result
                    // Keep status to one short line — multi-line title/id/size squeezed Continue on Thor
                    textStatus.text = getString(R.string.hoenn_dump_ready_short, result.entry.label)
                    textStatus.setTextColor(ContextCompat.getColor(this@DumpPickerActivity, R.color.hoenn_success))
                    Toast.makeText(this@DumpPickerActivity, textStatus.text, Toast.LENGTH_SHORT).show()
                    applyContinueLabel(enabled = true)
                }
                is DumpValidator.Result.Failure -> {
                    textStatus.text = result.message
                    textStatus.setTextColor(ContextCompat.getColor(this@DumpPickerActivity, R.color.hoenn_danger))
                    Toast.makeText(this@DumpPickerActivity, result.message, Toast.LENGTH_LONG).show()
                    applyContinueLabel(enabled = false)
                }
            }
        }
    }

    companion object {
        private const val TAG = "HoennForge"
    }
}
