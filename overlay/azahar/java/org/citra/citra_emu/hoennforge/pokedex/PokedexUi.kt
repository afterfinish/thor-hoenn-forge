// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.graphics.ColorUtils
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.R

object PokedexUi {
    private val TYPE_COLORS = mapOf(
        "Normal" to 0xFFA8A878.toInt(),
        "Fire" to 0xFFF08030.toInt(),
        "Water" to 0xFF6890F0.toInt(),
        "Electric" to 0xFFF8D030.toInt(),
        "Grass" to 0xFF78C850.toInt(),
        "Ice" to 0xFF98D8D8.toInt(),
        "Fighting" to 0xFFC03028.toInt(),
        "Poison" to 0xFFA040A0.toInt(),
        "Ground" to 0xFFE0C068.toInt(),
        "Flying" to 0xFFA890F0.toInt(),
        "Psychic" to 0xFFF85888.toInt(),
        "Bug" to 0xFFA8B820.toInt(),
        "Rock" to 0xFFB8A038.toInt(),
        "Ghost" to 0xFF705898.toInt(),
        "Dragon" to 0xFF7038F8.toInt(),
        "Dark" to 0xFF705848.toInt(),
        "Steel" to 0xFFB8B8D0.toInt(),
        "Fairy" to 0xFFEE99AC.toInt(),
    )

