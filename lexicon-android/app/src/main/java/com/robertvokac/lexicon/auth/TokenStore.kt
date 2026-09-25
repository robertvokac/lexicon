package com.robertvokac.lexicon.auth

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import com.robertvokac.lexicon.api.ServerUrl
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withContext
import java.security.GeneralSecurityException
import java.util.Base64

/**
 * Persists the Bearer token encrypted with [cipher]. The server URL and user
 * name are stored beside it in the clear and bound to the ciphertext as
 * associated data, so a token cannot be replayed against another server by
 * editing the file. Nothing here ever holds a password.
 *
 * This data store lives in its own file, which the backup rules exclude.
 */
class TokenStore(
    private val dataStore: DataStore<Preferences>,
    private val cipher: SecretCipher,
    private val allowCleartextDevelopmentHosts: Boolean,
    private val cryptoDispatcher: CoroutineDispatcher = Dispatchers.Default,
) {
    data class Stored(val server: ServerUrl, val username: String, val token: String, val refreshToken: String? = null) {
        override fun toString(): String = "Stored(server=$server, username=$username, token=<redacted>, refreshToken=<redacted>)"
    }

    suspend fun save(server: ServerUrl, username: String, token: String, refreshToken: String? = null) {
        val sealed = withContext(cryptoDispatcher) {
            cipher.encrypt(token.toByteArray(Charsets.UTF_8), associatedData(server.value, username, "access"))
        }
        val sealedRefresh = refreshToken?.let { secret ->
            withContext(cryptoDispatcher) {
                cipher.encrypt(secret.toByteArray(Charsets.UTF_8), associatedData(server.value, username, "refresh"))
            }
        }
        dataStore.edit {
            it[SERVER] = server.value
            it[USERNAME] = username
            it[TOKEN] = Base64.getEncoder().encodeToString(sealed)
            if (sealedRefresh == null) it.remove(REFRESH_TOKEN)
            else it[REFRESH_TOKEN] = Base64.getEncoder().encodeToString(sealedRefresh)
        }
    }

    /** The stored session, or null. Anything unreadable is removed. */
    suspend fun load(): Stored? {
        val preferences = dataStore.data.first()
        val serverText = preferences[SERVER] ?: return null
        val username = preferences[USERNAME] ?: return null
        val sealedText = preferences[TOKEN] ?: return null
        val server = ServerUrl.fromStored(serverText, allowCleartextDevelopmentHosts)
        val token = server?.let {
            withContext(cryptoDispatcher) {
                try {
                    val sealed = Base64.getDecoder().decode(sealedText)
                    String(cipher.decrypt(sealed, associatedData(serverText, username, "access")), Charsets.UTF_8)
                } catch (_: GeneralSecurityException) {
                    // Altered data, a restored backup, or a key the system
                    // invalidated: the session is simply gone.
                    null
                } catch (_: IllegalArgumentException) {
                    null
                }
            }
        }
        if (server == null || token.isNullOrEmpty()) {
            clear()
            return null
        }
        val refreshToken = preferences[REFRESH_TOKEN]?.let { encoded ->
            try {
                val sealed = Base64.getDecoder().decode(encoded)
                withContext(cryptoDispatcher) {
                    String(cipher.decrypt(sealed, associatedData(serverText, username, "refresh")), Charsets.UTF_8)
                }
            } catch (_: GeneralSecurityException) {
                null
            } catch (_: IllegalArgumentException) {
                null
            }
        }
        return Stored(server, username, token, refreshToken)
    }

    suspend fun clear() {
        dataStore.edit { it.clear() }
    }

    private fun associatedData(server: String, username: String, purpose: String): ByteArray =
        ("lexicon-session-v1\n$server\n$username" + if (purpose == "refresh") "\nrefresh" else "")
            .toByteArray(Charsets.UTF_8)

    private companion object {
        val SERVER = stringPreferencesKey("server")
        val USERNAME = stringPreferencesKey("username")
        val TOKEN = stringPreferencesKey("token")
        val REFRESH_TOKEN = stringPreferencesKey("refresh_token")
    }
}
