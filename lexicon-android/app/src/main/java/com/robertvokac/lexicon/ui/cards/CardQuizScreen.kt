package com.robertvokac.lexicon.ui.cards

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
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
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
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
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.model.QuizCard
import com.robertvokac.lexicon.ui.common.ErrorBanner
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.userMessage
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class CardQuizState(
    val loading: Boolean = true,
    val error: String? = null,
    val notFound: Boolean = false,
    /** The title of the item the quiz starts from. */
    val itemTitle: String = "",
    /** 0 quizzes the item's own cards; 1 to 3, the items that many links around it. */
    val depth: Int = 0,
    /** The depth Neighborhood returns to after This item. */
    val neighbourhoodDepth: Int = DEFAULT_NEIGHBOURHOOD_DEPTH,
    val cards: List<QuizCard> = emptyList(),
    val itemCount: Int = 0,
    val truncated: Boolean = false,
    /** The card on screen; the number of cards once all are answered. */
    val index: Int = 0,
    /** The current card's answer is showing. Showing it records nothing. */
    val revealed: Boolean = false,
    /** This session's Yes and No answers; they are not kept anywhere. */
    val known: Int = 0,
    val unknown: Int = 0,
    /** An answer is on its way to the server; Yes and No wait for it. */
    val busy: Boolean = false,
    val message: String? = null,
) {
    val current: QuizCard? get() = cards.getOrNull(index)
}

/** Where Neighborhood starts, as the relationship graph does. */
private const val DEFAULT_NEIGHBOURHOOD_DEPTH = 2
private const val MAX_DEPTH = 3

/**
 * A quiz over the cards of an item, or of the items around it, a card at a
 * time: the question, the answer on request, then "Do you know?". Only Yes and
 * No reach the server, which counts them on the card; the item's review and
 * understanding are left alone. Everything else lives here, so rotating the
 * phone keeps the place in the quiz.
 */
class CardQuizViewModel(private val container: AppContainer, private val itemId: Int, depth: Int) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(
        CardQuizState(
            depth = depth.coerceIn(0, MAX_DEPTH),
            neighbourhoodDepth = if (depth in 1..MAX_DEPTH) depth else DEFAULT_NEIGHBOURHOOD_DEPTH,
        ),
    )
    val state: StateFlow<CardQuizState> = _state.asStateFlow()
    private var loadJob: Job? = null

    init {
        load()
    }

    /** Loads the cards in scope from the server and starts again from the first. */
    fun load() {
        if (_state.value.busy) return
        loadJob?.cancel()
        val depth = _state.value.depth
        _state.update { it.copy(loading = true, error = null, notFound = false, message = null) }
        loadJob = viewModelScope.launch {
            try {
                val title = _state.value.itemTitle.ifEmpty { api.item(itemId).item.displayTitle }
                val set = api.quizCards(itemId, depth)
                _state.update {
                    it.copy(
                        loading = false,
                        itemTitle = title,
                        cards = set.cards,
                        itemCount = set.itemCount,
                        truncated = set.truncated,
                        index = 0,
                        revealed = false,
                        known = 0,
                        unknown = 0,
                    )
                }
            } catch (_: ApiException.NotFound) {
                _state.update { it.copy(loading = false, notFound = true) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage() ?: "Sign in to continue.") }
            }
        }
    }

    /** This item alone (0) or the items [depth] links around it. Another scope is another quiz. */
    fun setDepth(depth: Int) {
        val state = _state.value
        if (state.busy || depth == state.depth) return
        _state.update { it.copy(depth = depth, neighbourhoodDepth = if (depth > 0) depth else it.neighbourhoodDepth) }
        load()
    }

    fun showAnswer() = _state.update { if (it.current != null) it.copy(revealed = true) else it }

    /** Yes or No to "Do you know?". The next card follows once the server has counted the answer. */
    fun answer(known: Boolean) {
        val state = _state.value
        val card = state.current ?: return
        if (!state.revealed || state.busy) return
        // Set before the request leaves, so a second tap finds it busy.
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            try {
                api.attemptCard(card.id, known)
                _state.update {
                    it.copy(
                        busy = false,
                        index = it.index + 1,
                        revealed = false,
                        known = it.known + if (known) 1 else 0,
                        unknown = it.unknown + if (known) 0 else 1,
                    )
                }
                container.dataChanges.cardsChanged(card.itemId)
            } catch (_: ApiException.NotFound) {
                // Deleted elsewhere meanwhile: it cannot be answered, and the quiz goes on without counting it.
                _state.update {
                    it.copy(busy = false, index = it.index + 1, revealed = false, message = "This card was deleted meanwhile; it was skipped.")
                }
            } catch (failure: ApiException) {
                // The card stays, answer showing, for another try.
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardQuizScreen(viewModel: CardQuizViewModel, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Card quiz") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            ScopeChoice(state, onDepth = viewModel::setDepth)
            when {
                state.loading -> LoadingBox()
                state.notFound -> ErrorBox("This item no longer exists.", onRetry = null)
                state.error != null -> ErrorBox(checkNotNull(state.error), onRetry = viewModel::load)
                else -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
                    Column(
                        Modifier
                            .widthIn(max = 720.dp)
                            .fillMaxWidth()
                            .verticalScroll(rememberScrollState())
                            .padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        QuizBody(state, viewModel)
                    }
                }
            }
        }
    }
}

