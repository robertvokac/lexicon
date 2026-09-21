package com.robertvokac.lexicon.share

import android.content.Intent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ShareParsingTest {
    private fun share(text: String?, subject: String? = null, type: String = "text/plain") =
        LaunchIntents.parseShare(Intent.ACTION_SEND, type, text, subject)

    @Test
    fun aBrowserShareBecomesTitleAndLink() {
        assertEquals(
            SharePrefill("Pointer provenance - C++ reference", "<https://example.com/provenance>"),
            share("https://example.com/provenance", "Pointer provenance - C++ reference"),
        )
    }

    @Test
    fun aBareUrlLeavesTheTitleToThePerson() {
        assertEquals(SharePrefill("", "<https://example.com/a?b=c>"), share("  https://example.com/a?b=c  "))
    }

    @Test
    fun aWordBecomesTheTitle() {
        assertEquals(SharePrefill("RAII", ""), share("RAII"))
    }

    @Test
    fun aPassageKeepsItsFirstLineAsTitle() {
        assertEquals(
            SharePrefill("Object lifetime", "Object lifetime\nbegins when storage is obtained."),
            share("Object lifetime\r\nbegins when storage is obtained.\u0000"),
        )
    }

    @Test
    fun aSubjectWithTextKeepsBoth() {
        assertEquals(SharePrefill("Note", "Check https://example.com today"), share("Check https://example.com today", "Note"))
    }

    @Test
    fun onlyPlainTextSendsAreAccepted() {
        assertNull(share("x", type = "image/png"))
        assertNull(share("x", type = "text/html"))
        assertNull(LaunchIntents.parseShare(Intent.ACTION_VIEW, "text/plain", "x", null))
        assertNull(share(null))
        assertNull(share("   "))
        assertEquals(SharePrefill("x", ""), share("x", type = "text/plain; charset=utf-8"))
    }

    @Test
    fun sharedTextIsBounded() {
        val prefill = share("a".repeat(100_000), "t".repeat(1_000))!!
        assertEquals(LaunchIntents.MAX_CONTENT, prefill.content.length)
        assertEquals(LaunchIntents.MAX_TITLE, prefill.title.length)
    }

    @Test
    fun javascriptIsNotAUrl() {
        assertEquals(SharePrefill("javascript:alert(1)", ""), share("javascript:alert(1)"))
    }
}
