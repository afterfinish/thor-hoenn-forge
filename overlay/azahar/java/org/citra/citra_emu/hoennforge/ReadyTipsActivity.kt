// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.os.Bundle
import android.widget.Button
import androidx.appcompat.app.AppCompatActivity
import org.citra.citra_emu.R

/** Design 22 — tips after prepare, before Home. */
class ReadyTipsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_hoenn_ready_tips)
        val start = findViewById<Button>(R.id.buttonStart)
        start.setOnClickListener {
            if (!start.isEnabled) return@setOnClickListener
            start.isEnabled = false
            startActivity(Onboarding.intentTo(this, HomeActivity::class.java))
            finish()
        }
        val root = findViewById<android.view.View>(android.R.id.content)
        HoennFocus.enable(root)
        HoennFocus.installKeyRouting(this, root)
        start.post { start.requestFocus() }
    }
}
