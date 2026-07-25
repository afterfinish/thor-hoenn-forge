// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.graphics.Bitmap
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.text.TextRecognition
import com.google.mlkit.vision.text.latin.TextRecognizerOptions
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine

/**
 * On-device Latin OCR via bundled ML Kit.
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

    suspend fun recognize(bitmap: Bitmap): List<Token> =
        suspendCancellableCoroutine { cont ->
            val image = InputImage.fromBitmap(bitmap, 0)
            client.process(image)
                .addOnSuccessListener { result ->
                    val tokens = ArrayList<Token>()
                    for (block in result.textBlocks) {
                        for (line in block.lines) {
                            for (el in line.elements) {
                                val t = el.text.trim()
                                if (t.isEmpty()) continue
                                val box = el.boundingBox
                                val area = if (box != null) {
                                    box.width() * box.height()
                                } else {
                                    0
                                }
                                val conf = el.confidence ?: 0.5f
                                tokens.add(Token(t, conf, area))
                            }
                        }
                    }
                    // Prefer larger text first (battle nameplates)
                    tokens.sortByDescending { it.area }
                    if (cont.isActive) cont.resume(tokens)
                }
                .addOnFailureListener { e ->
                    if (cont.isActive) cont.resumeWithException(e)
                }
        }
}
