package com.robertvokac.lexicon.ui.items

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.CloudOff
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.FilterList
import androidx.compose.material.icons.filled.Inbox
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PushPin
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.InputChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.robertvokac.lexicon.inbox.PendingIdea
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.SortOrder
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LiteralTextKeyboard
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.SyncedTextField
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.launch

/** Where a new item's full editor should start. */
data class NewItemRequest(val groupId: Int?, val typeId: Int?, val title: String)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ItemsScreen(
    viewModel: ItemsViewModel,
    onOpenDrawer: () -> Unit,
    onOpenItem: (Int) -> Unit,
    onEditItem: (Int) -> Unit,
    onAddItem: (NewItemRequest) -> Unit,
    modifier: Modifier = Modifier,
    selectedItemId: Int? = null,
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    var showFilters by rememberSaveable { mutableStateOf(false) }
    val listState = rememberLazyListState()
    val keyboard = LocalSoftwareKeyboardController.current

    state.message?.let { message ->
        LaunchedEffect(message.id) {
            viewModel.messageShown(message)
            val result = snackbar.showSnackbar(
                message.text,
                actionLabel = if (message.openItemId != null) "Open" else null,
                duration = SnackbarDuration.Short,
            )
            if (result == SnackbarResult.ActionPerformed) message.openItemId?.let(onOpenItem)
        }
    }

    // Loads the next page as the end of the list comes into view.
    LaunchedEffect(listState) {
        snapshotFlow {
            val layout = listState.layoutInfo
            val last = layout.visibleItemsInfo.lastOrNull()?.index ?: -1
            // The row count is part of the key, so a short page near the end
            // asks again instead of waiting for a scroll.
            if (last >= layout.totalItemsCount - LOAD_AHEAD) layout.totalItemsCount else -1
        }
            .distinctUntilChanged()
            .filter { it >= 0 }
            .collect { viewModel.loadMore() }
    }

    Scaffold(
        modifier = modifier.onPreviewKeyEvent { event ->
            // Ctrl+N adds an item from a hardware keyboard.
            if (event.type == KeyEventType.KeyDown && event.isCtrlPressed && event.key == Key.N) {
                viewModel.openQuickAdd()
                true
            } else {
                false
            }
        },
        topBar = {
            TopAppBar(
                title = { Text("Lexicon") },
                navigationIcon = {
                    IconButton(onClick = onOpenDrawer) { Icon(Icons.Filled.Menu, contentDescription = "Open navigation") }
                },
                actions = {
                    IconButton(onClick = viewModel::openInbox) { Icon(Icons.Filled.Inbox, contentDescription = "Inbox: save an idea") }
                    IconButton(onClick = { showFilters = true }) {
                        val active = state.filters.activeCount
                        BadgedBox(badge = { if (active > 0) Badge { Text(active.toString()) } }) {
                            Icon(
                                Icons.Filled.FilterList,
                                contentDescription = if (active > 0) "Filters and sort, $active active" else "Filters and sort",
                            )
                        }
                    }
                    IconButton(onClick = viewModel::refresh) { Icon(Icons.Filled.Refresh, contentDescription = "Refresh") }
                },
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { viewModel.openQuickAdd() },
                icon = { Icon(Icons.Filled.Add, contentDescription = null) },
                text = { Text("Add") },
                modifier = Modifier.semantics { contentDescription = "Quick add item" },
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            SearchField(
                text = state.search,
                onTextChange = viewModel::setSearch,
                onSubmit = {
                    keyboard?.hide()
                    viewModel.submitSearch()
                },
            )
            ActiveFilterChips(state, viewModel, onShowFilters = { showFilters = true })
            if (state.waitingIdeas.isNotEmpty()) WaitingIdeasBanner(state.waitingIdeas, onShow = viewModel::openWaitingIdeas)
            if ((state.loading || state.refreshing) && state.items.isNotEmpty()) {
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            PullToRefreshBox(
                isRefreshing = state.refreshing,
                onRefresh = viewModel::refresh,
                modifier = Modifier.fillMaxSize(),
            ) {
                when {
                    state.loading && state.items.isEmpty() -> LoadingBox()
                    state.error != null && state.items.isEmpty() -> ErrorBox(state.error.orEmpty(), onRetry = viewModel::retry)
                    state.items.isEmpty() -> EmptyList(
                        search = state.search.trim(),
                        filtered = !state.filters.isEmpty,
                        onAdd = { viewModel.openQuickAdd() },
                        onClearFilters = viewModel::clearFilters,
                    )
                    else -> LazyColumn(
                        state = listState,
                        contentPadding = PaddingValues(bottom = 96.dp),
                        modifier = Modifier.fillMaxSize(),
                    ) {
                        item(key = "count", contentType = "count") {
                            Text(
                                "${state.totalCount} item${if (state.totalCount == 1) "" else "s"}",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                            )
                        }
                        items(state.items, key = { it.id ?: 0 }, contentType = { "item" }) { item ->
                            ItemRow(
                                item = item,
                                fieldSummary = fieldSummary(item, state),
                                selected = item.id != null && item.id == selectedItemId,
                                onOpen = { item.id?.let(onOpenItem) },
                                onEdit = { item.id?.let(onEditItem) },
                                onDelete = { viewModel.requestDelete(item) },
                            )
                            HorizontalDivider()
                        }
                        item(key = "footer", contentType = "footer") {
                            ListFooter(state, onRetry = viewModel::retryLoadMore)
                        }
                    }
                }
            }
        }
    }

    if (showFilters) {
        FilterSheet(state = state, viewModel = viewModel, onDismiss = { showFilters = false })
    }

    if (state.showWaitingIdeas) {
        WaitingIdeasDialog(
            ideas = state.waitingIdeas,
            sending = state.sendingIdeas,
            onSend = viewModel::sendWaitingIdeas,
            onEdit = viewModel::editWaitingIdea,
            onDelete = viewModel::deleteWaitingIdea,
            onDismiss = viewModel::closeWaitingIdeas,
        )
    }

    state.inbox?.let { inbox ->
        InboxDialog(
            state = inbox,
            onTitleChange = viewModel::setInboxTitle,
            onContentChange = viewModel::setInboxContent,
            onSave = viewModel::submitInbox,
            onDismiss = viewModel::dismissInbox,
        )
    }
    state.quickAdd?.let { quickAdd ->
        QuickAddDialog(
            state = quickAdd,
            onTitleChange = viewModel::setQuickAddTitle,
            onAdd = { viewModel.submitQuickAdd() },
            onMore = {
                val title = quickAdd.title.trim()
                viewModel.dismissQuickAdd()
                scope.launch {
                    val groupId = runCatching { viewModel.groupIdForNewItem() }.getOrNull()
                    onAddItem(NewItemRequest(groupId, state.filters.typeId, title))
                }
            },
            onDismiss = viewModel::dismissQuickAdd,
        )
        quickAdd.duplicates?.let { duplicates ->
            DuplicatesDialog(
                duplicates = duplicates,
                onShow = { item ->
                    viewModel.dismissQuickAdd()
                    item.id?.let(onOpenItem)
                },
                onAddAnyway = { viewModel.submitQuickAdd(addAnyway = true) },
                onDismiss = viewModel::dismissDuplicates,
            )
        }
    }

    state.pendingDelete?.let { item ->
        ConfirmDialog(
            title = "Delete item",
            message = "Delete item '${item.title}'?",
            confirmLabel = if (state.deleting) "Deleting…" else "Delete",
            onConfirm = viewModel::confirmDelete,
            onDismiss = viewModel::cancelDelete,
            destructive = true,
        )
    }
}

private const val LOAD_AHEAD = 6

private fun fieldSummary(item: Item, state: ItemsUiState): String {
    if (state.filters.typeId == null) return ""
    return state.typeFields.mapNotNull { field ->
        val value = field.id?.let(item::fieldValue).orEmpty()
        if (value.isEmpty()) null else "${field.name}: $value"
    }.take(3).joinToString(" · ")
}

@Composable
private fun SearchField(text: String, onTextChange: (String) -> Unit, onSubmit: () -> Unit) {
    OutlinedTextField(
        value = text,
        onValueChange = onTextChange,
        placeholder = { Text("Search titles, tags, content…") },
        leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
        trailingIcon = {
            if (text.isNotEmpty()) {
                IconButton(onClick = { onTextChange("") }) { Icon(Icons.Filled.Clear, contentDescription = "Clear search") }
            }
        },
        singleLine = true,
        keyboardOptions = LiteralTextKeyboard.copy(imeAction = ImeAction.Search),
        keyboardActions = KeyboardActions(onSearch = { onSubmit() }),
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
            .semantics { contentDescription = "Search items" },
    )
}

@Composable
private fun ActiveFilterChips(state: ItemsUiState, viewModel: ItemsViewModel, onShowFilters: () -> Unit) {
    val filters = state.filters
    val chips = buildList<Pair<String, () -> Unit>> {
        filters.groupId?.let { id ->
            add("Group: ${state.groups.firstOrNull { it.id == id }?.name ?: id}" to { viewModel.setGroup(null) })
        }
        filters.typeId?.let { id ->
            add("Type: ${state.types.firstOrNull { it.id == id }?.name ?: id}" to { viewModel.setType(null) })
        }
        if (filters.id.isNotBlank()) add("Id: ${filters.id}" to { viewModel.setIdFilter("") })
        if (filters.title.isNotBlank()) add("Title: ${filters.title}" to { viewModel.setTitleFilter("") })
        if (filters.disambiguation.isNotBlank()) add("Disambiguation: ${filters.disambiguation}" to { viewModel.setDisambiguationFilter("") })
        if (filters.alias.isNotBlank()) add("Alias: ${filters.alias}" to { viewModel.setAliasFilter("") })
        if (filters.tag.isNotEmpty()) add("Tag: ${filters.tag}" to { viewModel.setTag("") })
        if (filters.flag.isNotEmpty()) add("Flag: ${filters.flag}" to { viewModel.setFlag("") })
        filters.status?.let { add("Status: ${it.label}" to { viewModel.setStatus(null) }) }
        filters.understanding?.let { add("Understanding: ${it.label}" to { viewModel.setUnderstanding(null) }) }
        filters.pinned?.let { add((if (it) "Pinned" else "Not pinned") to { viewModel.setPinned(null) }) }
        state.typeFields.forEach { field ->
            val value = field.id?.let { filters.values[it] }.orEmpty()
            if (value.isNotBlank()) add("${field.name}: $value" to { viewModel.setValueFilter(field, "") })
        }
        filters.properties.forEachIndexed { index, property ->
            val text = if (property.value.isEmpty()) "${property.key} = (any)" else "${property.key} = ${property.value}"
            add("Property $text" to { viewModel.removePropertyFilter(index) })
        }
    }
    val sortLabel = state.sortColumns.firstOrNull { it.first == state.sortColumn }?.second ?: "Title"
    val sortArrow = if (state.sortOrder == SortOrder.Ascending) "↑" else "↓"
    Row(
        Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        InputChip(
            selected = false,
            onClick = onShowFilters,
            label = { Text("Sort: $sortLabel $sortArrow") },
            leadingIcon = { Icon(Icons.AutoMirrored.Filled.Sort, contentDescription = null, Modifier.size(18.dp)) },
            modifier = Modifier.semantics {
                contentDescription = "Sorted by $sortLabel, ${state.sortOrder.name.lowercase()}. Change sorting"
            },
        )
        chips.forEach { (label, remove) ->
            InputChip(
                selected = true,
                onClick = remove,
                label = { Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                trailingIcon = { Icon(Icons.Filled.Clear, contentDescription = null, Modifier.size(18.dp)) },
                modifier = Modifier.semantics { contentDescription = "Filter $label. Remove" },
            )
        }
        if (chips.size > 1) {
            TextButton(onClick = viewModel::clearFilters) { Text("Clear filters") }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ItemRow(
    item: Item,
    fieldSummary: String,
    selected: Boolean,
    onOpen: () -> Unit,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    val background = if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface
    val status = buildList {
        if (item.status != ItemStatus.None) add(item.status.label)
        if (item.understanding != UnderstandingLevel.Unknown) add(item.understanding.label)
    }
    Box {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(background)
                .combinedClickable(
                    onClick = onOpen,
                    onClickLabel = "Open",
                    onLongClick = { menu = true },
                    onLongClickLabel = "Show actions",
                )
                .semantics(mergeDescendants = true) {
                    this.selected = selected
                    customActions = listOf(
                        CustomAccessibilityAction("Edit") { onEdit(); true },
                        CustomAccessibilityAction("Delete") { onDelete(); true },
                    )
                }
                .padding(start = 16.dp, top = 10.dp, bottom = 10.dp, end = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(2.dp), modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            item.displayTitle,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Medium,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f, fill = false),
                        )
                        if (item.pinned) {
                            Spacer(Modifier.width(6.dp))
                            Icon(
                                Icons.Filled.PushPin,
                                contentDescription = "Pinned",
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(16.dp),
                            )
                        }
                    }
                    val meta = listOf(item.groupName, item.itemTypeName).filter { it.isNotEmpty() }.joinToString(" · ")
                    if (meta.isNotEmpty()) {
                        Text(meta, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    if (status.isNotEmpty()) {
                        Text(status.joinToString(" · "), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
                    }
                    item.matchSnippet?.takeIf { it.isNotBlank() }?.let { snippet ->
                        Text(
                            snippet,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    val labels = (item.tags + item.flags.map { "⚑ $it" })
                    if (labels.isNotEmpty()) {
                        Text(
                            labels.joinToString(", "),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    if (fieldSummary.isNotEmpty()) {
                        Text(fieldSummary, style = MaterialTheme.typography.bodySmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
            }
            IconButton(onClick = { menu = true }) {
                Icon(Icons.Filled.MoreVert, contentDescription = "Actions for ${item.title}")
            }
        }
        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
            DropdownMenuItem(
                text = { Text("Edit") },
                leadingIcon = { Icon(Icons.Filled.Edit, contentDescription = null) },
                onClick = {
                    menu = false
                    onEdit()
                },
            )
            DropdownMenuItem(
                text = { Text("Delete") },
                leadingIcon = { Icon(Icons.Filled.Delete, contentDescription = null) },
                onClick = {
                    menu = false
                    onDelete()
                },
            )
        }
    }
}

@Composable
private fun ListFooter(state: ItemsUiState, onRetry: () -> Unit) {
    Box(Modifier.fillMaxWidth().padding(16.dp), contentAlignment = Alignment.Center) {
        when {
            state.loadingMore -> LinearProgressIndicator(Modifier.fillMaxWidth())
            state.loadMoreError != null -> Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(state.loadMoreError, color = MaterialTheme.colorScheme.error)
                OutlinedButton(onClick = onRetry) { Text("Retry") }
            }
            state.endReached && state.items.size > 5 -> Text(
                "End of the list",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun EmptyList(search: String, filtered: Boolean, onAdd: () -> Unit, onClearFilters: () -> Unit) {
    Column(
        Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text("No items match the current filters.", style = MaterialTheme.typography.bodyLarge)
        Spacer(Modifier.size(12.dp))
        if (search.isNotEmpty()) {
            OutlinedButton(onClick = onAdd) { Text("Add “$search”") }
        }
        if (filtered) {
            TextButton(onClick = onClearFilters) { Text("Clear filters") }
        }
    }
}

@Composable
private fun QuickAddDialog(
    state: QuickAddState,
    onTitleChange: (String) -> Unit,
    onAdd: () -> Unit,
    onMore: () -> Unit,
    onDismiss: () -> Unit,
) {
    val focus = remember { FocusRequester() }
    AlertDialog(
        onDismissRequest = { if (!state.busy) onDismiss() },
        title = { Text("Quick add") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SyncedTextField(
                    value = state.title,
                    onValueChange = onTitleChange,
                    label = "Title",
                    singleLine = true,
                    enabled = !state.busy,
                    isError = state.error != null,
                    supportingText = state.error,
                    keyboardOptions = LiteralTextKeyboard.copy(imeAction = ImeAction.Done),
                    onKeyboardAction = { onAdd() },
                    modifier = Modifier.fillMaxWidth().focusRequester(focus),
                )
                if (state.busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            }
        },
        confirmButton = {
            TextButton(onClick = onAdd, enabled = !state.busy) { Text("Add") }
        },
        dismissButton = {
            Row {
                TextButton(onClick = onMore, enabled = !state.busy) { Text("More…") }
                TextButton(onClick = onDismiss, enabled = !state.busy) { Text("Cancel") }
            }
        },
    )
    LaunchedEffect(Unit) { focus.requestFocus() }
}

/** Ideas saved on this phone while the server could not be reached. */
@Composable
private fun WaitingIdeasBanner(ideas: List<PendingIdea>, onShow: () -> Unit) {
    val refused = ideas.count { it.problem != null }
    Surface(
        color = MaterialTheme.colorScheme.secondaryContainer,
        shape = MaterialTheme.shapes.medium,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 4.dp)
            .clickable(role = Role.Button, onClickLabel = "Show") { onShow() },
    ) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.CloudOff, contentDescription = null, tint = MaterialTheme.colorScheme.onSecondaryContainer)
            Text(
                (if (ideas.size == 1) "1 idea waits on this phone" else "${ideas.size} ideas wait on this phone") +
                    if (refused > 0) ", $refused refused by the server" else "",
                color = MaterialTheme.colorScheme.onSecondaryContainer,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.weight(1f).padding(start = 12.dp),
            )
            Text("Show", color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelLarge)
        }
    }
}

@Composable
private fun WaitingIdeasDialog(
    ideas: List<PendingIdea>,
    sending: Boolean,
    onSend: () -> Unit,
    onEdit: (PendingIdea) -> Unit,
    onDelete: (PendingIdea) -> Unit,
    onDismiss: () -> Unit,
) {
    var deleting by remember { mutableStateOf<PendingIdea?>(null) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Waiting on this phone") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(
                    "Inbox ideas saved while the server could not be reached. They go to the server by themselves as soon as it can be reached.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                for (idea in ideas) {
                    Column {
                        Text(idea.title, style = MaterialTheme.typography.titleSmall)
                        if (idea.content.isNotBlank()) {
                            Text(idea.content, style = MaterialTheme.typography.bodySmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        }
                        idea.problem?.let { Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error) }
                        Row {
                            TextButton(onClick = { onEdit(idea) }) { Text("Edit") }
                            TextButton(onClick = { deleting = idea }) { Text("Delete") }
                        }
                    }
                }
                if (sending) LinearProgressIndicator(Modifier.fillMaxWidth())
            }
        },
        confirmButton = { TextButton(onClick = onSend, enabled = !sending && ideas.any { it.problem == null }) { Text("Send now") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
    deleting?.let { idea ->
        ConfirmDialog(
            title = "Delete idea",
            message = "Delete “${idea.title}”? It has not reached the server, so it is gone for good.",
            confirmLabel = "Delete",
            onConfirm = {
                deleting = null
                onDelete(idea)
            },
            onDismiss = { deleting = null },
            destructive = true,
        )
    }
}

/** An idea, caught quickly: a title and plain text, saved to Default without a type. */
@Composable
internal fun InboxDialog(
    state: InboxState,
    onTitleChange: (String) -> Unit,
    onContentChange: (String) -> Unit,
    onSave: () -> Unit,
    onDismiss: () -> Unit,
) {
    val focus = remember { FocusRequester() }
    AlertDialog(
        onDismissRequest = { if (!state.busy) onDismiss() },
        title = { Text("Inbox") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    "Saved to Default, without a type. Sort it out later.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                SyncedTextField(
                    value = state.title,
                    onValueChange = onTitleChange,
                    label = "Title",
                    singleLine = true,
                    enabled = !state.busy,
                    isError = state.error != null,
                    supportingText = state.error,
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Next),
                    modifier = Modifier.fillMaxWidth().focusRequester(focus),
                )
                SyncedTextField(
                    value = state.content,
                    onValueChange = onContentChange,
                    label = "Idea",
                    singleLine = false,
                    minLines = 5,
                    enabled = !state.busy,
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (state.busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            }
        },
        confirmButton = { TextButton(onClick = onSave, enabled = !state.busy) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !state.busy) { Text("Cancel") } },
    )
    LaunchedEffect(Unit) { focus.requestFocus() }
}

@Composable
private fun DuplicatesDialog(
    duplicates: DuplicateCheck,
    onShow: (Item) -> Unit,
    onAddAnyway: () -> Unit,
    onDismiss: () -> Unit,
) {
    val twin = duplicates.twin
    if (twin != null) {
        // The database holds one item per title and group, so that one can
        // only be shown, not added again.
        AlertDialog(
            onDismissRequest = onDismiss,
            title = { Text("Already in Lexicon") },
            text = { Text("“${duplicates.title}” already exists in the group ${twin.groupName}.") },
            confirmButton = { TextButton(onClick = { onShow(twin) }) { Text("Show it") } },
            dismissButton = { TextButton(onClick = onDismiss) { Text("Close") } },
        )
        return
    }
    val lines = duplicates.existing.take(5).map { item ->
        val byTitle = item.title.equals(duplicates.title, ignoreCase = true) ||
            item.displayTitle.equals(duplicates.title, ignoreCase = true)
        "• ${item.displayTitle} (${item.groupName})" + if (byTitle) "" else ", alias “${duplicates.title}”"
    } + if (duplicates.existing.size > 5) listOf("• and ${duplicates.existing.size - 5} more") else emptyList()
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Already in Lexicon") },
        text = {
            Text(
                "“${duplicates.title}” already names:\n${lines.joinToString("\n")}\n\n" +
                    "Add another item called “${duplicates.title}”?",
            )
        },
        confirmButton = { TextButton(onClick = onAddAnyway) { Text("Add anyway") } },
        dismissButton = {
            Row {
                TextButton(onClick = { onShow(duplicates.existing.first()) }) { Text("Show it") }
                TextButton(onClick = onDismiss) { Text("Cancel") }
            }
        },
    )
}