    fun showChooserDialog(
        context: Context,
        hits: List<NameMatcher.Hit>,
        onPick: (Species) -> Unit,
        onCancel: () -> Unit,
    ) {
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.hoenn_pokedex_choose_title)
            .setView(buildChooserView(context, hits, onPick, onCancel))
            .setNegativeButton(R.string.hoenn_menu_back) { _, _ -> onCancel() }
            .setOnCancelListener { onCancel() }
            .show()
    }

    fun showEntryDialog(
        context: Context,
        species: Species,
        repo: PokedexRepository,
        multi: List<Species>? = null,
        onClose: () -> Unit,
    ) {
        val holder = arrayOf<androidx.appcompat.app.AlertDialog?>(null)
        fun open(sp: Species) {
            holder[0]?.dismiss()
            val root = buildEntryView(context, sp, repo, multi) { next -> open(next) }
            holder[0] = MaterialAlertDialogBuilder(context)
                .setTitle(context.getString(R.string.hoenn_pokedex_entry_title, sp.id, sp.name))
                .setView(root)
                .setPositiveButton(R.string.hoenn_pokedex_close) { _, _ -> onClose() }
                .setOnCancelListener { onClose() }
                .show()
        }
        open(species)
    }

    fun buildChooserView(
        context: Context,
        hits: List<NameMatcher.Hit>,
        onPick: (Species) -> Unit,
        onCancel: () -> Unit,
    ): View {
        val dens = context.resources.displayMetrics.density
        val pad = (12 * dens).toInt()
        val col = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(Color.parseColor("#FF0D1B2A"))
        }
        col.addView(
            TextView(context).apply {
                text = context.getString(R.string.hoenn_pokedex_choose_title)
                setTextColor(Color.WHITE)
                textSize = 18f
                typeface = Typeface.DEFAULT_BOLD
                setPadding(0, 0, 0, pad)
            },
        )
        for (h in hits) {
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(pad / 2, pad / 2, pad / 2, pad / 2)
                background = GradientDrawable().apply {
                    cornerRadius = 12f
                    setColor(Color.parseColor("#FF1B263B"))
                }
                isClickable = true
                isFocusable = true
                setOnClickListener { onPick(h.species) }
            }
            val img = ImageView(context).apply {
                layoutParams = LinearLayout.LayoutParams((56 * dens).toInt(), (56 * dens).toInt())
                scaleType = ImageView.ScaleType.FIT_CENTER
                contentDescription = h.species.name
            }
            loadSprite(img, h.species.id, h.species)
            row.addView(img)
            row.addView(
                TextView(context).apply {
                    text = "#${h.species.id}  ${h.species.name}\n${h.species.typesLabel}  ·  OCR “${h.token}”"
                    setTextColor(Color.WHITE)
                    textSize = 14f
                    setPadding(pad, 0, 0, 0)
                    layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                },
            )
            val lp = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT,
            ).apply { bottomMargin = pad / 2 }
            col.addView(row, lp)
        }
        col.addView(
            android.widget.Button(context).apply {
                text = context.getString(R.string.hoenn_menu_back)
                setOnClickListener { onCancel() }
            },
        )
        return ScrollView(context).apply { addView(col) }
    }

    /**
     * @param onNavigate called when user taps an evolution chip (or multi prev/next)
     */
    fun buildEntryView(
        context: Context,
        s: Species,
        repo: PokedexRepository,
        multi: List<Species>?,
        onNavigate: (Species) -> Unit,
    ): View {
        val dens = context.resources.displayMetrics.density
        val pad = (14 * dens).toInt()
        val scroll = ScrollView(context)
        val col = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(Color.parseColor("#FF0D1B2A"))
        }

        // Title
        col.addView(
            TextView(context).apply {
                text = context.getString(R.string.hoenn_pokedex_entry_title, s.id, s.name)
                setTextColor(Color.WHITE)
                textSize = 20f
                typeface = Typeface.DEFAULT_BOLD
            },
        )

        // Main sprite
        val c1 = TYPE_COLORS[s.type1] ?: 0xFF666666.toInt()
        val c2 = TYPE_COLORS[s.type2 ?: ""] ?: ColorUtils.blendARGB(c1, Color.BLACK, 0.25f)
        val artFrame = FrameLayoutWithBg(context, c1, c2)
        val mainImg = ImageView(context).apply {
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                (160 * dens).toInt(),
            )
            scaleType = ImageView.ScaleType.FIT_CENTER
            contentDescription = s.name
        }
        artFrame.addView(mainImg)
        col.addView(artFrame)
        loadSprite(mainImg, s.id, s)

        col.addView(section(context, "Types", s.typesLabel))
        col.addView(section(context, "Base stats  (BST ${s.bst})", null))

        fun bar(label: String, value: Int) {
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            row.addView(
                TextView(context).apply {
                    text = label
                    setTextColor(Color.parseColor("#FFB0C4DE"))
                    width = (48 * dens).toInt()
                    textSize = 13f
                },
            )
            row.addView(
                ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal).apply {
                    max = 255
                    progress = value.coerceIn(0, 255)
                    layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                },
            )
            row.addView(
                TextView(context).apply {
                    text = value.toString()
                    setTextColor(Color.WHITE)
                    width = (40 * dens).toInt()
                    gravity = Gravity.END
                    textSize = 13f
                },
            )
            col.addView(row)
        }
        bar("HP", s.hp)
        bar("Atk", s.atk)
        bar("Def", s.def)
        bar("SpA", s.spa)
        bar("SpD", s.spd)
        bar("Spe", s.spe)

        val abText = buildString {
            append(s.abilities.joinToString(" / "))
            if (!s.hidden.isNullOrBlank()) append("  ·  HA: ${s.hidden}")
        }
        col.addView(section(context, "Abilities", abText))

        // Evolution line — pictures, tappable
        col.addView(section(context, "Evolution line  (tap to open)", null))
        val evoScroll = HorizontalScrollView(context)
        val evoRow = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        for ((index, id) in s.evoLine.withIndex()) {
            val sp = repo.get(id) ?: continue
            val chip = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_HORIZONTAL
                setPadding(pad / 2, pad / 2, pad / 2, pad / 2)
                background = GradientDrawable().apply {
                    cornerRadius = 12f
                    setColor(
                        if (id == s.id) Color.parseColor("#FF1B4F72")
                        else Color.parseColor("#FF1B263B"),
                    )
                    if (id == s.id) {
                        setStroke((2 * dens).toInt(), Color.parseColor("#FF48CAE4"))
                    }
                }
                isClickable = true
                isFocusable = true
                setOnClickListener {
                    if (id != s.id) onNavigate(sp)
                }
            }
            val img = ImageView(context).apply {
                layoutParams = LinearLayout.LayoutParams((72 * dens).toInt(), (72 * dens).toInt())
                scaleType = ImageView.ScaleType.FIT_CENTER
                contentDescription = sp.name
            }
            loadSprite(img, sp.id, sp)
            chip.addView(img)
            chip.addView(
                TextView(context).apply {
                    text = sp.name
                    setTextColor(Color.WHITE)
                    textSize = 11f
                    gravity = Gravity.CENTER
                    maxLines = 1
                },
            )
            val method = if (index == 0) {
                null
            } else {
                sp.evoMethod
            }
            if (!method.isNullOrBlank()) {
                chip.addView(
                    TextView(context).apply {
                        text = method
                        setTextColor(Color.parseColor("#FF90CAF9"))
                        textSize = 9f
                        gravity = Gravity.CENTER
                        maxWidth = (96 * dens).toInt()
                    },
                )
            }
            evoRow.addView(chip)
            if (index < s.evoLine.lastIndex) {
                evoRow.addView(
                    TextView(context).apply {
                        text = "→"
                        setTextColor(Color.WHITE)
                        textSize = 18f
                        setPadding(pad / 3, 0, pad / 3, 0)
                    },
                )
            }
        }
        evoScroll.addView(evoRow)
        col.addView(evoScroll)

        // Multi-hit prev/next among OCR hits
        if (multi != null && multi.size > 1) {
            val idx = multi.indexOfFirst { it.id == s.id }.coerceAtLeast(0)
            val nav = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                setPadding(0, pad, 0, 0)
            }
            if (idx > 0) {
                nav.addView(
                    android.widget.Button(context).apply {
                        text = context.getString(R.string.hoenn_pokedex_prev)
                        setOnClickListener { onNavigate(multi[idx - 1]) }
                        layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                    },
                )
            }
            if (idx < multi.lastIndex) {
                nav.addView(
                    android.widget.Button(context).apply {
                        text = context.getString(R.string.hoenn_pokedex_next)
                        setOnClickListener { onNavigate(multi[idx + 1]) }
                        layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                    },
                )
            }
            col.addView(nav)
        }

        val moves = s.learnset.joinToString("\n") { m ->
            val lv = if (m.level <= 1) "—" else m.level.toString().padStart(2, ' ')
            "Lv $lv  ${m.name}"
        }
        col.addView(
            section(
                context,
                "Level-up moves (ORAS)",
                moves.ifBlank { "No learnset data" },
            ),
        )

        scroll.addView(col)
        return scroll
    }

    private fun loadSprite(imageView: ImageView, id: Int, species: Species) {
        val c1 = TYPE_COLORS[species.type1] ?: 0xFF444444.toInt()
        val placeholder = GradientDrawable(
            GradientDrawable.Orientation.TL_BR,
            intArrayOf(c1, ColorUtils.blendARGB(c1, Color.BLACK, 0.35f)),
        )
        // Offline assets first (bundled 96px sprites) — no network required
        PokedexSprites.bind(imageView, id, placeholder)
    }

    private fun section(context: Context, title: String, body: String?): TextView {
        return TextView(context).apply {
            text = if (body == null) title else "$title\n$body"
            setTextColor(if (body == null) Color.parseColor("#FF48CAE4") else Color.WHITE)
            textSize = if (body == null) 15f else 14f
            setPadding(0, (12 * context.resources.displayMetrics.density).toInt(), 0, 4)
            setLineSpacing(0f, 1.15f)
            if (body == null) typeface = Typeface.DEFAULT_BOLD
        }
    }

    /** Frame with type gradient behind sprite. */
    private class FrameLayoutWithBg(
        context: Context,
        c1: Int,
        c2: Int,
    ) : android.widget.FrameLayout(context) {
        init {
            background = GradientDrawable(
                GradientDrawable.Orientation.TL_BR,
                intArrayOf(c1, c2),
            ).apply { cornerRadius = 16f }
            val dens = context.resources.displayMetrics.density
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                (168 * dens).toInt(),
            )
            setPadding((8 * dens).toInt(), (8 * dens).toInt(), (8 * dens).toInt(), (8 * dens).toInt())
        }
    }
}
