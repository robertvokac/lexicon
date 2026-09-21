package com.robertvokac.lexicon.ui.groups

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
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
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.GroupWrite
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.WholeNumber
import com.robertvokac.lexicon.ui.common.userMessage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class GroupsState(
    val loading: Boolean = true,
    val error: String? = null,
    val groups: List<Group> = emptyList(),
    val busy: Boolean = false,
    val message: String? = null,
)

/** The counterpart of GroupManagerDialog. Groups stay flat; Default stays Default. */
class GroupsViewModel(private val container: AppContainer) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(GroupsState())
    val state: StateFlow<GroupsState> = _state.asStateFlow()

    init {
        load()
    }

    fun load() {
        viewModelScope.launch {
            try {
                val groups = api.groups()
                _state.update { it.copy(loading = false, error = null, groups = groups) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage()) }
            }
        }
    }

    /** The position a new group gets: after the last one. */
    fun nextPosition(): Int = (_state.value.groups.maxOfOrNull { it.position } ?: -1) + 1

    /** Creates or updates; [onDone] runs after success so the dialog can close. */
    fun save(id: Int?, group: GroupWrite, onDone: () -> Unit) {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                if (id == null) api.createGroup(group) else api.updateGroup(id, group)
                _state.update { it.copy(busy = false, groups = api.groups()) }
                container.dataChanges.groupsChanged()
                onDone()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun delete(group: Group) {
        val id = group.id ?: return
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                api.deleteGroup(id)
                _state.update { it.copy(busy = false, groups = api.groups(), message = "Deleted group '${group.name}'.") }
                container.dataChanges.groupsChanged()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun messageShown() = _state.update { it.copy(message = null) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GroupsScreen(viewModel: GroupsViewModel, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var editing by remember { mutableStateOf<Group?>(null) }
    var adding by rememberSaveable { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<Group?>(null) }

    state.message?.let { message ->
        LaunchedEffect(message) {
            viewModel.messageShown()
            snackbar.showSnackbar(message)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Groups") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { adding = true }) { Icon(Icons.Filled.Add, contentDescription = "Add group") }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.error != null -> ErrorBox(state.error.orEmpty(), onRetry = viewModel::load, modifier = Modifier.padding(padding))
            else -> LazyColumn(Modifier.padding(padding).fillMaxSize(), contentPadding = PaddingValues(bottom = 88.dp)) {
                item {
                    Text(
                        "Lower positions appear first. Deleting a group also deletes every item in it.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(16.dp),
                    )
                }
                items(state.groups, key = { it.id ?: 0 }) { group ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clickable(role = Role.Button, onClickLabel = "Edit") { editing = group }
                            .padding(start = 16.dp, top = 8.dp, bottom = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(group.name, style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Position ${group.position}" + if (group.description.isNotEmpty()) " · ${group.description}" else "",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 2,
                            )
                        }
                        IconButton(onClick = { deleting = group }, enabled = !state.busy) {
                            Icon(Icons.Filled.Delete, contentDescription = "Delete group ${group.name}")
                        }
                    }
                    HorizontalDivider()
                }
            }
        }
    }

    if (adding || editing != null) {
        val current = editing
        GroupDialog(
            title = if (current == null) "Add group" else "Edit group",
            initial = current ?: Group(name = "", position = viewModel.nextPosition()),
            busy = state.busy,
            onConfirm = { write ->
                viewModel.save(current?.id, write) {
                    adding = false
                    editing = null
                }
            },
            onDismiss = {
                adding = false
                editing = null
            },
        )
    }
    deleting?.let { group ->
        ConfirmDialog(
            title = "Delete group",
            message = "Delete group '${group.name}'? All items inside it will also be deleted.",
            confirmLabel = "Delete",
            onConfirm = {
                deleting = null
                viewModel.delete(group)
            },
            onDismiss = { deleting = null },
            destructive = true,
        )
    }
}

@Composable
private fun GroupDialog(title: String, initial: Group, busy: Boolean, onConfirm: (GroupWrite) -> Unit, onDismiss: () -> Unit) {
    var name by rememberSaveable { mutableStateOf(initial.name) }
    var description by rememberSaveable { mutableStateOf(initial.description) }
    var position by rememberSaveable { mutableStateOf(initial.position.toString()) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = { if (!busy) onDismiss() },
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SyncedTextField(
                    value = name,
                    onValueChange = {
                        name = it
                        error = null
                    },
                    label = "Name",
                    singleLine = true,
                    isError = error != null,
                    supportingText = error,
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = description,
                    onValueChange = { description = it },
                    label = "Description",
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = position,
                    onValueChange = { position = it },
                    accept = WholeNumber,
                    label = "Position",
                    singleLine = true,
                    supportingText = "Lower positions appear first. Groups with the same position are sorted by name.",
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(
                enabled = !busy,
                onClick = {
                    if (name.isBlank()) {
                        error = "Group name cannot be empty."
                    } else {
                        onConfirm(GroupWrite(name.trim(), description.trim(), position.toIntOrNull() ?: 0))
                    }
                },
            ) { Text(if (busy) "Saving…" else "Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !busy) { Text("Cancel") } },
    )
}
