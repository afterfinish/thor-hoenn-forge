// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.PermissionsHandler

/**
 * Onboarding step: pick Azahar/Citra user-data folder (saves, config, shaders).
 */
class DataDirActivity : HoennActivity() {
    private val pickDataDir = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree(),
    ) { uri ->
        if (uri == null) {
            Toast.makeText(this, R.string.hoenn_data_dir_required, Toast.LENGTH_LONG).show()
            return@registerForActivityResult
        }
        try {
            contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
        } catch (e: SecurityException) {
            Log.e(TAG, "Failed to persist data-dir permission", e)
            Toast.makeText(this, R.string.hoenn_data_dir_required, Toast.LENGTH_LONG).show()
            return@registerForActivityResult
        }

        PermissionsHandler.setCitraDirectory(uri.toString())
        DirectoryInitialization.resetCitraDirectoryState()
        val state = DirectoryInitialization.start()
        Log.i(TAG, "Data dir set: $uri state=$state")

        if (!PermissionsHandler.hasWriteAccess(this)) {
            Toast.makeText(this, R.string.hoenn_data_dir_required, Toast.LENGTH_LONG).show()
            return@registerForActivityResult
        }

        Toast.makeText(this, R.string.hoenn_data_dir_ok, Toast.LENGTH_SHORT).show()
        continueOnboarding()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Already configured → skip
        if (Onboarding.hasDataDirectory(this)) {
            Onboarding.ensureDirectoriesInitialized(this)
            continueOnboarding()
            return
        }

        setContentView(R.layout.activity_hoenn_data_dir)
        findViewById<TextView>(R.id.textStatus).setText(R.string.hoenn_data_dir_empty)
        findViewById<TextView?>(R.id.textPath)?.text = "—"
        val pick = findViewById<Button>(R.id.buttonPick)
        pick.setOnClickListener {
            PermissionsHandler.compatibleSelectDirectory(pickDataDir)
        }
        val root = findViewById<android.view.View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        pick.post { pick.requestFocus() }
    }

    private fun continueOnboarding() {
        val prefs = HoennPrefs(this)
        startActivity(Onboarding.intentTo(this, Onboarding.nextAfterDataDir(prefs)))
        finish()
    }

    companion object {
        private const val TAG = "HoennForge"
    }
}
