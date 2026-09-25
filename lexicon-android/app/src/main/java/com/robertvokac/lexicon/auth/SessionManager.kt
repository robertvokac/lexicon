package com.robertvokac.lexicon.auth

import com.robertvokac.lexicon.api.ApiCompatibility
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.api.ServerUrl
import com.robertvokac.lexicon.api.Session
import com.robertvokac.lexicon.api.SessionAccess
import com.robertvokac.lexicon.storage.SettingsStore
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Who is signed in to which server. The UI keeps one tree of screens per
 * [key]; signing in again as the same person after the session expired keeps
 * that tree, and with it every unsaved edit.
 */
data class Identity(val server: ServerUrl, val username: String, val epoch: Int) {
    val key: String get() = "$epoch|${server.value}|$username"
}

sealed interface SessionState {
    /** The stored session is being read and verified. */
    data object Restoring : SessionState

    /**
     * Show the login screen. [retained] is the identity of a session that
     * expired underneath the user; its screens stay alive behind the login.
     */
    data class SignedOut(val message: String? = null, val retained: Identity? = null) : SessionState

    data class SignedIn(val identity: Identity, val serverApiVersion: Int) : SessionState

    /** The server speaks another API version. Nothing is sent until that changes. */
    data class Incompatible(val server: ServerUrl, val message: String) : SessionState

    /** A stored session exists but the server could not be reached to verify it. */
    data class Unreachable(val server: ServerUrl, val username: String, val message: String) : SessionState
}

