package com.robertvokac.lexicon.ui.item

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.model.ItemBundle
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.markdown.Markdown
import com.robertvokac.lexicon.ui.markdown.MdBlock
import com.robertvokac.lexicon.util.DataChanges
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class ItemDetailState(
    val loading: Boolean = true,
    val error: String? = null,
    val notFound: Boolean = false,
    val bundle: ItemBundle? = null,
    val fields: List<ItemField> = emptyList(),
    val content: List<MdBlock> = emptyList(),
    val confirmDelete: Boolean = false,
    val deleting: Boolean = false,
    val deleted: Boolean = false,
    val actionError: String? = null,
    /** A running or finished download, as a line of text. */
    val transfer: String? = null,
    val transferring: Boolean = false,
    /** A wiki link resolved to this item: open it, then call [ItemDetailViewModel.itemLinkHandled]. */
    val openItem: Int? = null,
    /** A wiki link to no item, as "Title [disambiguation]": offer to create it. */
    val missingItem: Pair<String, String>? = null,
)

/**
 * One opened item: its content, values, metadata, links and backlinks.
 * Opening it records one read, as selecting an item does on the desktop.
 * The two-pane layout reuses one instance and calls [open] per selection.
 */
class ItemDetailViewModel(private val container: AppContainer, initialItemId: Int) : ViewModel() {
    private var itemId = initialItemId
    private val api = container.api
    private val transfer = BlobTransfer(container.api, container.contentResolver)
    private val _state = MutableStateFlow(ItemDetailState())
    val state: StateFlow<ItemDetailState> = _state.asStateFlow()
    private var loadJob: Job? = null
    private var readLogged = false

    init {
        load()
        viewModelScope.launch {
            container.dataChanges.events
                // The page shows no card, so card changes leave it alone.
                .filter { it.kind != DataChanges.Kind.Cards }
                .filter { it.kind != DataChanges.Kind.Items || it.itemId == null || it.itemId == itemId || isLinked(it.itemId) }
                .collect { if (!_state.value.deleted) load(quiet = true) }
        }
        viewModelScope.launch {
            container.sessions.state
                .map { it is SessionState.SignedIn }
                .distinctUntilChanged()
                .drop(1)
                .filter { it }
                .collect { load(quiet = _state.value.bundle != null) }
        }
    }

    private fun isLinked(otherId: Int): Boolean = _state.value.bundle?.let { bundle ->
        bundle.links.any { it.toItemId == otherId } || bundle.backlinks.any { it.fromItemId == otherId }
    } ?: false

    /** Shows another item; each opening records one read. */
    fun open(id: Int) {
        if (id == itemId) return
        itemId = id
        readLogged = false
        _state.value = ItemDetailState()
        load()
    }

    fun load(quiet: Boolean = false) {
        loadJob?.cancel()
        if (!quiet) _state.update { it.copy(loading = true, error = null) }
        val id = itemId
        loadJob = viewModelScope.launch {
            try {
                val bundle = api.item(id, withLinks = true)
                val fields = bundle.item.itemTypeId?.let { api.fields(it) } ?: emptyList()
                // Parsing a long note is off the main thread.
                val content = withContext(Dispatchers.Default) { Markdown.parse(bundle.item.content) }
                _state.update { it.copy(loading = false, error = null, notFound = false, bundle = bundle, fields = fields, content = content) }
                if (!readLogged) {
                    // Once per opening, never per recomposition or refresh.
                    readLogged = true
                    try {
                        api.logItemRead(id)
                    } catch (_: ApiException) {
                        // The read log is a statistic; failing it changes nothing shown.
                    }
                }
            } catch (_: ApiException.NotFound) {
                _state.update { it.copy(loading = false, notFound = true, bundle = null) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage()) }
            }
        }
    }

    /** A tap on [[title]] in the content. */
    fun openItemLink(title: String, disambiguation: String) {
        viewModelScope.launch {
            try {
                val id = api.resolveItem(title, disambiguation)
                _state.update { it.copy(openItem = id) }
            } catch (_: ApiException.NotFound) {
                _state.update { it.copy(missingItem = title to disambiguation) }
            } catch (failure: ApiException) {
                _state.update { it.copy(actionError = failure.userMessage()) }
            }
        }
    }

    fun itemLinkHandled() = _state.update { it.copy(openItem = null, missingItem = null) }

    fun requestDelete() = _state.update { it.copy(confirmDelete = true) }

    fun cancelDelete() = _state.update { it.copy(confirmDelete = false) }

    fun delete() {
        val itemId = itemId
        if (_state.value.deleting) return
        _state.update { it.copy(deleting = true) }
        viewModelScope.launch {
            try {
                api.deleteItem(itemId)
                _state.update { it.copy(deleting = false, confirmDelete = false, deleted = true) }
                container.dataChanges.itemChanged(itemId)
            } catch (failure: ApiException) {
                _state.update { it.copy(deleting = false, confirmDelete = false, actionError = failure.userMessage()) }
            }
        }
    }

    fun actionErrorShown() = _state.update { it.copy(actionError = null) }

    /** Saves a Blob field's bytes into the document the person just created. */
    fun downloadBlob(hash: String, target: android.net.Uri) {
        if (_state.value.transferring) return
        _state.update { it.copy(transferring = true, transfer = "Downloading\u2026") }
        viewModelScope.launch {
            try {
                transfer.download(hash, target) { received, total ->
                    _state.update {
                        it.copy(transfer = "Downloading ${BlobTransfer.formatSize(received)}" +
                            if (total > 0) " of ${BlobTransfer.formatSize(total)}" else "")
                    }
                }
                _state.update { it.copy(transferring = false, transfer = "Saved.") }
            } catch (failure: ApiException) {
                _state.update { it.copy(transferring = false, transfer = null, actionError = failure.userMessage()) }
            }
        }
    }
}
