package com.robertvokac.lexicon.storage

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map

enum class ThemePreference(val label: String) {
    System("System default"),
    Light("Light"),
    Dark("Dark"),
}

/**
 * Client preferences of this Android installation. They never reach the
 * server and never change the Qt or web client's preferences. None of them is
 * secret; the session token lives in TokenStore.
 */
class SettingsStore(private val dataStore: DataStore<Preferences>) {
    val theme: Flow<ThemePreference> = dataStore.data
        .map { preferences ->
            ThemePreference.entries.firstOrNull { it.name == preferences[THEME] } ?: ThemePreference.System
        }
        .distinctUntilChanged()

    val pageSize: Flow<Int> = dataStore.data
        .map { it[PAGE_SIZE]?.takeIf { size -> size in PAGE_SIZES } ?: DEFAULT_PAGE_SIZE }
        .distinctUntilChanged()

    val sessionInfo: Flow<SessionInfo> = dataStore.data
        .map { SessionInfo(it[IDLE_TIMEOUT] ?: 0, it[MAX_LIFETIME] ?: 0) }
        .distinctUntilChanged()

    suspend fun lastServerUrl(): String? = dataStore.data.first()[SERVER_URL]

    suspend fun lastUsername(): String? = dataStore.data.first()[USERNAME]

    suspend fun codeLanguage(): String = dataStore.data.first()[CODE_LANGUAGE].orEmpty()

    suspend fun setTheme(theme: ThemePreference) = dataStore.edit { it[THEME] = theme.name }

    suspend fun setPageSize(size: Int) = dataStore.edit { it[PAGE_SIZE] = size }

    suspend fun setCodeLanguage(language: String) = dataStore.edit { it[CODE_LANGUAGE] = language }

    suspend fun setServerUrl(url: String) = dataStore.edit { it[SERVER_URL] = url }

    suspend fun rememberSignIn(serverUrl: String, username: String, info: SessionInfo) = dataStore.edit {
        it[SERVER_URL] = serverUrl
        it[USERNAME] = username
        it[IDLE_TIMEOUT] = info.idleTimeoutSeconds
        it[MAX_LIFETIME] = info.absoluteLifetimeSeconds
    }

    data class SessionInfo(val idleTimeoutSeconds: Long, val absoluteLifetimeSeconds: Long)

    companion object {
        val PAGE_SIZES = listOf(20, 50, 100)
        const val DEFAULT_PAGE_SIZE = 50

        private val THEME = stringPreferencesKey("theme")
        private val PAGE_SIZE = intPreferencesKey("page_size")
        private val SERVER_URL = stringPreferencesKey("server_url")
        private val USERNAME = stringPreferencesKey("username")
        private val CODE_LANGUAGE = stringPreferencesKey("code_language")
        private val IDLE_TIMEOUT = longPreferencesKey("session_idle_timeout")
        private val MAX_LIFETIME = longPreferencesKey("session_max_lifetime")
    }
}
