package com.robertvokac.lexicon.api

/**
 * Every failure of a REST call, with a message meant for the person using the
 * app. Server messages are shown as sent: the server never puts SQL, paths or
 * secrets in them.
 */
sealed class ApiException(message: String, cause: Throwable? = null) : Exception(message, cause) {
    /** 400: invalid input, as explained by the server. */
    class Validation(message: String) : ApiException(message)

    /** 401: the session is missing, unknown or expired, or login failed. */
    class Unauthorized(message: String) : ApiException(message)

    /** 403: typically an Origin the server does not allow. */
    class Forbidden(message: String) : ApiException(message)

    /** 404: no such record. */
    class NotFound(message: String) : ApiException(message)

    /** 413: the body is over the server's --max-json-bytes or --max-blob-bytes. */
    class PayloadTooLarge(message: String) : ApiException(message)

    /** 429: too many failed logins. */
    class RateLimited(message: String, val retryAfterSeconds: Long?) : ApiException(message)

    /** 5xx: the server, or a proxy in front of it, could not complete the request. */
    class Server(val status: Int, message: String) : ApiException(message)

    /** Any other HTTP status. */
    class UnexpectedStatus(val status: Int, message: String) : ApiException(message)

    /** A 3xx answer. Redirects are never followed with credentials attached. */
    class Redirected(val location: String?) : ApiException(
        if (location.isNullOrBlank()) {
            "The server answered with a redirect. Check the server URL."
        } else {
            "The server redirected the request to $location. If that is the right server, use that URL instead."
        },
    )

    /** DNS failure, refused connection, no route. */
    class CannotConnect(message: String, cause: Throwable?) : ApiException(message, cause)

    class Timeout(cause: Throwable?) : ApiException("The server did not answer in time.", cause)

    /** The TLS handshake failed: untrusted certificate, wrong host name, old protocol. */
    class Tls(message: String, cause: Throwable?) : ApiException(message, cause)

    /** The network security configuration refused plain HTTP to this host. */
    class CleartextNotPermitted(host: String) : ApiException(
        "This build does not send plain http:// traffic to $host. Use https://.",
    )

    /** A response this client cannot understand: another API version, an unknown enum name. */
    class Incompatible(message: String, cause: Throwable? = null) : ApiException(message, cause)

    /** No session: the request was never sent. */
    class NotSignedIn : ApiException("You are not signed in.")

    /** Any other I/O failure. */
    class Network(message: String, cause: Throwable?) : ApiException(message, cause)
}
