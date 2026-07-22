// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.display.ScreenLayout
import org.citra.citra_emu.display.SecondaryDisplayLayout
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.EmulationMenuSettings

/**
 * Thor-oriented defaults for Azahar on dual-screen AYN Thor.
 *
 * Display model:
 * - Primary surface (usually top panel): 3DS **top** screen only
 * - Secondary surface (bottom panel): 3DS **bottom** (touch) only
 * - On-screen touch controls hidden (hardware "Odin Controller")
 * - Controller auto-mapped (Xbox layout + HAT d-pad; Odin/Thor style)
 */
object ThorProfile {
    private const val TAG = "HoennForge"

    fun applyIfNeeded(prefs: HoennPrefs) {
        // Always re-apply so layout/controls stay correct after updates
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

        // --- Graphics ---
        IntSetting.RESOLUTION_FACTOR.int = 3
        IntSetting.GRAPHICS_API.int = 2 // Vulkan
        BooleanSetting.NEW_3DS.boolean = true
        BooleanSetting.DISK_SHADER_CACHE.boolean = true
        BooleanSetting.ASYNC_SHADERS.boolean = true

        // --- Dual display (Thor top + bottom panels) ---
        // Primary = single top screen; secondary = bottom screen only
        BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean = true
        BooleanSetting.SWAP_SCREEN.boolean = false
        IntSetting.SCREEN_LAYOUT.int = ScreenLayout.SINGLE_SCREEN.int // 1
        IntSetting.SECONDARY_DISPLAY_LAYOUT.int = SecondaryDisplayLayout.BOTTOM_SCREEN.int // 2
        // Landscape orientation (2 is a common Azahar default for landscape)
        IntSetting.ORIENTATION_OPTION.int = 2

        // --- On-screen controls OFF (use built-in pads) ---
        EmulationMenuSettings.showOverlay = false

        // --- Controller: Odin Controller = Xbox-style face buttons + HAT d-pad ---
        try {
            InputBindingSetting.clearAllBindings()
            // isNintendoLayout=false → Xbox mapping (A=south KEYCODE_BUTTON_A)
            // useAxisDpad=true → HAT_X/HAT_Y (Odin Controller has these)
            InputBindingSetting.applyAutoMapBindings(
                isNintendoLayout = false,
                useAxisDpad = true,
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to apply controller auto-map", e)
        }

        // Persist ini settings
        val toSave = listOf(
            IntSetting.RESOLUTION_FACTOR,
            IntSetting.GRAPHICS_API,
            IntSetting.SCREEN_LAYOUT,
            IntSetting.SECONDARY_DISPLAY_LAYOUT,
            IntSetting.ORIENTATION_OPTION,
            BooleanSetting.NEW_3DS,
            BooleanSetting.DISK_SHADER_CACHE,
            BooleanSetting.ASYNC_SHADERS,
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
            "Thor profile applied: single primary top, secondary bottom, " +
                "overlay=false, xbox+hat bindings",
        )
    }
}
