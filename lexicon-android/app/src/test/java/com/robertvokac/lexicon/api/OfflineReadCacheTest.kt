package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.testing.softwareCipher
import kotlinx.coroutines.runBlocking
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.file.Files

class OfflineReadCacheTest {
    @Test
    fun cachedReadsSurviveAnOutageAndStayEncryptedPerAccount() = runBlocking {
        val directory = Files.createTempDirectory("lexicon-offline-read").toFile()
        val server = MockWebServer()
        try {
            server.start()
            val url = (ServerUrl.parse(server.url("/").toString(), true) as ServerUrl.Parsed.Valid).url
            val session = Session(url, "alice", "token")
            val sessions = object : SessionAccess {
                override val current: Session = session
                override fun onUnauthorized(session: Session) = Unit
            }
            val cache = OfflineReadCache(directory, softwareCipher())
            val client = ApiClient(AppContainer.httpClient(), sessions, offlineCache = cache)
            val api = LexiconApi(client)
            server.enqueue(MockResponse.Builder().code(200)
                .body("""{"groups":[{"id":1,"name":"Private notes","description":"","position":0}]}""").build())
            assertEquals("Private notes", api.groups().single().name)
            assertTrue(cache.hasIdentity(url, "alice"))
            assertFalse(cache.hasIdentity(url, "bob"))
            assertFalse(directory.listFiles()!!.single().readText().contains("Private notes"))
            server.close()
            assertEquals("Private notes", api.groups().single().name)
            assertTrue(client.offlineRead.value)
        } finally {
            runCatching { server.close() }
            directory.deleteRecursively()
        }
    }
}
