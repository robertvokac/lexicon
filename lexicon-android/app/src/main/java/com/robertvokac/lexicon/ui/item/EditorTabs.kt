package com.robertvokac.lexicon.ui.item

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DatePicker
import androidx.compose.material3.DatePickerDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.InputChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.VerticalDivider
import androidx.compose.material3.rememberDatePickerState
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ImageValues
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.ui.common.Choice
import com.robertvokac.lexicon.ui.common.ChoiceField
import com.robertvokac.lexicon.ui.common.LiteralTextKeyboard
import com.robertvokac.lexicon.ui.common.SectionHeader
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.WholeNumber
import com.robertvokac.lexicon.ui.common.TextInputDialog
import com.robertvokac.lexicon.ui.markdown.FormattingAction
import com.robertvokac.lexicon.ui.markdown.Markdown
import com.robertvokac.lexicon.ui.markdown.MarkdownBlocks
import com.robertvokac.lexicon.ui.markdown.MdBlock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.withContext

// Content ----------------------------------------------------------------------

/**
 * Markdown source and preview. A phone shows one at a time, as the web client
 * does on a narrow screen; a wide screen shows both side by side.
 */
@Composable
fun ContentTab(viewModel: ItemEditorViewModel) {
    var previewing by rememberSaveable { mutableStateOf(false) }
    var askLanguage by rememberSaveable { mutableStateOf(false) }
    var pickingItem by rememberSaveable { mutableStateOf(false) }
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val sideBySide = maxWidth >= 720.dp
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                if (!sideBySide) {
                    SingleChoiceSegmentedButtonRow(Modifier.padding(end = 8.dp)) {
                        SegmentedButton(
                            selected = !previewing,
                            onClick = { previewing = false },
                            shape = SegmentedButtonDefaults.itemShape(0, 2),
                        ) { Text("Source") }
                        SegmentedButton(
                            selected = previewing,
                            onClick = { previewing = true },
                            shape = SegmentedButtonDefaults.itemShape(1, 2),
                        ) { Text("Preview") }
                    }
                }
                if (sideBySide || !previewing) {
                    FormattingToolbar(
                        onAction = { action ->
                            if (action == FormattingAction.CodeBlock) askLanguage = true else viewModel.format(action)
                        },
                        onItemLink = { pickingItem = true },
                        modifier = Modifier.weight(1f),
                    )
                }
            }
            Row(Modifier.fillMaxSize()) {
                if (sideBySide || !previewing) {
                    OutlinedTextField(
                        state = viewModel.content,
                        placeholder = { Text("Markdown content…") },
                        lineLimits = TextFieldLineLimits.MultiLine(),
                        keyboardOptions = LiteralTextKeyboard,
                        textStyle = TextStyle(fontFamily = FontFamily.Monospace, fontSize = MaterialTheme.typography.bodyMedium.fontSize),
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxHeight()
                            .padding(8.dp)
                            .semantics { contentDescription = "Markdown content" },
                    )
                }
                if (sideBySide) VerticalDivider()
                if (sideBySide || previewing) {
                    MarkdownPreview(viewModel, Modifier.weight(1f).fillMaxHeight())
                }
            }
        }
    }
    if (pickingItem) {
        ItemPickerDialog(
            title = "Link to an item",
            onPick = { item ->
                pickingItem = false
                viewModel.insertItemLink(item.displayTitle)
            },
            onDismiss = { pickingItem = false },
        )
    }
    if (askLanguage) {
        CodeLanguageDialog(
            viewModel = viewModel,
            onConfirm = { language ->
                askLanguage = false
                viewModel.format(FormattingAction.CodeBlock, language)
            },
            onDismiss = { askLanguage = false },
        )
    }
}

