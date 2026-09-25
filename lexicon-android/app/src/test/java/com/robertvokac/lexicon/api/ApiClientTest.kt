package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemStatus
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.tls.HeldCertificate
import okhttp3.tls.HandshakeCertificates
import okio.Buffer
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.concurrent.TimeUnit

class ApiClientTest {
    private lateinit var server: MockWebServer
    private lateinit var sessions: RecordingSessions
    private lateinit var api: LexiconApi

    /** A SessionAccess that remembers every rejected session. */
    private class RecordingSessions : SessionAccess {
        override var current: Session? = null
        val rejected = mutableListOf<Session>()
        override fun onUnauthorized(session: Session) {
            rejected += session
            if (current?.token == session.token) current = null
        }
    }

    private fun serverUrl(): ServerUrl =
        (ServerUrl.parse(server.url("/").toString(), allowCleartextDevelopmentHosts = true) as ServerUrl.Parsed.Valid).url

    private fun json(body: String, code: Int = 200) = MockResponse.Builder()
        .code(code)
        .setHeader("Content-Type", "application/json; charset=utf-8")
        .body(body)
        .build()

    private fun errorResponse(code: Int, errorCode: String, message: String) =
        json("""{ "error": { "code": "$errorCode", "message": "$message" } }""", code)

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        sessions = RecordingSessions()
        sessions.current = Session(serverUrl(), "robert", "the-token")
        api = LexiconApi(ApiClient(AppContainer.httpClient(), sessions))
    }

    @After
    fun tearDown() {
        server.close()
    }

    @Test
    fun sendsTheBearerTokenToTheVersionedPath() = runBlocking {
        server.enqueue(json("""{ "groups": [ { "id": 1, "name": "Default", "description": "", "position": 0 } ] }"""))
        val groups = api.groups()
        assertEquals("Default", groups.single().name)
        val request = server.takeRequest()
        assertEquals("/api/v1/groups", request.url.encodedPath)
        assertEquals("Bearer the-token", request.headers["Authorization"])
        assertEquals("application/json", request.headers["Accept"])
        // Android is not a browser and never pretends to be one.
        assertNull(request.headers["Origin"])
    }

    @Test
    fun healthAndLoginSendNoToken() = runBlocking {
        server.enqueue(json("""{ "status": "ok", "apiVersion": 1, "application": "Lexicon" }"""))
        server.enqueue(json("""{ "token": "t", "username": "robert", "apiVersion": 1 }"""))
        assertEquals(1, api.health(serverUrl()).apiVersion)
        assertEquals("t", api.login(serverUrl(), "robert", "pw-pw-pw-pw-pw").token)
        assertNull(server.takeRequest().headers["Authorization"])
        val login = server.takeRequest()
        assertNull(login.headers["Authorization"])
        assertTrue(login.headers["Content-Type"]!!.startsWith("application/json"))
        assertEquals("""{"username":"robert","password":"pw-pw-pw-pw-pw","rememberDevice":false}""", login.body!!.utf8())
    }

    @Test
    fun theQueryIsPostedAsJson() = runBlocking {
        server.enqueue(json("""{ "items": [ { "id": 5, "title": "RAII", "status": "Completed" } ], "totalCount": 41 }"""))
        val page = api.queryItems(ItemQuery(searchText = "raii", statusFilter = ItemStatus.Completed, limit = 20, offset = 40))
        assertEquals(41, page.totalCount)
        assertEquals(ItemStatus.Completed, page.items.single().status)
        val body = server.takeRequest().body!!.utf8()
        assertTrue(body.contains("\"searchText\":\"raii\""))
        assertTrue(body.contains("\"statusFilter\":\"Completed\""))
        assertTrue(body.contains("\"offset\":40"))
    }

    @Test
    fun errorEnvelopesBecomeTypedFailures() = runBlocking {
        suspend fun failureFor(response: MockResponse): ApiException {
            server.enqueue(response)
            try {
                api.groups()
            } catch (failure: ApiException) {
                return failure
            }
            throw AssertionError("expected a failure")
        }
        val validation = failureFor(errorResponse(400, "validation", "Group name cannot be empty."))
        assertTrue(validation is ApiException.Validation)
        assertEquals("Group name cannot be empty.", validation.message)

        val notFound = failureFor(errorResponse(404, "not_found", "Item not found."))
        assertTrue(notFound is ApiException.NotFound)

        val conflict = failureFor(errorResponse(409, "conflict", "This item was changed elsewhere after you opened it."))
        assertTrue(conflict is ApiException.Conflict)
        assertEquals("This item was changed elsewhere after you opened it.", conflict.message)

        val tooLarge = failureFor(errorResponse(413, "payload_too_large", "The upload exceeds the configured blob size limit."))
        assertTrue(tooLarge is ApiException.PayloadTooLarge)
        assertTrue(tooLarge.message!!.contains("--max-blob-bytes"))

        val limited = failureFor(
            errorResponse(429, "too_many_requests", "Too many failed sign-in attempts. Try again later.")
                .newBuilder().setHeader("Retry-After", "120").build(),
        )
        assertTrue(limited is ApiException.RateLimited)
        assertEquals(120L, (limited as ApiException.RateLimited).retryAfterSeconds)
        assertTrue(limited.message!!.contains("120 seconds"))

        val storage = failureFor(errorResponse(500, "storage", "The server could not complete the operation."))
        assertTrue(storage is ApiException.Server)
        assertEquals("The server could not complete the operation.", storage.message)

        // A proxy's HTML error page is not an envelope; the status still explains it.
        val proxy = failureFor(MockResponse.Builder().code(502).body("<html>Bad gateway</html>").build())
        assertTrue(proxy is ApiException.Server)
        assertTrue(proxy.message!!.contains("502"))
    }

    @Test
    fun a401DropsTheSessionOnceAndIsNeverRetried() = runBlocking {
        server.enqueue(errorResponse(401, "unauthorized", "Authentication is required."))
        try {
            api.groups()
            fail("expected Unauthorized")
        } catch (_: ApiException.Unauthorized) {
        }
        assertEquals(1, server.requestCount)
        assertEquals(1, sessions.rejected.size)
        assertNull(sessions.current)
        // Without a session nothing is sent at all.
        try {
            api.groups()
            fail("expected NotSignedIn")
        } catch (_: ApiException.NotSignedIn) {
        }
        assertEquals(1, server.requestCount)
    }

    @Test
    fun aFailedLoginDoesNotTouchTheSession() = runBlocking {
        server.enqueue(errorResponse(401, "unauthorized", "Invalid user name or password."))
        try {
            api.login(serverUrl(), "robert", "wrong-password")
            fail("expected Unauthorized")
        } catch (failure: ApiException.Unauthorized) {
            assertEquals("Invalid user name or password.", failure.message)
        }
        assertTrue(sessions.rejected.isEmpty())
        assertEquals("the-token", sessions.current?.token)
    }

    @Test
    fun redirectsAreRefusedAndCredentialsNeverLeaveForAnotherHost() = runBlocking {
        MockWebServer().use { elsewhere ->
            elsewhere.start()
            server.enqueue(MockResponse.Builder().code(302).setHeader("Location", elsewhere.url("/api/v1/groups").toString()).build())
            try {
                api.groups()
                fail("expected Redirected")
            } catch (failure: ApiException.Redirected) {
                assertTrue(failure.message!!.contains(elsewhere.url("/").host))
            }
            assertEquals(0, elsewhere.requestCount)
            // A login redirected elsewhere must not carry the password there either.
            server.enqueue(MockResponse.Builder().code(307).setHeader("Location", elsewhere.url("/api/v1/auth/login").toString()).build())
            try {
                api.login(serverUrl(), "robert", "secret-password")
                fail("expected Redirected")
            } catch (_: ApiException.Redirected) {
            }
            assertEquals(0, elsewhere.requestCount)
        }
    }

    @Test
    fun anUnknownEnumIsAnIncompatibility() = runBlocking {
        server.enqueue(json("""{ "items": [ { "id": 5, "title": "x", "status": "Archived" } ], "totalCount": 1 }"""))
        try {
            api.queryItems(ItemQuery())
            fail("expected Incompatible")
        } catch (failure: ApiException.Incompatible) {
            assertTrue(failure.message!!.contains("does not understand"))
        }
    }

    @Test
    fun malformedJsonIsAnIncompatibility() = runBlocking {
        server.enqueue(json("<html>not json</html>"))
        try {
            api.groups()
            fail("expected Incompatible")
        } catch (_: ApiException.Incompatible) {
        }
    }

    @Test
    fun aRefusedConnectionSaysSo() = runBlocking {
        val url = serverUrl()
        server.close()
        try {
            api.health(url)
            fail("expected CannotConnect")
        } catch (failure: ApiException.CannotConnect) {
            assertTrue(failure.message!!.contains("Cannot connect"))
        }
    }

    @Test
    fun anUntrustedCertificateIsATlsFailure() = runBlocking {
        val certificate = HeldCertificate.Builder().addSubjectAlternativeName("localhost").build()
        val serverCertificates = HandshakeCertificates.Builder().heldCertificate(certificate).build()
        MockWebServer().use { tls ->
            tls.useHttps(serverCertificates.sslSocketFactory())
            tls.start()
            tls.enqueue(json("""{ "status": "ok", "apiVersion": 1 }"""))
            val url = (ServerUrl.parse(tls.url("/").toString(), true) as ServerUrl.Parsed.Valid).url
            try {
                // The client trusts the system store only: a self-signed
                // certificate is refused, never silently accepted.
                api.health(url)
                fail("expected Tls")
            } catch (failure: ApiException.Tls) {
                assertTrue(failure.message!!.contains("secure connection"))
            }
        }
    }

    @Test
    fun cancellingTheCoroutineCancelsTheCall() = runBlocking {
        server.enqueue(json("""{ "groups": [] }""").newBuilder().headersDelay(10, TimeUnit.SECONDS).build())
        val started = System.nanoTime()
        val call = async { runCatching { api.groups() } }
        delay(200)
        call.cancel()
        withTimeout(2_000) { call.join() }
        assertTrue(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - started) < 5_000)
        assertTrue(call.isCancelled)
    }

    @Test
    fun blobsStreamBothWays() = runBlocking {
        val bytes = ByteArray(300_000) { (it % 251).toByte() }
        server.enqueue(json("""{ "hash": "${"a".repeat(64)}" }""", 201))
        val progress = mutableListOf<Long>()
        val hash = api.uploadBlob(bytes.size.toLong(), { ByteArrayInputStream(bytes) }) { sent, _ -> progress += sent }
        assertEquals("a".repeat(64), hash)
        val upload = server.takeRequest()
        assertEquals("application/octet-stream", upload.headers["Content-Type"])
        assertEquals(bytes.size.toString(), upload.headers["Content-Length"])
        assertArrayEquals(bytes, upload.body!!.toByteArray())
        assertEquals(bytes.size.toLong(), progress.last())

        server.enqueue(MockResponse.Builder().code(200).setHeader("Content-Type", "application/octet-stream").body(Buffer().write(bytes)).build())
        val output = ByteArrayOutputStream()
        api.downloadBlob("b".repeat(64), { output }) { _, _ -> }
        assertArrayEquals(bytes, output.toByteArray())
        assertEquals("/api/v1/blobs/" + "b".repeat(64), server.takeRequest().url.encodedPath)
    }

    @Test
    fun aBlobHashIsValidatedBeforeItReachesAPath() = runBlocking {
        try {
            api.downloadBlob("../../lexicon.db", { ByteArrayOutputStream() }) { _, _ -> }
            fail("expected a refusal")
        } catch (_: IllegalArgumentException) {
        }
        assertEquals(0, server.requestCount)
        assertFalse(LexiconApi.isBlobHash("A".repeat(64)))
        assertTrue(LexiconApi.isBlobHash("0123456789abcdef".repeat(4)))
    }

    @Test
    fun emptyPostsCarryNoBody() = runBlocking {
        server.enqueue(MockResponse.Builder().code(204).build())
        api.logItemRead(7)
        val request = server.takeRequest()
        assertEquals("POST", request.method)
        assertEquals("/api/v1/items/7/read", request.url.encodedPath)
        assertEquals(0L, request.bodySize)
    }

    @Test
    fun accountAndHistoryRoutesUseTheAuthenticatedApi() = runBlocking {
        server.enqueue(json("""{"sessions":[{"id":"abc","createdAtSeconds":1,"lastSeenSeconds":2,"current":true}]}"""))
        assertEquals("abc", api.sessions().single().id)
        assertEquals("/api/v1/auth/sessions", server.takeRequest().url.encodedPath)

        server.enqueue(MockResponse.Builder().code(204).build())
        api.changePassword("old password", "new strong password")
        val change = server.takeRequest()
        assertEquals("/api/v1/auth/change-password", change.url.encodedPath)
        assertTrue(change.body!!.utf8().contains("new strong password"))
        assertEquals("Bearer the-token", change.headers["Authorization"])

        server.enqueue(json("""{"entries":[]}"""))
        assertTrue(api.trash().isEmpty())
        assertEquals("/api/v1/items/trash", server.takeRequest().url.encodedPath)
        server.enqueue(json("""{"itemId":19}"""))
        assertEquals(19, api.restoreItemHistory(7))
        val restore = server.takeRequest()
        assertEquals("POST", restore.method)
        assertEquals("/api/v1/items/history/7/restore", restore.url.encodedPath)
    }
}
