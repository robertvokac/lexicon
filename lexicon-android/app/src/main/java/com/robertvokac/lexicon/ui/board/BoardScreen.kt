package com.robertvokac.lexicon.ui.board

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.Choice
import com.robertvokac.lexicon.ui.common.ChoiceField
import com.robertvokac.lexicon.ui.common.ErrorBanner
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.TextInputDialog
import com.robertvokac.lexicon.ui.item.MarkdownEditor
import com.robertvokac.lexicon.ui.markdown.Markdown
import com.robertvokac.lexicon.ui.markdown.MarkdownBlocks
import com.robertvokac.lexicon.ui.markdown.MdBlock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BoardScreen(viewModel: BoardViewModel, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    var confirmDiscard by rememberSaveable { mutableStateOf(false) }
    var nameDialog by rememberSaveable { mutableStateOf<String?>(null) }
    var confirmDelete by rememberSaveable { mutableStateOf(false) }
    val selected = state.selected
    val close = {
        if (state.editing && viewModel.hasUnsavedChanges()) confirmDiscard = true else onBack()
    }
    BackHandler(onBack = close)

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        if (state.editing) "Edit Board — ${selected?.name.orEmpty()}"
                        else "Board — ${selected?.name.orEmpty()}",
                    )
                },
                navigationIcon = {
                    IconButton(onClick = close) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    when {
                        state.saving -> CircularProgressIndicator(Modifier.padding(end = 16.dp))
                        state.editing -> Button(onClick = viewModel::save) { Text("Save") }
                        !state.loading && state.loadError == null -> TextButton(onClick = viewModel::edit) { Text("Edit") }
                    }
                },
            )
        },
    ) { padding ->
        Box(Modifier.padding(padding).fillMaxSize()) {
            when {
                state.loading -> LoadingBox()
                state.loadError != null -> ErrorBox(state.loadError.orEmpty(), onRetry = viewModel::retry)
                state.editing -> {
                    Box(Modifier.fillMaxSize()) {
                        MarkdownEditor(
                            content = viewModel.editor,
                            onFormat = viewModel::format,
                            onItemLink = viewModel::insertItemLink,
                            lastCodeLanguage = viewModel::lastCodeLanguage,
                        )
                        state.saveError?.let { ErrorBanner("Not saved: $it") }
                    }
                }
                else -> Column(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
                    ChoiceField(
                        label = "Board",
                        choices = state.boards.map { Choice(it.id, it.name) },
                        selected = state.selectedId,
                        onSelected = viewModel::select,
                        enabled = !state.saving,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        TextButton(onClick = { nameDialog = "new" }, enabled = !state.saving) { Text("New") }
                        TextButton(onClick = { nameDialog = "rename" }, enabled = !state.saving) { Text("Rename") }
                        TextButton(
                            onClick = { confirmDelete = true },
                            enabled = !state.saving && state.boards.size > 1,
                        ) { Text("Delete") }
                    }
                    state.saveError?.let { ErrorBanner("Not saved: $it") }
                    Box(Modifier.weight(1f).fillMaxWidth()) { BoardView(state.content) }
                }
            }
        }
    }

    nameDialog?.let { action ->
        TextInputDialog(
            title = if (action == "new") "New Board" else "Rename Board",
            label = "Name",
            initial = if (action == "new") "New Board" else selected?.name.orEmpty(),
            confirmLabel = if (action == "new") "Create" else "Rename",
            onConfirm = { name ->
                nameDialog = null
                if (action == "new") viewModel.create(name) else viewModel.rename(name)
            },
            onDismiss = { nameDialog = null },
            validate = { name ->
                when {
                    name.isBlank() -> "Board name cannot be empty."
                    state.boards.any {
                        it.id != (if (action == "rename") state.selectedId else -1) &&
                            it.name.equals(name.trim(), ignoreCase = true)
                    } -> "A Board with this name already exists."
                    else -> null
                }
            },
        )
    }

    if (confirmDelete && selected != null) {
        ConfirmDialog(
            title = "Delete Board",
            message = "Delete Board '${selected.name}'? Its Markdown content cannot be restored.",
            confirmLabel = "Delete",
            onConfirm = { confirmDelete = false; viewModel.deleteSelected() },
            onDismiss = { confirmDelete = false },
            destructive = true,
        )
    }

    if (confirmDiscard) {
        ConfirmDialog(
            title = "Unsaved changes",
            message = "Discard the changes to the Board?",
            confirmLabel = "Discard",
            dismissLabel = "Keep editing",
            onConfirm = { confirmDiscard = false; onBack() },
            onDismiss = { confirmDiscard = false },
            destructive = true,
        )
    }
    if (state.conflict) {
        androidx.compose.material3.AlertDialog(
            onDismissRequest = viewModel::keepEditingAfterConflict,
            title = { Text("Board changed elsewhere") },
            text = { Text("Reload discards your changes. Overwrite replaces the newer Board.") },
            confirmButton = { Button(onClick = viewModel::overwriteConflict) { Text("Overwrite") } },
            dismissButton = {
                TextButton(onClick = viewModel::reloadAfterConflict) { Text("Reload") }
                TextButton(onClick = viewModel::keepEditingAfterConflict) { Text("Keep editing") }
            },
        )
    }
}

@Composable
private fun BoardView(content: String) {
    var blocks by remember { mutableStateOf<List<MdBlock>>(emptyList()) }
    LaunchedEffect(content) {
        blocks = withContext(Dispatchers.Default) { Markdown.parse(content) }
    }
    Box(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        if (blocks.isEmpty()) {
            Text("The Board is empty.", color = MaterialTheme.colorScheme.onSurfaceVariant)
        } else {
            MarkdownBlocks(blocks)
        }
    }
}
