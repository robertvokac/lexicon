package com.robertvokac.lexicon.ui.history

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.ItemHistoryEntry
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.userMessage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class HistoryState(
    val loading: Boolean = true,
    val entries: List<ItemHistoryEntry> = emptyList(),
    val error: String? = null,
    val busy: Boolean = false,
)

class HistoryViewModel(private val container: AppContainer, private val itemId: Int?) : ViewModel() {
    private val _state = MutableStateFlow(HistoryState())
    val state = _state.asStateFlow()

    init { load() }

    fun load() {
        viewModelScope.launch {
            _state.update { it.copy(loading = true, error = null) }
            try {
                val entries = if (itemId == null) container.api.trash() else container.api.itemHistory(itemId)
                _state.value = HistoryState(entries = entries)
            } catch (failure: ApiException) {
                _state.value = HistoryState(loading = false, error = failure.userMessage())
            }
        }
    }

    fun restore(entry: ItemHistoryEntry, onRestored: (Int) -> Unit) {
        if (_state.value.busy) return
        viewModelScope.launch {
            _state.update { it.copy(busy = true, error = null) }
            try {
                val id = container.api.restoreItemHistory(entry.id)
                container.dataChanges.itemChanged(id)
                onRestored(id)
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, error = failure.userMessage()) }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HistoryScreen(viewModel: HistoryViewModel, trash: Boolean, onBack: () -> Unit, onRestored: (Int) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val pending = remember { mutableStateOf<ItemHistoryEntry?>(null) }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (trash) "Trash" else "Item history") },
                navigationIcon = { IconButton(onClick = onBack) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                } },
            )
        },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.error != null && state.entries.isEmpty() ->
                ErrorBox(state.error.orEmpty(), onRetry = viewModel::load, modifier = Modifier.padding(padding))
            else -> LazyColumn(
                Modifier.padding(padding).fillMaxSize(),
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                state.error?.let { error -> item { Text(error, color = MaterialTheme.colorScheme.error) } }
                if (state.entries.isEmpty()) item { Text(if (trash) "Trash is empty." else "No older versions yet.") }
                items(state.entries, key = { it.id }) { entry ->
                    Card(Modifier.fillMaxWidth()) {
                        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(entry.item.displayTitle, style = MaterialTheme.typography.titleMedium)
                            Text("${entry.happenedAt} · revision ${entry.item.revision}",
                                style = MaterialTheme.typography.bodySmall)
                            if (entry.item.content.isNotBlank()) Text(entry.item.content,
                                maxLines = 5, overflow = TextOverflow.Ellipsis)
                            OutlinedButton(onClick = { pending.value = entry }, enabled = !state.busy) {
                                Text(if (trash) "Restore item" else "Restore this version")
                            }
                        }
                    }
                }
            }
        }
    }
    pending.value?.let { entry ->
        ConfirmDialog(
            title = "Restore ${entry.item.displayTitle}",
            message = if (trash) "Restore this deleted item?" else "Replace the current item and its links with this version?",
            confirmLabel = "Restore",
            onConfirm = { pending.value = null; viewModel.restore(entry, onRestored) },
            onDismiss = { pending.value = null },
        )
    }
}
