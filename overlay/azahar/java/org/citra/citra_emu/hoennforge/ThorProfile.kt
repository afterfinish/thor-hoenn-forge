// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.util.Log
import android.view.KeyEvent
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.display.ScreenLayout
import org.citra.citra_emu.display.SecondaryDisplayLayout
import org.citra.citra_emu.display.StereoMode
import org.citra.citra_emu.features.hotkeys.Hotkey
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.EmulationMenuSettings

/**
 * Thor-oriented defaults for Azahar on dual-screen AYN Thor (Adreno).
 *
 * Graphics tuned from Azahar Android guides + community ORAS notes:
 * - Vulkan + async shaders + disk cache
 * - Mono rendering (3D OFF) + disable right-eye render (big FPS win vs stereo default)
 * - Accurate mul for ORAS white/blue Pokémon (Adreno)
 * - Skip duplicate frames, no VSync lag, frame limit at 100%
 */
object ThorProfile {
    private const val TAG = "HoennForge"
    private const val INPUT_MAPPING_PREFIX = "InputMapping"

    /**
     * FPS and speed readout over the game.
     *
     * Off is the shipping value — it is a development aid, not something a player wants to
     * look at. Temporarily true while the 60 FPS patch is being evaluated, because judging
     * whether presentation rate actually changed is very hard without a number, and
     * [applyCore] re-stomps these settings on every launch so toggling it in Azahar's own
     * settings will not stick. Set back to false when that testing is finished.
     */
    private const val PERF_OVERLAY_DEFAULT = true

    fun applyIfNeeded(prefs: HoennPrefs) {
        applyCore()
        if (!prefs.thorApplied) {
            prefs.thorApplied = true
        }
    }

