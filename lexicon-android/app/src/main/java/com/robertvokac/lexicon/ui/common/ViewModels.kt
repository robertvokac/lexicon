package com.robertvokac.lexicon.ui.common

import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.createSavedStateHandle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException

val LocalAppContainer = staticCompositionLocalOf<AppContainer> { error("No AppContainer provided") }

/** A ViewModel built from the app container, scoped to the current navigation entry. */
@Composable
inline fun <reified VM : ViewModel> lexiconViewModel(
    key: String? = null,
    crossinline create: (AppContainer, SavedStateHandle) -> VM,
): VM {
    val container = LocalAppContainer.current
    return viewModel(
        key = key,
        factory = viewModelFactory { initializer { create(container, createSavedStateHandle()) } },
    )
}

/**
 * The message to show for a failed call, or null when the failure is a lost
 * session: the login screen is already explaining that.
 */
fun ApiException.userMessage(): String? = when (this) {
    is ApiException.Unauthorized, is ApiException.NotSignedIn -> null
    else -> message
}

/** A one-off message for a snackbar, optionally with an item to open. */
data class UserMessage(val text: String, val openItemId: Int? = null, val id: Long = System.nanoTime())