@Composable
private fun FormattingToolbar(onAction: (FormattingAction) -> Unit, onItemLink: () -> Unit, modifier: Modifier = Modifier) {
    Row(modifier.horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
        FormattingAction.entries.forEach { action ->
            TextButton(
                onClick = { onAction(action) },
                modifier = Modifier.semantics { contentDescription = action.description },
            ) { Text(action.label) }
        }
        TextButton(
            onClick = onItemLink,
            modifier = Modifier.semantics { contentDescription = "Link to an item" },
        ) { Text("[[ ]]") }
    }
}

/** Renders a moment after typing stops, off the main thread, so long notes stay responsive. */
@Composable
private fun MarkdownPreview(viewModel: ItemEditorViewModel, modifier: Modifier) {
    var blocks by remember { mutableStateOf<List<MdBlock>>(emptyList()) }
    LaunchedEffect(viewModel) {
        var first = true
        snapshotFlow { viewModel.content.text }.collectLatest { text ->
            if (!first) delay(PREVIEW_DEBOUNCE_MS)
            first = false
            blocks = withContext(Dispatchers.Default) { Markdown.parse(text.toString()) }
        }
    }
    Box(modifier.verticalScroll(rememberScrollState()).padding(16.dp)) {
        if (blocks.isEmpty()) {
            Text("Nothing to preview yet.", color = MaterialTheme.colorScheme.onSurfaceVariant)
        } else {
            MarkdownBlocks(blocks)
        }
    }
}

private const val PREVIEW_DEBOUNCE_MS = 300L

@Composable
private fun CodeLanguageDialog(viewModel: ItemEditorViewModel, onConfirm: (String) -> Unit, onDismiss: () -> Unit) {
    // The language of the last code block is offered for the next one.
    var initial by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) { initial = viewModel.lastCodeLanguage() }
    initial?.let { language ->
        TextInputDialog(
            title = "Code block",
            label = "Language (e.g. cpp, python, sql)",
            initial = language,
            confirmLabel = "Insert",
            onConfirm = onConfirm,
            onDismiss = onDismiss,
        )
    }
}

// Values -----------------------------------------------------------------------

@Composable
fun ValuesTab(state: EditorState, viewModel: ItemEditorViewModel) {
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        when {
            state.fields.typeId == null -> Text("Choose a type on the General tab to enter its values.")
            state.fieldsLoadFailed -> Text("The fields of this type could not be loaded.", color = MaterialTheme.colorScheme.error)
            state.typeFields.isEmpty() -> Text("This type has no fields yet.")
            else -> state.typeFields.forEach { field ->
                val id = field.id ?: return@forEach
                FieldEditor(
                    field = field,
                    value = state.fields.values[id].orEmpty(),
                    blob = state.blobs[id],
                    onChange = { viewModel.setValue(id, it) },
                    onUpload = { viewModel.uploadBlob(id, it) },
                    onUploadImage = { viewModel.uploadImage(id, it) },
                    onDownload = { hash, uri -> viewModel.downloadBlob(id, hash, uri) },
                    onClearBlob = { viewModel.clearBlob(id) },
                )
            }
        }
    }
}

