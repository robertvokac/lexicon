package com.robertvokac.lexicon.flows

import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.printToString
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import com.robertvokac.lexicon.testing.waitForCondition
import com.robertvokac.lexicon.testing.waitForText
import com.robertvokac.lexicon.testing.waitUntilGone
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import org.junit.runner.RunWith
import org.robolectric.annotation.GraphicsMode

/** The Inbox without the server: ideas wait on the phone and go out later. */
@RunWith(AndroidJUnit4::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class OfflineInboxFlowTest {
    @get:Rule(order = 0)
    val compose = createComposeRule()

    @get:Rule(order = 1)
    val dumpOnFailure = object : TestWatcher() {
        override fun failed(e: Throwable?, description: Description?) {
            runCatching { println("SCREEN\n" + compose.onAllNodes(isRoot()).printToString(maxDepth = Int.MAX_VALUE)) }
            runCatching { println("REQUESTS " + fake.requests.map { "${it.method} ${it.url.encodedPath}" }) }
        }
    }

    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        fake.addItem(Item(title = "Pointer provenance"))
        environment = TestEnvironment()
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    private fun container(): AppContainer =
        environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)

    private fun show(container: AppContainer) {
        val requests = LaunchRequests()
        compose.setContent { LexiconRoot(container, requests, onShareFinished = {}, onExit = {}) }
    }

    private fun field(label: String) = compose.onNode(hasSetTextAction() and hasText(label))

    private fun inDialog(text: String) = compose.onNode(hasText(text) and hasClickAction())

    @Test
    fun anIdeaSavedWhileTheServerIsDownWaitsAndIsSentOnRequest() {
        val container = container()
        signInDirectly(container, fake)
        show(container)
        compose.waitForText("Pointer provenance")

        fake.unavailable = true
        compose.onNodeWithContentDescription("Inbox: save an idea").performClick()
        compose.waitForText("Saved to Default, without a type. Sort it out later.")
        field("Title").performTextInput("Lock-free queue")
        field("Idea").performTextInput("Try a ring buffer.")
        inDialog("Save").performClick()
        // The banner, not the snackbar, which Robolectric does not show reliably.
        compose.waitForText("1 idea waits on this phone", substring = true)
        assertTrue(fake.items.values.none { it.title == "Lock-free queue" })

        fake.unavailable = false
        compose.onNode(hasText("Show") and hasClickAction()).performClick()
        compose.waitForText("Waiting on this phone")
        inDialog("Send now").performClick()
        compose.waitForCondition { fake.items.values.any { it.title == "Lock-free queue" } }
        compose.waitUntilGone(hasText("1 idea waits on this phone", substring = true))
        assertEquals("Try a ring buffer.", fake.items.values.single { it.title == "Lock-free queue" }.content)
        assertTrue(container.outbox.ideas.value.isEmpty())
    }

    @Test
    fun anAppStartedOfflineStillTakesAnIdeaAndSendsItOnceConnected() {
        // Signed in yesterday; today the phone starts without the server.
        signInDirectly(container(), fake)
        fake.unavailable = true
        val container = container()
        show(container)
        compose.waitForText("Cannot reach the server")
        inDialog("Save an idea for later").performClick()
        compose.waitForText("Saved to Default, without a type. Sort it out later.")
        inDialog("Save").performClick()
        compose.waitForText("Enter a title.")
        field("Title").performTextInput("Arena allocator")
        field("Idea").performTextInput("Bump pointer.")
        inDialog("Save").performClick()
        compose.waitForText("1 idea waits on this phone and is sent when the server can be reached.")

        fake.unavailable = false
        inDialog("Retry").performClick()
        compose.waitForCondition { fake.items.values.any { it.title == "Arena allocator" } }
        assertEquals("Bump pointer.", fake.items.values.single { it.title == "Arena allocator" }.content)
        // The list shows it, and the phone keeps nothing back.
        compose.waitForText("Arena allocator", substring = true)
        assertTrue(container.outbox.ideas.value.isEmpty())
        compose.waitForCondition { container.outbox.delivered.value == 0 }
    }
}
