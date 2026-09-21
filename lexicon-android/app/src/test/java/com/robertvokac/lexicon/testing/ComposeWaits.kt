package com.robertvokac.lexicon.testing

import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule

/**
 * Network answers arrive on OkHttp's threads, which Compose's idling does
 * not know about; these waits poll the semantics tree instead.
 */
fun ComposeTestRule.waitFor(matcher: SemanticsMatcher, timeoutMillis: Long = 10_000) {
    waitUntil(timeoutMillis) { onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty() }
}

fun ComposeTestRule.waitForText(text: String, substring: Boolean = false, timeoutMillis: Long = 10_000) =
    waitFor(hasText(text, substring = substring), timeoutMillis)

fun ComposeTestRule.waitUntilGone(matcher: SemanticsMatcher, timeoutMillis: Long = 10_000) {
    waitUntil(timeoutMillis) { onAllNodes(matcher).fetchSemanticsNodes().isEmpty() }
}

/**
 * Waits for a condition outside the UI, such as a request reaching the fake
 * server. Under Robolectric the main looper only runs when something waits
 * for idle, so each check does: that is where ViewModel coroutines resume.
 */
fun ComposeTestRule.waitForCondition(timeoutMillis: Long = 10_000, condition: () -> Boolean) =
    waitUntil(timeoutMillis) {
        waitForIdle()
        condition()
    }
