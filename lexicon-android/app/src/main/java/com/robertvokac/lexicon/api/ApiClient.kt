package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.model.ErrorEnvelope
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.SerializationException
import kotlinx.serialization.SerializationStrategy
import kotlinx.serialization.json.Json
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.Call
import okhttp3.Callback
import okhttp3.HttpUrl
import okhttp3.MediaType
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import okio.BufferedSink
import okio.source
import java.io.IOException
import java.io.InputStream
import java.io.InterruptedIOException
import java.io.OutputStream
import java.net.ConnectException
import java.net.NoRouteToHostException
import java.net.SocketTimeoutException
import java.net.UnknownHostException
import java.net.UnknownServiceException
import javax.net.ssl.SSLException
import javax.net.ssl.SSLHandshakeException
import javax.net.ssl.SSLPeerUnverifiedException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/**
 * The only place in the app that speaks HTTP. Every request goes through
 * [execute]: it builds the `/api/v1` URL, adds the Bearer token, encodes and
 * decodes JSON, maps failures to [ApiException] and reports a rejected session
 * exactly once. Cancelling the calling coroutine cancels the HTTP call.
 *
 * The OkHttpClient must not follow redirects (see [configure]): a redirect
 * would carry the Authorization header, or a password, to wherever it points.
 */
