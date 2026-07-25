// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.text.TextRecognition
import com.google.mlkit.vision.text.latin.TextRecognizerOptions
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine
import org.citra.citra_emu.utils.Log

/**
 * On-device Latin OCR via ML Kit.
 *
 * Prefer [recognizeQuick] (1–2 passes). Use [recognizeDeep] only when matching fails.
 * Full multi-pass on large bitmaps was ~30s; quick path targets a few seconds.
 */
object OcrService {
    data class Token(
        val text: String,
        val confidence: Float,
        val area: Int,
    )

    private val client by lazy {
        TextRecognition.getClient(TextRecognizerOptions.DEFAULT_OPTIONS)
    }

    /** Fast path: top-band contrast + optional full-frame (max ~2 ML Kit calls). */
    suspend fun recognizeQuick(bitmap: Bitmap): List<Token> {
        val scaled = scaleToMaxWidth(bitmap, 960)
        val toRecycle = ArrayList<Bitmap>()
        if (scaled !== bitmap) toRecycle.add(scaled)
        try {
            val merged = LinkedHashMap<String, Token>()
            // 1) Top band only — nameplates live here; much smaller image
            val band = cropTopBand(scaled, 0.45f)
            if (band != null) {
                toRecycle.add(band)
                val contrast = contrastBoost(band, contrast = 1.7f, brightness = 16f)
                toRecycle.add(contrast)
                merge(merged, "quick-band", recognizeOnce(contrast))
            }
            // 2) Full frame once if band was weak
            if (merged.size < 3) {
                merge(merged, "quick-full", recognizeOnce(scaled))
            }
            return merged.values.sortedByDescending { it.area }
        } finally {
            for (b in toRecycle) {
                if (!b.isRecycled) b.recycle()
            }
        }
    }

    /**
     * Deeper path when quick found nothing useful: contrast + B/W on modest upscale
     * (max ~3 extra ML Kit calls).
     */
    suspend fun recognizeDeep(bitmap: Bitmap): List<Token> {
        val scaled = scaleToMaxWidth(bitmap, 1100)
        val toRecycle = ArrayList<Bitmap>()
        if (scaled !== bitmap) toRecycle.add(scaled)
        try {
            val merged = LinkedHashMap<String, Token>()
            merge(merged, "deep-raw", recognizeOnce(scaled))

            val band = cropTopBand(scaled, 0.5f)
            if (band != null) {
                toRecycle.add(band)
                val hi = contrastBoost(band, contrast = 2.1f, brightness = 24f)
                toRecycle.add(hi)
                merge(merged, "deep-band-hi", recognizeOnce(hi))
                val bw = toBinary(band, threshold = 155)
                toRecycle.add(bw)
                merge(merged, "deep-band-bw", recognizeOnce(bw))
            }
            return merged.values.sortedByDescending { it.area }
        } finally {
            for (b in toRecycle) {
                if (!b.isRecycled) b.recycle()
            }
        }
    }

    /** @deprecated Prefer [recognizeQuick] then [recognizeDeep]. */
    suspend fun recognize(bitmap: Bitmap): List<Token> = recognizeQuick(bitmap)

    private fun merge(into: LinkedHashMap<String, Token>, label: String, tokens: List<Token>) {
        Log.info(
            "[Pokedex] OCR pass=$label tokens=${tokens.size}: ${
                tokens.take(8).joinToString { it.text }
            }",
        )
        for (t in tokens) {
            val key = t.text.lowercase()
            val prev = into[key]
            if (prev == null || t.area > prev.area || t.confidence > prev.confidence) {
                into[key] = t
            }
        }
    }

    private suspend fun recognizeOnce(bitmap: Bitmap): List<Token> =
        suspendCancellableCoroutine { cont ->
            val image = InputImage.fromBitmap(bitmap, 0)
            client.process(image)
                .addOnSuccessListener { result ->
                    val tokens = ArrayList<Token>()
                    for (block in result.textBlocks) {
                        for (line in block.lines) {
                            val lineText = line.text.trim()
                            if (lineText.isNotEmpty()) {
                                val box = line.boundingBox
                                val area = if (box != null) box.width() * box.height() else 0
                                tokens.add(Token(lineText, line.confidence ?: 0.55f, area))
                            }
                            for (el in line.elements) {
                                val t = el.text.trim()
                                if (t.isEmpty()) continue
                                val box = el.boundingBox
                                val area = if (box != null) box.width() * box.height() else 0
                                tokens.add(Token(t, el.confidence ?: 0.5f, area))
                            }
                        }
                    }
                    tokens.sortByDescending { it.area }
                    if (cont.isActive) cont.resume(tokens)
                }
                .addOnFailureListener { e ->
                    if (cont.isActive) cont.resumeWithException(e)
                }
        }

    private fun scaleToMaxWidth(src: Bitmap, maxWidth: Int): Bitmap {
        if (src.width <= maxWidth) return src
        val scale = maxWidth.toFloat() / src.width
        val w = maxWidth
        val h = (src.height * scale).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(src, w, h, true)
    }

    private fun contrastBoost(src: Bitmap, contrast: Float, brightness: Float): Bitmap {
        val cm = ColorMatrix(
            floatArrayOf(
                contrast, 0f, 0f, 0f, brightness,
                0f, contrast, 0f, 0f, brightness,
                0f, 0f, contrast, 0f, brightness,
                0f, 0f, 0f, 1f, 0f,
            ),
        )
        val out = Bitmap.createBitmap(src.width, src.height, Bitmap.Config.ARGB_8888)
        val c = Canvas(out)
        val p = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        p.colorFilter = ColorMatrixColorFilter(cm)
        c.drawBitmap(src, 0f, 0f, p)
        return out
    }

    private fun toBinary(src: Bitmap, threshold: Int): Bitmap {
        val w = src.width
        val h = src.height
        val pixels = IntArray(w * h)
        src.getPixels(pixels, 0, w, 0, 0, w, h)
        for (i in pixels.indices) {
            val c = pixels[i]
            val r = (c shr 16) and 0xFF
            val g = (c shr 8) and 0xFF
            val b = c and 0xFF
            val y = (0.299 * r + 0.587 * g + 0.114 * b).toInt()
            pixels[i] = if (y >= threshold) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
        }
        val out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        out.setPixels(pixels, 0, w, 0, 0, w, h)
        return out
    }

    private fun cropTopBand(src: Bitmap, fraction: Float): Bitmap? {
        val h = (src.height * fraction).toInt().coerceIn(32, src.height)
        if (h >= src.height) return null
        return try {
            Bitmap.createBitmap(src, 0, 0, src.width, h)
        } catch (_: Exception) {
            null
        }
    }
}
