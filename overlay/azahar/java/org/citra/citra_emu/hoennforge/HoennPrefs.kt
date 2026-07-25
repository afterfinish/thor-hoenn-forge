// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Context
import androidx.core.content.edit
import org.citra.citra_emu.hoennforge.randomizer.RandomizerConfig

class HoennPrefs(context: Context) {
    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    var legalAccepted: Boolean
        get() = prefs.getBoolean(KEY_LEGAL, false)
        set(value) = prefs.edit { putBoolean(KEY_LEGAL, value) }

    var dumpDisplayName: String?
        get() = prefs.getString(KEY_NAME, null)
        set(value) = prefs.edit { putString(KEY_NAME, value) }

    var dumpUri: String?
        get() = prefs.getString(KEY_URI, null)
        set(value) = prefs.edit { putString(KEY_URI, value) }

    var dumpTitleId: String?
        get() = prefs.getString(KEY_TITLE, null)
        set(value) = prefs.edit { putString(KEY_TITLE, value) }

    var dumpGameLabel: String?
        get() = prefs.getString(KEY_LABEL, null)
        set(value) = prefs.edit { putString(KEY_LABEL, value) }

    var dumpRegion: String?
        get() = prefs.getString(KEY_REGION, null)
        set(value) = prefs.edit { putString(KEY_REGION, value) }

    var thorApplied: Boolean
        get() = prefs.getBoolean(KEY_THOR, false)
        set(value) = prefs.edit { putBoolean(KEY_THOR, value) }

    /** True after prepare pipeline finished (vanilla or randomized). */
    var preparedReady: Boolean
        get() = prefs.getBoolean(KEY_PREPARED, false)
        set(value) = prefs.edit { putBoolean(KEY_PREPARED, value) }

    var randomizerJson: String?
        get() = prefs.getString(KEY_RANDOMIZER, null)
        set(value) = prefs.edit { putString(KEY_RANDOMIZER, value) }

    var randomizerConfig: RandomizerConfig
        get() = RandomizerConfig.fromJson(randomizerJson)
        set(value) {
            randomizerJson = value.toJsonString()
        }

    /** Camera zoom assist (START menu). L/R zoom + wide FOV via Gateway cheats. */
    var cameraZoomAssistEnabled: Boolean
        get() = prefs.getBoolean(KEY_CAM_ZOOM, false)
        set(value) = prefs.edit { putBoolean(KEY_CAM_ZOOM, value) }

    /** Right-stick free-look (START menu). Native freecam driver (houses). */
    var freelookEnabled: Boolean
        get() = prefs.getBoolean(KEY_FREELOOK, false)
        set(value) = prefs.edit { putBoolean(KEY_FREELOOK, value) }

    @Deprecated("Renamed to cameraZoomAssistEnabled", ReplaceWith("cameraZoomAssistEnabled"))
    var freecamEnabled: Boolean
        get() = cameraZoomAssistEnabled
        set(value) {
            cameraZoomAssistEnabled = value
        }

    val hasDump: Boolean
        get() = !dumpUri.isNullOrBlank() && !dumpTitleId.isNullOrBlank()

    fun clearDump() {
        prefs.edit {
            remove(KEY_NAME)
            remove(KEY_URI)
            remove(KEY_TITLE)
            remove(KEY_LABEL)
            remove(KEY_REGION)
            remove(KEY_PREPARED)
            remove(KEY_RANDOMIZER)
        }
    }

    /** Keep dump; clear prepare / randomizer for a new run. */
    fun clearPreparedRun() {
        prefs.edit {
            remove(KEY_PREPARED)
            remove(KEY_RANDOMIZER)
        }
    }

    companion object {
        private const val PREFS = "hoenn_forge"
        private const val KEY_LEGAL = "legal_accepted"
        private const val KEY_NAME = "dump_name"
        private const val KEY_URI = "dump_uri"
        private const val KEY_TITLE = "dump_title_id"
        private const val KEY_LABEL = "dump_game_label"
        private const val KEY_REGION = "dump_region"
        private const val KEY_THOR = "thor_applied"
        private const val KEY_PREPARED = "prepared_ready"
        private const val KEY_RANDOMIZER = "randomizer_json"
        private const val KEY_CAM_ZOOM = "camera_zoom_assist"
        private const val KEY_FREELOOK = "freelook_enabled"
        // legacy key migrated on first read via cameraZoomAssist if needed
        private const val KEY_FREECAM_LEGACY = "freecam_enabled"
    }

    init {
        // Migrate old freecam_enabled → camera zoom assist once
        if (prefs.contains(KEY_FREECAM_LEGACY) && !prefs.contains(KEY_CAM_ZOOM)) {
            prefs.edit {
                putBoolean(KEY_CAM_ZOOM, prefs.getBoolean(KEY_FREECAM_LEGACY, false))
            }
        }
    }
}
