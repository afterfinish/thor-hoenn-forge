package dev.tzigdon.hoennforge.data

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit

class AppPreferences(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var legalAccepted: Boolean
        get() = prefs.getBoolean(KEY_LEGAL, false)
        set(value) = prefs.edit { putBoolean(KEY_LEGAL, value) }

    var dumpDisplayName: String?
        get() = prefs.getString(KEY_DUMP_NAME, null)
        set(value) = prefs.edit { putString(KEY_DUMP_NAME, value) }

    var dumpUri: String?
        get() = prefs.getString(KEY_DUMP_URI, null)
        set(value) = prefs.edit { putString(KEY_DUMP_URI, value) }

    var dumpTitleId: String?
        get() = prefs.getString(KEY_TITLE_ID, null)
        set(value) = prefs.edit { putString(KEY_TITLE_ID, value) }

    var dumpGameLabel: String?
        get() = prefs.getString(KEY_GAME_LABEL, null)
        set(value) = prefs.edit { putString(KEY_GAME_LABEL, value) }

    var dumpRegion: String?
        get() = prefs.getString(KEY_REGION, null)
        set(value) = prefs.edit { putString(KEY_REGION, value) }

    fun clearDump() {
        prefs.edit {
            remove(KEY_DUMP_NAME)
            remove(KEY_DUMP_URI)
            remove(KEY_TITLE_ID)
            remove(KEY_GAME_LABEL)
            remove(KEY_REGION)
        }
    }

    val hasDump: Boolean
        get() = !dumpUri.isNullOrBlank() && !dumpTitleId.isNullOrBlank()

    companion object {
        private const val PREFS_NAME = "hoenn_forge"
        private const val KEY_LEGAL = "legal_accepted"
        private const val KEY_DUMP_NAME = "dump_name"
        private const val KEY_DUMP_URI = "dump_uri"
        private const val KEY_TITLE_ID = "dump_title_id"
        private const val KEY_GAME_LABEL = "dump_game_label"
        private const val KEY_REGION = "dump_region"
    }
}
