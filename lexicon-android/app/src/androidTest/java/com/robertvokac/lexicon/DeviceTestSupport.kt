package com.robertvokac.lexicon

import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assume.assumeTrue

/**
 * The LexiconServer these tests talk to, given as instrumentation arguments
 * (see scripts/run-device-tests.sh). Without them the tests are skipped.
 */
object DeviceServer {
    private val arguments get() = InstrumentationRegistry.getArguments()
    val url: String get() = arguments.getString("lexiconServer").orEmpty()
    val user: String get() = arguments.getString("lexiconUser").orEmpty()
    val password: String get() = arguments.getString("lexiconPassword").orEmpty()

    fun require() = assumeTrue(
        "No LexiconServer given (lexiconServer, lexiconUser, lexiconPassword); skipping.",
        url.isNotEmpty() && user.isNotEmpty() && password.isNotEmpty(),
    )
}

fun ComposeTestRule.waitFor(matcher: SemanticsMatcher, timeoutMillis: Long = 20_000) =
    waitUntil(timeoutMillis) { onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty() }

fun ComposeTestRule.waitForText(text: String, substring: Boolean = false, timeoutMillis: Long = 20_000) =
    waitFor(hasText(text, substring = substring), timeoutMillis)

fun ComposeTestRule.waitUntilGone(matcher: SemanticsMatcher, timeoutMillis: Long = 20_000) =
    waitUntil(timeoutMillis) { onAllNodes(matcher).fetchSemanticsNodes().isEmpty() }
