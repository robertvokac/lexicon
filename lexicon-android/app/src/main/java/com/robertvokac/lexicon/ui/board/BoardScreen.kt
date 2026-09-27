package com.robertvokac.lexicon.ui.board

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
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
import com.robertvokac.lexicon.ui.common.ErrorBanner
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
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
    val close = {
        if (state.editing && viewModel.hasUnsavedChanges()) confirmDiscard = true else onBack()
    }
    BackHandler(onBack = close)

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (state.editing) "Edit Board" else "Board") },
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
                else -> BoardView(state.content)
            }
        }
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
