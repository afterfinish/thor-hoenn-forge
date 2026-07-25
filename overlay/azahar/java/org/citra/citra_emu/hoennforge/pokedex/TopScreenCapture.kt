// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.pokedex

import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.view.PixelCopy
import android.view.SurfaceView
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.citra.citra_emu.NativeLibrary

/**
 * Capture the emulated **top** 3DS screen for OCR.
 * Prefers native RequestScreenshot; falls back to PixelCopy on the primary surface.
 */
object TopScreenCapture {
    /**
     * @param resScale 0 = use emulator resolution factor
     */
    fun captureNative(resScale: Int = 0): Bitmap? {
        return try {
            val packed = NativeLibrary.hoennCaptureTopScreen(resScale) ?: return null
            if (packed.size < 3) return null
            val w = packed[0]
            val h = packed[1]
            if (w <= 0 || h <= 0 || packed.size < 2 + w * h) return null
            val pixels = IntArray(w * h)
            System.arraycopy(packed, 2, pixels, 0, w * h)
            Bitmap.createBitmap(pixels, w, h, Bitmap.Config.ARGB_8888)
        } catch (_: Throwable) {
            null
        }
    }

    /**
     * Fallback: copy primary SurfaceView (on Thor this is usually the top screen).
     */
    fun captureSurface(surfaceView: SurfaceView, timeoutMs: Long = 1500): Bitmap? {
        val holder = surfaceView.holder ?: return null
        val surface = holder.surface ?: return null
        if (!surface.isValid) return null
        val w = surfaceView.width
        val h = surfaceView.height
        if (w <= 0 || h <= 0) return null
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val latch = CountDownLatch(1)
        val status = AtomicReference(PixelCopy.ERROR_UNKNOWN)
        try {
            PixelCopy.request(
                surfaceView,
                Rect(0, 0, w, h),
                bmp,
                { s ->
                    status.set(s)
                    latch.countDown()
                },
                Handler(Looper.getMainLooper()),
            )
            if (!latch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
                bmp.recycle()
                return null
            }
            if (status.get() != PixelCopy.SUCCESS) {
                bmp.recycle()
                return null
            }
            return bmp
        } catch (_: Throwable) {
            bmp.recycle()
            return null
        }
    }

    /** Upscale small bitmaps so OCR has enough glyph pixels. */
    fun ensureOcrSize(src: Bitmap, minWidth: Int = 800): Bitmap {
        if (src.width >= minWidth) return src
        val scale = minWidth.toFloat() / src.width
        val w = (src.width * scale).toInt().coerceAtLeast(1)
        val h = (src.height * scale).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(src, w, h, true)
    }
}
