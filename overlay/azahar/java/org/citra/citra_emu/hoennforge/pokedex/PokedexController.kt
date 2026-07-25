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
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.utils.Log

/**
 * START menu → Pokédex: OCR top screen → match → UI on **bottom** display when available.
 *
 * Capture prefers PixelCopy on the live surface so we avoid RequestScreenshot, which can
 * perturb present-frame sizing and look like a drop to 1× internal resolution.
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
                    // Prefer surface copy — does not change emulator render scale.
                    var bmp = surfaceView?.let { TopScreenCapture.captureSurface(it) }
                    if (bmp == null) {
                        Log.warning("[Pokedex] PixelCopy failed — native capture at scale=1 (OCR only)")
                        // Explicit 1: only the offscreen screenshot buffer size, not game setting.
                        bmp = TopScreenCapture.captureNative(1)
                    }
                    bmp?.let { TopScreenCapture.ensureOcrSize(it) }
                }

                restoreResolutionIfNeeded(resBefore)

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
                restoreResolutionIfNeeded(resBefore)
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
