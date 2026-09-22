package com.robertvokac.lexicon.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.BuildConfig
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.api.ServerUrl
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.storage.SettingsStore
import com.robertvokac.lexicon.storage.ThemePreference
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.SectionHeader
import com.robertvokac.lexicon.ui.common.TextInputDialog
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Client preferences of this installation and the session it holds. */
class SettingsViewModel(private val container: AppContainer) : ViewModel() {
    val theme: StateFlow<ThemePreference> =
        container.settings.theme.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), ThemePreference.System)
    val pageSize: StateFlow<Int> =
        container.settings.pageSize.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), SettingsStore.DEFAULT_PAGE_SIZE)
    val sessionInfo: StateFlow<SettingsStore.SessionInfo> =
        container.settings.sessionInfo.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), SettingsStore.SessionInfo(0, 0))
    val session: StateFlow<SessionState> = container.sessions.state

    fun setTheme(theme: ThemePreference) {
        viewModelScope.launch { container.settings.setTheme(theme) }
    }

    fun setPageSize(size: Int) {
        viewModelScope.launch { container.settings.setPageSize(size) }
    }

    /** Null when the URL is usable, or why not. */
    fun serverUrlProblem(input: String): String? =
        (ServerUrl.parse(input, container.allowCleartextDevelopmentHosts) as? ServerUrl.Parsed.Invalid)?.message

    /** Another server means another sign-in; the current session ends. */
    fun changeServer(input: String) {
        val parsed = ServerUrl.parse(input, container.allowCleartextDevelopmentHosts) as? ServerUrl.Parsed.Valid ?: return
        container.sessions.changeServer(parsed.url)
    }

    fun logout() {
        container.sessions.logout()
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(viewModel: SettingsViewModel, onBack: () -> Unit) {
    val theme by viewModel.theme.collectAsStateWithLifecycle()
    val pageSize by viewModel.pageSize.collectAsStateWithLifecycle()
    val sessionInfo by viewModel.sessionInfo.collectAsStateWithLifecycle()
    val session by viewModel.session.collectAsStateWithLifecycle()
    val signedIn = session as? SessionState.SignedIn
    var changingServer by rememberSaveable { mutableStateOf(false) }
    var confirmLogout by rememberSaveable { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
    ) { padding ->
        Column(
            Modifier
                .padding(padding)
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            SectionHeader("Server")
            Text(signedIn?.identity?.server?.value ?: "Not signed in")
            OutlinedButton(onClick = { changingServer = true }) { Text("Change server…") }

            SectionHeader("Session")
            Text(signedIn?.let { "Signed in as ${it.identity.username}" } ?: "Not signed in")
            if (sessionInfo.idleTimeoutSeconds > 0) {
                Text(
                    "The server ends a session after ${duration(sessionInfo.idleTimeoutSeconds)} without use, " +
                        "and after ${duration(sessionInfo.absoluteLifetimeSeconds)} at most. A new password on the server ends it too.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Text(
                "The session token is stored encrypted with a key in Android Keystore. Your password is never stored.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            OutlinedButton(onClick = { confirmLogout = true }, enabled = signedIn != null) { Text("Log out") }

            SectionHeader("Appearance")
            Column(Modifier.selectableGroup()) {
                ThemePreference.entries.forEach { option ->
                    RadioRow(option.label, selected = theme == option) { viewModel.setTheme(option) }
                }
            }

            SectionHeader("Item list")
            Text("Items loaded per page", style = MaterialTheme.typography.bodyMedium)
            Column(Modifier.selectableGroup()) {
                SettingsStore.PAGE_SIZES.forEach { size ->
                    RadioRow(size.toString(), selected = pageSize == size) { viewModel.setPageSize(size) }
                }
            }

            SectionHeader("About")
            Text("Lexicon for Android ${BuildConfig.VERSION_NAME}")
            Text("Client API version ${LexiconApi.API_VERSION}" + (signedIn?.let { ", server API version ${it.serverApiVersion}" } ?: ""))
            Text(
                "This app keeps no Lexicon database: LexiconServer is the source of truth. Blob maintenance " +
                    "(scanning and garbage collection) remains a local desktop and server administration feature.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(bottom = 24.dp),
            )
        }
    }

    if (changingServer) {
        TextInputDialog(
            title = "Change server",
            label = "Server URL",
            initial = signedIn?.identity?.server?.value ?: "https://",
            confirmLabel = "Change",
            supportingText = "You will sign in again. The current session ends.",
            keyboardOptions = com.robertvokac.lexicon.ui.common.LiteralTextKeyboard.copy(keyboardType = KeyboardType.Uri),
            validate = viewModel::serverUrlProblem,
            onConfirm = {
                changingServer = false
                viewModel.changeServer(it)
            },
            onDismiss = { changingServer = false },
        )
    }
    if (confirmLogout) {
        ConfirmDialog(
            title = "Log out",
            message = "Log out of ${signedIn?.identity?.server?.value.orEmpty()}? Unsaved changes in open editors are discarded.",
            confirmLabel = "Log out",
            onConfirm = {
                confirmLogout = false
                viewModel.logout()
            },
            onDismiss = { confirmLogout = false },
        )
    }
}

@Composable
private fun RadioRow(label: String, selected: Boolean, onSelect: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .selectable(selected = selected, onClick = onSelect, role = Role.RadioButton)
            .padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(selected = selected, onClick = null)
        Text(label, modifier = Modifier.padding(start = 12.dp))
    }
}

private fun duration(seconds: Long): String = when {
    seconds % 86_400 == 0L -> "${seconds / 86_400} day${if (seconds == 86_400L) "" else "s"}"
    seconds % 3_600 == 0L -> "${seconds / 3_600} hour${if (seconds == 3_600L) "" else "s"}"
    seconds % 60 == 0L -> "${seconds / 60} minutes"
    else -> "$seconds seconds"
}
