package com.robertvokac.lexicon.flows

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.isSelected
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.printToString
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.api.LexiconJson
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.Link
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import com.robertvokac.lexicon.testing.waitFor
import com.robertvokac.lexicon.testing.waitForCondition
import com.robertvokac.lexicon.testing.waitForText
import com.robertvokac.lexicon.testing.waitUntilGone
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot
import com.robertvokac.lexicon.ui.alarms.AlarmTimes
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import org.junit.runner.RunWith
import org.robolectric.annotation.GraphicsMode

/**
 * Cards and the card quiz through the real Compose UI against the in-memory
 * server: the list with the server's counts, adding, editing and deleting,
 * and the quiz from an item, its card list and the relationship graph.
 */
@RunWith(AndroidJUnit4::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class CardFlowsTest {
    @get:Rule(order = 0)
    val compose = createComposeRule()

    /** On a failure, what was on screen and what the server saw. */
    @get:Rule(order = 1)
    val dumpOnFailure = object : TestWatcher() {
        override fun failed(e: Throwable?, description: Description?) {
            runCatching { println("SCREEN\n" + compose.onAllNodes(isRoot()).printToString(maxDepth = Int.MAX_VALUE)) }
            runCatching { println("REQUESTS " + fake.requests.map { "${it.method} ${it.url.encodedPath}?${it.url.encodedQuery}" }) }
        }
    }

    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var restoration: StateRestorationTester
    private lateinit var raii: Item
    private lateinit var lifetime: Item

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        raii = fake.addItem(Item(title = "RAII", content = "Resource acquisition is initialization."))
        lifetime = fake.addItem(Item(title = "Object lifetime"))
        environment = TestEnvironment()
        val container = environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)
        signInDirectly(container, fake)
        val requests = LaunchRequests()
        // Set through the tester, so a test can take the screen down and restore it as rotation does.
        restoration = StateRestorationTester(compose)
        restoration.setContent { LexiconRoot(container, requests, onShareFinished = {}, onExit = {}) }
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    private fun field(label: String) = compose.onNode(hasSetTextAction() and hasText(label))

    private fun button(text: String) = compose.onNode(hasText(text) and hasClickAction())

    private fun inDialog(text: String) = compose.onNode(hasText(text) and hasClickAction() and hasAnyAncestor(isDialog()))

    private fun lastBody(method: String, path: String): JsonObject =
        LexiconJson.parseToJsonElement(fake.requestsTo(method, path).last().body!!.utf8()).jsonObject

    private fun quizRequests(itemId: Int?) = fake.requestsTo("GET", "/api/v1/items/$itemId/quiz-cards")

    private fun attempts(cardId: Int) = fake.requestsTo("POST", "/api/v1/cards/$cardId/attempt")

    /** From the item list: the item page, then [entry] of its menu. */
    private fun openFromItem(title: String, entry: String) {
        compose.waitFor(hasText(title) and hasClickAction())
        button(title).performClick()
        compose.waitFor(hasContentDescription("More actions"))
        compose.onNodeWithContentDescription("More actions").performClick()
        compose.waitFor(hasText(entry) and hasClickAction())
        button(entry).performClick()
    }

    /** Show answer, then Yes or No. */
    private fun answer(yes: Boolean) {
        compose.waitFor(hasText("Show answer") and hasClickAction())
        button("Show answer").performScrollTo().performClick()
        val label = if (yes) "Yes" else "No"
        compose.waitFor(hasText(label) and hasClickAction())
        button(label).performScrollTo().performClick()
    }

    @Test
    fun theCardsOfAnItemShowTheServersCounts() {
        fake.addCard(
            raii.id!!,
            "What does RAII tie to an object's lifetime?",
            "A resource.\nAcquired in the constructor, released in the destructor.",
            successCount = 4,
            failureCount = 2,
            lastAttempt = "2026-09-24T14:00:00Z",
        )
        fake.addCard(raii.id!!, "Co znamená řetězec?", "Příliš žluťoučký kůň")
        fake.addCard(lifetime.id!!, "Somebody else's question", "Somebody else's answer")
        openFromItem("RAII", "Cards")
        compose.waitForText("What does RAII tie to an object's lifetime?")
        compose.onNodeWithText("2 card(s). Only the quiz changes the counts.").assertIsDisplayed()
        compose.onNodeWithText("A resource.\nAcquired in the constructor, released in the destructor.").assertIsDisplayed()
        // The last attempt in the phone's time zone, as alarms are shown.
        compose.onNodeWithText("Success: 4 · Failure: 2 · Last attempt: ${AlarmTimes.describe("2026-09-24T14:00:00Z")}").assertIsDisplayed()
        compose.onNodeWithText("Příliš žluťoučký kůň").assertIsDisplayed()
        compose.onNodeWithText("Success: 0 · Failure: 0 · Last attempt: Never").assertIsDisplayed()
        compose.onNodeWithText("Somebody else's question").assertDoesNotExist()
        assertEquals(1, fake.requestsTo("GET", "/api/v1/items/${raii.id}/cards").size)
    }

    @Test
    fun cardsAreAddedEditedAndDeleted() {
        openFromItem("RAII", "Cards")
        compose.waitForText("No cards yet. Add one with the + button.")
        compose.onNodeWithContentDescription("Add card").performClick()
        compose.waitForText("Add card")
        // The server says what a card needs, in the dialog.
        field("Answer").performTextInput("A resource")
        inDialog("Save").performClick()
        compose.waitForText("Question cannot be empty.")
        assertTrue(fake.cards.isEmpty())
        field("Question").performTextInput("What does RAII tie\nto a scope?")
        compose.waitUntilGone(hasText("Question cannot be empty."))
        inDialog("Save").performClick()
        compose.waitFor(hasContentDescription("Delete card What does RAII tie"))
        val created = lastBody("POST", "/api/v1/items/${raii.id}/cards")
        assertEquals(setOf("question", "answer"), created.keys)
        assertEquals("What does RAII tie\nto a scope?", created["question"]!!.jsonPrimitive.content)
        assertEquals("A resource", created["answer"]!!.jsonPrimitive.content)
        val id = fake.cards.single().id
        compose.onNodeWithText("Success: 0 · Failure: 0 · Last attempt: Never").assertIsDisplayed()

        // Meanwhile a quiz elsewhere counted two answers.
        synchronized(fake) { fake.cards[0] = fake.cards[0].copy(successCount = 1, failureCount = 1, lastAttempt = "2026-09-20T08:00:00Z") }
        button("What does RAII tie\nto a scope?").performClick()
        compose.waitForText("Edit card")
        field("Answer").performTextReplacement("A resource, released when the scope ends")
        inDialog("Save").performClick()
        compose.waitUntilGone(hasText("Edit card"))
        // Only the text travels; the counts shown are the server's answer.
        assertEquals(setOf("question", "answer"), lastBody("PUT", "/api/v1/cards/$id").keys)
        compose.waitForText("Success: 1 · Failure: 1 · Last attempt: ${AlarmTimes.describe("2026-09-20T08:00:00Z")}")
        compose.onNodeWithText("A resource, released when the scope ends").assertIsDisplayed()
        assertEquals(1L, fake.card(id).successCount)

        // Delete asks first.
        compose.onNodeWithContentDescription("Delete card What does RAII tie").performClick()
        compose.waitForText("Delete the card 'What does RAII tie' and its counts?")
        inDialog("Cancel").performClick()
        compose.waitUntilGone(hasText("Delete the card 'What does RAII tie' and its counts?"))
        assertTrue(fake.requestsTo("DELETE", "/api/v1/cards/$id").isEmpty())
        compose.onNodeWithContentDescription("Delete card What does RAII tie").performClick()
        inDialog("Delete").performClick()
        compose.waitForText("No cards yet. Add one with the + button.")
        assertTrue(fake.cards.isEmpty())
    }

    @Test
    fun theQuizRecordsOnlyYesAndNo() {
        val first = fake.addCard(raii.id!!, "Co znamená řetězec?", "Příliš žluťoučký kůň")
        val second = fake.addCard(raii.id!!, "What is std::uint64_t?", "An unsigned 64-bit integer.")
        val third = fake.addCard(raii.id!!, "指针是什么?", "A pointer.\nIt has a provenance.")
        openFromItem("RAII", "Cards")
        compose.waitForText("Co znamená řetězec?")
        compose.onNodeWithContentDescription("Card quiz").performClick()
        compose.waitForText("1 / 3")
        assertEquals("depth=0&limit=150", quizRequests(raii.id).single().url.encodedQuery)
        compose.onNode(hasText("This item") and isSelected()).assertExists()
        compose.onNodeWithText("Item: RAII").assertIsDisplayed()
        compose.onNodeWithText("Co znamená řetězec?").assertIsDisplayed()
        compose.onNodeWithText("Příliš žluťoučký kůň").assertDoesNotExist()
        button("Yes").assertDoesNotExist()

        // Showing the answer asks nothing of the server.
        val before = fake.requests.size
        button("Show answer").performClick()
        compose.waitForText("Příliš žluťoučký kůň")
        compose.onNodeWithText("Do you know?").assertIsDisplayed()
        repeat(3) { compose.waitForIdle() }
        assertEquals(before, fake.requests.size)

        button("Yes").performScrollTo().performClick()
        compose.waitForText("2 / 3")
        assertEquals("""{"success":true}""", attempts(first.id).single().body!!.utf8())
        assertEquals(1L, fake.card(first.id).successCount)
        assertEquals(0L, fake.card(first.id).failureCount)
        assertEquals(fake.attemptTime, fake.card(first.id).lastAttempt)
        // The next card starts with its answer hidden.
        compose.onNodeWithText("An unsigned 64-bit integer.").assertDoesNotExist()

        answer(yes = false)
        compose.waitForText("3 / 3")
        assertEquals("""{"success":false}""", attempts(second.id).single().body!!.utf8())
        assertEquals(0L, fake.card(second.id).successCount)
        assertEquals(1L, fake.card(second.id).failureCount)

        answer(yes = true)
        compose.waitForText("Quiz finished")
        compose.onNodeWithText("Cards: 3").assertIsDisplayed()
        compose.onNodeWithText("Yes: 2").assertIsDisplayed()
        compose.onNodeWithText("No: 1").assertIsDisplayed()
        assertEquals(1L, fake.card(third.id).successCount)

        // A card quiz is not a review.
        assertTrue(fake.reviews.isEmpty())
        assertTrue(fake.requests.none { it.url.encodedPath.endsWith("/review") })
        assertEquals(UnderstandingLevel.Unknown, fake.items.getValue(raii.id!!).understanding)
        assertNull(fake.items.getValue(raii.id!!).reviewedAt)

        // Back in the list, the counts are the server's new ones.
        compose.onNodeWithContentDescription("Back").performClick()
        compose.waitForText("Success: 1 · Failure: 0 · Last attempt: ${AlarmTimes.describe(fake.attemptTime)}")
        compose.onNodeWithText("Success: 0 · Failure: 1 · Last attempt: ${AlarmTimes.describe(fake.attemptTime)}").assertIsDisplayed()
    }

    @Test
    fun aSecondTapWhileTheAnswerTravelsSendsNothing() {
        val first = fake.addCard(raii.id!!, "First question", "First answer")
        fake.addCard(raii.id!!, "Second question", "Second answer")
        fake.attemptDelayMillis = 1_500
        openFromItem("RAII", "Card quiz")
        compose.waitForText("1 / 2")
        button("Show answer").performClick()
        compose.waitFor(hasText("Yes") and hasClickAction())
        button("Yes").performClick()
        button("Yes").assertIsNotEnabled()
        button("No").assertIsNotEnabled()
        button("Yes").performClick()
        button("No").performClick()
        compose.waitForText("2 / 2")
        assertEquals(1, attempts(first.id).size)
        assertEquals(1L, fake.card(first.id).successCount)
        assertEquals(0L, fake.card(first.id).failureCount)
    }

    @Test
    fun aFailedAnswerStaysOnItsCard() {
        val card = fake.addCard(raii.id!!, "What is a dangling reference?", "One whose object has ended.")
        fake.attemptsFail = true
        openFromItem("RAII", "Card quiz")
        compose.waitForText("1 / 1")
        answer(yes = true)
        compose.waitForText("Try again later.")
        compose.onNodeWithText("1 / 1").assertIsDisplayed()
        compose.onNodeWithText("One whose object has ended.").assertIsDisplayed()
        button("Yes").assertIsEnabled()
        assertEquals(0L, fake.card(card.id).successCount)

        fake.attemptsFail = false
        button("No").performScrollTo().performClick()
        compose.waitForText("Quiz finished")
        compose.onNodeWithText("Cards: 1").assertIsDisplayed()
        compose.onNodeWithText("Yes: 0").assertIsDisplayed()
        compose.onNodeWithText("No: 1").assertIsDisplayed()
        assertEquals(1L, fake.card(card.id).failureCount)
    }

    @Test
    fun aCardDeletedMeanwhileIsSkipped() {
        val gone = fake.addCard(raii.id!!, "Deleted elsewhere?", "Yes.")
        val kept = fake.addCard(raii.id!!, "Still here?", "Yes.")
        openFromItem("RAII", "Card quiz")
        compose.waitForText("1 / 2")
        synchronized(fake) { fake.cards.removeAll { it.id == gone.id } }
        answer(yes = true)
        compose.waitForText("This card was deleted meanwhile; it was skipped.")
        compose.waitForText("2 / 2")
        answer(yes = false)
        compose.waitForText("Quiz finished")
        compose.onNodeWithText("Cards: 2").assertIsDisplayed()
        compose.onNodeWithText("Yes: 0").assertIsDisplayed()
        compose.onNodeWithText("No: 1").assertIsDisplayed()
        assertEquals(1L, fake.card(kept.id).failureCount)
    }

    @Test
    fun anItemWithoutCardsHasNothingToQuiz() {
        openFromItem("Object lifetime", "Card quiz")
        compose.waitForText("No cards are available for this quiz.")
        button("Show answer").assertDoesNotExist()
    }

    @Test
    fun theNeighborhoodQuizCoversTheItemsAround() {
        // Object lifetime <- RAII <- Pointer provenance: one and two links away.
        val provenance = fake.addItem(Item(title = "Pointer provenance"))
        fake.links += Link(id = 900, fromItemId = raii.id, toItemId = lifetime.id, linkType = LinkType.DependsOn)
        fake.links += Link(id = 901, fromItemId = provenance.id, toItemId = raii.id, linkType = LinkType.Related)
        fake.addCard(provenance.id!!, "Co je provenance ukazatele?", "Odkud ukazatel pochází.")
        fake.addCard(raii.id!!, "What does RAII release?", "Resources.")
        fake.addCard(lifetime.id!!, "When does a lifetime begin?", "When storage is obtained and initialized.")

        openFromItem("Object lifetime", "Card quiz")
        compose.waitForText("1 / 1")
        compose.onNodeWithText("Object lifetime · 1 card(s)").assertIsDisplayed()

        // Two links deep unless the graph said otherwise; the centre's cards first.
        button("Neighborhood").performClick()
        compose.waitForText("1 / 3")
        assertEquals("depth=2&limit=150", quizRequests(lifetime.id).last().url.encodedQuery)
        compose.onNode(hasText("2 links") and isSelected()).assertExists()
        compose.onNodeWithText("Object lifetime · 3 card(s) from 3 item(s)").assertIsDisplayed()
        compose.onNodeWithText("Item: Object lifetime").assertIsDisplayed()
        answer(yes = true)
        compose.waitForText("2 / 3")
        compose.onNodeWithText("Item: RAII").assertIsDisplayed()
        compose.onNodeWithText("What does RAII release?").assertIsDisplayed()
        answer(yes = false)
        compose.waitForText("3 / 3")
        compose.onNodeWithText("Item: Pointer provenance").assertIsDisplayed()
        compose.onNodeWithText("Co je provenance ukazatele?").assertIsDisplayed()

        // Another depth is another quiz, from the start.
        button("1 link").performClick()
        compose.waitForText("1 / 2")
        assertEquals("depth=1&limit=150", quizRequests(lifetime.id).last().url.encodedQuery)
        compose.onNodeWithText("Object lifetime · 2 card(s) from 2 item(s)").assertIsDisplayed()
        compose.onNodeWithText("The neighborhood is larger than shown", substring = true).assertDoesNotExist()
    }

    @Test
    fun aNeighborhoodLargerThanTheLimitSaysSo() {
        // Titled to sort after RAII, which stays on the list's first page.
        for (number in 1..151) {
            val neighbour = fake.addItem(Item(title = "Scope $number"))
            fake.links += Link(id = 1_000 + number, fromItemId = raii.id, toItemId = neighbour.id, linkType = LinkType.Related)
            if (number == 1) fake.addCard(checkNotNull(neighbour.id), "Near question", "Near answer")
        }
        openFromItem("RAII", "Card quiz")
        compose.waitForText("No cards are available for this quiz.")
        button("Neighborhood").performClick()
        compose.waitForText("1 / 1")
        button("1 link").performClick()
        compose.waitForText("The neighborhood is larger than shown: the quiz covers the nearest 150 items.")
        assertEquals("depth=1&limit=150", quizRequests(raii.id).last().url.encodedQuery)
        compose.onNodeWithText("RAII · 1 card(s) from 150 item(s)").assertIsDisplayed()
    }

    @Test
    fun theGraphQuizzesItsCentreAsDeepAsItGoes() {
        fake.links += Link(id = 900, fromItemId = raii.id, toItemId = lifetime.id, linkType = LinkType.DependsOn)
        fake.addCard(lifetime.id!!, "When does a lifetime end?", "When the destructor call starts.")
        openFromItem("RAII", "Relationship graph")
        compose.waitForText("2 item(s), 1 link(s)")
        // Centred on another item and one link deep: the quiz follows both.
        button("Object lifetime").performClick()
        button("Centre here").performClick()
        button("1 link").performClick()
        compose.waitForCondition {
            fake.requestsTo("GET", "/api/v1/items/${lifetime.id}/graph").any { it.url.queryParameter("depth") == "1" }
        }
        compose.onNodeWithContentDescription("Quiz cards").performClick()
        compose.waitForText("1 / 1")
        assertEquals("depth=1&limit=150", quizRequests(lifetime.id).single().url.encodedQuery)
        assertTrue(quizRequests(raii.id).isEmpty())
        compose.onNode(hasText("Neighborhood") and isSelected()).assertExists()
        compose.onNode(hasText("1 link") and isSelected()).assertExists()
        compose.onNodeWithText("When does a lifetime end?").assertIsDisplayed()
    }

    @Test
    fun theQuizKeepsItsPlaceWhenTheScreenIsRecreated() {
        val first = fake.addCard(raii.id!!, "First question", "First answer")
        fake.addCard(raii.id!!, "Second question", "Second answer")
        fake.addCard(raii.id!!, "Third question", "Third answer")
        openFromItem("RAII", "Card quiz")
        compose.waitForText("1 / 3")
        answer(yes = true)
        compose.waitForText("2 / 3")
        button("Show answer").performClick()
        compose.waitForText("Second answer")

        // As rotation does: the screen goes, its ViewModel stays.
        restoration.emulateSavedInstanceStateRestore()
        compose.waitForText("2 / 3")
        compose.onNodeWithText("Second answer").assertIsDisplayed()
        assertEquals(1, quizRequests(raii.id).size)
        assertEquals(1, attempts(first.id).size)
        button("No").performScrollTo().performClick()
        compose.waitForText("3 / 3")
        answer(yes = true)
        compose.waitForText("Quiz finished")
        compose.onNodeWithText("Yes: 2").assertIsDisplayed()
        compose.onNodeWithText("No: 1").assertIsDisplayed()
    }
}
