package dev.tzigdon.hoennforge.ui

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import dev.tzigdon.hoennforge.HoennForgeApp
import dev.tzigdon.hoennforge.R
import dev.tzigdon.hoennforge.databinding.ActivityHomeBinding

class HomeActivity : AppCompatActivity() {
    private lateinit var binding: ActivityHomeBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityHomeBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val prefs = HoennForgeApp.instance.preferences
        if (!prefs.hasDump) {
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        binding.textGame.text = prefs.dumpGameLabel ?: getString(R.string.app_name)
        binding.textMeta.text = getString(
            R.string.home_meta,
            prefs.dumpDisplayName ?: "—",
            prefs.dumpTitleId ?: "—",
            prefs.dumpRegion ?: "—",
        )

        binding.buttonPlay.setOnClickListener {
            // Milestone A next step: launch embedded Azahar core with this dump + Thor profile.
            Toast.makeText(
                this,
                R.string.play_placeholder,
                Toast.LENGTH_LONG,
            ).show()
        }

        binding.buttonChangeDump.setOnClickListener {
            prefs.clearDump()
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
        }
    }
}
