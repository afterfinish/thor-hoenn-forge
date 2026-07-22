// Copyright Hoenn Forge / uses Azahar stack (GPLv2+)
package org.citra.citra_emu.hoennforge

import android.content.Context
import android.content.Intent
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.PermissionsHandler

/**
 * Shared onboarding routing:
 * Legal → Data folder → Dump → Play mode → [Randomizer] → Prepare → Home
 */
object Onboarding {
    fun hasDataDirectory(context: Context): Boolean =
        PermissionsHandler.hasWriteAccess(context)

    /** Next activity class for cold start after legal acceptance. */
    fun nextAfterLegal(context: Context, prefs: HoennPrefs): Class<*> {
        if (!hasDataDirectory(context)) return DataDirActivity::class.java
        if (!prefs.hasDump) return DumpPickerActivity::class.java
        if (!prefs.preparedReady) return PlayModeActivity::class.java
        return HomeActivity::class.java
    }

    fun nextAfterDataDir(prefs: HoennPrefs): Class<*> {
        if (!prefs.hasDump) return DumpPickerActivity::class.java
        if (!prefs.preparedReady) return PlayModeActivity::class.java
        return HomeActivity::class.java
    }

    fun ensureDirectoriesInitialized(context: Context): Boolean {
        if (!hasDataDirectory(context)) return false
        DirectoryInitialization.start()
        return DirectoryInitialization.areCitraDirectoriesReady()
    }

    fun intentTo(context: Context, clazz: Class<*>): Intent =
        Intent(context, clazz)
}
