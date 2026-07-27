// Copyright Hoenn Forge — 60 FPS presentation patch for ORAS
package org.citra.citra_emu.hoennforge

import android.content.Context
import android.util.Log
import org.citra.citra_emu.NativeLibrary
import java.io.File

/**
 * Renders ORAS at 60 FPS without changing game speed, by a single-word patch to the
 * per-frame function's tail.
 *
 * The engine is fixed step: one `update()` call is one logical frame, and there is no time
 * delta anywhere in the loop to rescale. So the only honest version of "60 FPS" is to keep
 * `update()` at its stock rate and let `draw()` run on every frame instead of every other
 * one.
 *
 * The tail reads the frame manager's mode byte and skips drawing when the current frame was
 * an update frame:
 *
 * ```
 *   0010E530  E5D4000D  ldrb r0,[r4,#13]   ; mode: 0 = 60Hz, 1 = 30Hz
 *   0010E534  E2661000  rsb  r1,r6,#0      ; -shouldUpdate
 *   0010E538  E1100001  tst  r0,r1         ; draw iff !(mode && shouldUpdate)
 *   0010E53C  1A000004  bne  ...           ; -> return without drawing
 * ```
 *
 * Forcing that mode read to zero makes `tst` always zero, so the branch is never taken and
 * `draw()` always runs. Crucially the game's own mode byte is left alone, which means:
 *
 *  - overworld, mode 1: alternation still runs, so 30 updates and 60 draws per second
 *  - menus, mode 0: untouched, still 60 updates and 60 draws
 *
 * ORAS really does run its menus at 60Hz natively, so any patch that forced the mode byte
 * would have halved menu logic and made the cursor drag. This one cannot.
 *
 * The engine's own overrun protection is left in place: if the GPU misses its budget it arms
 * a one-shot skip latch and the next frame returns without drawing, so the failure mode is
 * an irregular cadence rather than a stall.
 *
 * **Unproven, and it is the whole feature:** whether those extra draws contain new images.
 * `update()` still runs 30 times a second, so unless the game's own draw function advances
 * animation, consecutive pairs render identical scenes and this buys an FPS counter and
 * nothing else. See `docs/60fps-v1_0-research.md`.
 */
object HoennSixtyFps {
    private const val TAG = "HoennSixtyFps"

    /** Byte offset inside the decompressed `code.bin`. Virtual address is this + 0x100000. */
    private const val PATCH_OFFSET = 0x00E530

    /** `ldrb r0,[r4,#13]` — what must already be there. */
    private val EXPECT = byteArrayOf(0x0D, 0x00, 0xD4.toByte(), 0xE5.toByte())

    /** `mov r0,#0` — what we write. */
    private val PATCH = byteArrayOf(0x00, 0x00, 0xA0.toByte(), 0xE3.toByte())

    /**
     * Titles whose dump has been opened and byte-checked at [PATCH_OFFSET]. Omega Ruby and
     * Alpha Sapphire USA are byte-identical there, and the four-instruction signature above
     * occurs exactly once in each 5.4 MB binary.
     *
     * Deliberately fail-closed for everything else. Adding a region means extracting its
     * ExeFS, confirming the signature is unique and lands on the same offset, and only then
     * listing it — not assuming that twin builds imply twin regions.
     */
    private val VERIFIED_TITLES = setOf(
        "000400000011C400", // Pokémon Omega Ruby (USA)
        "000400000011C500", // Pokémon Alpha Sapphire (USA)
    )

    fun isVerifiedTitle(titleIdHex: String?): Boolean =
        titleIdHex != null && normalise(titleIdHex) in VERIFIED_TITLES

    private fun normalise(titleIdHex: String) =
        titleIdHex.removePrefix("0x").uppercase().padStart(16, '0')

    /**
     * Writes or removes the patch to match [HoennPrefs.sixtyFpsEnabled]. Must be called
     * before the core loads the game — the IPS is read during ExeFS decompression, long
     * before anything is running.
     *
     * Called on every launch rather than once at prepare, because a prepare wipes
     * `load/mods/<title>` wholesale and would take the patch with it.
     */
    fun apply(context: Context) {
        val prefs = HoennPrefs(context)
        val titleId = prefs.dumpTitleId ?: return
        val target = ipsFile(titleId) ?: return

        if (!prefs.sixtyFpsEnabled) {
            if (target.exists() && target.delete()) {
                Log.i(TAG, "60 FPS off — removed ${target.absolutePath}")
            }
            return
        }
        if (!isVerifiedTitle(titleId)) {
            // Fail closed. A patch offset that has not been checked against this exact
            // build would write four bytes into the middle of whatever is really there.
            Log.w(TAG, "60 FPS unavailable: ${normalise(titleId)} is not a verified build")
            if (target.exists()) target.delete()
            return
        }
        try {
            target.parentFile?.mkdirs()
            target.writeBytes(buildIps())
            Log.i(TAG, "60 FPS patch written to ${target.absolutePath}")
        } catch (e: Exception) {
            Log.e(TAG, "60 FPS patch write failed", e)
        }
    }

    /**
     * `<user>/load/mods/<title>/exefs/code.ips`, where `<user>` is the data folder this
     * install was pointed at during onboarding. Never a literal path — every user picks
     * their own, and the emulator resolves it the same way for LayeredFS and cheats.
     */
    private fun ipsFile(titleIdHex: String): File? {
        val user = NativeLibrary.getUserDirectory()
        if (user.isBlank()) {
            Log.w(TAG, "user directory not set yet; skipping 60 FPS patch")
            return null
        }
        return File(user, "load/mods/${normalise(titleIdHex)}/exefs/code.ips")
    }

    /** IPS: "PATCH", then a 3-byte big-endian offset, 2-byte big-endian length, data, "EOF". */
    internal fun buildIps(): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        out.write("PATCH".toByteArray(Charsets.US_ASCII))
        out.write((PATCH_OFFSET shr 16) and 0xFF)
        out.write((PATCH_OFFSET shr 8) and 0xFF)
        out.write(PATCH_OFFSET and 0xFF)
        out.write((PATCH.size shr 8) and 0xFF)
        out.write(PATCH.size and 0xFF)
        out.write(PATCH)
        out.write("EOF".toByteArray(Charsets.US_ASCII))
        return out.toByteArray()
    }

    /** The instruction the patch expects to overwrite. Exposed for tests. */
    internal fun expectedBytes(): ByteArray = EXPECT.copyOf()
}
