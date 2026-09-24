package com.robertvokac.lexicon.ui.cards

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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Quiz
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
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.Card
import com.robertvokac.lexicon.model.CardWrite
import com.robertvokac.lexicon.ui.alarms.AlarmTimes
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.util.DataChanges
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class CardsState(
    val loading: Boolean = true,
    val error: String? = null,
    val notFound: Boolean = false,
    val itemTitle: String = "",
    val cards: List<Card> = emptyList(),
    val busy: Boolean = false,
    val message: String? = null,
    /** Why the card being edited was not saved; the dialog stays open. */
    val saveError: String? = null,
)

/**
 * The cards of one item: each question and answer with the server's counts.
 * Only the question and the answer are edited here; the counts move only when
 * a quiz records an answer, and the server keeps them.
 */
class CardsViewModel(private val container: AppContainer, private val itemId: Int) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(CardsState())
    val state: StateFlow<CardsState> = _state.asStateFlow()
    private var loadJob: Job? = null

    init {
        load()
        viewModelScope.launch {
            // A quiz answered one of these cards: show its new counts.
            container.dataChanges.events
                .filter { it.kind == DataChanges.Kind.Cards && it.itemId == itemId }
                .collect { load(quiet = true) }
        }
    }

    fun load(quiet: Boolean = false) {
        loadJob?.cancel()
        if (!quiet) _state.update { it.copy(loading = true, error = null) }
        loadJob = viewModelScope.launch {
            try {
                val title = _state.value.itemTitle.ifEmpty { api.item(itemId).item.displayTitle }
                val cards = api.cards(itemId)
                _state.update { it.copy(loading = false, error = null, notFound = false, itemTitle = title, cards = cards) }
            } catch (_: ApiException.NotFound) {
                _state.update { it.copy(loading = false, notFound = true) }
            } catch (failure: ApiException) {
                // A refresh that fails leaves the list as it was.
                _state.update {
                    if (quiet) it.copy(message = failure.userMessage())
                    else it.copy(loading = false, error = failure.userMessage() ?: "Sign in to continue.")
                }
            }
        }
    }

    /** Creates or updates; [onDone] runs after success so the dialog can close. */
    fun save(cardId: Int?, card: CardWrite, onDone: () -> Unit) {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true, saveError = null) }
        viewModelScope.launch {
            try {
                // The server's answer is the card as stored, counts included.
                val saved = if (cardId == null) api.createCard(itemId, card) else api.updateCard(cardId, card)
                _state.update { state ->
                    state.copy(
                        busy = false,
                        cards = if (cardId == null) state.cards + saved else state.cards.map { if (it.id == saved.id) saved else it },
                    )
                }
                onDone()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, saveError = failure.userMessage()) }
            }
        }
    }

    fun delete(card: Card) {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                api.deleteCard(card.id)
                _state.update { state ->
                    state.copy(busy = false, cards = state.cards.filter { it.id != card.id }, message = "Deleted the card '${card.label()}'.")
                }
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
                if (failure is ApiException.NotFound) load(quiet = true)
            }
        }
    }

    fun saveErrorShown() = _state.update { it.copy(saveError = null) }

    fun messageShown() = _state.update { it.copy(message = null) }
}

/** The question's first line, short enough for a label or a message. */
internal fun Card.label(): String {
    val line = question.lineSequence().firstOrNull { it.isNotBlank() }?.trim().orEmpty()
    return if (line.length > 40) line.take(39) + "…" else line
}

