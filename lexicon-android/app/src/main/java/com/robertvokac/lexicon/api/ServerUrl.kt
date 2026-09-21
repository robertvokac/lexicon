package com.robertvokac.lexicon.api

import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull

/**
 * A validated LexiconServer base URL: http or https, a host, an optional path
 * prefix for a reverse proxy, and nothing else. `/api/v1` is appended per
 * request. [value] never ends with a slash.
 */
class ServerUrl private constructor(val httpUrl: HttpUrl) {
    val value: String = httpUrl.toString().trimEnd('/')
    val host: String get() = httpUrl.host
    val isHttps: Boolean get() = httpUrl.isHttps

    /** This device or the emulator's alias for the development machine. */
    val isDevelopmentHost: Boolean get() = isDevelopmentHost(httpUrl.host)

    override fun equals(other: Any?): Boolean = other is ServerUrl && other.value == value
    override fun hashCode(): Int = value.hashCode()
    override fun toString(): String = value

    sealed interface Parsed {
        data class Valid(val url: ServerUrl) : Parsed
        data class Invalid(val message: String) : Parsed
    }

    companion object {
        private val schemePattern = Regex("^([A-Za-z][A-Za-z0-9+.-]*):")
        private val hostPortPattern = Regex("^[A-Za-z0-9.-]+:[0-9]+(/.*)?$")
        private val developmentHosts = setOf("localhost", "10.0.2.2", "10.0.3.2", "::1")

        fun isDevelopmentHost(host: String): Boolean =
            host.lowercase() in developmentHosts || host.startsWith("127.")

        /**
         * Parses what a person typed. Without a scheme, https:// is assumed.
         * [allowCleartextDevelopmentHosts] is true only in debug builds, whose
         * network security configuration permits exactly those hosts.
         */
        fun parse(input: String, allowCleartextDevelopmentHosts: Boolean): Parsed {
            var text = input.trim()
            if (text.isEmpty()) return Parsed.Invalid("Enter the server URL.")
            if (text.any { it.isWhitespace() || it.isISOControl() }) {
                return Parsed.Invalid("The server URL must not contain spaces.")
            }
            val scheme = schemePattern.find(text)?.groupValues?.get(1)?.lowercase()
            if (scheme != null && !text.contains("://") && !hostPortPattern.matches(text)) {
                return Parsed.Invalid("The server URL must start with https:// (\"$scheme:\" is not a server address).")
            }
            if (!text.contains("://")) text = "https://$text"
            val lowered = text.lowercase()
            if (!lowered.startsWith("https://") && !lowered.startsWith("http://")) {
                return Parsed.Invalid("The server URL must use https:// (or http:// for a development server).")
            }
            val url = text.toHttpUrlOrNull()
                ?: return Parsed.Invalid("The server URL is not a valid address.")
            if (url.username.isNotEmpty() || url.password.isNotEmpty()) {
                return Parsed.Invalid("Do not put a user name or password in the server URL.")
            }
            if (url.query != null || url.fragment != null || text.contains('?') || text.contains('#')) {
                return Parsed.Invalid("The server URL must not contain ? or #.")
            }
            // Keep a reverse-proxy path prefix, drop trailing slashes, and
            // accept a pasted API URL by removing its /api/v1 suffix.
            val segments = url.pathSegments.filter { it.isNotEmpty() }.toMutableList()
            if (segments.size >= 2 && segments[segments.size - 2] == "api" && segments.last() == "v1") {
                segments.removeAt(segments.size - 1)
                segments.removeAt(segments.size - 1)
            }
            val normalized = url.newBuilder().apply {
                encodedPath("/")
                segments.forEach { addPathSegment(it) }
            }.build()
            if (!normalized.isHttps) {
                if (!isDevelopmentHost(normalized.host)) {
                    return Parsed.Invalid(
                        "Refusing to send your password over plain http:// to ${normalized.host}. " +
                            "Use https://, as LexiconServer does for every address but this machine.",
                    )
                }
                if (!allowCleartextDevelopmentHosts) {
                    return Parsed.Invalid(
                        "This build connects over https:// only. Plain http:// to a development " +
                            "server is available in debug builds.",
                    )
                }
            }
            return Parsed.Valid(ServerUrl(normalized))
        }

        /** For values this app stored itself after validating them. */
        fun fromStored(value: String, allowCleartextDevelopmentHosts: Boolean): ServerUrl? =
            (parse(value, allowCleartextDevelopmentHosts) as? Parsed.Valid)?.url
    }
}
