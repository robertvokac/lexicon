package com.robertvokac.lexicon.auth

import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import com.robertvokac.lexicon.api.ServerUrl
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.softwareCipher
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.File
import java.security.GeneralSecurityException

class TokenStoreTest {
    private val environment = TestEnvironment()
    private val server = (ServerUrl.parse("https://lexicon.example.com", false) as ServerUrl.Parsed.Valid).url

    @After
    fun tearDown() = environment.close()

    @Test
    fun roundTripsTheSession() = runBlocking {
        environment.tokenStore.save(server, "robert", "opaque-token-value", "refresh-secret-value")
        val stored = environment.tokenStore.load()!!
        assertEquals(server, stored.server)
        assertEquals("robert", stored.username)
        assertEquals("opaque-token-value", stored.token)
        assertEquals("refresh-secret-value", stored.refreshToken)
        assertFalse(stored.toString().contains("opaque-token-value"))
        assertFalse(stored.toString().contains("refresh-secret-value"))
    }

    @Test
    fun aDebugHttpSessionOnTheNetworkSurvivesARestart() = runBlocking {
        val httpServer = (ServerUrl.parse("http://192.168.1.20:8628", true, allowHttpForTesting = true)
            as ServerUrl.Parsed.Valid).url
        environment.tokenStore.save(httpServer, "robert", "test-token")
        val restored = environment.tokenStore.load()!!
        assertEquals(httpServer, restored.server)
        assertEquals("test-token", restored.token)
    }

    @Test
    fun theTokenIsNeverStoredInTheClear() = runBlocking {
        environment.tokenStore.save(server, "robert", "opaque-token-value", "refresh-secret-value")
        val file = File(environment.directory, "session.preferences_pb")
        val raw = file.readBytes().toString(Charsets.ISO_8859_1)
        assertFalse(raw.contains("opaque-token-value"))
        assertFalse(raw.contains("refresh-secret-value"))
        val sealed = environment.sessionStore.data.first()[stringPreferencesKey("token")]!!
        assertFalse(sealed.contains("opaque-token-value"))
    }

    @Test
    fun clearRemovesEverything() = runBlocking {
        environment.tokenStore.save(server, "robert", "opaque-token-value")
        environment.tokenStore.clear()
        assertNull(environment.tokenStore.load())
        assertEquals(0, environment.sessionStore.data.first().asMap().size)
    }

    @Test
    fun aTokenMovedToAnotherServerDoesNotDecrypt() = runBlocking {
        environment.tokenStore.save(server, "robert", "opaque-token-value")
        environment.sessionStore.edit { it[stringPreferencesKey("server")] = "https://evil.example.com" }
        assertNull(environment.tokenStore.load())
        // And the unreadable remains are gone.
        assertEquals(0, environment.sessionStore.data.first().asMap().size)
    }

    @Test
    fun corruptedDataIsDiscarded() = runBlocking {
        environment.tokenStore.save(server, "robert", "opaque-token-value")
        environment.sessionStore.edit { it[stringPreferencesKey("token")] = "bm90IGEgcmVhbCBjaXBoZXJ0ZXh0" }
        assertNull(environment.tokenStore.load())
        environment.sessionStore.edit {
            it[stringPreferencesKey("server")] = server.value
            it[stringPreferencesKey("username")] = "robert"
            it[stringPreferencesKey("token")] = "%%% not base64 %%%"
        }
        assertNull(environment.tokenStore.load())
    }

    @Test
    fun anotherKeyCannotReadTheToken() = runBlocking {
        environment.tokenStore.save(server, "robert", "opaque-token-value")
        // What a restored backup looks like: the data without this device's key.
        val otherDevice = TokenStore(environment.sessionStore, softwareCipher(), allowCleartextDevelopmentHosts = false)
        assertNull(otherDevice.load())
    }

    @Test
    fun theCipherIsAuthenticatedAndRandomized() {
        val cipher = softwareCipher()
        val first = cipher.encrypt("token".toByteArray(), "aad".toByteArray())
        val second = cipher.encrypt("token".toByteArray(), "aad".toByteArray())
        assertFalse(first.contentEquals(second))
        assertEquals("token", String(cipher.decrypt(first, "aad".toByteArray())))
        val tampered = first.copyOf().also { it[it.size - 1] = (it[it.size - 1] + 1).toByte() }
        assertThrows(GeneralSecurityException::class.java) { cipher.decrypt(tampered, "aad".toByteArray()) }
        assertThrows(GeneralSecurityException::class.java) { cipher.decrypt(first, "other".toByteArray()) }
    }
}
