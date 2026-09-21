package com.robertvokac.lexicon.auth

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiClient
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.api.ServerUrl
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

class SessionManagerTest {
    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var sessions: SessionManager
    private lateinit var api: LexiconApi

    private fun newManager(): SessionManager {
        lateinit var manager: SessionManager
        val client = ApiClient(AppContainer.httpClient(), object : com.robertvokac.lexicon.api.SessionAccess {
            override val current get() = manager.current
            override fun onUnauthorized(session: com.robertvokac.lexicon.api.Session) = manager.onUnauthorized(session)
        })
        api = LexiconApi(client)
        manager = SessionManager({ api }, environment.tokenStore, environment.settings, environment.scope, allowCleartextDevelopmentHosts = true)
        return manager
    }

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        environment = TestEnvironment()
        sessions = newManager()
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    private suspend fun awaitState(predicate: (SessionState) -> Boolean): SessionState =
        withTimeout(10_000) { sessions.state.first(predicate) }

    @Test
    fun loginChecksHealthThenStoresTheTokenEncrypted() = runBlocking {
        val result = sessions.login(fake.baseUrl + "/", fake.username, fake.password)
        assertEquals(SessionManager.LoginResult.Success, result)
        val state = sessions.state.value as SessionState.SignedIn
        assertEquals("robert", state.identity.username)
        assertEquals(1, state.serverApiVersion)
        // Health first, then login: the version is checked before credentials travel.
        assertEquals(listOf("/api/v1/health", "/api/v1/auth/login"), fake.requests.map { it.url.encodedPath })
        val stored = environment.tokenStore.load()!!
        assertEquals("token-1", stored.token)
        assertEquals(fake.baseUrl, environment.settings.lastServerUrl())
        assertEquals("robert", environment.settings.lastUsername())
        assertEquals(28800L, environment.settings.sessionInfo.first().idleTimeoutSeconds)
    }

    @Test
    fun wrongCredentialsAreReportedAndNothingIsStored() = runBlocking {
        val result = sessions.login(fake.baseUrl, fake.username, "wrong password")
        assertEquals(SessionManager.LoginResult.Failure("Invalid user name or password."), result)
        assertNull(environment.tokenStore.load())
        assertTrue(sessions.state.value !is SessionState.SignedIn)
    }

    @Test
    fun anIncompatibleServerIsRefusedBeforeTheLogin() = runBlocking {
        fake.apiVersion = 2
        val result = sessions.login(fake.baseUrl, fake.username, fake.password)
        assertTrue(result is SessionManager.LoginResult.Failure)
        assertTrue((result as SessionManager.LoginResult.Failure).message.contains("version 2"))
        assertEquals(listOf("/api/v1/health"), fake.requests.map { it.url.encodedPath })
    }

    @Test
    fun aBadUrlNeverReachesTheNetwork() = runBlocking {
        val result = sessions.login("javascript:alert(1)", "robert", "pw")
        assertTrue(result is SessionManager.LoginResult.Failure)
        assertTrue(fake.requests.isEmpty())
    }

    @Test
    fun restoreVerifiesTheStoredSession() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        val restarted = newManager().also { sessions = it }
        restarted.start()
        val state = awaitState { it is SessionState.SignedIn } as SessionState.SignedIn
        assertEquals("robert", state.identity.username)
        assertTrue(fake.requests.any { it.url.encodedPath == "/api/v1/auth/me" })
    }

    @Test
    fun aRestartedServerSendsTheUserBackToLogin() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        fake.tokens.clear()
        val restarted = newManager().also { sessions = it }
        restarted.start()
        val state = awaitState { it is SessionState.SignedOut } as SessionState.SignedOut
        assertTrue(state.message!!.contains("expired"))
        assertNull(environment.tokenStore.load())
    }

    @Test
    fun anExpiredSessionClearsTheStoreAndKeepsTheScreensAlive() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        val identity = (sessions.state.value as SessionState.SignedIn).identity
        fake.tokens.clear()
        try {
            api.groups()
            fail("expected Unauthorized")
        } catch (_: ApiException.Unauthorized) {
        }
        val state = sessions.state.value as SessionState.SignedOut
        assertEquals(identity, state.retained)
        withTimeout(5_000) {
            while (environment.tokenStore.load() != null) kotlinx.coroutines.delay(20)
        }
        // Signing in again as the same person keeps the same screens.
        sessions.login(fake.baseUrl, fake.username, fake.password)
        assertEquals(identity, (sessions.state.value as SessionState.SignedIn).identity)
        // Only one 401 reached the server; nothing retried in a loop.
        assertEquals(1, fake.requests.count { it.url.encodedPath == "/api/v1/groups" })
    }

    @Test
    fun logoutForgetsTheTokenEvenWhenTheServerIsGone() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        fake.close()
        sessions.logout().join()
        assertTrue(sessions.state.value is SessionState.SignedOut)
        assertNull((sessions.state.value as SessionState.SignedOut).retained)
        assertNull(environment.tokenStore.load())
        assertNull(sessions.current)
    }

    @Test
    fun logoutTellsTheServer() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        sessions.logout().join()
        val logout = fake.requestsTo("POST", "/api/v1/auth/logout").single()
        assertEquals("Bearer token-1", logout.headers["Authorization"])
        assertTrue(fake.tokens.isEmpty())
    }

    @Test
    fun anotherUserStartsFresh() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        val first = (sessions.state.value as SessionState.SignedIn).identity
        sessions.logout().join()
        sessions.login(fake.baseUrl, fake.username, fake.password)
        val second = (sessions.state.value as SessionState.SignedIn).identity
        // An explicit logout ends the screens too; the next sign-in is a new tree.
        assertTrue(first.key != second.key)
    }

    @Test
    fun changingTheServerEndsTheSession() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        val other = (ServerUrl.parse("https://other.example.com", false) as ServerUrl.Parsed.Valid).url
        sessions.changeServer(other).join()
        assertTrue(sessions.state.value is SessionState.SignedOut)
        assertNull(environment.tokenStore.load())
        assertEquals("https://other.example.com", environment.settings.lastServerUrl())
    }

    @Test
    fun anUnreachableServerKeepsTheStoredSessionForRetry() = runBlocking {
        sessions.login(fake.baseUrl, fake.username, fake.password)
        fake.close()
        val restarted = newManager().also { sessions = it }
        restarted.start()
        val state = awaitState { it is SessionState.Unreachable } as SessionState.Unreachable
        assertTrue(state.message.contains("Cannot connect"))
        assertNotNull(environment.tokenStore.load())
        restarted.abandonStoredSession().join()
        assertNull(environment.tokenStore.load())
    }
}
