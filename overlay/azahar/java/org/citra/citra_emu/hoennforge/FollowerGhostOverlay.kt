// Copyright Hoenn Forge — sprite ghost of party lead (rough follower dogfood)
package org.citra.citra_emu.hoennforge

import android.app.Activity
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.hoennforge.pokedex.PokedexRepository
import org.citra.citra_emu.utils.Log
import kotlin.math.cos
import kotlin.math.sin

/**
 * Shows party-lead sprite on Thor bottom screen (SecondaryDisplay overlay).
 * Lags slightly with left stick so you can see *something* follow — not an
 * in-engine 3D field model (that needs MdlAdd / BehindWalk RE).
 */
object FollowerGhostOverlay {
    private const val TAG = "HoennForgeFollowerGhost"
    private val handler = Handler(Looper.getMainLooper())
    private var running = false
    private var speciesId = 0
    private var lagX = 0f
    private var lagY = 0f
    private var t = 0f
    private var image: ImageView? = null
    private var label: TextView? = null
    private var root: LinearLayout? = null

    private val tick = object : Runnable {
        override fun run() {
            if (!running) return
            // Parse native status for pad if present; else gentle bob
            var padX = 0f
            var padY = 0f
            try {
                val s = NativeLibrary.hoennFollowerStatus()
                // format: fol=1 pl=... fo=... hist=N dll=... sp=83 pad=0.12,-0.40
                val padIdx = s.indexOf("pad=")
                if (padIdx >= 0) {
                    val part = s.substring(padIdx + 4).substringBefore(' ')
                    val xy = part.split(',')
                    if (xy.size == 2) {
                        padX = xy[0].toFloatOrNull() ?: 0f
                        padY = xy[1].toFloatOrNull() ?: 0f
                    }
                }
            } catch (_: Exception) {
            }
            // Lag toward opposite of stick (trail behind movement)
            val targetX = -padX * 48f
            val targetY = padY * 36f
            lagX += (targetX - lagX) * 0.12f
            lagY += (targetY - lagY) * 0.12f
            t += 0.15f
            val bob = sin(t.toDouble()).toFloat() * 4f
            image?.translationX = lagX
            image?.translationY = lagY + bob
            // Subtle walk wobble
            image?.rotation = cos(t.toDouble()).toFloat() * 4f * (if (kotlin.math.abs(padX) + kotlin.math.abs(padY) > 0.15f) 1f else 0.2f)
            handler.postDelayed(this, 33L)
        }
    }

    fun show(activity: Activity, species: Int, partyList: List<Int>) {
        hide(activity)
        speciesId = species
        if (species !in 1..721) {
            Log.warning("[$TAG] bad species $species")
            return
        }
        val repo = PokedexRepository.get(activity)
        val name = repo.get(species)?.name ?: "#$species"
        val partyNames = partyList.joinToString(", ") { id ->
            repo.get(id)?.name ?: "#$id"
        }

        val themed = activity
        val density = themed.resources.displayMetrics.density
        val pad = (12 * density).toInt()

        val col = LinearLayout(themed).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.argb(200, 12, 18, 28))
            setPadding(pad, pad, pad, pad)
            gravity = Gravity.CENTER_HORIZONTAL
        }

        val title = TextView(themed).apply {
            text = themed.getString(R.string.hoenn_follower_ghost_title)
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
        }
        col.addView(title)

        val iv = ImageView(themed).apply {
            adjustViewBounds = true
            scaleType = ImageView.ScaleType.FIT_CENTER
            val lp = LinearLayout.LayoutParams((120 * density).toInt(), (120 * density).toInt())
            lp.topMargin = (8 * density).toInt()
            layoutParams = lp
        }
        try {
            themed.assets.open("pokedex/sprites/$species.png").use { stream ->
                iv.setImageBitmap(BitmapFactory.decodeStream(stream))
            }
        } catch (e: Exception) {
            Log.warning("[$TAG] sprite $species: $e")
        }
        col.addView(iv)
        image = iv

        val lab = TextView(themed).apply {
            text = themed.getString(R.string.hoenn_follower_ghost_lead, name, species)
            setTextColor(Color.rgb(180, 220, 255))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            gravity = Gravity.CENTER
            setPadding(0, (6 * density).toInt(), 0, 0)
        }
        col.addView(lab)
        label = lab

        val sub = TextView(themed).apply {
            text = themed.getString(R.string.hoenn_follower_ghost_party, partyNames)
            setTextColor(Color.LTGRAY)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            gravity = Gravity.CENTER
        }
        col.addView(sub)

        val note = TextView(themed).apply {
            text = themed.getString(R.string.hoenn_follower_ghost_note)
            setTextColor(Color.rgb(255, 200, 120))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            gravity = Gravity.CENTER
            setPadding(0, (8 * density).toInt(), 0, 0)
        }
        col.addView(note)

        root = col
        var shown = false
        if (activity is EmulationActivity) {
            try {
                shown = activity.secondaryDisplayManager.showOverlay(col)
            } catch (e: Exception) {
                Log.warning("[$TAG] secondary overlay: $e")
            }
        }
        if (!shown) {
            // Fallback: float on primary (bottom-end)
            try {
                val parent = activity.findViewById<FrameLayout>(android.R.id.content)
                val lp = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.WRAP_CONTENT,
                    FrameLayout.LayoutParams.WRAP_CONTENT,
                    Gravity.BOTTOM or Gravity.END,
                )
                lp.setMargins(pad, pad, pad, pad * 4)
                parent.addView(col, lp)
                shown = true
                Log.warning("[$TAG] primary fallback overlay")
            } catch (e: Exception) {
                Log.error("[$TAG] no overlay surface: $e")
                return
            }
        }

        running = true
        lagX = 0f
        lagY = 0f
        t = 0f
        handler.post(tick)
        Log.warning("[$TAG] ghost ON species=$species $name party=$partyList")
    }

    fun hide(activity: Activity?) {
        running = false
        handler.removeCallbacks(tick)
        try {
            if (activity is EmulationActivity) {
                activity.secondaryDisplayManager.hideOverlay()
            }
        } catch (_: Exception) {
        }
        try {
            (root?.parent as? FrameLayout)?.removeView(root)
        } catch (_: Exception) {
        }
        root = null
        image = null
        label = null
        Log.info("[$TAG] ghost OFF")
    }
}
