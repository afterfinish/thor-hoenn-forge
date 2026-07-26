// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary
import java.io.ByteArrayOutputStream
import java.io.File

/**
 * 60 FPS mode for OR/AS — true 60 FPS presentation at *normal* game speed.
 *
 * ORAS's per-frame function runs every vblank (60 Hz) and alternates: game logic `update()`
 * on one frame, `draw()` on the next. That alternation is what produces 30 FPS. The widely
 * circulated "60 FPS" Action Replay codes for these games defeat the alternation by latching
 * the update flag on, which makes `update()` run every vblank too — that is exactly why those
 * codes also double game speed. Render rate and logic rate are separable in this engine; the
 * codes just do not separate them.
 *
 * The patch here keeps `draw()` unconditional (true 60 FPS output) and runs `update()` a fixed
 * number of times per rendered frame, so the game stays at normal speed. Mechanism and offsets
 * are from Zetta_D's ORAS reverse engineering — see the README credits.
 *
 * ## Delivery
 *
 * A load-time IPS against the decompressed `.code` section, written to
 * `load/mods/<TitleID>/exefs/code.ips`. Azahar applies it in `NCCHContainer::ApplyCodePatch`
 * before the dynarmic JIT ever translates the code, so there is no stale-translation hazard —
 * unlike writing to `.text` through the Gateway cheat engine at runtime, whose invalidation
 * only covers the currently bound JIT.
 *
 * Because the patch lands at load time, toggling it takes effect on the **next launch**.
 *
 * ## Safety
 *
 * The user's dump is never touched. The IPS lives in app storage next to the LayeredFS romfs
 * mods, and toggling off deletes it. A wrong patch can crash the game on boot but cannot damage
 * the dump or the device, and toggling off always recovers.
 *
 * ## Why builds are gated
 *
 * These offsets were published for **Alpha Sapphire v1.4** and are unverified here — the region
 * of the source build was not stated, this project ingests NCSD cart dumps (the v1.0 base build),
 * and Omega Ruby is a separate build again. Applying them to a build they were not derived from
 * would patch arbitrary instructions. So [availability] returns [Availability.UNSUPPORTED] for
 * every title with no registry entry, and entries that have not been confirmed on-device are
 * marked [Availability.EXPERIMENTAL] so the UI can require an explicit opt-in.
 *
 * Adding a build means locating its per-frame function and appending a [BuildPatch]. Search key:
 * the two per-frame deltas in microseconds, 16666 (60 FPS) and 33333 (30 FPS), which sit in the
 * literal pool as `1A 41 00 00` and `35 82 00 00`.
 */
object HoennSixtyFps {
    private const val TAG = "HoennForge60"

    /** 3DS user-process `.text` base. IPS offsets are offsets into the decompressed `.code`. */
    private const val TEXT_BASE = 0x00100000

    /**
     * Logic ticks per rendered frame. 2 keeps the game at normal speed; each +1 adds one
     * `update()` per frame (3 = double speed). Treat as empirically tuned — verify against a
     * stopwatch on a known-length in-game animation before changing it.
     */
    private const val LOGIC_TICKS_PER_FRAME = 2

    /** An IPS record at this offset would be read as the "EOF" terminator. */
    private const val IPS_EOF_OFFSET = 0x454F46

    enum class Availability {
        /** A patch exists for this build and has been confirmed on-device. */
        AVAILABLE,

        /** A patch exists but is unverified for this build — require explicit opt-in. */
        EXPERIMENTAL,

        /** No patch is known for this build. */
        UNSUPPORTED,
    }

    /** One contiguous run of ARM (A32) words to write at [vaddr]. */
    private class CodePatch(val vaddr: Int, val words: List<Long>)

    private class BuildPatch(
        val label: String,
        val verified: Boolean,
        val patches: List<CodePatch>,
    )

    /**
     * Zetta_D's counted-loop patch, reproduced verbatim.
     *
     * ```
     * 0010E36C  mov   r6, #1            ; shouldUpdate = true
     * 0010E370  mov   r12, #X           ; logic ticks per rendered frame
     * 0010E374  str   r12, [pc, #0x10]  ; seed the counter at 0010E38C
     * 0010E378  ldr   r12, [pc, #0xC]   ; reload counter
     * 0010E37C  subs  r12, r12, #1
     * 0010E380  beq   0010E55C          ; counter spent -> draw()
     * 0010E384  str   r12, [pc, #0]
     * 0010E388  b     0010E3C8          ; -> update()
     * 0010E38C  .word 0xFFFFFFFF        ; counter storage, inside .text
     * ...
     * 0010E528  b     0010E378          ; after update(), back to the counter
     * ```
     *
     * The counter lives in a data word inside `.text` and is rewritten every frame, so from the
     * emulator's point of view this is self-modifying code, and the reload is a PC-relative
     * literal load — the kind of thing a JIT may fold at translation time. It was validated on
     * physical hardware only; no emulator run has ever been reported. If it misbehaves under
     * dynarmic, the fix is to drop the loop entirely (force the update flag, call `update()`
     * once, fall through to an unconditional `draw()`), which needs no counter and no SMC —
     * but that rewrite has to be authored against a disassembly of the real function.
     */
    private fun alphaSapphirePublishedPatch(): BuildPatch {
        val ticks = LOGIC_TICKS_PER_FRAME.toLong()
        return BuildPatch(
            label = "Alpha Sapphire (published v1.4 offsets)",
            verified = false,
            patches = listOf(
                CodePatch(
                    vaddr = 0x0010E36C,
                    words = listOf(
                        0xE3A06001L, // mov   r6, #1
                        0xE3A0C000L or ticks, // mov   r12, #X
                        0xE58FC010L, // str   r12, [pc, #0x10]
                        0xE59FC00CL, // ldr   r12, [pc, #0xC]
                        0xE25CC001L, // subs  r12, r12, #1
                        0x0A000075L, // beq   0x0010E55C
                        0xE58FC000L, // str   r12, [pc, #0]
                        0xEA00000EL, // b     0x0010E3C8
                        0xFFFFFFFFL, // counter storage
                    ),
                ),
                CodePatch(
                    vaddr = 0x0010E528,
                    words = listOf(0xEAFFFF92L), // b 0x0010E378
                ),
            ),
        )
    }

