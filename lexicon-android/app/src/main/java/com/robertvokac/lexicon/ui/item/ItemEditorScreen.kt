package com.robertvokac.lexicon.ui.item

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
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
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.ui.common.Choice
import com.robertvokac.lexicon.ui.common.ChoiceField
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBanner
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LiteralTextKeyboard
import com.robertvokac.lexicon.ui.common.LoadingBox

/**
 * The six-tab item editor as one full screen. [onClose] is called with the
 * saved item's ID after a save, or null when the edit is abandoned.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ItemEditorScreen(viewModel: ItemEditorViewModel, onClose: (savedItemId: Int?) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var confirmDiscard by rememberSaveable { mutableStateOf(false) }

    val close = {
        if (viewModel.hasUnsavedChanges()) confirmDiscard = true else onClose(null)
    }
    BackHandler(onBack = close)

    val save = state.save
    LaunchedEffect(save) { if (save is SaveStatus.Saved) onClose(save.itemId) }
    state.message?.let { message ->
        LaunchedEffect(message) {
            viewModel.messageShown()
            snackbar.showSnackbar(message)
        }
    }

    Scaffold(
        modifier = Modifier.onPreviewKeyEvent { event ->
            // Ctrl+S saves from a hardware keyboard.
            if (event.type == KeyEventType.KeyDown && event.isCtrlPressed && event.key == Key.S) {
                viewModel.save()
                true
            } else {
                false
            }
        },
        topBar = {
            TopAppBar(
                title = { Text(if (state.isNew) "Add item" else "Edit item") },
                navigationIcon = {
                    IconButton(onClick = close) { Icon(Icons.Filled.Close, contentDescription = "Close editor") }
                },
                actions = {
                    if (state.saving) {
                        CircularProgressIndicator(
                            Modifier
                                .padding(end = 16.dp)
                                .size(24.dp)
                                .semantics { contentDescription = "Saving" },
                        )
                    } else {
                        Button(
                            onClick = viewModel::save,
                            enabled = !state.loading && state.loadError == null,
                            modifier = Modifier.padding(end = 8.dp),
                        ) { Text("Save") }
                    }
                },
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize().imePadding()) {
            when {
                state.loading -> LoadingBox()
                state.loadError != null -> ErrorBox(state.loadError.orEmpty(), onRetry = viewModel::retryLoad)
                else -> {
                    SaveStatusLine(state.save)
                    val tabs = EditorTab.entries
                    PrimaryScrollableTabRow(selectedTabIndex = tabs.indexOf(state.tab), edgePadding = 8.dp) {
                        tabs.forEach { tab ->
                            val enabled = tab != EditorTab.Values || state.fields.typeId != null
                            Tab(
                                selected = state.tab == tab,
                                onClick = { viewModel.selectTab(tab) },
                                enabled = enabled,
                                text = { Text(tabLabel(tab, state)) },
                            )
                        }
                    }
                    Box(Modifier.weight(1f).fillMaxWidth()) {
                        when (state.tab) {
                            EditorTab.General -> GeneralTab(state, viewModel)
                            EditorTab.Content -> ContentTab(viewModel)
                            EditorTab.Values -> ValuesTab(state, viewModel)
                            EditorTab.Metadata -> MetadataTab(state, viewModel)
                            EditorTab.Links -> LinksTab(state, viewModel, incoming = false)
                            EditorTab.Backlinks -> LinksTab(state, viewModel, incoming = true)
                        }
                    }
                }
            }
        }
    }

    if (confirmDiscard) {
        ConfirmDialog(
            title = "Unsaved changes",
            message = "Discard the changes to this item?",
            confirmLabel = "Discard",
            dismissLabel = "Keep editing",
            onConfirm = {
                confirmDiscard = false
                onClose(null)
            },
            onDismiss = { confirmDiscard = false },
            destructive = true,
        )
    }
    state.typeChange?.let { request ->
        ConfirmDialog(
            title = "Change type",
            message = "Changing the type will remove ${request.affected} field value(s) from this item. Continue?",
            confirmLabel = "Change type",
            onConfirm = viewModel::confirmTypeChange,
            onDismiss = viewModel::cancelTypeChange,
            destructive = true,
        )
    }
    state.saveNeedsTypeConfirmation?.let { count ->
        ConfirmDialog(
            title = "Change type",
            message = "Changing the type will remove $count saved field value(s) from this item. Continue?",
            confirmLabel = "Save",
            onConfirm = viewModel::confirmSaveTypeChange,
            onDismiss = viewModel::cancelSaveTypeChange,
            destructive = true,
        )
    }
}

private fun tabLabel(tab: EditorTab, state: EditorState): String = when (tab) {
    EditorTab.Links -> if (state.fields.links.isEmpty()) tab.label else "${tab.label} (${state.fields.links.size})"
    EditorTab.Backlinks -> if (state.fields.backlinks.isEmpty()) tab.label else "${tab.label} (${state.fields.backlinks.size})"
    else -> tab.label
}

@Composable
private fun SaveStatusLine(save: SaveStatus) {
    val modifier = Modifier
        .fillMaxWidth()
        .padding(horizontal = 16.dp, vertical = 4.dp)
        .semantics { liveRegion = LiveRegionMode.Polite }
    when (save) {
        SaveStatus.Saving -> Text("Saving…", style = MaterialTheme.typography.labelLarge, modifier = modifier)
        is SaveStatus.Saved -> Text("Saved", style = MaterialTheme.typography.labelLarge, modifier = modifier)
        is SaveStatus.Failed -> ErrorBanner("Not saved: ${save.message}", modifier)
        SaveStatus.Idle -> Unit
    }
}

@Composable
private fun GeneralTab(state: EditorState, viewModel: ItemEditorViewModel) {
    val fields = state.fields
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        ChoiceField(
            label = "Group",
            choices = state.groups.map { Choice<Int?>(it.id, it.name) },
            selected = fields.groupId,
            onSelected = viewModel::setGroup,
        )
        ChoiceField(
            label = "Type",
            choices = listOf(Choice<Int?>(null, "None")) + state.types.map { Choice(it.id, it.displayName, it.description.ifEmpty { null }) },
            selected = fields.typeId,
            onSelected = viewModel::setType,
        )
        OutlinedTextField(
            value = fields.title,
            onValueChange = viewModel::setTitle,
            label = { Text("Title") },
            singleLine = true,
            isError = fields.title.isBlank() && state.save is SaveStatus.Failed,
            supportingText = if (fields.title.isBlank() && state.save is SaveStatus.Failed) {
                { Text("Title cannot be empty.") }
            } else {
                null
            },
            keyboardOptions = LiteralTextKeyboard,
            modifier = Modifier.fillMaxWidth(),
        )
        OutlinedTextField(
            value = fields.disambiguation,
            onValueChange = viewModel::setDisambiguation,
            label = { Text("Disambiguation") },
            singleLine = true,
            supportingText = { Text("Tells apart items with the same title in one group.") },
            keyboardOptions = LiteralTextKeyboard,
            modifier = Modifier.fillMaxWidth(),
        )
        ChoiceField(
            label = "Status",
            choices = ItemStatus.entries.map { Choice(it, it.label) },
            selected = fields.status,
            onSelected = viewModel::setStatus,
        )
        ChoiceField(
            label = "Understanding",
            choices = UnderstandingLevel.entries.map { Choice(it, it.label, it.description) },
            selected = fields.understanding,
            onSelected = viewModel::setUnderstanding,
            supportingText = fields.understanding.description,
        )
        Row(
            Modifier
                .fillMaxWidth()
                .toggleable(value = fields.pinned, role = Role.Switch, onValueChange = viewModel::setPinned)
                .padding(vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Pinned", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Switch(checked = fields.pinned, onCheckedChange = null)
        }
    }
}