    private fun applyCore() {
        try {
            Settings().loadSettings()
        } catch (e: Exception) {
            Log.w(TAG, "loadSettings before Thor profile", e)
        }

        // --- Graphics / performance (Azahar Android setup + handheld advice) ---
        // 3x ≈ 1200×720 top. Was 4x, which looks marginally sharper but holds frame rate
        // noticeably less well on Thor; dogfooding preferred the steadier 3x.
        IntSetting.RESOLUTION_FACTOR.int = 3
        IntSetting.GRAPHICS_API.int = 2 // Vulkan
        BooleanSetting.NEW_3DS.boolean = true
        BooleanSetting.CPU_JIT.boolean = true
        IntSetting.CPU_CLOCK_SPEED.int = 100

        // Sharp pixels for pixel-art / grass
        BooleanSetting.LINEAR_FILTERING.boolean = false
        IntSetting.TEXTURE_SAMPLING.int = 1 // NearestNeighbor
        IntSetting.TEXTURE_FILTER.int = 0 // NoFilter

        // ORAS Adreno: white/blue Pokémon fix (needs HW shaders)
        BooleanSetting.HW_SHADER.boolean = true
        BooleanSetting.SHADER_JIT.boolean = true
        BooleanSetting.SHADERS_ACCURATE_MUL.boolean = true
        BooleanSetting.SPIRV_SHADER_GEN.boolean = true
        BooleanSetting.DISABLE_SPIRV_OPTIMIZER.boolean = true

        // Shader stutter mitigation (Joey RH / Azahar guides)
        BooleanSetting.ASYNC_SHADERS.boolean = true
        BooleanSetting.DISK_SHADER_CACHE.boolean = true

        // CRITICAL: Azahar default STEREOSCOPIC_3D_MODE=2 is SIDE_BY_SIDE_FULL —
        // that renders *two* eyes. Force mono + drop right eye for ~2× fill-rate.
        IntSetting.STEREOSCOPIC_3D_MODE.int = StereoMode.OFF.int
        IntSetting.STEREOSCOPIC_3D_DEPTH.int = 0
        BooleanSetting.DISABLE_RIGHT_EYE_RENDER.boolean = true
        BooleanSetting.SWAP_EYES_3D.boolean = false

        // Frame pacing: no vsync wait; cap at native (ORAS ~30)
        BooleanSetting.VSYNC.boolean = false
        BooleanSetting.USE_FRAME_LIMIT.boolean = true
        IntSetting.FRAME_LIMIT.int = 100
        BooleanSetting.USE_SKIP_DUPLICATE_FRAMES.boolean = true
        BooleanSetting.SIMULATE_3DS_GPU_TIMINGS.boolean = false
        IntSetting.DELAY_RENDER_THREAD_US.int = 0
        BooleanSetting.DEBUG_RENDERER.boolean = false

        // Audio: stretch helps when emulated FPS dips (avoids crackle)
        BooleanSetting.ENABLE_AUDIO_STRETCHING.boolean = true
        BooleanSetting.ENABLE_REALTIME_AUDIO.boolean = false

        // No HD texture tax by default
        BooleanSetting.CUSTOM_TEXTURES.boolean = false
        BooleanSetting.PRELOAD_TEXTURES.boolean = false
        BooleanSetting.ASYNC_CUSTOM_LOADING.boolean = true

        // Turbo when L3 held/toggled
        IntSetting.TURBO_LIMIT.int = 300

        // See PERF_OVERLAY_DEFAULT — off for players, on while 60 FPS is being measured.
        BooleanSetting.PERF_OVERLAY_ENABLE.boolean = PERF_OVERLAY_DEFAULT
        BooleanSetting.PERF_OVERLAY_SHOW_FPS.boolean = PERF_OVERLAY_DEFAULT
        BooleanSetting.PERF_OVERLAY_SHOW_SPEED.boolean = PERF_OVERLAY_DEFAULT
        BooleanSetting.PERF_OVERLAY_BACKGROUND.boolean = PERF_OVERLAY_DEFAULT

        // --- Dual display ---
        BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean = true
        BooleanSetting.SWAP_SCREEN.boolean = false
        IntSetting.SCREEN_LAYOUT.int = ScreenLayout.SINGLE_SCREEN.int
        IntSetting.SECONDARY_DISPLAY_LAYOUT.int = SecondaryDisplayLayout.BOTTOM_SCREEN.int
        IntSetting.ORIENTATION_OPTION.int = 2

        EmulationMenuSettings.showOverlay = false
        EmulationMenuSettings.swapScreens = false

        // --- Controller ---
        try {
            InputBindingSetting.clearAllBindings()
            InputBindingSetting.applyAutoMapBindings(
                isNintendoLayout = true,
                useAxisDpad = true,
            )
            ensureCStickMapped()
            applyL3TurboHotkey()
            unmap3dsStartButton()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to apply controller mappings", e)
        }

        val toSave = listOf(
            IntSetting.RESOLUTION_FACTOR,
            IntSetting.GRAPHICS_API,
            IntSetting.SCREEN_LAYOUT,
            IntSetting.SECONDARY_DISPLAY_LAYOUT,
            IntSetting.ORIENTATION_OPTION,
            IntSetting.TURBO_LIMIT,
            IntSetting.TEXTURE_FILTER,
            IntSetting.TEXTURE_SAMPLING,
            IntSetting.STEREOSCOPIC_3D_MODE,
            IntSetting.STEREOSCOPIC_3D_DEPTH,
            IntSetting.FRAME_LIMIT,
            IntSetting.CPU_CLOCK_SPEED,
            IntSetting.DELAY_RENDER_THREAD_US,
            BooleanSetting.NEW_3DS,
            BooleanSetting.CPU_JIT,
            BooleanSetting.LINEAR_FILTERING,
            BooleanSetting.SHADERS_ACCURATE_MUL,
            BooleanSetting.ASYNC_SHADERS,
            BooleanSetting.DISK_SHADER_CACHE,
            BooleanSetting.HW_SHADER,
            BooleanSetting.SHADER_JIT,
            BooleanSetting.SPIRV_SHADER_GEN,
            BooleanSetting.DISABLE_SPIRV_OPTIMIZER,
            BooleanSetting.DISABLE_RIGHT_EYE_RENDER,
            BooleanSetting.SWAP_EYES_3D,
            BooleanSetting.VSYNC,
            BooleanSetting.USE_FRAME_LIMIT,
            BooleanSetting.USE_SKIP_DUPLICATE_FRAMES,
            BooleanSetting.SIMULATE_3DS_GPU_TIMINGS,
            BooleanSetting.DEBUG_RENDERER,
            BooleanSetting.ENABLE_AUDIO_STRETCHING,
            BooleanSetting.ENABLE_REALTIME_AUDIO,
            BooleanSetting.CUSTOM_TEXTURES,
            BooleanSetting.PRELOAD_TEXTURES,
            BooleanSetting.ASYNC_CUSTOM_LOADING,
            BooleanSetting.ENABLE_SECONDARY_DISPLAY,
            BooleanSetting.SWAP_SCREEN,
            BooleanSetting.PERF_OVERLAY_ENABLE,
            BooleanSetting.PERF_OVERLAY_SHOW_FPS,
            BooleanSetting.PERF_OVERLAY_SHOW_SPEED,
            BooleanSetting.PERF_OVERLAY_BACKGROUND,
        )
        for (setting in toSave) {
            try {
                SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, setting)
            } catch (e: Exception) {
                Log.w(TAG, "save ${setting.key} failed", e)
            }
        }

