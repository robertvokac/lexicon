package com.robertvokac.lexicon.ui.common

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.GraphicsMode
import android.os.Looper
import java.time.Duration

@RunWith(AndroidJUnit4::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class SyncedTextFieldTest {
    @get:Rule
    val compose = createComposeRule()

    private val field get() = compose.onNode(hasSetTextAction())

    @Test
    fun aSlowEchoNeverDropsWhatWasTypedSince() {
        var stored by mutableStateOf("")
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
        compose.setContent {
            SyncedTextField(value = stored, onValueChange = { text ->
                // A caller that stores the text a little later, as a ViewModel may.
                scope.launch {
                    delay(30)
                    stored = text
                }
            }, label = "Title")
        }
        // Every keystroke lands before the first echo comes back.
        "Monoid".forEach { field.performTextInput(it.toString()) }
        field.assertTextEquals("Title", "Monoid")
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(100))
        compose.waitForIdle()
        assertEquals("Monoid", stored)
        field.assertTextEquals("Title", "Monoid")
    }

    @Test
    fun aChangeFromOutsideReplacesTheText() {
        var stored by mutableStateOf("")
        compose.setContent { SyncedTextField(value = stored, onValueChange = { stored = it }, label = "Alias") }
        field.performTextInput("semi")
        compose.waitForIdle()
        assertEquals("semi", stored)
        stored = ""
        compose.waitForIdle()
        field.assertTextEquals("Alias", "")
        field.performTextInput("x")
        compose.waitForIdle()
        assertEquals("x", stored)
    }

    @Test
    fun rejectedInputNeverReachesTheCaller() {
        var stored by mutableStateOf("")
        compose.setContent { SyncedTextField(value = stored, onValueChange = { stored = it }, label = "Id", accept = Digits) }
        "12a3".forEach { field.performTextInput(it.toString()) }
        compose.waitForIdle()
        assertEquals("123", stored)
        assertEquals(true, WholeNumber("-12"))
        assertEquals(false, WholeNumber("1-2"))
    }
}
