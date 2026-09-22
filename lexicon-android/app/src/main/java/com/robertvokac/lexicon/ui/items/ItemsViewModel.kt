package com.robertvokac.lexicon.ui.items

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.auth.Identity
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.inbox.IdeaOutbox
import com.robertvokac.lexicon.inbox.PendingIdea
import com.robertvokac.lexicon.model.ColumnFilters
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.ItemType
import com.robertvokac.lexicon.model.ItemWrite
import com.robertvokac.lexicon.model.PropertyFilter
import com.robertvokac.lexicon.model.SaveItemRequest
import com.robertvokac.lexicon.model.SortColumns
import com.robertvokac.lexicon.model.SortOrder
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.formatItemTitle
import com.robertvokac.lexicon.storage.SettingsStore
import com.robertvokac.lexicon.ui.common.UserMessage
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.util.DataChanges
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Quick Add: a title, the Default group semantics, and nothing else. */
data class QuickAddState(
    val title: String,
    val busy: Boolean = false,
    val error: String? = null,
    /** Items already named like this, shown before anything is added. */
    val duplicates: DuplicateCheck? = null,
)

/** The Inbox: an idea caught quickly, as plain text, in Default and without a type. */
data class InboxState(
    val title: String = "",
    val content: String = "",
    val busy: Boolean = false,
    val error: String? = null,
    /** The idea waiting on this phone that is being edited, if any. */
    val waitingId: String? = null,
)

data class DuplicateCheck(
    val title: String,
    val groupName: String,
    val existing: List<Item>,
    /** An item with this exact title and no disambiguation in the target group. */
    val twin: Item?,
)

data class ItemsUiState(
    val search: String = "",
    val filters: ItemFilters = ItemFilters(),
    val sortColumn: Int = SortColumns.TITLE,
    val sortOrder: SortOrder = SortOrder.Ascending,
    val items: List<Item> = emptyList(),
    val totalCount: Int = 0,
    val loading: Boolean = true,
    val refreshing: Boolean = false,
    val loadingMore: Boolean = false,
    val endReached: Boolean = false,
    val error: String? = null,
    val loadMoreError: String? = null,
    val groups: List<Group> = emptyList(),
    val types: List<ItemType> = emptyList(),
    val typeFields: List<ItemField> = emptyList(),
    val tags: List<String> = emptyList(),
    val flags: List<String> = emptyList(),
    val selectedItemId: Int? = null,
    val quickAdd: QuickAddState? = null,
    val inbox: InboxState? = null,
    /** Inbox ideas of this account waiting on this phone for the server. */
    val waitingIdeas: List<PendingIdea> = emptyList(),
    val showWaitingIdeas: Boolean = false,
    val sendingIdeas: Boolean = false,
    val pendingDelete: Item? = null,
    val deleting: Boolean = false,
    val message: UserMessage? = null,
) {
    val sortColumns: List<Pair<Int, String>> get() = SortColumns.available(if (filters.typeId != null) typeFields else emptyList())
}

