package com.robertvokac.lexicon.ui.login

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.clearText
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedSecureTextField
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.autofill.ContentType
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentType
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.R
import com.robertvokac.lexicon.auth.SessionManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class LoginState(
    val busy: Boolean = false,
    val error: String? = null,
    val allowHttpForTesting: Boolean = false,
)

/**
 * Health, API version, then login. The password lives in [password] only
 * while it is typed and is cleared as soon as the server has answered with a
 * session; it is never saved, logged or put into saved instance state.
 */
class LoginViewModel(private val container: AppContainer) : ViewModel() {
    val server = TextFieldState()
    val username = TextFieldState()
    val password = TextFieldState()
    private val _state = MutableStateFlow(LoginState())
    val state: StateFlow<LoginState> = _state.asStateFlow()
    val canAllowHttpForTesting: Boolean = container.allowCleartextDevelopmentHosts

    init {
        viewModelScope.launch {
            server.setTextAndPlaceCursorAtEnd(container.settings.lastServerUrl() ?: container.defaultServerUrl)
            username.setTextAndPlaceCursorAtEnd(container.settings.lastUsername().orEmpty())
        }
    }

    fun setAllowHttpForTesting(allow: Boolean) {
        _state.update { it.copy(allowHttpForTesting = allow && canAllowHttpForTesting, error = null) }
    }

    fun login() {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true, error = null) }
        viewModelScope.launch {
            val result = container.sessions.login(
                server.text.toString(),
                username.text.toString(),
                password.text.toString(),
                allowHttpForTesting = _state.value.allowHttpForTesting,
            )
            when (result) {
                SessionManager.LoginResult.Success -> {
                    password.clearText()
                    _state.update { LoginState() }
                }
                is SessionManager.LoginResult.Failure -> _state.update { it.copy(busy = false, error = result.message) }
            }
        }
    }

    override fun onCleared() {
        password.clearText()
    }
}

@Composable
fun LoginScreen(viewModel: LoginViewModel, notice: String?, modifier: Modifier = Modifier) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    Surface(modifier.fillMaxSize()) {
        Box(Modifier.fillMaxSize().safeDrawingPadding().imePadding(), contentAlignment = Alignment.Center) {
            Column(
                Modifier
                    .widthIn(max = 480.dp)
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Image(painterResource(R.drawable.ic_lexicon_logo), contentDescription = null, modifier = Modifier.size(72.dp))
                Text("Lexicon", style = MaterialTheme.typography.headlineMedium)
                Text(
                    "Sign in to your LexiconServer.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                notice?.let {
                    Text(
                        it,
                        textAlign = TextAlign.Center,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    )
                }
                OutlinedTextField(
                    state = viewModel.server,
                    label = { Text("Server URL") },
                    placeholder = { Text("https://api.example.com") },
                    lineLimits = TextFieldLineLimits.SingleLine,
                    enabled = !state.busy,
                    keyboardOptions = KeyboardOptions(
                        capitalization = KeyboardCapitalization.None,
                        autoCorrectEnabled = false,
                        keyboardType = KeyboardType.Uri,
                        imeAction = ImeAction.Next,
                    ),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (viewModel.canAllowHttpForTesting) {
                    Row(
                        Modifier.fillMaxWidth().toggleable(
                            value = state.allowHttpForTesting,
                            enabled = !state.busy,
                            role = Role.Checkbox,
                            onValueChange = viewModel::setAllowHttpForTesting,
                        ),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Checkbox(checked = state.allowHttpForTesting, onCheckedChange = null, enabled = !state.busy)
                        Text("Allow HTTP for testing")
                    }
                    if (state.allowHttpForTesting) {
                        Text(
                            "Your password and session token will travel without encryption. Use only on a trusted test network.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                }
                OutlinedTextField(
                    state = viewModel.username,
                    label = { Text("User name") },
                    lineLimits = TextFieldLineLimits.SingleLine,
                    enabled = !state.busy,
                    keyboardOptions = KeyboardOptions(
                        capitalization = KeyboardCapitalization.None,
                        autoCorrectEnabled = false,
                        imeAction = ImeAction.Next,
                    ),
                    modifier = Modifier.fillMaxWidth().semantics { contentType = ContentType.Username },
                )
                OutlinedSecureTextField(
                    state = viewModel.password,
                    label = { Text("Password") },
                    enabled = !state.busy,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                    onKeyboardAction = { viewModel.login() },
                    modifier = Modifier.fillMaxWidth().semantics { contentType = ContentType.Password },
                )
                state.error?.let {
                    Text(
                        it,
                        color = MaterialTheme.colorScheme.error,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive },
                    )
                }
                Button(onClick = viewModel::login, enabled = !state.busy, modifier = Modifier.fillMaxWidth()) {
                    if (state.busy) CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp) else Text("Log in")
                }
            }
        }
    }
}

/** The server speaks another API version: nothing is sent until that changes. */
@Composable
fun CompatibilityScreen(server: String, message: String, onRetry: () -> Unit, onChangeServer: () -> Unit, modifier: Modifier = Modifier) {
    StatusScreen("Incompatible server", server, message, onRetry, "Use another server", onChangeServer, modifier)
}

/** A stored session could not be verified because the server did not answer. */
@Composable
fun UnreachableScreen(
    server: String,
    message: String,
    onRetry: () -> Unit,
    onSignIn: () -> Unit,
    modifier: Modifier = Modifier,
    waitingIdeas: Int = 0,
    onSaveIdea: (() -> Unit)? = null,
    onBrowseSaved: (() -> Unit)? = null,
) {
    StatusScreen("Cannot reach the server", server, message, onRetry, "Sign in again", onSignIn, modifier) {
        if (onBrowseSaved != null) {
            OutlinedButton(onClick = onBrowseSaved) { Text("Browse saved copy") }
            Text("Saved items are read-only until the server is reachable.",
                style = MaterialTheme.typography.bodySmall)
        }
        // An idea should not wait for the network: it waits on the phone.
        if (onSaveIdea != null) {
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            OutlinedButton(onClick = onSaveIdea) { Text("Save an idea for later") }
            Text(
                when (waitingIdeas) {
                    0 -> "It is kept on this phone and sent when the server can be reached."
                    1 -> "1 idea waits on this phone and is sent when the server can be reached."
                    else -> "$waitingIdeas ideas wait on this phone and are sent when the server can be reached."
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
private fun StatusScreen(
    title: String,
    server: String,
    message: String,
    onRetry: () -> Unit,
    otherLabel: String,
    onOther: () -> Unit,
    modifier: Modifier,
    extra: @Composable () -> Unit = {},
) {
    Surface(modifier.fillMaxSize()) {
        Box(Modifier.fillMaxSize().safeDrawingPadding(), contentAlignment = Alignment.Center) {
            Column(
                Modifier.widthIn(max = 480.dp).padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(title, style = MaterialTheme.typography.headlineSmall)
                Text(server, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(message, textAlign = TextAlign.Center)
                Button(onClick = onRetry) { Text("Retry") }
                OutlinedButton(onClick = onOther) { Text(otherLabel) }
                extra()
            }
        }
    }
}

@Composable
fun StartingScreen(modifier: Modifier = Modifier) {
    Surface(modifier.fillMaxSize()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
    }
}