class ApiClient(
    private val httpClient: OkHttpClient,
    private val sessions: SessionAccess,
    private val json: Json = LexiconJson,
    private val offlineCache: OfflineReadCache? = null,
) {
    private val _offlineRead = MutableStateFlow(false)
    val offlineRead = _offlineRead.asStateFlow()
    /** How a request authenticates. */
    sealed interface Auth {
        /** The current session's Bearer token; fails with NotSignedIn without one. */
        data object Session : Auth

        /** No Authorization header, against an explicit server (health, login). */
        data class Anonymous(val server: ServerUrl) : Auth

        /** An explicit session, for logging out a session that is being dropped. */
        class Explicit(val session: com.robertvokac.lexicon.api.Session) : Auth
    }

    suspend fun <T> get(path: String, response: DeserializationStrategy<T>, auth: Auth = Auth.Session, query: Map<String, String> = emptyMap()): T =
        read("GET", path, null, response, auth, query)

    suspend fun <B, T> post(path: String, body: B, request: SerializationStrategy<B>, response: DeserializationStrategy<T>, auth: Auth = Auth.Session): T {
        val encoded = json.encodeToString(request, body)
        return read("POST", path, encoded.toRequestBody(JSON), response, auth, emptyMap(), encoded)
    }

    private suspend fun <T> read(
        method: String,
        path: String,
        body: RequestBody?,
        response: DeserializationStrategy<T>,
        auth: Auth,
        query: Map<String, String>,
        encodedBody: String = "",
    ): T {
        val session = if (auth == Auth.Session) sessions.current else null
        val cacheable = session != null && offlineCache != null &&
            ((method == "POST" && path == "items/query") ||
                (method == "GET" && listOf("items", "groups", "types", "fields", "usage", "search", "cards")
                    .any { path == it || path.startsWith("$it/") }))
        if (!cacheable) return execute(method, path, body, auth, query) { decode(response, it) }
        val requestKey = buildString {
            append(method).append(' ').append(path)
            query.toSortedMap().forEach { (key, value) -> append('\n').append(key).append('=').append(value) }
            append('\n').append(encodedBody)
        }
        try {
            val (value, raw) = execute(method, path, body, auth, query) { reply ->
                val text = reply.body.string()
                json.decodeFromString(response, text) to text
            }
            offlineCache.write(session, requestKey, raw)
            _offlineRead.value = false
            return value
        } catch (failure: ApiException) {
            if (failure !is ApiException.CannotConnect && failure !is ApiException.Timeout &&
                failure !is ApiException.Network) throw failure
            val cached = offlineCache.read(session, requestKey) ?: throw failure
            val value = runCatching { json.decodeFromString(response, cached) }.getOrNull() ?: throw failure
            _offlineRead.value = true
            return value
        }
    }

    suspend fun <B, T> put(path: String, body: B, request: SerializationStrategy<B>, response: DeserializationStrategy<T>): T =
        execute("PUT", path, jsonBody(request, body), Auth.Session) { decode(response, it) }

    /** A request without a body, answered with 204 (logout, read log, deletes). */
    suspend fun send(method: String, path: String, auth: Auth = Auth.Session) {
        execute(method, path, if (method == "POST") EMPTY_BODY else null, auth) { }
    }

    suspend fun <B> postNoResponse(path: String, body: B, request: SerializationStrategy<B>) {
        execute("POST", path, jsonBody(request, body), Auth.Session) { }
    }

    /**
     * Streams [open] to the server as `application/octet-stream`, or as JSON
     * when [json] is set. [size] may be -1 when unknown; the body is then sent
     * chunked, which the server accepts for blobs only.
     */
    suspend fun <T> upload(
        path: String,
        size: Long,
        open: () -> InputStream,
        onProgress: (sent: Long, total: Long) -> Unit,
        response: DeserializationStrategy<T>,
        json: Boolean = false,
    ): T = execute("POST", path, StreamingBody(size, open, onProgress, if (json) JSON else OCTET_STREAM), Auth.Session) {
        decode(response, it)
    }

    /** Streams a response into [output]. */
    suspend fun download(
        path: String,
        output: () -> OutputStream,
        onProgress: (received: Long, total: Long) -> Unit,
        query: Map<String, String> = emptyMap(),
        accept: String = "application/octet-stream",
    ) {
        execute("GET", path, null, Auth.Session, query = query, accept = accept) { response ->
            val body = response.body
            val total = body.contentLength()
            output().use { sink ->
                body.byteStream().use { source ->
                    val buffer = ByteArray(BUFFER_SIZE)
                    var received = 0L
                    while (true) {
                        val read = source.read(buffer)
                        if (read < 0) break
                        sink.write(buffer, 0, read)
                        received += read
                        onProgress(received, total)
                    }
                }
            }
        }
    }

    private suspend fun <T> execute(
        method: String,
        path: String,
        body: RequestBody?,
        auth: Auth,
        query: Map<String, String> = emptyMap(),
        accept: String = "application/json",
        handle: (Response) -> T,
    ): T {
        val session: Session?
        val server: ServerUrl
        when (auth) {
            Auth.Session -> {
                session = sessions.current ?: throw ApiException.NotSignedIn()
                server = session.server
            }
            is Auth.Explicit -> {
                session = auth.session
                server = auth.session.server
            }
            is Auth.Anonymous -> {
                session = null
                server = auth.server
            }
        }
        val request = Request.Builder()
            .url(apiUrl(server, path, query))
            .method(method, body)
            .header("Accept", accept)
            .apply { if (session != null) header("Authorization", "Bearer ${session.token}") }
            .build()
        val call = httpClient.newCall(request)
        try {
            return call.await { response ->
                when {
                    response.isSuccessful -> handle(response)
                    else -> throw errorFor(response, server)
                }
            }
        } catch (unauthorized: ApiException.Unauthorized) {
            // Only a session the server rejected is dropped. A failed login is
            // an Anonymous request and leaves everything as it was. Never retry:
            // that is how request loops start.
            if (session != null && auth == Auth.Session) sessions.onUnauthorized(session)
            throw unauthorized
        } catch (failure: ApiException) {
            throw failure
        } catch (failure: SerializationException) {
            throw incompatible(failure)
        } catch (failure: IOException) {
            throw transportFailure(failure, server)
        }
    }

    private fun <T> jsonBody(serializer: SerializationStrategy<T>, value: T): RequestBody =
        json.encodeToString(serializer, value).toRequestBody(JSON)

    private fun <T> decode(deserializer: DeserializationStrategy<T>, response: Response): T {
        val text = response.body.string()
        return json.decodeFromString(deserializer, text)
    }

    private fun incompatible(cause: Exception): ApiException.Incompatible =
        ApiException.Incompatible(
            "The server sent a response this version of Lexicon does not understand. " +
                "Check that the app and LexiconServer are up to date.",
            cause,
        )

    private fun errorFor(response: Response, server: ServerUrl): ApiException {
        val status = response.code
        if (status in 300..399) return ApiException.Redirected(response.header("Location"))
        val envelope = runCatching {
            json.decodeFromString(ErrorEnvelope.serializer(), response.peekBody(MAX_ERROR_BYTES).string())
        }.getOrNull()
        val serverMessage = envelope?.error?.message?.takeIf { it.isNotBlank() }
        return when (status) {
            400 -> ApiException.Validation(serverMessage ?: "The server refused the request as invalid.")
            401 -> ApiException.Unauthorized(serverMessage ?: "Authentication is required.")
            403 -> ApiException.Forbidden(serverMessage ?: "The server does not allow this request.")
            404 -> ApiException.NotFound(serverMessage ?: "The server has no such record.")
            409 -> ApiException.Conflict(serverMessage ?: "The record was changed elsewhere.")
            413 -> ApiException.PayloadTooLarge(
                (serverMessage ?: "The request is too large.") +
                    " The server's size limit is set with --max-blob-bytes and --max-json-bytes.",
            )
            429 -> {
                val retryAfter = response.header("Retry-After")?.trim()?.toLongOrNull()
                ApiException.RateLimited(
                    (serverMessage ?: "Too many requests.") +
                        (retryAfter?.let { " Try again in $it seconds." } ?: ""),
                    retryAfter,
                )
            }
            in 500..599 -> ApiException.Server(
                status,
                serverMessage
                    ?: "The server at ${server.host}, or a proxy in front of it, failed (HTTP $status).",
            )
            else -> ApiException.UnexpectedStatus(
                status,
                serverMessage ?: "The server answered with HTTP $status.",
            )
        }
    }

    /**
     * OkHttp tries every address of a host and reports the last failure, with
     * the earlier ones suppressed. A certificate problem on one address is the
     * real story even when the next address merely refused the connection.
     */
    private fun transportFailure(failure: IOException, server: ServerUrl): ApiException {
        val tls = related(failure).filterIsInstance<SSLException>().firstOrNull()
        return describe(tls ?: failure, server)
    }

    /** The failure, its causes and everything suppressed along the way. */
    private fun related(failure: Throwable): Sequence<Throwable> = sequence {
        val seen = java.util.Collections.newSetFromMap(java.util.IdentityHashMap<Throwable, Boolean>())
        val pending = ArrayDeque(listOf(failure))
        while (pending.isNotEmpty()) {
            val next = pending.removeFirst()
            if (!seen.add(next)) continue
            yield(next)
            next.cause?.let(pending::add)
            pending.addAll(next.suppressed)
        }
    }

    private fun describe(failure: IOException, server: ServerUrl): ApiException = when (failure) {
        is UnknownHostException -> ApiException.CannotConnect(
            "Cannot find the host ${server.host}. Check the server URL and the network connection.",
            failure,
        )
        is ConnectException, is NoRouteToHostException -> ApiException.CannotConnect(
            "Cannot connect to ${server.value}. Check that LexiconServer is running and reachable from this device.",
            failure,
        )
        is SocketTimeoutException -> ApiException.Timeout(failure)
        is SSLPeerUnverifiedException -> ApiException.Tls(
            "The certificate of ${server.host} does not match the host name, so the connection was refused.",
            failure,
        )
        is SSLHandshakeException -> ApiException.Tls(
            "A secure connection to ${server.host} could not be established. " +
                "The server's certificate is not trusted by this device, or TLS is misconfigured.",
            failure,
        )
        is SSLException -> ApiException.Tls(
            "The secure connection to ${server.host} failed. If the server speaks plain HTTP, " +
                "it is not the https:// server the URL names.",
            failure,
        )
        is UnknownServiceException ->
            if (failure.message.orEmpty().contains("CLEARTEXT", ignoreCase = true)) {
                ApiException.CleartextNotPermitted(server.host)
            } else {
                ApiException.Network("The connection to the server failed.", failure)
            }
        is InterruptedIOException ->
            if (failure.message == "timeout") ApiException.Timeout(failure)
            else ApiException.Network("The request was interrupted.", failure)
        else -> ApiException.Network(
            "The connection to ${server.host} failed" +
                (failure.message?.let { ": $it" } ?: "."),
            failure,
        )
    }

    private class StreamingBody(
        private val size: Long,
        private val open: () -> InputStream,
        private val onProgress: (Long, Long) -> Unit,
        private val type: MediaType,
    ) : RequestBody() {
        override fun contentType() = type
        override fun contentLength() = size

        // A document stream can be read once, so OkHttp must not replay it.
        override fun isOneShot() = true

        override fun writeTo(sink: BufferedSink) {
            open().source().use { source ->
                var sent = 0L
                while (true) {
                    val read = source.read(sink.buffer, BUFFER_SIZE.toLong())
                    if (read < 0) break
                    sent += read
                    sink.emitCompleteSegments()
                    onProgress(sent, size)
                }
            }
        }
    }

    companion object {
        private val JSON = "application/json; charset=utf-8".toMediaType()
        private val OCTET_STREAM = "application/octet-stream".toMediaType()
        private val EMPTY_BODY = ByteArray(0).toRequestBody(null)
        private const val BUFFER_SIZE = 64 * 1024
        private const val MAX_ERROR_BYTES = 64L * 1024

        /** `<server>/api/v1/<path>` with each query parameter encoded. */
        fun apiUrl(server: ServerUrl, path: String, query: Map<String, String> = emptyMap()): HttpUrl =
            server.httpUrl.newBuilder()
                .addPathSegments("api/v1")
                .addPathSegments(path.trimStart('/'))
                .apply { query.forEach { (key, value) -> addQueryParameter(key, value) } }
                .build()

        /** Settings every Lexicon HTTP client needs, whatever else it adds. */
        fun configure(builder: OkHttpClient.Builder): OkHttpClient.Builder = builder
            .followRedirects(false)
            .followSslRedirects(false)
    }
}

/**
 * Enqueues the call and hands the response to [handle] on OkHttp's thread,
 * where blocking reads belong. Cancelling the coroutine cancels the call,
 * which also aborts a body that is still streaming.
 */
private suspend fun <T> Call.await(handle: (Response) -> T): T =
    suspendCancellableCoroutine { continuation ->
        continuation.invokeOnCancellation { cancel() }
        enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                continuation.resumeWithException(e)
            }

            override fun onResponse(call: Call, response: Response) {
                val result = runCatching { response.use(handle) }
                result.fold(continuation::resume, continuation::resumeWithException)
            }
        })
    }