/** A native editor for one typed field, storing the REST string form. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun FieldEditor(
    field: ItemField,
    value: String,
    blob: BlobStatus?,
    onChange: (String) -> Unit,
    onUpload: (android.net.Uri) -> Unit,
    onUploadImage: (android.net.Uri) -> Unit,
    onDownload: (String, android.net.Uri) -> Unit,
    onClearBlob: () -> Unit,
) {
    val problem = FieldValues.problem(field, value)
    when (field.dataType) {
        FieldDataType.Boolean -> Column {
            Text(field.name, style = MaterialTheme.typography.labelLarge)
            val options = listOf("" to "Not set", "false" to "False", "true" to "True")
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                options.forEachIndexed { index, (stored, label) ->
                    SegmentedButton(
                        selected = value == stored,
                        onClick = { onChange(stored) },
                        shape = SegmentedButtonDefaults.itemShape(index, options.size),
                    ) { Text(label) }
                }
            }
        }
        FieldDataType.Enum -> ChoiceField(
            label = field.name,
            choices = listOf(Choice("", "Not set")) + field.enumOptions.map { Choice(it, it) } +
                (if (value.isNotEmpty() && value !in field.enumOptions) listOf(Choice(value, "$value (not an option)")) else emptyList()),
            selected = value,
            onSelected = onChange,
            supportingText = problem,
        )
        FieldDataType.Blob -> BlobEditor(field, value, blob, onUpload, onDownload, onClearBlob)
        FieldDataType.Image -> ImageEditor(field, value, blob, onUploadImage, onDownload, onClearBlob)
        FieldDataType.Date -> PickerTextField(field, value, problem, "YYYY-MM-DD", Icons.Filled.CalendarMonth, "Pick a date", onChange) { done ->
            DateDialog(initial = value, onPicked = { onChange(it); done() }, onDismiss = done)
        }
        FieldDataType.Time -> PickerTextField(field, value, problem, "HH:MM:SS", Icons.Filled.Schedule, "Pick a time", onChange) { done ->
            TimeDialog(initial = value, onPicked = { onChange(it); done() }, onDismiss = done)
        }
        FieldDataType.Timestamp -> PickerTextField(field, value, problem, "YYYY-MM-DDTHH:MM:SS", Icons.Filled.CalendarMonth, "Pick a date and time", onChange) { done ->
            var date by remember { mutableStateOf<String?>(null) }
            val chosen = date
            if (chosen == null) {
                DateDialog(initial = FieldValues.timestampDate(value).orEmpty(), onPicked = { date = it }, onDismiss = done)
            } else {
                TimeDialog(
                    initial = FieldValues.timestampTime(value).orEmpty(),
                    onPicked = {
                        onChange(FieldValues.timestamp(chosen, it))
                        done()
                    },
                    onDismiss = done,
                )
            }
        }
        FieldDataType.Integer, FieldDataType.Float -> OutlinedTextField(
            value = value,
            onValueChange = { onChange(FieldValues.sanitize(field.dataType, it)) },
            label = { Text(field.name) },
            singleLine = true,
            isError = problem != null,
            supportingText = { Text(problem ?: field.dataType.name) },
            keyboardOptions = KeyboardOptions(
                keyboardType = if (field.dataType == FieldDataType.Integer) KeyboardType.Number else KeyboardType.Decimal,
            ),
            modifier = Modifier.fillMaxWidth(),
        )
        FieldDataType.Text -> OutlinedTextField(
            value = value,
            onValueChange = onChange,
            label = { Text(field.name) },
            minLines = 3,
            supportingText = { Text("Text") },
            keyboardOptions = LiteralTextKeyboard,
            modifier = Modifier.fillMaxWidth(),
        )
        FieldDataType.Other -> OutlinedTextField(
            value = value,
            onValueChange = onChange,
            label = { Text(field.name) },
            singleLine = true,
            supportingText = { Text("Other") },
            keyboardOptions = LiteralTextKeyboard,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun PickerTextField(
    field: ItemField,
    value: String,
    problem: String?,
    placeholder: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    pickLabel: String,
    onChange: (String) -> Unit,
    picker: @Composable (done: () -> Unit) -> Unit,
) {
    var picking by rememberSaveable { mutableStateOf(false) }
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        label = { Text(field.name) },
        placeholder = { Text(placeholder) },
        singleLine = true,
        isError = problem != null,
        supportingText = { Text(problem ?: "${field.dataType.name} · $placeholder") },
        trailingIcon = {
            Row {
                if (value.isNotEmpty()) {
                    IconButton(onClick = { onChange("") }) { Icon(Icons.Filled.Clear, contentDescription = "Clear ${field.name}") }
                }
                IconButton(onClick = { picking = true }) { Icon(icon, contentDescription = "$pickLabel for ${field.name}") }
            }
        },
        keyboardOptions = LiteralTextKeyboard,
        modifier = Modifier.fillMaxWidth(),
    )
    if (picking) picker { picking = false }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun DateDialog(initial: String, onPicked: (String) -> Unit, onDismiss: () -> Unit) {
    val pickerState = rememberDatePickerState(initialSelectedDateMillis = FieldValues.dateToPickerMillis(initial))
    DatePickerDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(
                onClick = { pickerState.selectedDateMillis?.let { onPicked(FieldValues.pickerMillisToDate(it)) } ?: onDismiss() },
            ) { Text("OK") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    ) {
        DatePicker(state = pickerState)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun TimeDialog(initial: String, onPicked: (String) -> Unit, onDismiss: () -> Unit) {
    val (hour, minute) = FieldValues.timeParts(initial) ?: (12 to 0)
    val pickerState = rememberTimePickerState(initialHour = hour, initialMinute = minute, is24Hour = true)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Time") },
        text = { TimePicker(state = pickerState) },
        confirmButton = {
            TextButton(onClick = { onPicked(FieldValues.formatTime(pickerState.hour, pickerState.minute)) }) { Text("OK") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

/**
 * Upload picks a document through the system picker and sends its bytes;
 * Save as writes the stored bytes to a document the person creates.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun BlobEditor(
    field: ItemField,
    value: String,
    blob: BlobStatus?,
    onUpload: (android.net.Uri) -> Unit,
    onDownload: (String, android.net.Uri) -> Unit,
    onClear: () -> Unit,
) {
    val open = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let(onUpload) }
    val create = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null && LexiconApi.isBlobHash(value)) onDownload(value, uri)
    }
    val busy = blob?.busy == true
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(field.name, style = MaterialTheme.typography.labelLarge)
        Text(
            value.ifEmpty { "No file (SHA-256)" },
            fontFamily = FontFamily.Monospace,
            style = MaterialTheme.typography.bodySmall,
            color = if (value.isEmpty()) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurface,
        )
        blob?.let { Text(it.text, style = MaterialTheme.typography.bodySmall) }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { open.launch(arrayOf("*/*")) }, enabled = !busy) {
                Text(if (value.isEmpty()) "Upload…" else "Replace…")
            }
            OutlinedButton(
                onClick = { create.launch(BlobTransfer.suggestedName(field.name, value)) },
                enabled = !busy && LexiconApi.isBlobHash(value),
            ) { Text("Save as…") }
            TextButton(onClick = onClear, enabled = !busy && value.isNotEmpty()) { Text("Clear") }
        }
    }
}

