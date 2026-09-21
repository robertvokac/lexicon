package com.robertvokac.lexicon

import android.util.Log
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.printToString
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assume.assumeTrue
import org.junit.rules.TestWatcher
import org.junit.runner.Description

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
    try {
        waitUntil(timeoutMillis) { onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty() }
    } catch (timeout: ComposeTimeoutException) {
        // What was on screen while waiting, before any cleanup changes it.
        dumpScreen(this)
        throw timeout
    }

fun dumpScreen(rule: ComposeTestRule) {
    runCatching {
        rule.onAllNodes(isRoot()).printToString(maxDepth = Int.MAX_VALUE).chunked(3_000).forEach { Log.e("LexiconTest", it) }
    }
}

fun ComposeTestRule.waitForText(text: String, substring: Boolean = false, timeoutMillis: Long = 20_000) =
    waitFor(hasText(text, substring = substring), timeoutMillis)

fun ComposeTestRule.waitUntilGone(matcher: SemanticsMatcher, timeoutMillis: Long = 20_000) =
    waitUntil(timeoutMillis) { onAllNodes(matcher).fetchSemanticsNodes().isEmpty() }

/** Writes what was on screen to logcat when a test fails; the report keeps logcat. */
class DumpOnFailure(private val rule: () -> ComposeTestRule) : TestWatcher() {
    override fun failed(e: Throwable?, description: Description?) {
        dumpScreen(rule())
    }
}
