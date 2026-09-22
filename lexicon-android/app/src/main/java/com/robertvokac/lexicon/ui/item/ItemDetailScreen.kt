package com.robertvokac.lexicon.ui.item

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Hub
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PushPin
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
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
import androidx.compose.runtime.CompositionLocalProvider
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ImageValues
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.Link
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.formatItemTitle
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.markdown.LocalItemLinkHandler
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LabelChip
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.markdown.markdownItems

/** "Custom: generalizes" for custom links, the plain label otherwise. */
fun linkDescription(type: LinkType, customValue: String): String =
    if (type == LinkType.Custom && customValue.isNotEmpty()) "Custom: $customValue" else type.label

/**
 * An item as it reads: content, values, metadata, links and backlinks.
 * [showBack] is false in the two-pane layout, where the list stays visible.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ItemDetailScreen(
    viewModel: ItemDetailViewModel,
    onBack: (() -> Unit)?,
    onEdit: () -> Unit,
    onOpenItem: (Int) -> Unit,
    onDeleted: () -> Unit,
    modifier: Modifier = Modifier,
    onCreateItem: (title: String, disambiguation: String) -> Unit = { _, _ -> },
    onShowGraph: ((Int) -> Unit)? = null,
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    state.openItem?.let { id ->
        LaunchedEffect(id) {
            viewModel.itemLinkHandled()
            onOpenItem(id)
        }
    }
    state.missingItem?.let { (title, disambiguation) ->
        ConfirmDialog(
            title = "Create item",
            message = "No item is called '${formatItemTitle(title, disambiguation)}'. Create it?",
            confirmLabel = "Create",
            onConfirm = {
                viewModel.itemLinkHandled()
                onCreateItem(title, disambiguation)
            },
            onDismiss = viewModel::itemLinkHandled,
        )
    }
    val snackbar = remember { SnackbarHostState() }
    var menu by remember { mutableStateOf(false) }
    var pendingHash by rememberSaveable { mutableStateOf<String?>(null) }
    var viewingImage by remember { mutableStateOf<Pair<String, String>?>(null) }
    viewingImage?.let { (name, value) -> ImageViewerDialog(value, name) { viewingImage = null } }
    val saveAs = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val hash = pendingHash
        pendingHash = null
        if (uri != null && hash != null) viewModel.downloadBlob(hash, uri)
    }

    LaunchedEffect(state.deleted) { if (state.deleted) onDeleted() }
    state.actionError?.let { message ->
        LaunchedEffect(message) {
            viewModel.actionErrorShown()
            snackbar.showSnackbar(message)
        }
    }
    state.transfer?.takeIf { !state.transferring }?.let { message ->
        LaunchedEffect(message) { snackbar.showSnackbar(message) }
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = {
                    Text(state.bundle?.item?.displayTitle ?: "Item", maxLines = 1, overflow = TextOverflow.Ellipsis)
                },
                navigationIcon = {
                    if (onBack != null) {
                        IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") }
                    }
                },
                actions = {
                    if (state.bundle != null) {
                        IconButton(onClick = onEdit) { Icon(Icons.Filled.Edit, contentDescription = "Edit item") }
                        IconButton(onClick = { menu = true }) { Icon(Icons.Filled.MoreVert, contentDescription = "More actions") }
                        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                            if (onShowGraph != null) {
                                DropdownMenuItem(
                                    text = { Text("Relationship graph") },
                                    leadingIcon = { Icon(Icons.Filled.Hub, contentDescription = null) },
                                    onClick = {
                                        menu = false
                                        state.bundle?.item?.id?.let(onShowGraph)
                                    },
                                )
                            }
                            DropdownMenuItem(
                                text = { Text("Refresh") },
                                leadingIcon = { Icon(Icons.Filled.Refresh, contentDescription = null) },
                                onClick = {
                                    menu = false
                                    viewModel.load()
                                },
                            )
                            DropdownMenuItem(
                                text = { Text("Delete") },
                                leadingIcon = { Icon(Icons.Filled.Delete, contentDescription = null) },
                                onClick = {
                                    menu = false
                                    viewModel.requestDelete()
                                },
                            )
                        }
                    }
                },
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        val bundle = state.bundle
        when {
            state.loading && bundle == null -> LoadingBox(Modifier.padding(padding))
            state.notFound -> ErrorBox("This item no longer exists.", onRetry = null, modifier = Modifier.padding(padding))
            bundle == null -> ErrorBox(state.error ?: "The item could not be loaded.", onRetry = { viewModel.load() }, modifier = Modifier.padding(padding))
            else -> CompositionLocalProvider(LocalItemLinkHandler provides viewModel::openItemLink) {
                LazyColumn(
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 32.dp),
                    modifier = Modifier.padding(padding).fillMaxSize(),
                ) {
                    header(bundle.item)
                    values(
                        state,
                        onSaveAs = { name, value ->
                            // An image is saved under its own extension.
                            val image = ImageValues.parse(value)
                            pendingHash = image?.hash ?: value
                            saveAs.launch(if (image != null) ImageValues.fileName(name, value) else BlobTransfer.suggestedName(name, value))
                        },
                        onViewImage = { name, value -> viewingImage = name to value },
                    )
                    properties(bundle.item)
                    item(key = "content-title") { Heading("Content") }
                    if (state.content.isEmpty()) {
                        item(key = "content-empty") { Muted("This item has no content yet.") }
                    } else {
                        markdownItems(state.content, keyPrefix = "md")
                    }
                    links("Links", "links", bundle.links, incoming = false, onOpenItem)
                    links("Backlinks", "backlinks", bundle.backlinks, incoming = true, onOpenItem)
                }
            }
        }
    }

    if (state.confirmDelete) {
        ConfirmDialog(
            title = "Delete item",
            message = "Delete item '${state.bundle?.item?.title.orEmpty()}'?",
            confirmLabel = if (state.deleting) "Deleting…" else "Delete",
            onConfirm = viewModel::delete,
            onDismiss = viewModel::cancelDelete,
            destructive = true,
        )
    }
}

@Composable
private fun Heading(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(top = 20.dp, bottom = 6.dp).semantics { heading() },
    )
}

@Composable
private fun Muted(text: String) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@OptIn(ExperimentalLayoutApi::class)
private fun LazyListScope.header(item: Item) {
    item(key = "header") {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(item.title, style = MaterialTheme.typography.headlineSmall, modifier = Modifier.semantics { heading() })
            if (item.disambiguation.isNotEmpty()) {
                Text("[${item.disambiguation}]", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            val meta = listOf(item.groupName, item.itemTypeName).filter { it.isNotEmpty() }.joinToString(" · ")
            if (meta.isNotEmpty()) Muted(meta)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (item.status != ItemStatus.None) LabelChip("Status: ${item.status.label}")
                if (item.understanding != UnderstandingLevel.Unknown) LabelChip("Understanding: ${item.understanding.label}")
                if (item.pinned) {
                    LabelChip("Pinned") { Icon(Icons.Filled.PushPin, contentDescription = null, modifier = Modifier.size(16.dp)) }
                }
            }
            ValueChips("Tags", item.tags)
            ValueChips("Flags", item.flags)
            ValueChips("Aliases", item.aliases)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ValueChips(label: String, values: List<String>) {
    if (values.isEmpty()) return
    Column {
        Text(label, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            values.forEach { LabelChip(it) }
        }
    }
}

private fun LazyListScope.values(
    state: ItemDetailState,
    onSaveAs: (String, String) -> Unit,
    onViewImage: (String, String) -> Unit,
) {
    val item = state.bundle?.item ?: return
    if (item.itemTypeId == null || state.fields.isEmpty()) return
    item(key = "values-title") { Heading("Values") }
    items(state.fields, key = { "field-${it.id}" }) { field ->
        val value = field.id?.let(item::fieldValue).orEmpty()
        Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(field.name, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                when {
                    value.isEmpty() -> Muted("Not set")
                    field.dataType == FieldDataType.Image && ImageValues.parse(value) != null -> Column {
                        Text(ImageValues.describe(value).orEmpty(), style = MaterialTheme.typography.bodySmall)
                        StoredImage(
                            value,
                            contentDescription = "${field.name} image",
                            modifier = Modifier
                                .padding(top = 4.dp)
                                .heightIn(max = 220.dp)
                                .clickable(role = Role.Button, onClickLabel = "View") { onViewImage(field.name, value) },
                        )
                    }
                    field.dataType == FieldDataType.Blob -> Text(value, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                    field.dataType == FieldDataType.Boolean -> Text(if (value == "true") "True" else "False")
                    else -> Text(value)
                }
            }
            if ((field.dataType == FieldDataType.Blob || field.dataType == FieldDataType.Image) && value.isNotEmpty()) {
                TextButton(onClick = { onSaveAs(field.name, value) }, enabled = !state.transferring) { Text("Save as…") }
            }
        }
    }
    state.transfer?.let { transfer -> if (state.transferring) item(key = "transfer") { Muted(transfer) } }
}

private fun LazyListScope.properties(item: Item) {
    if (item.properties.isEmpty()) return
    item(key = "properties-title") { Heading("Properties") }
    items(item.properties, key = { "property-${it.key}" }) { property ->
        Text("${property.key} = ${property.value}", modifier = Modifier.padding(vertical = 2.dp))
    }
}

private fun LazyListScope.links(title: String, key: String, links: List<Link>, incoming: Boolean, onOpenItem: (Int) -> Unit) {
    item(key = "$key-title") { Heading(title) }
    if (links.isEmpty()) {
        item(key = "$key-none") { Muted("None") }
        return
    }
    items(links, key = { "$key-${it.id}" }) { link ->
        val targetId = if (incoming) link.fromItemId else link.toItemId
        val targetTitle = if (incoming) link.fromItemTitle else link.toItemTitle
        Column(
            Modifier
                .fillMaxWidth()
                .clickable(enabled = targetId != null, role = Role.Button, onClickLabel = "Open $targetTitle") {
                    targetId?.let(onOpenItem)
                }
                .padding(vertical = 10.dp),
        ) {
            Text(targetTitle, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.primary)
            Text(
                linkDescription(link.linkType, link.customValue) + if (link.position != 0) " · position ${link.position}" else "",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}