    /**
     * Known patches by title ID.
     *
     * Alpha Sapphire USA only, and unverified: the published offsets did not state a region, and
     * region builds do not share offsets. Every other OR/AS build — including Omega Ruby and the
     * EUR/JPN Alpha Sapphire titles — needs its own RE pass before it can be listed here.
     */
    private val registry: Map<Long, BuildPatch> = mapOf(
        0x000400000011C500L to alphaSapphirePublishedPatch(),
    )

    fun availability(titleId: Long): Availability {
        val build = registry[titleId] ?: return Availability.UNSUPPORTED
        return if (build.verified) Availability.AVAILABLE else Availability.EXPERIMENTAL
    }

    fun isSupportedTitle(titleId: Long): Boolean =
        availability(titleId) != Availability.UNSUPPORTED

    fun buildLabel(titleId: Long): String? = registry[titleId]?.label

    /** True when the patch file is currently on disk for [titleId]. */
    fun isPatchPresent(titleId: Long): Boolean = patchFile(titleId)?.isFile == true

    /**
     * Write the patch for [titleId]. Returns false when the build is unsupported or the user
     * directory is not available yet.
     */
    fun apply(titleId: Long): Boolean {
        val build = registry[titleId] ?: return false
        val target = patchFile(titleId) ?: return false
        return try {
            target.parentFile?.mkdirs()
            target.writeBytes(buildIps(build.patches))
            Log.i(TAG, "wrote ${target.absolutePath} for ${build.label}")
            true
        } catch (e: Exception) {
            Log.w(TAG, "failed writing 60 FPS patch", e)
            false
        }
    }

    /** Delete the patch for [titleId]. Returns true when nothing is left on disk. */
    fun remove(titleId: Long): Boolean {
        val target = patchFile(titleId) ?: return false
        return try {
            if (target.exists()) {
                target.delete()
            }
            // Leave load/mods/<title>/ alone — the randomizer owns romfs/ in the same tree.
            !target.exists()
        } catch (e: Exception) {
            Log.w(TAG, "failed removing 60 FPS patch", e)
            false
        }
    }

    /**
     * Whether the emulation session that is currently running actually booted with the patch.
     *
     * Distinct from [isPatchPresent]: toggling writes the file immediately, but the loader only
     * reads it at launch, so the UI needs this to tell the user a change is still pending.
     */
    @Volatile
    private var sessionPatched: Boolean = false

    fun isActiveThisSession(): Boolean = sessionPatched

    /**
     * Re-assert the on-disk state to match [enabled], record it as the session state, and report
     * what the state actually is.
     *
     * Call this before launching emulation: [org.citra.citra_emu.hoennforge.randomizer]'s
     * `clearMods()` deletes the whole `load/mods/<title>` tree, which takes the patch with it,
     * so a prepare run between launches would otherwise silently turn the feature off.
     */
    fun sync(titleId: Long, enabled: Boolean): Boolean {
        val wanted = enabled && registry.containsKey(titleId)
        val applied = when {
            wanted == isPatchPresent(titleId) -> wanted
            wanted -> apply(titleId)
            else -> !remove(titleId)
        }
        sessionPatched = applied
        return applied
    }

    private fun patchFile(titleId: Long): File? {
        val user = NativeLibrary.getUserDirectory()
        if (user.isBlank()) return null
        val title = OrasTitles.formatTitleId(titleId)
        return File(user, "load/mods/$title/exefs/code.ips")
    }

    /**
     * Build an IPS: `"PATCH"`, then records of 3-byte big-endian offset, 2-byte big-endian
     * length, and little-endian ARM words, then `"EOF"`.
     */
    private fun buildIps(patches: List<CodePatch>): ByteArray {
        val out = ByteArrayOutputStream()
        out.write("PATCH".toByteArray(Charsets.US_ASCII))
        for (patch in patches) {
            val offset = patch.vaddr - TEXT_BASE
            require(offset in 0..0xFFFFFF) {
                "IPS offset 0x%X out of 24-bit range".format(offset)
            }
            require(offset != IPS_EOF_OFFSET) {
                "IPS record offset collides with the EOF marker"
            }
            val data = ByteArrayOutputStream()
            for (word in patch.words) {
                data.write((word and 0xFF).toInt())
                data.write(((word ushr 8) and 0xFF).toInt())
                data.write(((word ushr 16) and 0xFF).toInt())
                data.write(((word ushr 24) and 0xFF).toInt())
            }
            val bytes = data.toByteArray()
            require(bytes.isNotEmpty() && bytes.size <= 0xFFFF) {
                "IPS record length ${bytes.size} out of range"
            }
            out.write((offset ushr 16) and 0xFF)
            out.write((offset ushr 8) and 0xFF)
            out.write(offset and 0xFF)
            out.write((bytes.size ushr 8) and 0xFF)
            out.write(bytes.size and 0xFF)
            out.write(bytes)
        }
        out.write("EOF".toByteArray(Charsets.US_ASCII))
        return out.toByteArray()
    }
}
