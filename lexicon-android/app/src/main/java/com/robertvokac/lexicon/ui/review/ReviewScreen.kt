package com.robertvokac.lexicon.ui.review

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ReviewRating
import com.robertvokac.lexicon.ui.common.Choice
import com.robertvokac.lexicon.ui.common.ChoiceField
import com.robertvokac.lexicon.ui.common.ErrorBanner
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.markdown.Markdown
import com.robertvokac.lexicon.ui.markdown.MarkdownBlocks
import com.robertvokac.lexicon.ui.markdown.MdBlock
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class ReviewState(
    val loading: Boolean = true,
    val error: String? = null,
    val groups: List<Group> = emptyList(),
    val groupId: Int? = null,
    val queue: List<Item> = emptyList(),
    val dueCount: Int = 0,
    /** The content of the current card, once the answer is asked for. */
    val answer: List<MdBlock>? = null,
    val reviewed: Int = 0,
    val busy: Boolean = false,
    val message: String? = null,
) {
    val current: Item? get() = queue.firstOrNull()
}

/**
 * The items due for review, a card at a time: the title first, the content
 * on request, then a rating that moves the understanding and decides when the
 * item is due again. The server applies the rules; the buttons only preview
 * them.
 */
class ReviewViewModel(private val container: AppContainer) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(ReviewState())
    val state: StateFlow<ReviewState> = _state.asStateFlow()

    init {
        load()
    }

    fun load() {
        _state.update { it.copy(loading = true, error = null, message = null) }
        viewModelScope.launch {
            try {
                val groups = api.groups()
                val queue = api.reviewQueue(_state.value.groupId, BATCH)
                _state.update { it.copy(loading = false, groups = groups, queue = queue.items, dueCount = queue.dueCount, answer = null) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage() ?: "Sign in to continue.") }
            }
        }
    }

    fun selectGroup(groupId: Int?) {
        _state.update { it.copy(groupId = groupId) }
        load()
    }

    fun showAnswer() {
        val item = _state.value.current ?: return
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                val content = api.item(checkNotNull(item.id)).item.content
                _state.update { it.copy(busy = false, answer = Markdown.parse(content)) }
                runCatching { api.logItemRead(checkNotNull(item.id)) }
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun rate(rating: ReviewRating) {
        val state = _state.value
        val item = state.current ?: return
        if (state.answer == null || state.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                val updated = api.reviewItem(checkNotNull(item.id), rating)
                // Forgotten items come back at the end of this sitting.
                val rest = state.queue.drop(1) + if (rating == ReviewRating.Again) listOf(updated) else emptyList()
                _state.update { it.copy(busy = false, queue = rest, answer = null, reviewed = it.reviewed + 1) }
                container.dataChanges.itemChanged(item.id)
                if (rest.isEmpty()) refreshCount()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun skip() {
        _state.update { it.copy(queue = it.queue.drop(1), answer = null) }
        if (_state.value.queue.isEmpty()) refreshCount()
    }

    private fun refreshCount() {
        viewModelScope.launch {
            try {
                val due = api.reviewQueue(_state.value.groupId, 1).dueCount
                _state.update { it.copy(dueCount = due) }
            } catch (_: ApiException) {
                // The summary stands without the count.
            }
        }
    }

    fun messageShown() = _state.update { it.copy(message = null) }

    private companion object {
        const val BATCH = 20
    }
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ReviewScreen(viewModel: ReviewViewModel, onBack: () -> Unit, onOpenItem: (Int) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Review") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
    ) { padding ->
        Box(Modifier.padding(padding).fillMaxSize(), contentAlignment = Alignment.TopCenter) {
            when {
                state.loading -> LoadingBox()
                state.error != null -> ErrorBox(checkNotNull(state.error), onRetry = viewModel::load)
                else -> Column(
                    Modifier
                        .widthIn(max = 720.dp)
                        .fillMaxWidth()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    ChoiceField(
                        label = "Group",
                        choices = listOf(Choice<Int?>(null, "All groups")) + state.groups.map { Choice(it.id, it.name) },
                        selected = state.groupId,
                        onSelected = viewModel::selectGroup,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Text("${state.dueCount} due", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    state.message?.let { ErrorBanner(it) }
                    val item = state.current
                    if (item == null) {
                        Text(
                            if (state.dueCount > 0) "${state.reviewed} reviewed. ${state.dueCount} more item(s) are due."
                            else "${state.reviewed} reviewed. Nothing else is due now.",
                            textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth().padding(vertical = 24.dp).semantics { liveRegion = LiveRegionMode.Polite },
                        )
                        if (state.dueCount > 0) Button(onClick = viewModel::load, modifier = Modifier.align(Alignment.CenterHorizontally)) { Text("Continue") }
                    } else {
                        Text(item.displayTitle, style = MaterialTheme.typography.headlineMedium, modifier = Modifier.semantics { heading() })
                        Text(
                            listOfNotNull(
                                item.groupName.ifEmpty { null },
                                item.itemTypeName.ifEmpty { null },
                                item.tags.joinToString(", ").ifEmpty { null },
                                item.understanding.label,
                                item.reviewedAt?.let { "last reviewed ${it.take(10)}" } ?: "never reviewed",
                            ).joinToString(" · "),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        val answer = state.answer
                        if (answer == null) {
                            Button(onClick = viewModel::showAnswer, enabled = !state.busy) { Text("Show answer") }
                        } else {
                            if (answer.isEmpty()) {
                                Text("This item has no content.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                            } else {
                                MarkdownBlocks(answer)
                            }
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                ReviewRating.entries.forEach { rating ->
                                    val days = ReviewRating.intervalDays(rating.levelAfter(item.understanding))
                                    OutlinedButton(onClick = { viewModel.rate(rating) }, enabled = !state.busy) {
                                        Text("${rating.name} (${if (days == 1) "1 day" else "$days days"})")
                                    }
                                }
                            }
                        }
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            TextButton(onClick = viewModel::skip) { Text("Skip") }
                            TextButton(onClick = { item.id?.let(onOpenItem) }) { Text("Open item") }
                        }
                    }
                }
            }
        }
    }
}
