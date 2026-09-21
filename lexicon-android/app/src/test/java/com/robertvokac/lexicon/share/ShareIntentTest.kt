package com.robertvokac.lexicon.share

import android.content.Context
import android.content.Intent
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.performClick
import com.robertvokac.lexicon.testing.waitForText
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.MainActivity
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** ACTION_SEND text/plain opens a prefilled editor and saves nothing by itself. */
@RunWith(AndroidJUnit4::class)
class ShareIntentTest {
    @get:Rule
    val compose = createEmptyComposeRule()

    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        environment = TestEnvironment()
        val application = ApplicationProvider.getApplicationContext<LexiconApplication>()
        application.container = environment.container(application.contentResolver)
        signInDirectly(application.container, fake)
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    private fun shareIntent(text: String, subject: String?) =
        Intent(ApplicationProvider.getApplicationContext<Context>(), MainActivity::class.java).apply {
            action = Intent.ACTION_SEND
            type = "text/plain"
            putExtra(Intent.EXTRA_TEXT, text)
            if (subject != null) putExtra(Intent.EXTRA_SUBJECT, subject)
        }

    @Test
    fun parsesTheIntentExtras() {
        val request = LaunchIntents.parse(shareIntent("https://example.com/raii", "RAII - cppreference"))
        assertEquals(LaunchRequest.Share(SharePrefill("RAII - cppreference", "<https://example.com/raii>")), request)
        assertEquals(LaunchRequest.QuickAdd, LaunchIntents.parse(Intent(LaunchIntents.ACTION_QUICK_ADD)))
        assertNull(LaunchIntents.parse(Intent(Intent.ACTION_VIEW)))
        val stream = Intent(Intent.ACTION_SEND).apply { type = "image/png" }
        assertNull(LaunchIntents.parse(stream))
        assertNull(LaunchIntents.parse(null))
    }

    @Test
    fun theQuickAddShortcutOpensQuickAdd() {
        val intent = Intent(ApplicationProvider.getApplicationContext<Context>(), MainActivity::class.java)
            .setAction(LaunchIntents.ACTION_QUICK_ADD)
        ActivityScenario.launch<MainActivity>(intent).use {
            compose.waitForText("Quick add")
            assertTrue(fake.requestsTo("POST", "/api/v1/items").isEmpty())
        }
    }

    @Test
    fun aShareOpensThePrefilledEditorWithoutSaving() {
        ActivityScenario.launch<MainActivity>(shareIntent("https://example.com/raii", "RAII - cppreference")).use {
            compose.waitUntil(10_000) { compose.onAllNodes(hasText("Add item")).fetchSemanticsNodes().isNotEmpty() }
            compose.waitForText("RAII - cppreference")
            compose.onNodeWithText("RAII - cppreference").assertIsDisplayed()
            compose.onNodeWithText("Content").performClick()
            compose.waitForText("<https://example.com/raii>")
            compose.waitForIdle()
            // Nothing is written because another app sent an intent.
            assertTrue(fake.requestsTo("POST", "/api/v1/items").isEmpty())
            assertTrue(fake.requestsTo("PUT", "/api/v1/items").isEmpty())
        }
    }
}
