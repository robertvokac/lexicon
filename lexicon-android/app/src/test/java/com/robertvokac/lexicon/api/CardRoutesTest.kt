package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.model.CardWrite
import kotlinx.coroutines.runBlocking
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import mockwebserver3.RecordedRequest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Every card route: method, path, body, the answer decoded, and failures typed. */
class CardRoutesTest {
    private lateinit var server: MockWebServer
    private lateinit var api: LexiconApi

    private val card = """
        { "id": 12, "itemId": 42, "question": "What does pointer provenance describe?",
          "answer": "Where a pointer came from.", "successCount": 4, "failureCount": 2,
          "lastAttempt": "2026-09-24T14:00:00Z" }
    """

    private fun json(body: String, code: Int = 200) = MockResponse.Builder()
        .code(code)
        .setHeader("Content-Type", "application/json; charset=utf-8")
        .body(body)
        .build()

    private fun errorResponse(code: Int, errorCode: String, message: String) =
        json("""{ "error": { "code": "$errorCode", "message": "$message" } }""", code)

    private fun RecordedRequest.target(): String = "$method ${url.encodedPath}" + (url.encodedQuery?.let { "?$it" } ?: "")

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        val url = (ServerUrl.parse(server.url("/").toString(), allowCleartextDevelopmentHosts = true) as ServerUrl.Parsed.Valid).url
        val sessions = object : SessionAccess {
            override val current: Session = Session(url, "robert", "the-token")
            override fun onUnauthorized(session: Session) = Unit
        }
        api = LexiconApi(ApiClient(AppContainer.httpClient(), sessions))
    }

    @After
    fun tearDown() {
        server.close()
    }

    @Test
    fun theCardsOfAnItemAreListed() = runBlocking {
        server.enqueue(json("""{ "cards": [ $card, { "id": 13, "itemId": 42, "question": "Q", "answer": "A", "successCount": 0, "failureCount": 0, "lastAttempt": null } ] }"""))
        val cards = api.cards(42)
        assertEquals(listOf(12, 13), cards.map { it.id })
        assertEquals(4L, cards[0].successCount)
        assertEquals("2026-09-24T14:00:00Z", cards[0].lastAttempt)
        assertNull(cards[1].lastAttempt)
        val request = server.takeRequest()
        assertEquals("GET /api/v1/items/42/cards", request.target())
        assertEquals("Bearer the-token", request.headers["Authorization"])
    }

    @Test
    fun aCardIsCreatedFromItsQuestionAndAnswer() = runBlocking {
        server.enqueue(json("""{ "card": { "id": 14, "itemId": 42, "question": "Co znamená řetězec?", "answer": "指针\nstd::uint64_t", "successCount": 0, "failureCount": 0, "lastAttempt": null } }""", 201))
        val created = api.createCard(42, CardWrite("Co znamená řetězec?", "指针\nstd::uint64_t"))
        assertEquals(14, created.id)
        assertEquals("指针\nstd::uint64_t", created.answer)
        val request = server.takeRequest()
        assertEquals("POST /api/v1/items/42/cards", request.target())
        assertEquals("application/json; charset=utf-8", request.headers["Content-Type"])
        assertEquals("""{"question":"Co znamená řetězec?","answer":"指针\nstd::uint64_t"}""", request.body!!.utf8())
    }

    @Test
    fun aCardIsReadUpdatedAndDeleted() = runBlocking {
        server.enqueue(json("""{ "card": $card }"""))
        server.enqueue(json("""{ "card": $card }"""))
        server.enqueue(MockResponse.Builder().code(204).build())
        assertEquals("What does pointer provenance describe?", api.card(12).question)
        // The answer, counts included, is what the server stored.
        val updated = api.updateCard(12, CardWrite("New question", "New answer"))
        assertEquals(4L, updated.successCount)
        assertEquals(2L, updated.failureCount)
        api.deleteCard(12)
        assertEquals("GET /api/v1/cards/12", server.takeRequest().target())
        val put = server.takeRequest()
        assertEquals("PUT /api/v1/cards/12", put.target())
        assertEquals("""{"question":"New question","answer":"New answer"}""", put.body!!.utf8())
        val delete = server.takeRequest()
        assertEquals("DELETE /api/v1/cards/12", delete.target())
        assertEquals(0L, delete.bodySize)
    }

    @Test
    fun anAttemptSendsExactlyTrueOrFalse() = runBlocking {
        server.enqueue(json("""{ "card": { "id": 12, "itemId": 42, "question": "Q", "answer": "A", "successCount": 5, "failureCount": 2, "lastAttempt": "2026-09-24T14:01:00Z" } }"""))
        server.enqueue(json("""{ "card": { "id": 12, "itemId": 42, "question": "Q", "answer": "A", "successCount": 5, "failureCount": 3, "lastAttempt": "2026-09-24T14:02:00Z" } }"""))
        val yes = api.attemptCard(12, success = true)
        assertEquals(5L, yes.successCount)
        assertEquals("2026-09-24T14:01:00Z", yes.lastAttempt)
        val no = api.attemptCard(12, success = false)
        assertEquals(3L, no.failureCount)
        val first = server.takeRequest()
        assertEquals("POST /api/v1/cards/12/attempt", first.target())
        assertEquals("""{"success":true}""", first.body!!.utf8())
        assertEquals("""{"success":false}""", server.takeRequest().body!!.utf8())
    }

    @Test
    fun theQuizAsksForItsScopeAndTheItemLimit() = runBlocking {
        val set = """
            { "cards": [ { "id": 12, "itemId": 42, "itemTitle": "pointer provenance", "question": "Q", "answer": "A",
                           "successCount": 0, "failureCount": 0, "lastAttempt": null } ],
              "itemCount": 8, "truncated": true }
        """
        server.enqueue(json(set))
        server.enqueue(json(set))
        val quiz = api.quizCards(42)
        assertEquals("pointer provenance", quiz.cards.single().itemTitle)
        assertEquals(8, quiz.itemCount)
        assertTrue(quiz.truncated)
        api.quizCards(42, depth = 3)
        assertEquals("GET /api/v1/items/42/quiz-cards?depth=0&limit=150", server.takeRequest().target())
        assertEquals("GET /api/v1/items/42/quiz-cards?depth=3&limit=150", server.takeRequest().target())
        assertEquals(150, LexiconApi.QUIZ_ITEM_LIMIT)
    }

    @Test
    fun cardFailuresAreTyped() = runBlocking {
        suspend fun failureOf(response: MockResponse, call: suspend () -> Unit): ApiException {
            server.enqueue(response)
            try {
                call()
            } catch (failure: ApiException) {
                return failure
            }
            throw AssertionError("expected a failure")
        }
        val blank = failureOf(errorResponse(400, "validation", "Card question cannot be empty.")) { api.createCard(42, CardWrite(" ", "A")) }
        assertTrue(blank is ApiException.Validation)
        assertEquals("Card question cannot be empty.", blank.message)
        assertTrue(failureOf(errorResponse(400, "validation", "Card answer cannot be empty.")) { api.updateCard(12, CardWrite("Q", "")) } is ApiException.Validation)
        assertTrue(failureOf(errorResponse(400, "validation", "An attempt is a success or not.")) { api.attemptCard(12, true) } is ApiException.Validation)
        assertTrue(failureOf(errorResponse(404, "not_found", "Item not found.")) { api.cards(404) } is ApiException.NotFound)
        assertTrue(failureOf(errorResponse(404, "not_found", "Item not found.")) { api.createCard(404, CardWrite("Q", "A")) } is ApiException.NotFound)
        assertTrue(failureOf(errorResponse(404, "not_found", "Card not found.")) { api.card(404) } is ApiException.NotFound)
        assertTrue(failureOf(errorResponse(404, "not_found", "Card not found.")) { api.deleteCard(404) } is ApiException.NotFound)
        assertTrue(failureOf(errorResponse(404, "not_found", "Card not found.")) { api.attemptCard(404, false) } is ApiException.NotFound)
        assertTrue(failureOf(errorResponse(404, "not_found", "Item not found.")) { api.quizCards(404, 2) } is ApiException.NotFound)
    }
}
