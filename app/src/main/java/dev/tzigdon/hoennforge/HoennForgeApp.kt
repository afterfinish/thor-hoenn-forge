package dev.tzigdon.hoennforge

import android.app.Application
import dev.tzigdon.hoennforge.data.AppPreferences

class HoennForgeApp : Application() {
    lateinit var preferences: AppPreferences
        private set

    override fun onCreate() {
        super.onCreate()
        instance = this
        preferences = AppPreferences(this)
    }

    companion object {
        lateinit var instance: HoennForgeApp
            private set
    }
}
