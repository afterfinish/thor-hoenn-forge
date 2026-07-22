// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Context
import androidx.core.content.edit

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

    val hasDump: Boolean
        get() = !dumpUri.isNullOrBlank() && !dumpTitleId.isNullOrBlank()

    fun clearDump() {
        prefs.edit {
            remove(KEY_NAME)
            remove(KEY_URI)
            remove(KEY_TITLE)
            remove(KEY_LABEL)
            remove(KEY_REGION)
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
    }
}
