package com.robertvokac.lexicon.ui.types

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.VerticalDivider
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
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.FieldWrite
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemType
import com.robertvokac.lexicon.model.TypeWrite
import com.robertvokac.lexicon.ui.common.Choice
import com.robertvokac.lexicon.ui.common.ChoiceField
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.WholeNumber

/**
 * The type list. On a wide screen the selected type's fields sit beside it;
 * on a phone a type opens its own screen through [onOpenType].
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TypesScreen(viewModel: TypesViewModel, wide: Boolean, onBack: () -> Unit, onOpenType: (Int) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var addingType by rememberSaveable { mutableStateOf(false) }
    MessageEffect(state.message, snackbar, viewModel::messageShown)

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Types") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { addingType = true },
                icon = { Icon(Icons.Filled.Add, contentDescription = null) },
                text = { Text("Add type") },
                modifier = Modifier.semantics { contentDescription = "Add type" },
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.error != null -> ErrorBox(state.error.orEmpty(), onRetry = viewModel::load, modifier = Modifier.padding(padding))
            else -> Row(Modifier.padding(padding).fillMaxSize()) {
                LazyColumn(Modifier.weight(if (wide) 0.4f else 1f).fillMaxHeight(), contentPadding = PaddingValues(bottom = 88.dp)) {
                    if (state.types.isEmpty()) {
                        item { Text("No types yet.", modifier = Modifier.padding(16.dp)) }
                    }
                    items(state.types, key = { it.id ?: 0 }) { type ->
                        val selected = wide && type.id == state.selectedTypeId
                        Column(
                            Modifier
                                .fillMaxWidth()
                                .background(if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface)
                                .clickable(role = Role.Button, onClickLabel = "Show fields") {
                                    val id = type.id ?: return@clickable
                                    if (wide) viewModel.select(id) else onOpenType(id)
                                }
                                .semantics { this.selected = selected }
                                .padding(16.dp),
                        ) {
                            Text(type.name, style = MaterialTheme.typography.titleMedium)
                            Text(
                                type.scopeLabel + if (type.description.isNotEmpty()) " · ${type.description}" else "",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 2,
                            )
                        }
                        HorizontalDivider()
                    }
                }
                if (wide) {
                    VerticalDivider()
                    Column(Modifier.weight(0.6f).fillMaxHeight()) {
                        val selected = state.selectedType
                        if (selected == null) {
                            Text("Select a type to see its fields.", modifier = Modifier.padding(16.dp))
                        } else {
                            TypeHeader(selected, state.busy, viewModel)
                            FieldsPane(state, viewModel)
                        }
                    }
                }
            }
        }
    }

    if (addingType) {
        TypeDialog(
            title = "Add type",
            initial = null,
            groups = state.groups,
            busy = state.busy,
            onConfirm = { write -> viewModel.saveType(null, write) { addingType = false } },
            onDismiss = { addingType = false },
        )
    }
    TypesConfirmationDialog(state, viewModel)
}

/** One type with its fields: the phone's second level of the type manager. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TypeDetailScreen(viewModel: TypesViewModel, typeId: Int, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var editingType by rememberSaveable { mutableStateOf(false) }
    var addingField by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(typeId) { if (state.selectedTypeId != typeId) viewModel.select(typeId) }
    MessageEffect(state.message, snackbar, viewModel::messageShown)
    val type = state.types.firstOrNull { it.id == typeId }
    // A deleted type has nothing left to show.
    LaunchedEffect(type, state.loading) { if (type == null && !state.loading) onBack() }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(type?.name ?: "Type") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
                actions = {
                    if (type != null) {
                        IconButton(onClick = { editingType = true }, enabled = !state.busy) {
                            Icon(Icons.Filled.Edit, contentDescription = "Edit type")
                        }
                        IconButton(onClick = { viewModel.requestDeleteType(type) }, enabled = !state.busy) {
                            Icon(Icons.Filled.Delete, contentDescription = "Delete type")
                        }
                    }
                },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { addingField = true }) { Icon(Icons.Filled.Add, contentDescription = "Add field") }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            if (type != null) {
                Text(
                    "Available in: ${type.scopeLabel}" + if (type.description.isNotEmpty()) "\n${type.description}" else "",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(16.dp),
                )
                FieldsPane(state, viewModel, showAdd = false)
            }
        }
    }

    if (editingType && type != null) {
        TypeDialog(
            title = "Edit type",
            initial = type,
            groups = state.groups,
            busy = state.busy,
            onConfirm = { write -> viewModel.saveType(type.id, write) { editingType = false } },
            onDismiss = { editingType = false },
        )
    }
    if (addingField) {
        FieldDialog(
            title = "Add field",
            initial = null,
            nextPosition = viewModel.nextFieldPosition(),
            busy = state.busy,
            onConfirm = { write -> viewModel.saveField(null, write) { addingField = false } },
            onDismiss = { addingField = false },
        )
    }
    TypesConfirmationDialog(state, viewModel)
}

@Composable
private fun MessageEffect(message: String?, snackbar: SnackbarHostState, shown: () -> Unit) {
    if (message == null) return
    LaunchedEffect(message) {
        shown()
        snackbar.showSnackbar(message)
    }
}

@Composable
private fun TypeHeader(type: ItemType, busy: Boolean, viewModel: TypesViewModel) {
    var editing by rememberSaveable { mutableStateOf(false) }
    val state by viewModel.state.collectAsStateWithLifecycle()
    Row(Modifier.fillMaxWidth().padding(start = 16.dp, top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(type.name, style = MaterialTheme.typography.titleLarge)
            Text("Available in: ${type.scopeLabel}", style = MaterialTheme.typography.bodySmall)
        }
        IconButton(onClick = { editing = true }, enabled = !busy) { Icon(Icons.Filled.Edit, contentDescription = "Edit type") }
        IconButton(onClick = { viewModel.requestDeleteType(type) }, enabled = !busy) { Icon(Icons.Filled.Delete, contentDescription = "Delete type") }
    }
    if (editing) {
        TypeDialog(
            title = "Edit type",
            initial = type,
            groups = state.groups,
            busy = busy,
            onConfirm = { write -> viewModel.saveType(type.id, write) { editing = false } },
            onDismiss = { editing = false },
        )
    }
}

@Composable
private fun FieldsPane(state: TypesState, viewModel: TypesViewModel, showAdd: Boolean = true) {
    var editing by remember { mutableStateOf<ItemField?>(null) }
    var adding by rememberSaveable { mutableStateOf(false) }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 88.dp)) {
        item {
            Text("Fields", style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(16.dp))
        }
        when {
            state.fieldsLoading && state.fields.isEmpty() -> item { Text("Loading…", modifier = Modifier.padding(16.dp)) }
            state.fieldsError != null -> item { Text(state.fieldsError, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(16.dp)) }
            state.fields.isEmpty() -> item { Text("This type has no fields yet.", modifier = Modifier.padding(16.dp)) }
        }
        items(state.fields, key = { it.id ?: 0 }) { field ->
            Row(
                Modifier
                    .fillMaxWidth()
                    .clickable(role = Role.Button, onClickLabel = "Edit") { editing = field }
                    .padding(start = 16.dp, top = 6.dp, bottom = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text(field.name, style = MaterialTheme.typography.bodyLarge)
                    Text(
                        "Position ${field.position} · ${field.dataType.name}" +
                            if (field.dataType == FieldDataType.Enum) ": ${field.enumOptions.joinToString(", ")}" else "",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                IconButton(onClick = { viewModel.requestDeleteField(field) }, enabled = !state.busy) {
                    Icon(Icons.Filled.Delete, contentDescription = "Delete field ${field.name}")
                }
            }
            HorizontalDivider()
        }
        if (showAdd) {
            item {
                OutlinedButton(onClick = { adding = true }, modifier = Modifier.padding(16.dp)) {
                    Icon(Icons.Filled.Add, contentDescription = null)
                    Text("Add field")
                }
            }
        }
    }
    if (adding || editing != null) {
        val current = editing
        FieldDialog(
            title = if (current == null) "Add field" else "Edit field",
            initial = current,
            nextPosition = viewModel.nextFieldPosition(),
            busy = state.busy,
            onConfirm = { write ->
                viewModel.saveField(current, write) {
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
}

@Composable
private fun TypesConfirmationDialog(state: TypesState, viewModel: TypesViewModel) {
    val confirmation = state.confirmation ?: return
    ConfirmDialog(
        title = when (confirmation) {
            is TypesConfirmation.DeleteType -> "Delete type"
            is TypesConfirmation.DeleteField -> "Delete field"
            is TypesConfirmation.ChangeField -> "Change field data type"
        },
        message = confirmation.message,
        confirmLabel = when (confirmation) {
            is TypesConfirmation.ChangeField -> "Change"
            else -> "Delete"
        },
        onConfirm = viewModel::confirm,
        onDismiss = viewModel::cancelConfirmation,
        destructive = true,
    )
}

@Composable
private fun TypeDialog(
    title: String,
    initial: ItemType?,
    groups: List<Group>,
    busy: Boolean,
    onConfirm: (TypeWrite) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by rememberSaveable { mutableStateOf(initial?.name.orEmpty()) }
    var description by rememberSaveable { mutableStateOf(initial?.description.orEmpty()) }
    var groupId by rememberSaveable { mutableStateOf(initial?.groupId) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = { if (!busy) onDismiss() },
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.verticalScroll(rememberScrollState())) {
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
                ChoiceField(
                    label = "Available in",
                    choices = listOf(Choice<Int?>(null, "All groups")) + groups.map { Choice(it.id, it.name) },
                    selected = groupId,
                    onSelected = { groupId = it },
                )
            }
        },
        confirmButton = {
            TextButton(enabled = !busy, onClick = {
                if (name.isBlank()) error = "Type name cannot be empty." else onConfirm(TypeWrite(name.trim(), description.trim(), groupId))
            }) { Text(if (busy) "Saving…" else "Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !busy) { Text("Cancel") } },
    )
}

@Composable
private fun FieldDialog(
    title: String,
    initial: ItemField?,
    nextPosition: Int,
    busy: Boolean,
    onConfirm: (FieldWrite) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by rememberSaveable { mutableStateOf(initial?.name.orEmpty()) }
    var dataType by rememberSaveable { mutableStateOf(initial?.dataType ?: FieldDataType.Text) }
    var position by rememberSaveable { mutableStateOf((initial?.position ?: nextPosition).toString()) }
    var options by rememberSaveable { mutableStateOf(initial?.enumOptions.orEmpty().joinToString("\n")) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = { if (!busy) onDismiss() },
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.verticalScroll(rememberScrollState())) {
                SyncedTextField(
                    value = name,
                    onValueChange = {
                        name = it
                        error = null
                    },
                    label = "Name",
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                ChoiceField(
                    label = "Data type",
                    choices = FieldDataType.entries.map { Choice(it, it.name) },
                    selected = dataType,
                    onSelected = {
                        dataType = it
                        error = null
                    },
                )
                SyncedTextField(
                    value = position,
                    onValueChange = { position = it },
                    accept = WholeNumber,
                    label = "Position",
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = options,
                    onValueChange = {
                        options = it
                        error = null
                    },
                    label = "Enum options",
                    placeholder = "One option per line",
                    enabled = dataType == FieldDataType.Enum,
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth(),
                )
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
        },
        confirmButton = {
            TextButton(enabled = !busy, onClick = {
                val enumOptions = if (dataType == FieldDataType.Enum) {
                    options.lines().map { it.trim() }.filter { it.isNotEmpty() }
                } else {
                    emptyList()
                }
                error = when {
                    name.isBlank() -> "Field name cannot be empty."
                    dataType == FieldDataType.Enum && enumOptions.isEmpty() -> "Enum fields need at least one option."
                    else -> null
                }
                if (error == null) onConfirm(FieldWrite(name.trim(), dataType, position.toIntOrNull() ?: 0, enumOptions))
            }) { Text(if (busy) "Saving…" else "Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !busy) { Text("Cancel") } },
    )
}