/** This item, or its neighborhood one to three links deep. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ScopeChoice(state: CardQuizState, onDepth: (Int) -> Unit) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SingleChoiceSegmentedButtonRow {
            SegmentedButton(
                selected = state.depth == 0,
                onClick = { onDepth(0) },
                enabled = !state.busy,
                shape = SegmentedButtonDefaults.itemShape(0, 2),
            ) { Text("This item") }
            SegmentedButton(
                selected = state.depth > 0,
                onClick = { onDepth(state.neighbourhoodDepth) },
                enabled = !state.busy,
                shape = SegmentedButtonDefaults.itemShape(1, 2),
            ) { Text("Neighborhood") }
        }
        if (state.depth > 0) {
            SingleChoiceSegmentedButtonRow {
                (1..MAX_DEPTH).forEach { depth ->
                    SegmentedButton(
                        selected = state.depth == depth,
                        onClick = { onDepth(depth) },
                        enabled = !state.busy,
                        shape = SegmentedButtonDefaults.itemShape(depth - 1, MAX_DEPTH),
                    ) { Text(if (depth == 1) "1 link" else "$depth links") }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun QuizBody(state: CardQuizState, viewModel: CardQuizViewModel) {
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    Text(
        state.itemTitle + " · " + if (state.depth == 0) "${state.cards.size} card(s)" else "${state.cards.size} card(s) from ${state.itemCount} item(s)",
        style = MaterialTheme.typography.labelLarge,
        color = muted,
    )
    if (state.truncated) {
        Surface(color = MaterialTheme.colorScheme.secondaryContainer, shape = MaterialTheme.shapes.small, modifier = Modifier.fillMaxWidth()) {
            Text(
                "The neighborhood is larger than shown: the quiz covers the nearest ${LexiconApi.QUIZ_ITEM_LIMIT} items.",
                color = MaterialTheme.colorScheme.onSecondaryContainer,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(12.dp),
            )
        }
    }
    state.message?.let { ErrorBanner(it) }
    val card = state.current
    when {
        state.cards.isEmpty() -> Text(
            "No cards are available for this quiz.",
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(vertical = 24.dp),
        )
        card == null -> Column(
            Modifier.fillMaxWidth().padding(vertical = 16.dp).semantics { liveRegion = LiveRegionMode.Polite },
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text("Quiz finished", style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
            Text("Cards: ${state.cards.size}")
            Text("Yes: ${state.known}")
            Text("No: ${state.unknown}")
            Button(onClick = viewModel::load, modifier = Modifier.padding(top = 12.dp)) { Text("Start again") }
        }
        else -> {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Item: ${card.itemTitle}", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary, modifier = Modifier.weight(1f))
                Text(
                    "${state.index + 1} / ${state.cards.size}",
                    style = MaterialTheme.typography.labelLarge,
                    color = muted,
                    modifier = Modifier.semantics {
                        contentDescription = "Card ${state.index + 1} of ${state.cards.size}"
                        liveRegion = LiveRegionMode.Polite
                    },
                )
            }
            Text("Question", style = MaterialTheme.typography.labelLarge, color = muted)
            Text(card.question, style = MaterialTheme.typography.headlineSmall, modifier = Modifier.semantics { heading() })
            if (!state.revealed) {
                Button(onClick = viewModel::showAnswer) { Text("Show answer") }
            } else {
                HorizontalDivider()
                Text("Answer", style = MaterialTheme.typography.labelLarge, color = muted)
                Text(card.answer, style = MaterialTheme.typography.bodyLarge)
                Text("Do you know?", style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(top = 8.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { viewModel.answer(true) }, enabled = !state.busy) { Text("Yes") }
                    OutlinedButton(onClick = { viewModel.answer(false) }, enabled = !state.busy) { Text("No") }
                }
            }
        }
    }
}
