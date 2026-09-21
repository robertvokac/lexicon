package com.robertvokac.lexicon.ui.item

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.SortColumns
import com.robertvokac.lexicon.ui.common.LiteralTextKeyboard
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.lexiconViewModel
import com.robertvokac.lexicon.ui.common.userMessage
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class ItemPickerState(
    val query: String = "",
    val results: List<Item> = emptyList(),
    val totalCount: Int = 0,
    val loading: Boolean = false,
    val error: String? = null,
)

/** Finds an item by searching on the server, a page at a time. */
class ItemPickerViewModel(private val api: LexiconApi) : ViewModel() {
    private val _state = MutableStateFlow(ItemPickerState())
    val state: StateFlow<ItemPickerState> = _state.asStateFlow()
    private var job: Job? = null

    init {
        search("", debounce = false)
    }

    fun search(text: String, debounce: Boolean = true) {
        _state.update { it.copy(query = text, loading = true, error = null) }
        job?.cancel()
        job = viewModelScope.launch {
            if (debounce) delay(250)
            try {
                val page = api.queryItems(
                    ItemQuery(searchText = text.trim(), limit = RESULTS, sortColumn = SortColumns.TITLE),
                )
                _state.update { it.copy(results = page.items, totalCount = page.totalCount, loading = false) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage()) }
            }
        }
    }

    private companion object {
        const val RESULTS = 50
    }
}

@Composable
fun ItemPickerDialog(title: String, onPick: (Item) -> Unit, onDismiss: () -> Unit) {
    val viewModel = lexiconViewModel(key = "item-picker") { container, _ -> ItemPickerViewModel(container.api) }
    val state by viewModel.state.collectAsStateWithLifecycle()
    val focus = remember { FocusRequester() }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SyncedTextField(
                    value = state.query,
                    onValueChange = viewModel::search,
                    label = "Search items",
                    leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                    singleLine = true,
                    keyboardOptions = LiteralTextKeyboard.copy(imeAction = ImeAction.Search),
                    modifier = Modifier.fillMaxWidth().focusRequester(focus),
                )
                if (state.loading) LinearProgressIndicator(Modifier.fillMaxWidth())
                state.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                LazyColumn(Modifier.heightIn(max = 360.dp)) {
                    items(state.results, key = { it.id ?: 0 }) { item ->
                        Column(
                            Modifier
                                .fillMaxWidth()
                                .clickable(role = Role.Button, onClickLabel = "Choose") { onPick(item) }
                                .padding(vertical = 10.dp),
                        ) {
                            Text(item.displayTitle, style = MaterialTheme.typography.bodyLarge)
                            Text(
                                listOf(item.groupName, item.itemTypeName).filter { it.isNotEmpty() }.joinToString(" · "),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        HorizontalDivider()
                    }
                }
                if (state.totalCount > state.results.size) {
                    Text(
                        "Showing ${state.results.size} of ${state.totalCount}. Type more to narrow the search.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
    LaunchedEffect(Unit) { focus.requestFocus() }
}
