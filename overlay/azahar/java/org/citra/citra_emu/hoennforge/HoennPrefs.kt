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

    /** Outdoor free look: fixed 12 deg yaw probe used to validate the row detector. */
    var gpuCamProbe: Boolean
        get() = prefs.getBoolean(KEY_GPUCAM_PROBE, false)
        set(value) = prefs.edit { putBoolean(KEY_GPUCAM_PROBE, value) }

    /** Outdoor free look: -1 auto (locked row only), -2 all qualifying triples (the
     *  default, since a scene's rigid transforms live at several rows), 0..93 fixed. */
    var gpuCamRowMode: Int
        get() = prefs.getInt(KEY_GPUCAM_ROW, HoennGpuCam.ROW_MODE_ALL)
        set(value) = prefs.edit { putInt(KEY_GPUCAM_ROW, value) }

    /** Outdoor free look: read the uniform triple as columns rather than rows. */
    var gpuCamTranspose: Boolean
        get() = prefs.getBoolean(KEY_GPUCAM_TRANSPOSE, false)
        set(value) = prefs.edit { putBoolean(KEY_GPUCAM_TRANSPOSE, value) }

    /** Outdoor free look: orbit radius in eye-space units. Zero swivels about the eye
     *  instead of orbiting the player, which cannot displace geometry. */
    var gpuCamRadius: Float
        get() = prefs.getFloat(KEY_GPUCAM_RADIUS, 0f)
        set(value) = prefs.edit { putFloat(KEY_GPUCAM_RADIUS, value) }

    /** Outdoor free look: bitmask, 1 inverts yaw and 2 inverts pitch. */
    var gpuCamInvert: Int
        get() = prefs.getInt(KEY_GPUCAM_INVERT, 0)
        set(value) = prefs.edit { putInt(KEY_GPUCAM_INVERT, value) }

    /** Where the orbit centre sits — see [HoennGpuCam.PIVOT_CALIBRATED]. */
    var gpuCamPivotMode: Int
        get() = prefs.getInt(KEY_GPUCAM_PIVOT, HoennGpuCam.PIVOT_CALIBRATED)
        set(value) = prefs.edit { putInt(KEY_GPUCAM_PIVOT, value) }

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
        private const val KEY_GPUCAM_PROBE = "gpucam_probe"
        private const val KEY_GPUCAM_ROW = "gpucam_row_mode"
        private const val KEY_GPUCAM_TRANSPOSE = "gpucam_transpose"
        private const val KEY_GPUCAM_RADIUS = "gpucam_radius"
        private const val KEY_GPUCAM_INVERT = "gpucam_invert"
        private const val KEY_GPUCAM_PIVOT = "gpucam_pivot_mode"
        private const val KEY_GPUCAM_GEN = "gpucam_settings_gen"
        private const val GPUCAM_GEN = 4
        // legacy key migrated on first read via cameraZoomAssist if needed
        private const val KEY_FREECAM_LEGACY = "freecam_enabled"
    }

    init {
        // Device testing disproved several of the outdoor camera's original defaults: the
        // column-major matrix layout rotates the world about its origin, an orbit radius
        // taken from the game's camera object is on the wrong scale entirely, and putting
        // the orbit centre on the Z axis at the measured distance throws the scene off
        // screen whichever sign is used, because that distance describes a point forward
        // *and below* the eye. Retire whatever the user was left holding, once per
        // generation, so a stale knob cannot masquerade as a broken fix.
        if (prefs.getInt(KEY_GPUCAM_GEN, 0) < GPUCAM_GEN) {
            prefs.edit {
                putInt(KEY_GPUCAM_GEN, GPUCAM_GEN)
                remove(KEY_GPUCAM_TRANSPOSE)
                remove(KEY_GPUCAM_RADIUS)
                remove(KEY_GPUCAM_ROW)
                remove(KEY_GPUCAM_PIVOT)
            }
        }
        // Migrate old freecam_enabled → camera zoom assist once
        if (prefs.contains(KEY_FREECAM_LEGACY) && !prefs.contains(KEY_CAM_ZOOM)) {
            prefs.edit {
                putBoolean(KEY_CAM_ZOOM, prefs.getBoolean(KEY_FREECAM_LEGACY, false))
            }
        }
    }
}
