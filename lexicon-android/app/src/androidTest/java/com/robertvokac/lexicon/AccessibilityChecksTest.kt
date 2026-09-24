package com.robertvokac.lexicon

import androidx.activity.ComponentActivity
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.accessibility.enableAccessibilityChecks
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.tryPerformAccessibilityChecks
import android.view.KeyEvent
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.model.CardWrite
import com.robertvokac.lexicon.model.ItemWrite
import com.robertvokac.lexicon.model.SaveItemRequest
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The Accessibility Test Framework over every main screen: labels, touch
 * target sizes, contrast and more. Any finding fails the test.
 */
@RunWith(AndroidJUnit4::class)
class AccessibilityChecksTest {
    @get:Rule(order = 0)
    val compose = createAndroidComposeRule<ComponentActivity>()

    @get:Rule(order = 1)
    val dump = DumpOnFailure { compose }

    private val container get() = (compose.activity.application as LexiconApplication).container
    private val title = "Accessibility check ${System.currentTimeMillis()}"
    private var itemId: Int? = null

    @Before
    fun setUp() {
        DeviceServer.require()
        runBlocking {
            container.sessions.logout().join()
            container.sessions.login(DeviceServer.url, DeviceServer.user, DeviceServer.password)
            itemId = container.api.createItem(
                SaveItemRequest(ItemWrite(groupId = container.api.defaultGroupId(), title = title, tags = listOf("a11y"), content = "# Heading\n\nText with a [link](https://example.com)."))
            ).id
            container.api.createCard(checkNotNull(itemId), CardWrite("What does the check look at?", "Labels, touch targets\nand contrast."))
            container.sessions.logout().join()
        }
        val requests = LaunchRequests()
        compose.setContent { LexiconRoot(container, requests, onShareFinished = {}, onExit = {}) }
        compose.enableAccessibilityChecks()
    }

    @After
    fun tearDown() {
        runBlocking {
            if (container.sessions.current == null) container.sessions.login(DeviceServer.url, DeviceServer.user, DeviceServer.password)
            itemId?.let { container.api.deleteItem(it) }
            container.sessions.logout().join()
        }
    }

    private fun check() {
        compose.waitForIdle()
        compose.onAllNodes(isRoot())[0].tryPerformAccessibilityChecks()
    }

    private fun back() {
        InstrumentationRegistry.getInstrumentation().sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        compose.waitForIdle()
    }

    private fun open(entry: String) {
        compose.onNodeWithContentDescription("Open navigation").performClick()
        compose.waitFor(hasText(entry) and hasClickAction())
        compose.onNode(hasText(entry) and hasClickAction()).performClick()
    }

    @Test
    fun everyMainScreenPassesTheChecks() {
        compose.waitForText("Log in")
        check()
        compose.onNode(hasSetTextAction() and hasText("Server URL")).performTextReplacement(DeviceServer.url)
        compose.onNode(hasSetTextAction() and hasText("User name")).performTextReplacement(DeviceServer.user)
        compose.onNode(hasSetTextAction() and hasText("Password")).performTextInput(DeviceServer.password)
        compose.onNode(hasText("Log in") and hasClickAction()).performClick()
        compose.waitFor(hasText(title) and hasClickAction())
        check()

        compose.onNode(hasContentDescription("Filters and sort", substring = true) and hasClickAction()).performClick()
        compose.waitForText("Clear filters")
        check()
        back()
        compose.waitUntilGone(hasText("Clear filters"))

        compose.onNode(hasText(title) and hasClickAction()).performClick()
        compose.waitForText("Text with a link.")
        check()

        // The item's cards, and a quiz over them before and after the answer shows.
        compose.onNodeWithContentDescription("More actions").performClick()
        compose.waitFor(hasText("Cards") and hasClickAction())
        compose.onNode(hasText("Cards") and hasClickAction()).performClick()
        compose.waitForText("What does the check look at?")
        check()
        compose.onNodeWithContentDescription("Card quiz").performClick()
        compose.waitFor(hasText("Show answer") and hasClickAction())
        check()
        compose.onNode(hasText("Show answer") and hasClickAction()).performClick()
        compose.waitForText("Do you know?")
        check()
        back()
        compose.waitForText("What does the check look at?")
        back()
        compose.waitForText("Text with a link.")
        compose.onNodeWithContentDescription("Edit item").performClick()
        compose.waitFor(hasSetTextAction() and hasText("Title"))
        check()
        for (tab in listOf("Content", "Metadata", "Links", "Backlinks")) {
            compose.onNode(hasText(tab, substring = true) and hasClickAction()).performClick()
            compose.waitForIdle()
            check()
        }
        compose.onNodeWithContentDescription("Close editor").performClick()
        back()

        for ((entry, marker) in listOf("Groups" to "Default", "Types" to "Types", "All tags" to "Usage count", "Settings" to "Appearance")) {
            open(entry)
            compose.waitForText(marker)
            check()
            back()
        }
    }
}
