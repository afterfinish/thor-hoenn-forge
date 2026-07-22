// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.utils.SettingsFile

/**
 * Applies Thor-oriented defaults once into Azahar config.
 * Values mirror profiles/thor.json in the Hoenn Forge repo.
 */
object ThorProfile {
    fun applyIfNeeded(prefs: HoennPrefs) {
        if (prefs.thorApplied) {
            // Still re-apply resolution each session in case user reset settings
            applyCore()
            return
        }
        applyCore()
        prefs.thorApplied = true
    }

    private fun applyCore() {
        // Ensure settings enums are hydrated from disk
        Settings().loadSettings()

        // 3x internal resolution
        IntSetting.RESOLUTION_FACTOR.int = 3
        // GRAPHICS_API: 1 = OpenGL, 2 = Vulkan (default in Azahar is 2)
        IntSetting.GRAPHICS_API.int = 2
        BooleanSetting.NEW_3DS.boolean = true

        // Persist individual keys
        SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.RESOLUTION_FACTOR)
        SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.GRAPHICS_API)
        SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, BooleanSetting.NEW_3DS)

        NativeLibrary.reloadSettings()
    }
}
