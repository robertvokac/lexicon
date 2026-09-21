package com.robertvokac.lexicon

import android.content.ContentResolver
import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.preferencesDataStore
import com.robertvokac.lexicon.api.ApiClient
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.auth.AesGcmSecretCipher
import com.robertvokac.lexicon.auth.KeystoreKeys
import com.robertvokac.lexicon.auth.SecretCipher
import com.robertvokac.lexicon.auth.SessionManager
import com.robertvokac.lexicon.auth.TokenStore
import com.robertvokac.lexicon.storage.SettingsStore
import com.robertvokac.lexicon.util.DataChanges
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import okhttp3.OkHttpClient
import java.util.concurrent.TimeUnit

// Two files: settings may be backed up, the session never is (see
// res/xml/data_extraction_rules.xml).
private val Context.settingsDataStore: DataStore<Preferences> by preferencesDataStore(name = "settings")
private val Context.sessionDataStore: DataStore<Preferences> by preferencesDataStore(name = "session")

/**
 * The application's object graph, built once per process. It holds only the
 * application context, never an Activity.
 */
class AppContainer(
    val settings: SettingsStore,
    tokenStore: TokenStore,
    httpClient: OkHttpClient,
    val contentResolver: ContentResolver,
    /** True in debug builds, whose network security config allows these hosts. */
    val allowCleartextDevelopmentHosts: Boolean,
    /** Work that must outlive a screen, such as telling the server about a logout. */
    val applicationScope: CoroutineScope,
    val defaultServerUrl: String,
) {
    val sessions: SessionManager = SessionManager(
        api = { api },
        tokenStore = tokenStore,
        settings = settings,
        scope = applicationScope,
        allowCleartextDevelopmentHosts = allowCleartextDevelopmentHosts,
    )
    val api: LexiconApi = LexiconApi(ApiClient(httpClient, sessions))
    val dataChanges = DataChanges()

    companion object {
        fun create(context: Context): AppContainer {
            val application = context.applicationContext
            return AppContainer(
                settings = SettingsStore(application.settingsDataStore),
                tokenStore = TokenStore(
                    application.sessionDataStore,
                    keystoreCipher(),
                    BuildConfig.DEBUG,
                ),
                httpClient = httpClient(),
                contentResolver = application.contentResolver,
                allowCleartextDevelopmentHosts = BuildConfig.DEBUG,
                applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
                defaultServerUrl = BuildConfig.DEFAULT_SERVER_URL,
            )
        }

        fun keystoreCipher(): SecretCipher = AesGcmSecretCipher(KeystoreKeys::sessionKey)

        /**
         * Redirects are not followed, there is no HTTP cache, no cookie jar,
         * and no request or response logging: headers and bodies carry the
         * token and the password.
         */
        fun httpClient(): OkHttpClient = ApiClient.configure(OkHttpClient.Builder())
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS)
            .writeTimeout(60, TimeUnit.SECONDS)
            .addInterceptor { chain ->
                chain.proceed(
                    chain.request().newBuilder()
                        .header("User-Agent", "Lexicon-Android/${BuildConfig.VERSION_NAME}")
                        .build(),
                )
            }
            .build()
    }
}
