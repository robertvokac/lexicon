package com.robertvokac.lexicon.ui.overview

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.model.UsageValue
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.userMessage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** The read-only overviews of the desktop View menu. */
enum class OverviewKind(val title: String, val header: String) {
    Tags("All tags", "Tag"),
    Flags("All flags", "Flag"),
    Aliases("All aliases", "Alias"),
}

data class OverviewState(
    val loading: Boolean = true,
    val refreshing: Boolean = false,
    val error: String? = null,
    val values: List<UsageValue> = emptyList(),
)

class OverviewViewModel(private val api: LexiconApi, private val kind: OverviewKind) : ViewModel() {
    private val _state = MutableStateFlow(OverviewState())
    val state: StateFlow<OverviewState> = _state.asStateFlow()

    init {
        load()
    }

    fun load(refreshing: Boolean = false) {
        _state.update { it.copy(refreshing = refreshing, error = null) }
        viewModelScope.launch {
            try {
                val values = when (kind) {
                    OverviewKind.Tags -> api.tagUsage()
                    OverviewKind.Flags -> api.flagUsage()
                    OverviewKind.Aliases -> api.aliasUsage()
                }
                _state.update { it.copy(loading = false, refreshing = false, values = values) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, refreshing = false, error = failure.userMessage()) }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OverviewScreen(viewModel: OverviewViewModel, kind: OverviewKind, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(kind.title) },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.error != null && state.values.isEmpty() ->
                ErrorBox(state.error.orEmpty(), onRetry = { viewModel.load() }, modifier = Modifier.padding(padding))
            else -> PullToRefreshBox(
                isRefreshing = state.refreshing,
                onRefresh = { viewModel.load(refreshing = true) },
                modifier = Modifier.padding(padding).fillMaxSize(),
            ) {
                LazyColumn(Modifier.fillMaxSize()) {
                    item(key = "header") {
                        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp).semantics { heading() }) {
                            Text(kind.header, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                            Text("Usage count", fontWeight = FontWeight.Bold)
                        }
                        HorizontalDivider()
                    }
                    if (state.values.isEmpty()) {
                        item(key = "empty") { Text("Nothing yet.", modifier = Modifier.padding(16.dp)) }
                    }
                    items(state.values, key = { it.value }) { usage ->
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 16.dp, vertical = 12.dp)
                                .clearAndSetSemantics {
                                    contentDescription = "${usage.value}, used ${usage.usageCount} time${if (usage.usageCount == 1) "" else "s"}"
                                },
                        ) {
                            Text(usage.value, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                            Text(usage.usageCount.toString(), style = MaterialTheme.typography.bodyLarge)
                        }
                        HorizontalDivider()
                    }
                }
            }
        }
    }
}