class SessionManager(
    private val api: () -> LexiconApi,
    private val tokenStore: TokenStore,
    private val settings: SettingsStore,
    private val scope: CoroutineScope,
    private val allowCleartextDevelopmentHosts: Boolean,
    private val canBrowseOffline: (ServerUrl, String) -> Boolean = { _, _ -> false },
) : SessionAccess {
    sealed interface LoginResult {
        data object Success : LoginResult
        data class Failure(val message: String) : LoginResult
    }

    private val lock = Any()
    private val storage = Mutex()
    private val started = AtomicBoolean(false)
    private val _state = MutableStateFlow<SessionState>(SessionState.Restoring)
    private var active: Session? = null
    private var pending: Session? = null
    private var epoch = 0

    val state: StateFlow<SessionState> = _state.asStateFlow()

    override val current: Session? get() = synchronized(lock) { active }

    /**
     * Reads and verifies the stored session, once per process, unless someone
     * has signed in already.
     */
    fun start() {
        if (started.compareAndSet(false, true) && _state.value == SessionState.Restoring) scope.launch { restore() }
    }

    private suspend fun restore() {
        val stored = try {
            storage.withLock { tokenStore.load() }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            null
        }
        if (stored == null) {
            _state.value = SessionState.SignedOut()
            return
        }
        verify(Session(stored.server, stored.username, stored.token))
    }

    /** Checks the API version first, then that the server still knows the token. */
    private suspend fun verify(session: Session) {
        _state.value = SessionState.Restoring
        try {
            val health = api().health(session.server)
            val compatibility = ApiCompatibility.check(health, session.server)
            if (compatibility is ApiCompatibility.Result.Incompatible) {
                pending = session
                _state.value = SessionState.Incompatible(session.server, compatibility.message)
                return
            }
            val me = api().me(session)
            if (me.apiVersion != LexiconApi.API_VERSION) {
                pending = session
                _state.value = SessionState.Incompatible(
                    session.server,
                    "This Lexicon app speaks API version ${LexiconApi.API_VERSION}, but the server " +
                        "reports version ${me.apiVersion} for this session.",
                )
                return
            }
            pending = null
            activate(session, health.apiVersion)
        } catch (_: ApiException.Unauthorized) {
            pending = null
            storage.withLock { tokenStore.clear() }
            _state.value = SessionState.SignedOut("Your session has expired. Sign in again.")
        } catch (failure: ApiException) {
            pending = session
            _state.value = SessionState.Unreachable(
                session.server,
                session.username,
                failure.message ?: "The server could not be reached.",
            )
        }
    }

    /** Verifies the stored session again after Unreachable or Incompatible. */
    fun retry(): Job? {
        val session = pending ?: return null
        return scope.launch { verify(session) }
    }

    /** Opens the encrypted local read cache with the stored identity. Writes
     * still require the server, and the next 401 ends this session. */
    fun browseOffline(): Boolean {
        val session = pending ?: return false
        if (!canBrowseOffline(session.server, session.username)) return false
        pending = null
        activate(session, LexiconApi.API_VERSION)
        return true
    }

    /** Leaves Unreachable or Incompatible for the login screen, forgetting the stored session. */
    fun abandonStoredSession(): Job = scope.launch {
        pending = null
        storage.withLock { tokenStore.clear() }
        _state.value = SessionState.SignedOut()
    }

    /**
     * Health, API version, then login. The password is used for this one
     * request and is neither stored nor logged.
     */
    suspend fun login(
        serverInput: String,
        username: String,
        password: String,
        allowHttpForTesting: Boolean = false,
    ): LoginResult {
        val server = when (val parsed = ServerUrl.parse(serverInput, allowCleartextDevelopmentHosts, allowHttpForTesting)) {
            is ServerUrl.Parsed.Valid -> parsed.url
            is ServerUrl.Parsed.Invalid -> return LoginResult.Failure(parsed.message)
        }
        if (username.isBlank() || password.isEmpty()) {
            return LoginResult.Failure("Enter the user name and password.")
        }
        return try {
            val health = api().health(server)
            val compatibility = ApiCompatibility.check(health, server)
            if (compatibility is ApiCompatibility.Result.Incompatible) {
                return LoginResult.Failure(compatibility.message)
            }
            val response = api().login(server, username, password)
            if (response.apiVersion != LexiconApi.API_VERSION) {
                return LoginResult.Failure(
                    "This Lexicon app speaks API version ${LexiconApi.API_VERSION}, but the server " +
                        "signed you in with version ${response.apiVersion}.",
                )
            }
            val name = response.username.ifEmpty { username }
            val session = Session(server, name, response.token)
            storage.withLock {
                try {
                    tokenStore.save(server, name, response.token)
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (_: Exception) {
                    // Without a working key the session lasts until the app
                    // closes; it is never written unencrypted.
                    tokenStore.clear()
                }
                settings.rememberSignIn(
                    server.value,
                    name,
                    SettingsStore.SessionInfo(response.idleTimeoutSeconds, response.absoluteLifetimeSeconds),
                )
                activate(session, health.apiVersion)
            }
            LoginResult.Success
        } catch (failure: ApiException.Unauthorized) {
            LoginResult.Failure(failure.message ?: "Invalid user name or password.")
        } catch (failure: ApiException) {
            LoginResult.Failure(failure.message ?: "Sign-in failed.")
        }
    }

    private fun activate(session: Session, serverApiVersion: Int) = synchronized(lock) {
        val retained = (_state.value as? SessionState.SignedOut)?.retained
        val identity = if (retained != null && retained.server == session.server && retained.username == session.username) {
            retained
        } else {
            Identity(session.server, session.username, ++epoch)
        }
        active = session
        _state.value = SessionState.SignedIn(identity, serverApiVersion)
    }

    override fun onUnauthorized(session: Session) {
        synchronized(lock) {
            if (active?.token != session.token) return
            active = null
            _state.value = SessionState.SignedOut(
                "Your session has expired or was ended on the server. Sign in again.",
                retained = (_state.value as? SessionState.SignedIn)?.identity,
            )
        }
        scope.launch {
            // A login that finished in the meantime owns the store now.
            storage.withLock { if (current == null) tokenStore.clear() }
        }
    }

    /**
     * Ends the session here at once and forgets the stored token, then tells
     * the server. The local session is gone even if the server cannot be
     * reached.
     */
    fun logout(message: String = "You are signed out."): Job {
        val session = synchronized(lock) {
            val previous = active
            active = null
            _state.value = SessionState.SignedOut(message)
            previous
        }
        return scope.launch {
            storage.withLock { if (current == null) tokenStore.clear() }
            if (session != null) {
                withTimeoutOrNull(LOGOUT_TIMEOUT_MS) {
                    try {
                        api().logout(session)
                    } catch (_: ApiException) {
                        // The server may be gone; the token dies with its sessions.
                    }
                }
            }
        }
    }

    /** A different server invalidates the session; the next login is against [server]. */
    fun changeServer(server: ServerUrl): Job = scope.launch {
        logout("Sign in to ${server.value}.").join()
        settings.setServerUrl(server.value)
    }

    private companion object {
        const val LOGOUT_TIMEOUT_MS = 10_000L
    }
}