class ItemsViewModel(private val container: AppContainer) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(ItemsUiState())
    val state: StateFlow<ItemsUiState> = _state.asStateFlow()

    private var pageSize = SettingsStore.DEFAULT_PAGE_SIZE
    private var queryJob: Job? = null
    private var moreJob: Job? = null
    private var generation = 0

    init {
        viewModelScope.launch {
            pageSize = container.settings.pageSize.first()
            loadReferenceData()
            reload()
        }
        viewModelScope.launch {
            container.settings.pageSize.drop(1).collect {
                pageSize = it
                reload()
            }
        }
        viewModelScope.launch {
            container.dataChanges.events.collect { change ->
                if (change.kind != DataChanges.Kind.Items) loadReferenceData()
                else refreshUsage()
                reload(keepLoaded = true)
            }
        }
        viewModelScope.launch {
            combine(container.outbox.ideas, container.sessions.state) { ideas, _ ->
                identity()?.let { me -> ideas.filter { it.server == me.server.value && it.username == me.username } }.orEmpty()
            }.collect { mine ->
                _state.update { it.copy(waitingIdeas = mine, showWaitingIdeas = it.showWaitingIdeas && mine.isNotEmpty()) }
            }
        }
        viewModelScope.launch {
            container.outbox.delivered.filter { it > 0 }.collect { count ->
                container.outbox.deliveredShown()
                _state.update { it.copy(message = UserMessage(if (count == 1) "Sent 1 idea saved on this phone." else "Sent $count ideas saved on this phone.")) }
                container.dataChanges.itemChanged(null)
            }
        }
        viewModelScope.launch {
            // A session that expired underneath this screen is back: catch up.
            container.sessions.state
                .map { it is SessionState.SignedIn }
                .distinctUntilChanged()
                .drop(1)
                .filter { it }
                .collect {
                    loadReferenceData()
                    reload(keepLoaded = true)
                }
        }
    }

    // Queries -----------------------------------------------------------------

    private fun currentQuery(limit: Int, offset: Int): ItemQuery = _state.value.let {
        buildItemQuery(it.search, it.filters, it.typeFields, it.sortColumn, it.sortOrder, limit, offset)
    }

    /**
     * Starts over from the first page. Any request still running for an older
     * query is cancelled, so its answer can never replace a newer one.
     * [keepLoaded] reloads as many rows as are shown, keeping the scroll place.
     */
    private fun reload(debounceMillis: Long = 0, keepLoaded: Boolean = false, refreshing: Boolean = false) {
        queryJob?.cancel()
        moreJob?.cancel()
        val requestGeneration = ++generation
        val limit = if (keepLoaded) _state.value.items.size.coerceIn(pageSize, MAX_LIMIT) else pageSize
        _state.update {
            it.copy(
                loading = !keepLoaded && !refreshing,
                refreshing = refreshing,
                loadingMore = false,
                error = null,
                loadMoreError = null,
            )
        }
        queryJob = viewModelScope.launch {
            if (debounceMillis > 0) delay(debounceMillis)
            try {
                val page = api.queryItems(currentQuery(limit, 0))
                if (requestGeneration != generation) return@launch
                val items = page.items.distinctBy { it.id }
                _state.update {
                    it.copy(
                        items = items,
                        totalCount = page.totalCount,
                        endReached = page.items.size < limit || items.size >= page.totalCount,
                        loading = false,
                        refreshing = false,
                    )
                }
            } catch (failure: ApiException) {
                if (requestGeneration != generation) return@launch
                _state.update { it.copy(loading = false, refreshing = false, error = failure.userMessage()) }
            }
        }
    }

    /** The next page, appended without duplicates. */
    fun loadMore() {
        val current = _state.value
        if (current.loading || current.loadingMore || current.endReached || current.error != null ||
            current.loadMoreError != null || moreJob?.isActive == true
        ) {
            return
        }
        val requestGeneration = generation
        val offset = current.items.size
        _state.update { it.copy(loadingMore = true) }
        moreJob = viewModelScope.launch {
            try {
                val page = api.queryItems(currentQuery(pageSize, offset))
                if (requestGeneration != generation) return@launch
                _state.update { state ->
                    // Items added or removed meanwhile shift the offsets; a row
                    // the list already shows is never shown twice.
                    val known = state.items.mapTo(HashSet()) { it.id }
                    val items = state.items + page.items.filter { it.id !in known }
                    state.copy(
                        items = items,
                        totalCount = page.totalCount,
                        endReached = page.items.size < pageSize || items.size >= page.totalCount,
                        loadingMore = false,
                    )
                }
            } catch (failure: ApiException) {
                if (requestGeneration != generation) return@launch
                _state.update { it.copy(loadingMore = false, loadMoreError = failure.userMessage()) }
            }
        }
    }

    fun retryLoadMore() {
        _state.update { it.copy(loadMoreError = null) }
        loadMore()
    }

    fun refresh() {
        viewModelScope.launch { loadReferenceData() }
        reload(keepLoaded = true, refreshing = true)
    }

    fun retry() = reload()

    private suspend fun loadReferenceData() {
        try {
            coroutineScope {
                val filters = _state.value.filters
                val groups = async { api.groups() }
                val types = async { api.types(filters.groupId) }
                val fields = async { filters.typeId?.let { api.fields(it) } ?: emptyList() }
                val tags = async { api.tagUsage().map { it.value } }
                val flags = async { api.flagUsage().map { it.value } }
                val loadedTypes = types.await()
                _state.update { state ->
                    val typeStillThere = state.filters.typeId == null || loadedTypes.any { it.id == state.filters.typeId }
                    val groupStillThere = groups.await().let { list ->
                        state.filters.groupId == null || list.any { it.id == state.filters.groupId }
                    }
                    state.copy(
                        groups = groups.await(),
                        types = loadedTypes,
                        typeFields = if (typeStillThere) fields.await() else emptyList(),
                        tags = tags.await(),
                        flags = flags.await(),
                        filters = state.filters.copy(
                            groupId = if (groupStillThere) state.filters.groupId else null,
                            typeId = if (typeStillThere) state.filters.typeId else null,
                            values = if (typeStillThere) state.filters.values else emptyMap(),
                            tag = state.filters.tag.takeIf { it.isEmpty() || it in tags.await() }.orEmpty(),
                            flag = state.filters.flag.takeIf { it.isEmpty() || it in flags.await() }.orEmpty(),
                        ),
                    )
                }
            }
        } catch (_: ApiException) {
            // The filter choices are a convenience; the list reports errors itself.
        }
    }

    private suspend fun refreshUsage() {
        try {
            val tags = api.tagUsage().map { it.value }
            val flags = api.flagUsage().map { it.value }
            _state.update { it.copy(tags = tags, flags = flags) }
        } catch (_: ApiException) {
        }
    }

    // Search, filters and sorting --------------------------------------------

    fun setSearch(text: String) {
        _state.update { it.copy(search = text) }
        reload(debounceMillis = TYPING_DEBOUNCE_MS)
    }

    /** The keyboard's search key: no need to wait for the debounce. */
    fun submitSearch() = reload()

    private fun updateFilters(debounce: Boolean = false, change: (ItemFilters) -> ItemFilters) {
        _state.update { it.copy(filters = change(it.filters)) }
        reload(debounceMillis = if (debounce) TYPING_DEBOUNCE_MS else 0)
    }

    fun setGroup(groupId: Int?) {
        _state.update { it.copy(filters = it.filters.copy(groupId = groupId)) }
        queryJob?.cancel()
        viewModelScope.launch {
            // The types a group offers include those available in all groups.
            val types = try {
                api.types(groupId)
            } catch (failure: ApiException) {
                _state.update { it.copy(error = failure.userMessage()) }
                return@launch
            }
            val keepType = _state.value.filters.typeId.let { it == null || types.any { type -> type.id == it } }
            _state.update {
                it.copy(
                    types = types,
                    typeFields = if (keepType) it.typeFields else emptyList(),
                    sortColumn = if (keepType || it.sortColumn < SortColumns.FIRST_FIELD) it.sortColumn else SortColumns.TITLE,
                    filters = if (keepType) it.filters else it.filters.copy(typeId = null, values = emptyMap()),
                )
            }
            reload()
        }
    }

    fun setType(typeId: Int?) {
        _state.update {
            it.copy(
                filters = it.filters.copy(typeId = typeId, values = emptyMap()),
                typeFields = emptyList(),
                sortColumn = if (it.sortColumn >= SortColumns.FIRST_FIELD) SortColumns.TITLE else it.sortColumn,
            )
        }
        queryJob?.cancel()
        viewModelScope.launch {
            val fields = try {
                typeId?.let { api.fields(it) } ?: emptyList()
            } catch (failure: ApiException) {
                _state.update { it.copy(error = failure.userMessage()) }
                return@launch
            }
            if (_state.value.filters.typeId == typeId) _state.update { it.copy(typeFields = fields) }
            reload()
        }
    }

    fun setIdFilter(value: String) = updateFilters(debounce = true) { it.copy(id = value.filter(Char::isDigit)) }
    fun setTitleFilter(value: String) = updateFilters(debounce = true) { it.copy(title = value) }
    fun setDisambiguationFilter(value: String) = updateFilters(debounce = true) { it.copy(disambiguation = value) }
    fun setAliasFilter(value: String) = updateFilters(debounce = true) { it.copy(alias = value) }
    fun setTag(value: String) = updateFilters { it.copy(tag = value) }
    fun setFlag(value: String) = updateFilters { it.copy(flag = value) }
    fun setStatus(value: ItemStatus?) = updateFilters { it.copy(status = value) }
    fun setUnderstanding(value: UnderstandingLevel?) = updateFilters { it.copy(understanding = value) }
    fun setPinned(value: Boolean?) = updateFilters { it.copy(pinned = value) }

    fun setValueFilter(field: ItemField, value: String) {
        val id = field.id ?: return
        updateFilters(debounce = !field.dataType.filtersExactly) { it.copy(values = it.values + (id to value)) }
    }

    fun addPropertyFilter(filter: PropertyFilter) = updateFilters { it.copy(properties = it.properties + filter) }

    fun replacePropertyFilter(index: Int, filter: PropertyFilter) = updateFilters {
        it.copy(properties = it.properties.toMutableList().also { list -> list[index] = filter })
    }

    fun removePropertyFilter(index: Int) = updateFilters {
        it.copy(properties = it.properties.toMutableList().also { list -> list.removeAt(index) })
    }

    fun clearFilters() {
        val hadGroup = _state.value.filters.groupId != null
        _state.update {
            it.copy(
                filters = ItemFilters(),
                typeFields = emptyList(),
                sortColumn = if (it.sortColumn >= SortColumns.FIRST_FIELD) SortColumns.TITLE else it.sortColumn,
            )
        }
        if (hadGroup) setGroup(null) else reload()
    }

    fun setSort(column: Int, order: SortOrder) {
        _state.update { it.copy(sortColumn = column, sortOrder = order) }
        reload()
    }

    // Selection (two-pane layout) ---------------------------------------------

    fun select(itemId: Int?) = _state.update { it.copy(selectedItemId = itemId) }

    // Quick Add ---------------------------------------------------------------

    fun openQuickAdd(title: String = _state.value.search.trim()) =
        _state.update { it.copy(quickAdd = QuickAddState(title)) }

    fun setQuickAddTitle(title: String) =
        _state.update { it.copy(quickAdd = it.quickAdd?.copy(title = title, error = null)) }

    fun dismissQuickAdd() = _state.update { it.copy(quickAdd = null) }

    fun dismissDuplicates() = _state.update { it.copy(quickAdd = it.quickAdd?.copy(duplicates = null)) }

    /**
     * The group a new item goes to: the selected group, else the group of a
     * group-scoped selected type, else Default, exactly as Qt and web decide.
     */
    suspend fun groupIdForNewItem(): Int {
        val state = _state.value
        state.filters.groupId?.let { return it }
        state.types.firstOrNull { it.id == state.filters.typeId }?.groupId?.let { return it }
        val defaultGroupId = api.defaultGroupId()
        // A historical database may just have gained its Default group.
        runCatching { api.groups() }.getOrNull()?.let { groups -> _state.update { it.copy(groups = groups) } }
        return defaultGroupId
    }

    /**
     * Adds the title as a new item. Unless [addAnyway], items already named
     * like this anywhere are shown first and nothing is written.
     */
    fun submitQuickAdd(addAnyway: Boolean = false) {
        val quickAdd = _state.value.quickAdd ?: return
        if (quickAdd.busy) return
        val title = quickAdd.title.trim()
        if (title.isEmpty()) {
            _state.update { it.copy(quickAdd = quickAdd.copy(error = "Enter a title.")) }
            return
        }
        _state.update { it.copy(quickAdd = quickAdd.copy(busy = true, error = null, duplicates = null)) }
        viewModelScope.launch {
            try {
                val groupId = groupIdForNewItem()
                if (!addAnyway) {
                    val existing = itemsNamed(title)
                    if (existing.isNotEmpty()) {
                        val groupName = _state.value.groups.firstOrNull { it.id == groupId }?.name.orEmpty()
                        val twin = existing.firstOrNull { it.groupId == groupId && it.title == title && it.disambiguation.isEmpty() }
                        _state.update {
                            it.copy(quickAdd = it.quickAdd?.copy(busy = false, duplicates = DuplicateCheck(title, groupName, existing, twin)))
                        }
                        return@launch
                    }
                }
                val typeId = _state.value.filters.typeId
                val saved = api.createItem(SaveItemRequest(ItemWrite(groupId = groupId, itemTypeId = typeId, title = title)))
                _state.update {
                    it.copy(
                        quickAdd = null,
                        search = "",
                        message = UserMessage("Added “$title”.", openItemId = saved.id),
                    )
                }
                container.dataChanges.itemChanged(saved.id)
            } catch (failure: ApiException) {
                _state.update {
                    it.copy(quickAdd = it.quickAdd?.copy(busy = false, error = failure.userMessage() ?: it.quickAdd.error))
                }
            }
        }
    }

    // Inbox -------------------------------------------------------------------

    fun openInbox() = _state.update { it.copy(inbox = InboxState()) }

    /** The signed-in account, or the one whose session expired under this screen. */
    private fun identity(): Identity? = when (val session = container.sessions.state.value) {
        is SessionState.SignedIn -> session.identity
        is SessionState.SignedOut -> session.retained
        else -> null
    }

    fun setInboxTitle(title: String) = _state.update { it.copy(inbox = it.inbox?.copy(title = title, error = null)) }

    fun setInboxContent(content: String) = _state.update { it.copy(inbox = it.inbox?.copy(content = content)) }

    fun dismissInbox() = _state.update { it.copy(inbox = null) }

    /** Saves the idea to Default, without a type, whatever the filters show. */
    fun submitInbox() {
        val inbox = _state.value.inbox ?: return
        if (inbox.busy) return
        val title = inbox.title.trim()
        if (title.isEmpty()) {
            _state.update { it.copy(inbox = inbox.copy(error = "Enter a title.")) }
            return
        }
        _state.update { it.copy(inbox = inbox.copy(busy = true, error = null)) }
        viewModelScope.launch {
            try {
                val groupId = api.defaultGroupId()
                val saved = api.createItem(SaveItemRequest(ItemWrite(groupId = groupId, title = title, content = inbox.content)))
                inbox.waitingId?.let { container.outbox.remove(it) }
                _state.update { it.copy(inbox = null, message = UserMessage("Saved “$title” to the Inbox.", openItemId = saved.id)) }
                container.dataChanges.itemChanged(saved.id)
            } catch (failure: ApiException) {
                val me = identity()
                if (me != null && IdeaOutbox.keepsForLater(failure)) {
                    // No connection, the server away or the session gone: the
                    // idea waits on this phone instead of being lost.
                    val waitingId = inbox.waitingId
                    if (waitingId != null) {
                        container.outbox.update(waitingId, title, inbox.content)
                    } else {
                        container.outbox.add(title, inbox.content, me.server.value, me.username)
                    }
                    _state.update {
                        it.copy(inbox = null, message = UserMessage("Saved “$title” on this phone. It goes to the server as soon as it can."))
                    }
                } else {
                    // Everything typed stays, with the reason.
                    _state.update { it.copy(inbox = it.inbox?.copy(busy = false, error = failure.userMessage() ?: it.inbox.error)) }
                }
            }
        }
    }

    // Ideas waiting on this phone --------------------------------------------

    fun openWaitingIdeas() = _state.update { it.copy(showWaitingIdeas = true) }

    fun closeWaitingIdeas() = _state.update { it.copy(showWaitingIdeas = false) }

    /** Tries to send them now. */
    fun sendWaitingIdeas() {
        val me = identity() ?: return
        if (_state.value.sendingIdeas) return
        _state.update { it.copy(sendingIdeas = true) }
        viewModelScope.launch {
            try {
                val sent = container.outbox.flush(me.server.value, me.username)
                if (sent == 0 && _state.value.waitingIdeas.any { it.problem == null }) {
                    _state.update { it.copy(message = UserMessage("The server cannot be reached yet. The ideas stay on this phone.")) }
                }
            } catch (failure: ApiException) {
                _state.update { it.copy(message = failure.userMessage()?.let(::UserMessage)) }
            } finally {
                _state.update { it.copy(sendingIdeas = false) }
            }
        }
    }

    /** Opens the Inbox with the idea, to change it and send it again. */
    fun editWaitingIdea(idea: PendingIdea) =
        _state.update { it.copy(showWaitingIdeas = false, inbox = InboxState(title = idea.title, content = idea.content, waitingId = idea.id)) }

    fun deleteWaitingIdea(idea: PendingIdea) {
        viewModelScope.launch { container.outbox.remove(idea.id) }
    }

    /** Items anywhere whose title, full title or alias is exactly [text], ignoring case. */
    private suspend fun itemsNamed(text: String): List<Item> = coroutineScope {
        val byTitle = async { api.queryItems(ItemQuery(columnFilters = ColumnFilters(title = text), limit = MAX_LIMIT)) }
        val byAlias = async { api.queryItems(ItemQuery(columnFilters = ColumnFilters(alias = text), limit = MAX_LIMIT)) }
        (byTitle.await().items + byAlias.await().items)
            .distinctBy { it.id }
            .filter { namedExactly(it, text) }
    }

    // Delete ------------------------------------------------------------------

    fun requestDelete(item: Item) = _state.update { it.copy(pendingDelete = item) }

    fun cancelDelete() = _state.update { it.copy(pendingDelete = null) }

    fun confirmDelete() {
        val item = _state.value.pendingDelete ?: return
        val id = item.id ?: return
        if (_state.value.deleting) return
        _state.update { it.copy(deleting = true) }
        viewModelScope.launch {
            try {
                api.deleteItem(id)
                _state.update { state ->
                    state.copy(
                        pendingDelete = null,
                        deleting = false,
                        items = state.items.filter { it.id != id },
                        totalCount = (state.totalCount - 1).coerceAtLeast(0),
                        selectedItemId = state.selectedItemId.takeIf { it != id },
                        message = UserMessage("Deleted “${item.title}”."),
                    )
                }
                container.dataChanges.itemChanged(id)
            } catch (failure: ApiException) {
                _state.update {
                    it.copy(
                        pendingDelete = null,
                        deleting = false,
                        message = failure.userMessage()?.let { text -> UserMessage(text) },
                    )
                }
            }
        }
    }

    fun messageShown(message: UserMessage) = _state.update { if (it.message?.id == message.id) it.copy(message = null) else it }

    override fun onCleared() {
        queryJob?.cancel()
        moreJob?.cancel()
    }

    companion object {
        const val TYPING_DEBOUNCE_MS = 300L
        const val MAX_LIMIT = 1000

        /** Whether [text] is the item's title, its full title or one of its aliases, ignoring case. */
        fun namedExactly(item: Item, text: String): Boolean {
            val wanted = text.lowercase()
            return item.title.lowercase() == wanted ||
                formatItemTitle(item.title, item.disambiguation).lowercase() == wanted ||
                item.aliases.any { it.lowercase() == wanted }
        }
    }
}
