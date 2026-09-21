package com.robertvokac.lexicon.api

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ServerUrlTest {
    private fun valid(input: String, debug: Boolean = false): String =
        when (val parsed = ServerUrl.parse(input, debug)) {
            is ServerUrl.Parsed.Valid -> parsed.url.value
            is ServerUrl.Parsed.Invalid -> throw AssertionError("'$input' was refused: ${parsed.message}")
        }

    private fun invalid(input: String, debug: Boolean = false): String =
        when (val parsed = ServerUrl.parse(input, debug)) {
            is ServerUrl.Parsed.Valid -> throw AssertionError("'$input' was accepted as ${parsed.url}")
            is ServerUrl.Parsed.Invalid -> parsed.message
        }

    @Test
    fun normalizesWhatPeopleType() {
        assertEquals("https://api.example.com", valid("https://api.example.com"))
        assertEquals("https://api.example.com", valid("  https://api.example.com/  "))
        assertEquals("https://api.example.com", valid("https://api.example.com///"))
        assertEquals("https://api.example.com", valid("api.example.com"))
        assertEquals("https://api.example.com:8443", valid("api.example.com:8443"))
        assertEquals("https://example.com/lexicon", valid("https://example.com/lexicon/"))
        assertEquals("https://api.example.com", valid("HTTPS://API.EXAMPLE.COM"))
    }

    @Test
    fun aPastedApiUrlBecomesTheBaseUrl() {
        assertEquals("https://api.example.com", valid("https://api.example.com/api/v1"))
        assertEquals("https://example.com/lexicon", valid("https://example.com/lexicon/api/v1/"))
    }

    @Test
    fun refusesSchemesThatAreNotServers() {
        assertTrue(invalid("javascript:alert(1)").contains("https://"))
        invalid("file:///sdcard/lexicon.db")
        invalid("content://com.example/items")
        invalid("ftp://example.com")
        invalid("data:text/html,hi")
        invalid("intent://x#Intent;end")
    }

    @Test
    fun refusesMalformedAndAmbiguousUrls() {
        invalid("")
        invalid("   ")
        invalid("https://")
        invalid("https://exa mple.com")
        invalid("https://user:secret@example.com")
        invalid("https://example.com/?token=1")
        invalid("https://example.com/#top")
    }

    @Test
    fun plainHttpOnlyReachesDevelopmentHostsInDebugBuilds() {
        assertEquals("http://10.0.2.2:8628", valid("http://10.0.2.2:8628", debug = true))
        assertEquals("http://127.0.0.1:8628", valid("http://127.0.0.1:8628", debug = true))
        assertEquals("http://localhost:8628", valid("http://localhost:8628/", debug = true))
        assertTrue(invalid("http://10.0.2.2:8628", debug = false).contains("https://"))
        assertTrue(invalid("http://lexicon.example.com", debug = true).contains("Refusing"))
        assertTrue(invalid("http://192.168.1.20:8628", debug = true).contains("Refusing"))
    }

    @Test
    fun equalityFollowsTheNormalizedValue() {
        val a = (ServerUrl.parse("https://a.example/", false) as ServerUrl.Parsed.Valid).url
        val b = (ServerUrl.parse("a.example", false) as ServerUrl.Parsed.Valid).url
        assertEquals(a, b)
        assertEquals("https://a.example/api/v1/items/7", ApiClient.apiUrl(a, "items/7").toString())
        assertEquals(
            "https://a.example/api/v1/items/resolve?title=a%26b%20c",
            ApiClient.apiUrl(a, "items/resolve", mapOf("title" to "a&b c")).toString(),
        )
    }
}
