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
 * On-device Latin OCR via ML Kit, with multi-pass preprocessing to help pixel fonts
 * (ORAS nameplates) that ML Kit otherwise mangles (W/M, thin strokes, etc.).
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

    /**
     * Run OCR on several preprocessed variants and merge unique tokens (largest area first).
     */
    suspend fun recognize(bitmap: Bitmap): List<Token> {
        val passes = buildPasses(bitmap)
        val merged = LinkedHashMap<String, Token>()
        for ((label, bmp) in passes) {
            try {
                val tokens = recognizeOnce(bmp)
                Log.info("[Pokedex] OCR pass=$label tokens=${tokens.size}: ${tokens.take(8).joinToString { it.text }}")
                for (t in tokens) {
                    val key = t.text.lowercase()
                    val prev = merged[key]
                    if (prev == null || t.area > prev.area || t.confidence > prev.confidence) {
                        merged[key] = t
                    }
                }
            } catch (e: Exception) {
                Log.warning("[Pokedex] OCR pass=$label failed: $e")
            } finally {
                if (bmp !== bitmap && !bmp.isRecycled) bmp.recycle()
            }
        }
        return merged.values.sortedByDescending { it.area }
    }

    private fun buildPasses(src: Bitmap): List<Pair<String, Bitmap>> {
        val out = ArrayList<Pair<String, Bitmap>>(6)
        out.add("raw" to src)
        // Upscale aggressively for thin ORAS UI fonts
        val big = upscale(src, minWidth = 1200)
        if (big !== src) out.add("upscale" to big)
        out.add("contrast" to contrastBoost(big, contrast = 1.6f, brightness = 12f))
        out.add("high-contrast" to contrastBoost(big, contrast = 2.2f, brightness = 20f))
        out.add("bw" to toBinary(big, threshold = 160))
        out.add("bw-inv" to toBinary(big, threshold = 140, invert = true))
        // Upper band often holds species name on summary / battle UI
        cropTopBand(big, fraction = 0.42f)?.let { out.add("top-band" to it) }
        return out
    }

    private suspend fun recognizeOnce(bitmap: Bitmap): List<Token> =
        suspendCancellableCoroutine { cont ->
            val image = InputImage.fromBitmap(bitmap, 0)
            client.process(image)
                .addOnSuccessListener { result ->
                    val tokens = ArrayList<Token>()
                    // Full lines (helps multi-word names: Mr. Mime, Type: Null)
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

    private fun upscale(src: Bitmap, minWidth: Int): Bitmap {
        if (src.width >= minWidth) return src
        val scale = minWidth.toFloat() / src.width
        val w = (src.width * scale).toInt().coerceAtLeast(1)
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

    private fun toBinary(src: Bitmap, threshold: Int, invert: Boolean = false): Bitmap {
        val w = src.width
        val h = src.height
        val pixels = IntArray(w * h)
        src.getPixels(pixels, 0, w, 0, 0, w, h)
        for (i in pixels.indices) {
            val c = pixels[i]
            val r = (c shr 16) and 0xFF
            val g = (c shr 8) and 0xFF
            val b = c and 0xFF
            // Luma; ORAS UI text is light-on-dark or dark-on-light depending on screen
            val y = (0.299 * r + 0.587 * g + 0.114 * b).toInt()
            val white = if (invert) y < threshold else y >= threshold
            pixels[i] = if (white) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
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