        try {
            NativeLibrary.reloadSettings()
        } catch (e: Exception) {
            Log.w(TAG, "reloadSettings failed", e)
        }

        Log.i(
            TAG,
            "Thor profile: Vulkan 3x, stereo OFF, disable_right_eye=true, " +
                "async_shaders+disk_cache, accurate_mul, skip_dup_frames, " +
                "perf overlay ${if (PERF_OVERLAY_DEFAULT) "on" else "off"}",
        )
    }

    /**
     * Bind host L3 (thumb-left click) → turbo hotkey.
     * Public so emulation can re-apply after freecam / settings load (was dead until
     * map/camera refresh after freecam work).
     *
     * Uses StringSet mapping (same as InputBindingSetting.writeButtonMapping). Also
     * removes a leftover Int mapping at the same key so getButtonSet does not ClassCast
     * or ignore the turbo code.
     */
    fun applyL3TurboHotkey() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        // Same key scheme as InputBindingSetting.getInputButtonKey(keyId)
        val hostKey = "${INPUT_MAPPING_PREFIX}_HostAxis_${KeyEvent.KEYCODE_BUTTON_THUMBL}"
        val reverseKey =
            "${INPUT_MAPPING_PREFIX}_ReverseMapping_${Settings.HOTKEY_TURBO_LIMIT}"
        val turboCode = Hotkey.TURBO_LIMIT.button.toString()

        // Merge with any existing codes on this key (don't wipe other binds)
        val existing = try {
            prefs.getStringSet(hostKey, null)?.toMutableSet() ?: mutableSetOf()
        } catch (_: ClassCastException) {
            // Old int-style bind — drop it so StringSet turbo can land
            prefs.edit().remove(hostKey).apply()
            mutableSetOf()
        }
        existing.add(turboCode)

        prefs.edit()
            .putString(Settings.HOTKEY_ENABLE, "") // empty = hotkeys always armed
            .putString(Settings.HOTKEY_TURBO_LIMIT, "Button L3")
            .putStringSet(hostKey, existing)
            .putString(reverseKey, hostKey)
            .apply()

        Log.i(TAG, "Mapped L3 ($hostKey) -> turbo hotkey $turboCode codes=$existing")
    }

    private fun ensureCStickMapped() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val editor = prefs.edit()
        bindAxis(editor, android.view.MotionEvent.AXIS_Z, NativeLibrary.ButtonType.STICK_C, 0, false)
        bindAxis(editor, android.view.MotionEvent.AXIS_RZ, NativeLibrary.ButtonType.STICK_C, 1, false)
        bindAxis(editor, android.view.MotionEvent.AXIS_RX, NativeLibrary.ButtonType.STICK_C, 0, false)
        bindAxis(editor, android.view.MotionEvent.AXIS_RY, NativeLibrary.ButtonType.STICK_C, 1, false)
        editor.apply()
        Log.i(TAG, "C-Stick: mapped AXIS_Z/RZ and AXIS_RX/RY -> STICK_C")
    }

    private fun bindAxis(
        editor: android.content.SharedPreferences.Editor,
        axis: Int,
        guestStick: Int,
        guestOrientation: Int,
        inverted: Boolean,
    ) {
        val hostKey = "${INPUT_MAPPING_PREFIX}_HostAxis_$axis"
        val guestCode = guestStick.toString()
        editor.putStringSet(hostKey, mutableSetOf(guestCode))
        editor.putInt(InputBindingSetting.getInputAxisButtonKey(axis), guestStick)
        editor.putInt(InputBindingSetting.getInputAxisOrientationKey(axis), guestOrientation)
        editor.putBoolean(InputBindingSetting.getInputAxisInvertedKey(axis), inverted)
    }

    private fun unmap3dsStartButton() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val hostKey = "${INPUT_MAPPING_PREFIX}_HostAxis_${KeyEvent.KEYCODE_BUTTON_START}"
        val reverseKey =
            "${INPUT_MAPPING_PREFIX}_ReverseMapping_${Settings.KEY_BUTTON_START}"
        prefs.edit()
            .remove(Settings.KEY_BUTTON_START)
            .remove(hostKey)
            .remove(reverseKey)
            .apply()
        Log.i(TAG, "Unmapped host Start from 3DS Start (reserved for quick menu)")
    }
}
