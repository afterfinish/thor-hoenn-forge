// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.util.Log
import android.view.KeyEvent
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.display.ScreenLayout
import org.citra.citra_emu.display.SecondaryDisplayLayout
import org.citra.citra_emu.features.hotkeys.Hotkey
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.EmulationMenuSettings

/**
 * Thor-oriented defaults for Azahar on dual-screen AYN Thor.
 *
 * - Dual display: primary = top only, secondary = bottom only
 * - Overlay off; Odin Controller with **Nintendo / 3DS face layout**
 * - L3 (left stick click) = turbo / fast-forward toggle
 * - Async shader compilation + disk shader cache (shader storage) ON
 */
object ThorProfile {
    private const val TAG = "HoennForge"
    private const val INPUT_MAPPING_PREFIX = "InputMapping"

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

        // --- Graphics / performance ---
        // 4x internal res ≈ 1600×960 top screen (closest integer under 1080p)
        IntSetting.RESOLUTION_FACTOR.int = 4
        IntSetting.GRAPHICS_API.int = 2 // Vulkan
        BooleanSetting.NEW_3DS.boolean = true
        // Nearest display filter + force nearest sampling (game-controlled still blurs grass)
        BooleanSetting.LINEAR_FILTERING.boolean = false
        IntSetting.TEXTURE_SAMPLING.int = 1 // NearestNeighbor
        IntSetting.TEXTURE_FILTER.int = 0 // NoFilter (Anime4K etc. off)
        // Asynchronous shader compilation + persistent shader storage
        BooleanSetting.ASYNC_SHADERS.boolean = true
        BooleanSetting.DISK_SHADER_CACHE.boolean = true
        BooleanSetting.HW_SHADER.boolean = true
        BooleanSetting.SHADER_JIT.boolean = true
        // Turbo speed when enabled via L3 (200 = 2x default in Azahar)
        IntSetting.TURBO_LIMIT.int = 300

        // --- Dual display ---
        BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean = true
        BooleanSetting.SWAP_SCREEN.boolean = false
        IntSetting.SCREEN_LAYOUT.int = ScreenLayout.SINGLE_SCREEN.int
        IntSetting.SECONDARY_DISPLAY_LAYOUT.int = SecondaryDisplayLayout.BOTTOM_SCREEN.int
        IntSetting.ORIENTATION_OPTION.int = 2

        EmulationMenuSettings.showOverlay = false
        EmulationMenuSettings.swapScreens = false

        // --- Controller: Nintendo / 3DS face layout + HAT d-pad + L3 turbo ---
        try {
            InputBindingSetting.clearAllBindings()
            // 3DS layout: A = right (east) = KEYCODE_BUTTON_A, B = bottom = KEYCODE_BUTTON_B
            InputBindingSetting.applyAutoMapBindings(
                isNintendoLayout = true,
                useAxisDpad = true,
            )
            applyL3TurboHotkey()
            // Start is reserved for Hoenn quick menu (save states), not 3DS Start
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
            BooleanSetting.NEW_3DS,
            BooleanSetting.LINEAR_FILTERING,
            BooleanSetting.ASYNC_SHADERS,
            BooleanSetting.DISK_SHADER_CACHE,
            BooleanSetting.HW_SHADER,
            BooleanSetting.SHADER_JIT,
            BooleanSetting.ENABLE_SECONDARY_DISPLAY,
            BooleanSetting.SWAP_SCREEN,
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
            "Thor profile: dual screens, overlay off, 3DS face layout, " +
                "L3=turbo, linear_filter=false (nearest), async_shaders=true",
        )
    }

    /**
     * Map left stick click (L3 / KEYCODE_BUTTON_THUMBL) to turbo toggle hotkey.
     * Hotkey enable is left empty so the binding works without a modifier.
     */
    private fun applyL3TurboHotkey() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val hostKey = "${INPUT_MAPPING_PREFIX}_HostAxis_${KeyEvent.KEYCODE_BUTTON_THUMBL}"
        val reverseKey =
            "${INPUT_MAPPING_PREFIX}_ReverseMapping_${Settings.HOTKEY_TURBO_LIMIT}"
        val turboCode = Hotkey.TURBO_LIMIT.button.toString()

        prefs.edit()
            // Empty enable key = hotkeys always active (see HotkeyUtility)
            .putString(Settings.HOTKEY_ENABLE, "")
            .putString(Settings.HOTKEY_TURBO_LIMIT, "Button L3")
            .putStringSet(hostKey, mutableSetOf(turboCode))
            .putString(reverseKey, hostKey)
            .apply()

        Log.i(TAG, "Mapped L3 ($hostKey) -> turbo hotkey $turboCode")
    }

    /** Start on Thor opens Hoenn menu; do not also send 3DS Start. */
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
