package com.robertvokac.lexicon.testing

import android.content.ContentResolver
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.Preferences
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.auth.AesGcmSecretCipher
import com.robertvokac.lexicon.auth.SecretCipher
import com.robertvokac.lexicon.auth.TokenStore
import com.robertvokac.lexicon.storage.SettingsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import java.io.File
import java.nio.file.Files
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

/** AES-GCM with a software key: the Keystore cipher's code path, off the device. */
fun softwareCipher(key: SecretKey = KeyGenerator.getInstance("AES").apply { init(256) }.generateKey()): SecretCipher =
    AesGcmSecretCipher { key }

fun tempDataStore(directory: File, name: String, scope: CoroutineScope): DataStore<Preferences> =
    PreferenceDataStoreFactory.create(scope = scope) { File(directory, "$name.preferences_pb") }

/** Everything an AppContainer needs, in a temporary directory. */
class TestEnvironment(
    val directory: File = Files.createTempDirectory("lexicon-test").toFile(),
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    val cipher: SecretCipher = softwareCipher(),
) {
    val settingsStore: DataStore<Preferences> = tempDataStore(directory, "settings", scope)
    val sessionStore: DataStore<Preferences> = tempDataStore(directory, "session", scope)
    val settings = SettingsStore(settingsStore)
    val tokenStore = TokenStore(sessionStore, cipher, allowCleartextDevelopmentHosts = true)

    /** Needs Robolectric for the content resolver; plain JVM tests build the parts they use. */
    fun container(contentResolver: ContentResolver): AppContainer = AppContainer(
        settings = settings,
        tokenStore = tokenStore,
        httpClient = AppContainer.httpClient(),
        contentResolver = contentResolver,
        allowCleartextDevelopmentHosts = true,
        applicationScope = scope,
        defaultServerUrl = "",
        outboxFile = File(directory, "inbox-outbox.json"),
    )

    fun close() {
        directory.deleteRecursively()
    }
}
