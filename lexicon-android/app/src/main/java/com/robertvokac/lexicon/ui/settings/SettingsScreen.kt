package com.robertvokac.lexicon.ui.settings

import android.net.Uri
import android.provider.DocumentsContract
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.selection.toggleable
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
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.BuildConfig
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.api.ServerUrl
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.model.ImportReport
import com.robertvokac.lexicon.storage.SettingsStore
import com.robertvokac.lexicon.storage.ThemePreference
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.MessageDialog
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.item.BlobTransfer
import com.robertvokac.lexicon.ui.common.SectionHeader
import com.robertvokac.lexicon.ui.common.TextInputDialog
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.FileNotFoundException
import java.io.IOException
import java.time.LocalDate

/** An export or import on its way, or how the last one ended. */
data class ExchangeState(
    val busy: Boolean = false,
    val status: String? = null,
    /** The last import's report, shown once. */
    val report: ImportReport? = null,
)

/** Client preferences of this installation and the session it holds. */
class SettingsViewModel(private val container: AppContainer) : ViewModel() {
    private val transfer = BlobTransfer(container.api, container.contentResolver)
    private val _exchange = MutableStateFlow(ExchangeState())
    val exchange: StateFlow<ExchangeState> = _exchange.asStateFlow()

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
        (ServerUrl.parse(input, container.allowCleartextDevelopmentHosts,
            allowHttpForTesting = container.allowCleartextDevelopmentHosts) as? ServerUrl.Parsed.Invalid)?.message

    /** Another server means another sign-in; the current session ends. */
    fun changeServer(input: String) {
        // This only chooses a URL. The login screen still requires the HTTP checkbox before credentials are sent.
        val parsed = ServerUrl.parse(input, container.allowCleartextDevelopmentHosts,
            allowHttpForTesting = container.allowCleartextDevelopmentHosts) as? ServerUrl.Parsed.Valid ?: return
        container.sessions.changeServer(parsed.url)
    }

    fun logout() {
        container.sessions.logout()
    }

    /** Writes the whole dictionary into the document the person created. */
    fun exportTo(target: Uri, includeFiles: Boolean) {
        if (_exchange.value.busy) return
        _exchange.value = ExchangeState(busy = true, status = "Exporting…")
        viewModelScope.launch {
            val resolver = container.contentResolver
            try {
                container.api.exportDictionary(
                    includeFiles,
                    output = { resolver.openOutputStream(target, "wt") ?: throw FileNotFoundException("The document cannot be written.") },
                    onProgress = { received, _ ->
                        _exchange.value = ExchangeState(busy = true, status = "Exporting… ${BlobTransfer.formatSize(received)}")
                    },
                )
                _exchange.value = ExchangeState(status = "The dictionary was exported.")
            } catch (failure: CancellationException) {
                throw failure
            } catch (failure: Exception) {
                // A document left incomplete is deleted again.
                withContext(NonCancellable + Dispatchers.IO) {
                    runCatching { DocumentsContract.deleteDocument(resolver, target) }
                }
                val message = (failure as? ApiException)?.userMessage() ?: failure.message
                _exchange.value = ExchangeState(status = "Not exported: $message")
            }
        }
    }

    /** Merges the export the person picked into the dictionary. */
    fun importFrom(source: Uri) {
        if (_exchange.value.busy) return
        _exchange.value = ExchangeState(busy = true, status = "Importing…")
        viewModelScope.launch {
            val resolver = container.contentResolver
            try {
                val size = transfer.describe(source).size
                // The server wants the size up front; a provider that does not
                // know it is read into memory first.
                val buffered = if (size < 0) {
                    withContext(Dispatchers.IO) {
                        (resolver.openInputStream(source) ?: throw FileNotFoundException("The document cannot be read.")).use { it.readBytes() }
                    }
                } else {
                    null
                }
                val report = container.api.importDictionary(
                    size = buffered?.size?.toLong() ?: size,
                    open = {
                        buffered?.inputStream()
                            ?: resolver.openInputStream(source)
                            ?: throw FileNotFoundException("The document cannot be read.")
                    },
                    onProgress = { sent, total ->
                        val progress = if (total > 0) " ${sent * 100 / total}%" else ""
                        _exchange.value = ExchangeState(busy = true, status = "Importing…$progress")
                    },
                )
                _exchange.value = ExchangeState(status = report.summary, report = report)
                container.dataChanges.groupsChanged()
                container.dataChanges.typesChanged()
                container.dataChanges.itemChanged(null)
            } catch (failure: CancellationException) {
                throw failure
            } catch (failure: ApiException) {
                _exchange.value = ExchangeState(status = "Nothing was imported: ${failure.userMessage() ?: "sign in again."}")
            } catch (failure: IOException) {
                _exchange.value = ExchangeState(status = "The file could not be read: ${failure.message}")
            } catch (failure: SecurityException) {
                _exchange.value = ExchangeState(status = "The file could not be read: ${failure.message}")
            }
        }
    }

    fun reportShown() = _exchange.update { it.copy(report = null) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(viewModel: SettingsViewModel, onBack: () -> Unit) {
    val theme by viewModel.theme.collectAsStateWithLifecycle()
    val pageSize by viewModel.pageSize.collectAsStateWithLifecycle()
    val sessionInfo by viewModel.sessionInfo.collectAsStateWithLifecycle()
    val session by viewModel.session.collectAsStateWithLifecycle()
    val signedIn = session as? SessionState.SignedIn
    val exchange by viewModel.exchange.collectAsStateWithLifecycle()
    var changingServer by rememberSaveable { mutableStateOf(false) }
    var confirmLogout by rememberSaveable { mutableStateOf(false) }
    var includeFiles by rememberSaveable { mutableStateOf(true) }
    var pendingImport by rememberSaveable { mutableStateOf<Uri?>(null) }
    val exportLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        if (uri != null) viewModel.exportTo(uri, includeFiles)
    }
    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        pendingImport = uri
    }

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

            SectionHeader("Export and import")
            Text(
                "The whole dictionary as one file, for a backup or another Lexicon: groups, types, items and links.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = 48.dp)
                    .toggleable(value = includeFiles, onValueChange = { includeFiles = it }, role = Role.Switch),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Include attached files", modifier = Modifier.weight(1f))
                Switch(checked = includeFiles, onCheckedChange = null)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(
                    onClick = { exportLauncher.launch("lexicon-${LocalDate.now()}.json") },
                    enabled = signedIn != null && !exchange.busy,
                ) { Text("Export…") }
                OutlinedButton(
                    onClick = { importLauncher.launch(arrayOf("application/json", "text/plain", "application/octet-stream")) },
                    enabled = signedIn != null && !exchange.busy,
                ) { Text("Import…") }
            }
            exchange.status?.let {
                Text(it, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
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
    pendingImport?.let { uri ->
        ConfirmDialog(
            title = "Import",
            message = "Merge this export into the dictionary? Groups, types and fields are matched by name. " +
                "Items that are already here, with the same group, title and disambiguation, are left as they are.",
            confirmLabel = "Import",
            onConfirm = {
                pendingImport = null
                viewModel.importFrom(uri)
            },
            onDismiss = { pendingImport = null },
        )
    }
    exchange.report?.takeIf { it.warnings.isNotEmpty() }?.let { report ->
        MessageDialog(
            title = "Import",
            message = report.summary + "\n\n" + report.warnings.joinToString("\n") { "• $it" },
            onDismiss = viewModel::reportShown,
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
