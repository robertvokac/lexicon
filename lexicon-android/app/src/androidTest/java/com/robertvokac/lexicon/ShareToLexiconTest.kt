package com.robertvokac.lexicon

import android.content.Intent
import androidx.compose.ui.test.junit4.v2.createEmptyComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.assertIsDisplayed
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.auth.SessionManager
import com.robertvokac.lexicon.model.ItemQuery
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** The Android share sheet path on a device: a prefilled editor, nothing saved by itself. */
@RunWith(AndroidJUnit4::class)
class ShareToLexiconTest {
    @get:Rule
    val compose = createEmptyComposeRule()

    private val container get() = ApplicationProvider.getApplicationContext<LexiconApplication>().container

    @Before
    fun setUp() {
        DeviceServer.require()
        val result = runBlocking { container.sessions.login(DeviceServer.url, DeviceServer.user, DeviceServer.password) }
        assertEquals(SessionManager.LoginResult.Success, result)
    }

    @Test
    fun aSharedLinkOpensAPrefilledEditor() {
        val subject = "Shared from a browser ${System.currentTimeMillis()}"
        val before = runBlocking { container.api.queryItems(ItemQuery(limit = 1)).totalCount }
        val intent = Intent(ApplicationProvider.getApplicationContext(), MainActivity::class.java)
            .setAction(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_TEXT, "https://example.com/shared")
            .putExtra(Intent.EXTRA_SUBJECT, subject)
        ActivityScenario.launch<MainActivity>(intent).use {
            compose.waitForText("Add item")
            compose.waitForText(subject)
            compose.onNodeWithText(subject).assertIsDisplayed()
            compose.waitForIdle()
            assertEquals(before, runBlocking { container.api.queryItems(ItemQuery(limit = 1)).totalCount })
        }
        runBlocking { container.sessions.logout().join() }
    }
}
