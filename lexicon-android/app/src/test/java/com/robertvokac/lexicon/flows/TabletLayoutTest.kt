package com.robertvokac.lexicon.flows

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.storage.ThemePreference
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import com.robertvokac.lexicon.testing.waitFor
import com.robertvokac.lexicon.testing.waitForCondition
import com.robertvokac.lexicon.testing.waitForText
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/** A large landscape screen shows the list and the selected item side by side. */
@RunWith(AndroidJUnit4::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(qualifiers = "w1280dp-h800dp-land-mdpi")
class TabletLayoutTest {
    @get:Rule
    val compose = createComposeRule()

    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        fake.addItem(Item(title = "RAII", content = "Resource acquisition is initialization."))
        fake.addItem(Item(title = "Object lifetime", content = "Begins when storage is obtained."))
        environment = TestEnvironment()
        val container = environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)
        signInDirectly(container, fake)
        val requests = LaunchRequests()
        compose.setContent { LexiconRoot(container, requests, onShareFinished = {}, onExit = {}) }
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    @Test
    fun theListStaysBesideTheSelectedItem() {
        compose.waitForText("Select an item to read it here.")
        compose.waitFor(hasText("RAII") and hasClickAction())
        compose.onNode(hasText("RAII") and hasClickAction()).performClick()
        compose.waitForText("Resource acquisition is initialization.")
        compose.onNodeWithText("Object lifetime").assertIsDisplayed()
        compose.onNode(hasText("Object lifetime") and hasClickAction()).performClick()
        compose.waitForText("Begins when storage is obtained.")
        compose.onNodeWithText("RAII").assertIsDisplayed()
        // Each selection is one opening, recorded once.
        compose.waitForCondition { fake.reads.size == 2 }
        assertEquals(fake.items.values.map { it.id }, fake.reads.toList())
    }

    @Test
    fun theThemeIsAClientPreference() {
        compose.waitForText("Select an item to read it here.")
        compose.onNodeWithContentDescription("Open navigation").performClick()
        compose.onNode(hasText("Settings") and hasClickAction()).performClick()
        compose.waitForText("Appearance")
        compose.onNode(hasText("Dark") and hasClickAction()).performClick()
        compose.waitForCondition { runBlocking { environment.settings.theme.first() } == ThemePreference.Dark }
        compose.onNode(hasText("Light") and hasClickAction()).performClick()
        compose.waitForCondition { runBlocking { environment.settings.theme.first() } == ThemePreference.Light }
        // Nothing about the theme reaches the server.
        assertEquals(0, fake.requests.count { it.url.encodedPath.contains("setting", ignoreCase = true) })
    }
}