/** "Success: 4 · Failure: 2 · Last attempt: Wed 2026-09-24 16:00", in local time. */
internal fun statistics(successCount: Long, failureCount: Long, lastAttempt: String?): String =
    "Success: $successCount · Failure: $failureCount · Last attempt: ${lastAttempt?.let { AlarmTimes.describe(it) } ?: "Never"}"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardsScreen(viewModel: CardsViewModel, onBack: () -> Unit, onQuiz: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var adding by rememberSaveable { mutableStateOf(false) }
    // IDs rather than cards, so an open dialog survives rotation.
    var editingId by rememberSaveable { mutableStateOf<Int?>(null) }
    var deletingId by rememberSaveable { mutableStateOf<Int?>(null) }

    state.message?.let { message ->
        LaunchedEffect(message) {
            viewModel.messageShown()
            snackbar.showSnackbar(message)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Cards") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
                actions = {
                    IconButton(onClick = onQuiz) { Icon(Icons.Filled.Quiz, contentDescription = "Card quiz") }
                },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { adding = true }) { Icon(Icons.Filled.Add, contentDescription = "Add card") }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.notFound -> ErrorBox("This item no longer exists.", onRetry = null, modifier = Modifier.padding(padding))
            state.error != null -> ErrorBox(state.error.orEmpty(), onRetry = { viewModel.load() }, modifier = Modifier.padding(padding))
            else -> LazyColumn(Modifier.padding(padding).fillMaxSize(), contentPadding = PaddingValues(bottom = 88.dp)) {
                item(key = "header") {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(state.itemTitle, style = MaterialTheme.typography.headlineSmall, modifier = Modifier.semantics { heading() })
                        Text(
                            if (state.cards.isEmpty()) "No cards yet. Add one with the + button."
                            else "${state.cards.size} card(s). Only the quiz changes the counts.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                items(state.cards, key = { it.id }) { card ->
                    CardRow(card, busy = state.busy, onEdit = { editingId = card.id }, onDelete = { deletingId = card.id })
                    HorizontalDivider()
                }
            }
        }
    }

    val editing = state.cards.firstOrNull { it.id == editingId }
    if (adding || editing != null) {
        CardDialog(
            initial = editing,
            busy = state.busy,
            serverError = state.saveError,
            onConfirm = { write ->
                viewModel.save(editing?.id, write) {
                    adding = false
                    editingId = null
                }
            },
            onEdited = viewModel::saveErrorShown,
            onDismiss = {
                viewModel.saveErrorShown()
                adding = false
                editingId = null
            },
        )
    }
    state.cards.firstOrNull { it.id == deletingId }?.let { card ->
        ConfirmDialog(
            title = "Delete card",
            message = "Delete the card '${card.label()}' and its counts?",
            confirmLabel = "Delete",
            onConfirm = {
                deletingId = null
                viewModel.delete(card)
            },
            onDismiss = { deletingId = null },
            destructive = true,
        )
    }
}

@Composable
private fun CardRow(card: Card, busy: Boolean, onEdit: () -> Unit, onDelete: () -> Unit) {
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(role = Role.Button, onClickLabel = "Edit") { onEdit() }
            .padding(start = 16.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text("Question", style = MaterialTheme.typography.labelLarge, color = muted)
            Text(card.question, style = MaterialTheme.typography.bodyLarge)
            Text("Answer", style = MaterialTheme.typography.labelLarge, color = muted, modifier = Modifier.padding(top = 4.dp))
            Text(card.answer, style = MaterialTheme.typography.bodyMedium)
            Text(
                statistics(card.successCount, card.failureCount, card.lastAttempt),
                style = MaterialTheme.typography.bodySmall,
                color = muted,
                modifier = Modifier.padding(top = 4.dp),
            )
        }
        IconButton(onClick = onDelete, enabled = !busy) {
            Icon(Icons.Filled.Delete, contentDescription = "Delete card ${card.label()}")
        }
    }
}

/** The question and the answer, several lines each. The counts are shown, never edited. */
@Composable
private fun CardDialog(
    initial: Card?,
    busy: Boolean,
    serverError: String?,
    onConfirm: (CardWrite) -> Unit,
    onEdited: () -> Unit,
    onDismiss: () -> Unit,
) {
    var question by rememberSaveable { mutableStateOf(initial?.question.orEmpty()) }
    var answer by rememberSaveable { mutableStateOf(initial?.answer.orEmpty()) }
    AlertDialog(
        onDismissRequest = { if (!busy) onDismiss() },
        title = { Text(if (initial == null) "Add card" else "Edit card") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SyncedTextField(
                    value = question,
                    onValueChange = {
                        question = it
                        onEdited()
                    },
                    label = "Question",
                    singleLine = false,
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = answer,
                    onValueChange = {
                        answer = it
                        onEdited()
                    },
                    label = "Answer",
                    singleLine = false,
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth(),
                )
                if (initial != null) {
                    Text(
                        statistics(initial.successCount, initial.failureCount, initial.lastAttempt) + ". Saving keeps them.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                // The server decides what a card needs, and says so.
                if (serverError != null) {
                    Text(serverError, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
                }
            }
        },
        confirmButton = {
            TextButton(enabled = !busy, onClick = { onConfirm(CardWrite(question, answer)) }) { Text(if (busy) "Saving…" else "Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !busy) { Text("Cancel") } },
    )
}
