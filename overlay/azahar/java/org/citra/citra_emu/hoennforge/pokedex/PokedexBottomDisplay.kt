// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.app.Activity
import android.content.Context
import android.view.View
import android.widget.Toast
import androidx.appcompat.view.ContextThemeWrapper
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.utils.Log

/**
 * Pokédex UI on the **bottom** screen via SecondaryDisplay overlay (same Presentation
 * that already owns Thor's secondary display). Falls back to primary dialog only if
 * secondary overlay is unavailable.
 */
object PokedexBottomDisplay {
    fun dismiss(activity: Activity? = null) {
        try {
            if (activity is EmulationActivity) {
                activity.secondaryDisplayManager.hideOverlay()
            }
        } catch (e: Exception) {
            Log.warning("[Pokedex] dismiss overlay: $e")
        }
    }

    fun showChooser(
        activity: Activity,
        hits: List<NameMatcher.Hit>,
        onPick: (Species) -> Unit,
        onCancel: () -> Unit,
    ) {
        val themed = themedContext(activity)
        val view = PokedexUi.buildChooserView(themed, hits, { sp ->
            // Keep bottom; open entry
            showEntry(activity, sp, PokedexRepository.get(activity), hits.map { it.species })
        }, {
            dismiss(activity)
            onCancel()
        })
        if (showOnBottom(activity, view)) {
            Toast.makeText(activity, R.string.hoenn_pokedex_on_bottom, Toast.LENGTH_SHORT).show()
        } else {
            Log.warning("[Pokedex] bottom overlay unavailable — primary dialog")
            Toast.makeText(activity, R.string.hoenn_pokedex_top_fallback, Toast.LENGTH_SHORT).show()
            PokedexUi.showChooserDialog(themed, hits, onPick) {
                dismiss(activity)
                onCancel()
            }
        }
    }

    fun showEntry(
        activity: Activity,
        species: Species,
        repo: PokedexRepository,
        multi: List<Species>? = null,
        onClose: () -> Unit = {},
    ) {
        val themed = themedContext(activity)
        fun build(sp: Species): View {
            val entry = PokedexUi.buildEntryView(themed, sp, repo, multi) { next ->
                showEntry(activity, next, repo, multi, onClose)
            }
            // Wrap with close button
            val dens = themed.resources.displayMetrics.density
            val col = android.widget.LinearLayout(themed).apply {
                orientation = android.widget.LinearLayout.VERTICAL
                setBackgroundColor(android.graphics.Color.parseColor("#FF0D1B2A"))
            }
            col.addView(
                android.widget.Button(themed).apply {
                    text = themed.getString(R.string.hoenn_pokedex_close)
                    setOnClickListener {
                        dismiss(activity)
                        onClose()
                    }
                },
                android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                    android.widget.LinearLayout.LayoutParams.WRAP_CONTENT,
                ),
            )
            col.addView(
                entry,
                android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                    0,
                    1f,
                ).apply {
                    // dens unused except layout weight
                    @Suppress("UNUSED_EXPRESSION")
                    dens
                },
            )
            return col
        }

        val view = build(species)
        if (showOnBottom(activity, view)) {
            Toast.makeText(activity, R.string.hoenn_pokedex_on_bottom, Toast.LENGTH_SHORT).show()
        } else {
            Log.warning("[Pokedex] bottom overlay unavailable — primary dialog")
            Toast.makeText(activity, R.string.hoenn_pokedex_top_fallback, Toast.LENGTH_SHORT).show()
            PokedexUi.showEntryDialog(themed, species, repo, multi) {
                dismiss(activity)
                onClose()
            }
        }
    }

    private fun showOnBottom(activity: Activity, view: View): Boolean {
        if (activity !is EmulationActivity) return false
        return try {
            val mgr = activity.secondaryDisplayManager
            // Ensure presentation exists
            mgr.updateDisplay()
            val ok = mgr.showOverlay(view)
            Log.warning(
                "[Pokedex] showOverlay ok=$ok physical=${mgr.hasPhysicalSecondary} " +
                    "displayId=${mgr.currentDisplayId}",
            )
            ok
        } catch (e: Exception) {
            Log.warning("[Pokedex] showOnBottom failed: $e")
            false
        }
    }

    private fun themedContext(activity: Activity): Context {
        return ContextThemeWrapper(
            activity,
            com.google.android.material.R.style.Theme_Material3_DayNight_NoActionBar,
        )
    }
}
