package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.model.Health
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ApiCompatibilityTest {
    private val server = (ServerUrl.parse("https://lexicon.example.com", false) as ServerUrl.Parsed.Valid).url

    @Test
    fun versionOneIsCompatible() {
        assertEquals(ApiCompatibility.Result.Compatible, ApiCompatibility.check(Health("ok", 1, "Lexicon"), server))
        assertEquals(ApiCompatibility.Result.Compatible, ApiCompatibility.check(Health("ok", 1), server))
    }

    @Test
    fun anotherVersionIsExplained() {
        val result = ApiCompatibility.check(Health("ok", 2, "Lexicon"), server)
        assertTrue(result is ApiCompatibility.Result.Incompatible)
        val message = (result as ApiCompatibility.Result.Incompatible).message
        assertTrue(message.contains("version 1"))
        assertTrue(message.contains("version 2"))
    }

    @Test
    fun anotherApplicationIsNotLexicon() {
        val result = ApiCompatibility.check(Health("ok", 1, "Something"), server)
        assertTrue(result is ApiCompatibility.Result.Incompatible)
    }
}