/**
 * An Image field: the picture, what kind of image it is, and Choose, View,
 * Save as and Clear. Only PNG, JPEG, GIF, WebP and BMP documents are offered.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ImageEditor(
    field: ItemField,
    value: String,
    blob: BlobStatus?,
    onPick: (android.net.Uri) -> Unit,
    onDownload: (String, android.net.Uri) -> Unit,
    onClear: () -> Unit,
) {
    val image = ImageValues.parse(value)
    val open = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let(onPick) }
    val create = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument(image?.mediaType ?: "image/*")) { uri ->
        val hash = ImageValues.parse(value)?.hash
        if (uri != null && hash != null) onDownload(hash, uri)
    }
    var viewing by rememberSaveable { mutableStateOf(false) }
    val busy = blob?.busy == true
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(field.name, style = MaterialTheme.typography.labelLarge)
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Box(
                Modifier
                    .size(width = 160.dp, height = 120.dp)
                    .clip(MaterialTheme.shapes.small)
                    .background(MaterialTheme.colorScheme.surfaceVariant)
                    .clickable(enabled = image != null, role = Role.Button, onClickLabel = "View") { viewing = true },
                contentAlignment = Alignment.Center,
            ) {
                if (image != null) {
                    StoredImage(value, contentDescription = "${field.name} image", maxEdge = 480, modifier = Modifier.fillMaxSize())
                } else {
                    Text("No image", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ImageValues.describe(value)?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }
                blob?.let { Text(it.text, style = MaterialTheme.typography.bodySmall) }
            }
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { open.launch(ImageValues.mediaTypes.toTypedArray()) }, enabled = !busy) {
                Text(if (image == null) "Choose image…" else "Replace…")
            }
            OutlinedButton(onClick = { viewing = true }, enabled = image != null) { Text("View") }
            OutlinedButton(
                onClick = { create.launch(ImageValues.fileName(field.name, value)) },
                enabled = !busy && image != null,
            ) { Text("Save as…") }
            TextButton(onClick = onClear, enabled = !busy && value.isNotEmpty()) { Text("Clear") }
        }
    }
    if (viewing && image != null) ImageViewerDialog(value, field.name) { viewing = false }
}

// Metadata ---------------------------------------------------------------------

/** Tags, flags, aliases and properties, each with Add, Edit and Remove. */
@Composable
fun MetadataTab(state: EditorState, viewModel: ItemEditorViewModel) {
    LaunchedEffect(Unit) { viewModel.loadSuggestions() }
    var dialog by remember { mutableStateOf<MetadataDialog?>(null) }
    val fields = state.fields
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        ValueSection("Tags", "Add tag", fields.tags, onAdd = { dialog = MetadataDialog.Tag(-1) }, onEdit = { dialog = MetadataDialog.Tag(it) }, onRemove = viewModel::removeTag)
        ValueSection("Flags", "Add flag", fields.flags, onAdd = { dialog = MetadataDialog.Flag(-1) }, onEdit = { dialog = MetadataDialog.Flag(it) }, onRemove = viewModel::removeFlag)
        ValueSection("Aliases", "Add alias", fields.aliases, onAdd = { dialog = MetadataDialog.Alias(-1) }, onEdit = { dialog = MetadataDialog.Alias(it) }, onRemove = viewModel::removeAlias)
        SectionHeader("Properties")
        if (fields.properties.isEmpty()) Text("None", color = MaterialTheme.colorScheme.onSurfaceVariant)
        fields.properties.forEachIndexed { index, property ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("${property.key} = ${property.value}", modifier = Modifier.weight(1f))
                IconButton(onClick = { dialog = MetadataDialog.PropertyEdit(index) }) {
                    Icon(Icons.Filled.Edit, contentDescription = "Edit property ${property.key}")
                }
                IconButton(onClick = { viewModel.removeProperty(index) }) {
                    Icon(Icons.Filled.Delete, contentDescription = "Remove property ${property.key}")
                }
            }
        }
        OutlinedButton(onClick = { dialog = MetadataDialog.PropertyEdit(-1) }) {
            Icon(Icons.Filled.Add, contentDescription = null)
            Text("Add property")
        }
    }

    when (val current = dialog) {
        is MetadataDialog.Tag -> TextInputDialog(
            title = if (current.index < 0) "Add tag" else "Edit tag",
            label = if (current.index < 0) "Values, separated by commas" else "Value",
            initial = fields.tags.getOrNull(current.index).orEmpty(),
            suggestions = state.tagSuggestions,
            onConfirm = {
                if (current.index < 0) viewModel.addTags(it) else viewModel.editTag(current.index, it)
                dialog = null
            },
            onDismiss = { dialog = null },
        )
        is MetadataDialog.Flag -> TextInputDialog(
            title = if (current.index < 0) "Add flag" else "Edit flag",
            label = if (current.index < 0) "Values, separated by commas" else "Value",
            initial = fields.flags.getOrNull(current.index).orEmpty(),
            suggestions = state.flagSuggestions,
            onConfirm = {
                if (current.index < 0) viewModel.addFlags(it) else viewModel.editFlag(current.index, it)
                dialog = null
            },
            onDismiss = { dialog = null },
        )
        is MetadataDialog.Alias -> TextInputDialog(
            title = if (current.index < 0) "Add alias" else "Edit alias",
            label = "Value",
            initial = fields.aliases.getOrNull(current.index).orEmpty(),
            suggestions = state.aliasSuggestions,
            onConfirm = {
                if (current.index < 0) viewModel.addAlias(it) else viewModel.editAlias(current.index, it)
                dialog = null
            },
            onDismiss = { dialog = null },
        )
        is MetadataDialog.PropertyEdit -> PropertyDialog(
            title = if (current.index < 0) "Add property" else "Edit property",
            initialKey = fields.properties.getOrNull(current.index)?.key.orEmpty(),
            initialValue = fields.properties.getOrNull(current.index)?.value.orEmpty(),
            onConfirm = { key, value -> viewModel.putProperty(current.index, key, value).also { if (it == null) dialog = null } },
            onDismiss = { dialog = null },
        )
        null -> Unit
    }
}

