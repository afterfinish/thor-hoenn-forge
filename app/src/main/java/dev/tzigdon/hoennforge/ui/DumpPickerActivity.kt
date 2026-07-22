package dev.tzigdon.hoennforge.ui

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import dev.tzigdon.hoennforge.HoennForgeApp
import dev.tzigdon.hoennforge.R
import dev.tzigdon.hoennforge.databinding.ActivityDumpPickerBinding
import dev.tzigdon.hoennforge.dump.DumpValidator
import dev.tzigdon.hoennforge.dump.OrasTitles
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class DumpPickerActivity : AppCompatActivity() {
    private lateinit var binding: ActivityDumpPickerBinding
    private var pendingUri: Uri? = null
    private var pendingSuccess: DumpValidator.Result.Success? = null

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
            // Some providers do not support persistable permissions; still try validate.
        }
        validateUri(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityDumpPickerBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.buttonPick.setOnClickListener {
            openDump.launch(
                arrayOf(
                    "application/octet-stream",
                    "*/*",
                ),
            )
        }
        binding.buttonContinue.isEnabled = false
        binding.buttonContinue.setOnClickListener {
            val success = pendingSuccess ?: return@setOnClickListener
            val uri = pendingUri ?: return@setOnClickListener
            val prefs = HoennForgeApp.instance.preferences
            prefs.dumpUri = uri.toString()
            prefs.dumpDisplayName = success.displayName
            prefs.dumpTitleId = OrasTitles.formatTitleId(success.titleId)
            prefs.dumpGameLabel = success.entry.label
            prefs.dumpRegion = success.entry.region
            startActivity(Intent(this, HomeActivity::class.java))
            finish()
        }
    }

    private fun validateUri(uri: Uri) {
        binding.progress.visibility = View.VISIBLE
        binding.textStatus.text = getString(R.string.dump_checking)
        binding.buttonContinue.isEnabled = false
        pendingUri = uri
        pendingSuccess = null

        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                DumpValidator.validate(this@DumpPickerActivity, uri)
            }
            binding.progress.visibility = View.GONE
            when (result) {
                is DumpValidator.Result.Success -> {
                    pendingSuccess = result
                    val gb = result.sizeBytes / (1024.0 * 1024.0 * 1024.0)
                    binding.textStatus.text = getString(
                        R.string.dump_ok,
                        result.entry.label,
                        OrasTitles.formatTitleId(result.titleId),
                        String.format("%.2f", gb),
                    )
                    binding.buttonContinue.isEnabled = true
                }
                is DumpValidator.Result.Failure -> {
                    binding.textStatus.text = result.message
                    binding.buttonContinue.isEnabled = false
                }
            }
        }
    }
}
