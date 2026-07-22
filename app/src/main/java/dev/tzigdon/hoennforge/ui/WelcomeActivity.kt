package dev.tzigdon.hoennforge.ui

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import dev.tzigdon.hoennforge.HoennForgeApp
import dev.tzigdon.hoennforge.databinding.ActivityWelcomeBinding

class WelcomeActivity : AppCompatActivity() {
    private lateinit var binding: ActivityWelcomeBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = HoennForgeApp.instance.preferences
        if (prefs.legalAccepted && prefs.hasDump) {
            startActivity(Intent(this, HomeActivity::class.java))
            finish()
            return
        }
        if (prefs.legalAccepted) {
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
            return
        }

        binding = ActivityWelcomeBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.checkboxLegal.setOnCheckedChangeListener { _, checked ->
            binding.buttonContinue.isEnabled = checked
        }
        binding.buttonContinue.isEnabled = false
        binding.buttonContinue.setOnClickListener {
            prefs.legalAccepted = true
            startActivity(Intent(this, DumpPickerActivity::class.java))
            finish()
        }
    }
}