private sealed interface MetadataDialog {
    data class Tag(val index: Int) : MetadataDialog
    data class Flag(val index: Int) : MetadataDialog
    data class Alias(val index: Int) : MetadataDialog
    data class PropertyEdit(val index: Int) : MetadataDialog
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ValueSection(title: String, addLabel: String, values: List<String>, onAdd: () -> Unit, onEdit: (Int) -> Unit, onRemove: (Int) -> Unit) {
    SectionHeader(title)
    if (values.isEmpty()) Text("None", color = MaterialTheme.colorScheme.onSurfaceVariant)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        values.forEachIndexed { index, value ->
            InputChip(
                selected = false,
                onClick = { onEdit(index) },
                label = { Text(value) },
                trailingIcon = {
                    IconButton(onClick = { onRemove(index) }) {
                        Icon(Icons.Filled.Clear, contentDescription = "Remove $value")
                    }
                },
                modifier = Modifier.semantics { contentDescription = "$value. Edit" },
            )
        }
    }
    OutlinedButton(onClick = onAdd) {
        Icon(Icons.Filled.Add, contentDescription = null)
        Text(addLabel)
    }
}

@Composable
private fun PropertyDialog(
    title: String,
    initialKey: String,
    initialValue: String,
    onConfirm: (key: String, value: String) -> String?,
    onDismiss: () -> Unit,
) {
    var key by rememberSaveable { mutableStateOf(initialKey) }
    var value by rememberSaveable { mutableStateOf(initialValue) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SyncedTextField(
                    value = key,
                    onValueChange = {
                        key = it
                        error = null
                    },
                    label = "Key",
                    singleLine = true,
                    isError = error != null,
                    supportingText = error,
                    keyboardOptions = LiteralTextKeyboard,
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = value,
                    onValueChange = { value = it },
                    label = "Value",
                    keyboardOptions = LiteralTextKeyboard,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = { TextButton(onClick = { error = onConfirm(key, value) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

// Links and backlinks -----------------------------------------------------------

/**
 * Outgoing links, or incoming ones. Both lists are saved with the item as
 * their complete state, in the same request.
 */
@Composable
fun LinksTab(state: EditorState, viewModel: ItemEditorViewModel, incoming: Boolean) {
    val links = if (incoming) state.fields.backlinks else state.fields.links
    var editing by remember { mutableStateOf<LinkEntry?>(null) }
    var adding by rememberSaveable { mutableStateOf(false) }
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
        item {
            Text(
                if (incoming) "Items that link to this one." else "Items this one links to.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(vertical = 8.dp),
            )
        }
        if (links.isEmpty()) item { Text("None", color = MaterialTheme.colorScheme.onSurfaceVariant) }
        items(links, key = { it.key }) { link ->
            Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(link.title, style = MaterialTheme.typography.bodyLarge)
                    Text(
                        "[${link.position}] ${linkDescription(link.linkType, link.customValue)}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                IconButton(onClick = { editing = link }) { Icon(Icons.Filled.Edit, contentDescription = "Edit link to ${link.title}") }
                IconButton(onClick = { viewModel.removeLink(incoming, link.key) }) {
                    Icon(Icons.Filled.Delete, contentDescription = "Remove link to ${link.title}")
                }
            }
            HorizontalDivider()
        }
        item {
            OutlinedButton(onClick = { adding = true }, modifier = Modifier.padding(vertical = 12.dp)) {
                Icon(Icons.Filled.Add, contentDescription = null)
                Text(if (incoming) "Add backlink" else "Add link")
            }
        }
        if (!incoming) {
            item {
                OutlinedButton(onClick = viewModel::addLinksFromContent, modifier = Modifier.padding(bottom = 12.dp)) {
                    Text("Add links from content")
                }
                state.linksFromContent?.let {
                    Text(
                        it,
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(bottom = 12.dp).semantics { liveRegion = LiveRegionMode.Polite },
                    )
                }
            }
        }
    }
    if (adding || editing != null) {
        LinkDialog(
            title = when {
                incoming && editing != null -> "Edit backlink"
                incoming -> "Add backlink"
                editing != null -> "Edit link"
                else -> "Add link"
            },
            targetLabel = if (incoming) "Source item" else "Target item",
            initial = editing,
            newKey = viewModel::newLinkKey,
            onConfirm = {
                viewModel.putLink(incoming, it)
                adding = false
                editing = null
            },
            onDismiss = {
                adding = false
                editing = null
            },
        )
    }
}

@Composable
private fun LinkDialog(
    title: String,
    targetLabel: String,
    initial: LinkEntry?,
    newKey: () -> Long,
    onConfirm: (LinkEntry) -> Unit,
    onDismiss: () -> Unit,
) {
    var targetId by rememberSaveable { mutableStateOf(initial?.itemId) }
    var targetTitle by rememberSaveable { mutableStateOf(initial?.title.orEmpty()) }
    var type by rememberSaveable { mutableStateOf(initial?.linkType ?: LinkType.Related) }
    var custom by rememberSaveable { mutableStateOf(initial?.customValue.orEmpty()) }
    var position by rememberSaveable { mutableStateOf((initial?.position ?: 0).toString()) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    var picking by rememberSaveable { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.verticalScroll(rememberScrollState())) {
                Text(targetLabel, style = MaterialTheme.typography.labelLarge)
                OutlinedButton(onClick = { picking = true }, modifier = Modifier.fillMaxWidth()) {
                    Text(if (targetId == null) "Choose…" else targetTitle)
                }
                ChoiceField(
                    label = "Link type",
                    choices = LinkType.persistable.map { Choice(it, it.label) },
                    selected = type,
                    onSelected = {
                        type = it
                        error = null
                    },
                )
                SyncedTextField(
                    value = custom,
                    onValueChange = {
                        custom = it
                        error = null
                    },
                    label = "Custom value",
                    enabled = type == LinkType.Custom,
                    singleLine = true,
                    keyboardOptions = LiteralTextKeyboard,
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = position,
                    onValueChange = { position = it },
                    accept = WholeNumber,
                    label = "Position",
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth(),
                )
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val target = targetId
                error = when {
                    target == null -> "Choose ${targetLabel.lowercase()}."
                    type == LinkType.Custom && custom.isBlank() -> "Custom links need a value."
                    else -> null
                }
                if (error == null && target != null) {
                    onConfirm(
                        LinkEntry(
                            key = initial?.key ?: newKey(),
                            id = initial?.id,
                            itemId = target,
                            title = targetTitle,
                            linkType = type,
                            customValue = if (type == LinkType.Custom) custom.trim() else "",
                            position = position.toIntOrNull() ?: 0,
                        ),
                    )
                }
            }) { Text("OK") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
    if (picking) {
        ItemPickerDialog(
            title = targetLabel,
            onPick = { item ->
                targetId = item.id
                targetTitle = item.displayTitle
                error = null
                picking = false
            },
            onDismiss = { picking = false },
        )
    }
}
