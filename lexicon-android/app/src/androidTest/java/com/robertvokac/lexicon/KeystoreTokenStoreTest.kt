package com.robertvokac.lexicon

import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.robertvokac.lexicon.api.ServerUrl
import com.robertvokac.lexicon.auth.TokenStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** The session token encrypted with a real Android Keystore key. */
@RunWith(AndroidJUnit4::class)
class KeystoreTokenStoreTest {
    @Test
    fun theTokenIsEncryptedWithTheKeystoreKey() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val file = File(context.cacheDir, "keystore-test.preferences_pb").also { it.delete() }
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        try {
            val store = TokenStore(PreferenceDataStoreFactory.create(scope = scope) { file }, AppContainer.keystoreCipher(), true)
            val server = (ServerUrl.parse("https://lexicon.example.com", false) as ServerUrl.Parsed.Valid).url
            store.save(server, "robert", "device-token-value")
            assertFalse(file.readBytes().toString(Charsets.ISO_8859_1).contains("device-token-value"))
            assertEquals("device-token-value", store.load()?.token)
            store.clear()
            assertNull(store.load())
        } finally {
            scope.cancel()
            file.delete()
        }
    }
}
