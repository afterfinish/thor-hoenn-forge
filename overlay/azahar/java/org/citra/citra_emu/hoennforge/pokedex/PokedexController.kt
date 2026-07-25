// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.app.Activity
import android.app.AlertDialog
import android.view.SurfaceView
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.utils.Log

/**
 * START menu → Pokédex: OCR top screen → match → UI on bottom display when available.
 * If OCR finds nothing, offer typed name search.
 */
object PokedexController {
    fun open(fragment: Fragment, surfaceView: SurfaceView?) {
        if (!fragment.isAdded) return
        val activity: Activity = fragment.requireActivity()
        val ctx = fragment.requireContext()
        Toast.makeText(ctx, R.string.hoenn_pokedex_scanning, Toast.LENGTH_SHORT).show()

        val resBefore = IntSetting.RESOLUTION_FACTOR.int.let { if (it > 0) it else 4 }

        fragment.lifecycleScope.launch {
            try {
                val repo = withContext(Dispatchers.IO) {
                    PokedexRepository.get(ctx)
                }
                val bitmap = withContext(Dispatchers.Default) {
                    var bmp = surfaceView?.let { TopScreenCapture.captureSurface(it) }
                    if (bmp == null) {
                        Log.warning("[Pokedex] PixelCopy failed — native capture at scale=1")
                        bmp = TopScreenCapture.captureNative(1)
                    }
                    // Keep capture modest — OCR service scales as needed
                    bmp?.let { TopScreenCapture.ensureOcrSize(it, minWidth = 640) }
                }

                restoreResolutionIfNeeded(resBefore)

                if (bitmap == null) {
                    Toast.makeText(ctx, R.string.hoenn_pokedex_capture_fail, Toast.LENGTH_LONG).show()
                    promptManualSearch(activity, fragment, repo)
                    return@launch
                }

                val hits = withContext(Dispatchers.Default) {
                    try {
                        // Fast path (~1–2 ML Kit calls). Deep path only if no match.
                        val t0 = System.nanoTime()
                        var tokens = OcrService.recognizeQuick(bitmap)
                        var matched = NameMatcher.matchTokens(tokens.map { it.text }, repo)
                        val quickMs = (System.nanoTime() - t0) / 1_000_000
                        Log.info(
                            "[Pokedex] quick ${quickMs}ms tokens=${tokens.size} hits=${matched.size}: " +
                                tokens.take(10).joinToString { it.text },
                        )
                        if (matched.isEmpty()) {
                            val t1 = System.nanoTime()
                            tokens = OcrService.recognizeDeep(bitmap)
                            matched = NameMatcher.matchTokens(tokens.map { it.text }, repo)
                            val deepMs = (System.nanoTime() - t1) / 1_000_000
                            Log.info(
                                "[Pokedex] deep ${deepMs}ms tokens=${tokens.size} hits=${matched.size}: " +
                                    tokens.take(10).joinToString { it.text },
                            )
                        }
                        matched
                    } finally {
                        if (!bitmap.isRecycled) bitmap.recycle()
                    }
                }

                if (!fragment.isAdded) return@launch

                when {
                    hits.isEmpty() -> {
                        Toast.makeText(ctx, R.string.hoenn_pokedex_no_match, Toast.LENGTH_SHORT)
                            .show()
                        promptManualSearch(activity, fragment, repo)
                    }
                    hits.size == 1 -> {
                        PokedexBottomDisplay.showEntry(activity, hits[0].species, repo, null)
                    }
                    else -> {
                        val multi = hits.map { it.species }
                        PokedexBottomDisplay.showChooser(
                            activity = activity,
                            hits = hits,
                            onPick = { sp ->
                                PokedexBottomDisplay.showEntry(activity, sp, repo, multi)
                            },
                            onCancel = { PokedexBottomDisplay.dismiss() },
                        )
                    }
                }
            } catch (e: Exception) {
                Log.error("[Pokedex] failed: $e")
                restoreResolutionIfNeeded(resBefore)
                if (fragment.isAdded) {
                    Toast.makeText(
                        ctx,
                        ctx.getString(R.string.hoenn_pokedex_error, e.message ?: "unknown"),
                        Toast.LENGTH_LONG,
                    ).show()
                    try {
                        promptManualSearch(
                            activity,
                            fragment,
                            withContext(Dispatchers.IO) { PokedexRepository.get(ctx) },
                        )
                    } catch (_: Exception) {
                    }
                }
            }
        }
    }

    /**
     * Manual name entry when OCR fails or user prefers typing.
     */
    fun promptManualSearch(
        activity: Activity,
        fragment: Fragment,
        repo: PokedexRepository,
    ) {
        if (!fragment.isAdded) return
        val dens = activity.resources.displayMetrics.density
        val pad = (20 * dens).toInt()
        val input = EditText(activity).apply {
            hint = activity.getString(R.string.hoenn_pokedex_type_hint)
            setSingleLine()
            setPadding(pad, pad / 2, pad, pad / 2)
        }
        val wrap = FrameLayout(activity).apply {
            setPadding(pad, pad / 2, pad, 0)
            addView(
                input,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.WRAP_CONTENT,
                ),
            )
        }

        AlertDialog.Builder(activity)
            .setTitle(R.string.hoenn_pokedex_type_title)
            .setMessage(R.string.hoenn_pokedex_type_body)
            .setView(wrap)
            .setPositiveButton(R.string.hoenn_pokedex_search) { _, _ ->
                val q = input.text?.toString().orEmpty().trim()
                if (q.isEmpty()) {
                    Toast.makeText(
                        activity,
                        R.string.hoenn_pokedex_type_empty,
                        Toast.LENGTH_SHORT,
                    ).show()
                    return@setPositiveButton
                }
                val hits = NameMatcher.matchQuery(q, repo)
                if (hits.isEmpty()) {
                    Toast.makeText(
                        activity,
                        activity.getString(R.string.hoenn_pokedex_type_none, q),
                        Toast.LENGTH_LONG,
                    ).show()
                    return@setPositiveButton
                }
                if (hits.size == 1) {
                    PokedexBottomDisplay.showEntry(activity, hits[0].species, repo, null)
                } else {
                    val multi = hits.map { it.species }
                    PokedexBottomDisplay.showChooser(
                        activity = activity,
                        hits = hits,
                        onPick = { sp ->
                            PokedexBottomDisplay.showEntry(activity, sp, repo, multi)
                        },
                        onCancel = { PokedexBottomDisplay.dismiss() },
                    )
                }
            }
            .setNegativeButton(R.string.hoenn_pokedex_close, null)
            .show()
        input.requestFocus()
    }

    private fun restoreResolutionIfNeeded(wanted: Int) {
        try {
            val current = IntSetting.RESOLUTION_FACTOR.int
            if (current == wanted && current > 1) return
            Log.warning("[Pokedex] restoring resolution $current -> $wanted")
            IntSetting.RESOLUTION_FACTOR.int = wanted
            NativeLibrary.reloadSettings()
        } catch (e: Exception) {
            Log.warning("[Pokedex] restoreResolution failed: $e")
        }
    }
}
