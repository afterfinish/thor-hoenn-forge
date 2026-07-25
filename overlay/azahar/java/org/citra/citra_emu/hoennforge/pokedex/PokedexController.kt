// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.app.Activity
import android.view.SurfaceView
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.Log

/**
 * START menu → Pokédex: OCR top screen → match → UI on **bottom** display when available.
 */
object PokedexController {
    fun open(fragment: Fragment, surfaceView: SurfaceView?) {
        if (!fragment.isAdded) return
        val activity: Activity = fragment.requireActivity()
        val ctx = fragment.requireContext()
        Toast.makeText(ctx, R.string.hoenn_pokedex_scanning, Toast.LENGTH_SHORT).show()

        fragment.lifecycleScope.launch {
            try {
                val repo = withContext(Dispatchers.IO) {
                    PokedexRepository.get(ctx)
                }
                val bitmap = withContext(Dispatchers.Default) {
                    var bmp = TopScreenCapture.captureNative(0)
                    if (bmp == null && surfaceView != null) {
                        Log.warning("[Pokedex] native capture failed — PixelCopy fallback")
                        bmp = TopScreenCapture.captureSurface(surfaceView)
                    }
                    bmp?.let { TopScreenCapture.ensureOcrSize(it) }
                }
                if (bitmap == null) {
                    Toast.makeText(ctx, R.string.hoenn_pokedex_capture_fail, Toast.LENGTH_LONG).show()
                    return@launch
                }

                val tokens = withContext(Dispatchers.Default) {
                    try {
                        OcrService.recognize(bitmap)
                    } finally {
                        if (!bitmap.isRecycled) bitmap.recycle()
                    }
                }
                Log.info("[Pokedex] OCR tokens=${tokens.size}: ${tokens.take(12).joinToString { it.text }}")

                val hits = withContext(Dispatchers.Default) {
                    NameMatcher.matchTokens(tokens.map { it.text }, repo)
                }

                if (!fragment.isAdded) return@launch

                when {
                    hits.isEmpty() -> {
                        Toast.makeText(ctx, R.string.hoenn_pokedex_no_match, Toast.LENGTH_LONG).show()
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
                if (fragment.isAdded) {
                    Toast.makeText(
                        ctx,
                        ctx.getString(R.string.hoenn_pokedex_error, e.message ?: "unknown"),
                        Toast.LENGTH_LONG,
                    ).show()
                }
            }
        }
    }
}
