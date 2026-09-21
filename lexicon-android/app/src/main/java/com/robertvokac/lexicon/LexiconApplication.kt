package com.robertvokac.lexicon

import android.app.Application

class LexiconApplication : Application() {
    /** Replaced by tests before any Activity starts. */
    lateinit var container: AppContainer

    override fun onCreate() {
        super.onCreate()
        if (!::container.isInitialized) container = AppContainer.create(this)
    }
}
