package com.robertvokac.lexicon.ui.items

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.PropertyFilter
import com.robertvokac.lexicon.model.SortOrder
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.ui.common.Choice
import com.robertvokac.lexicon.ui.common.ChoiceField
import com.robertvokac.lexicon.ui.common.LiteralTextKeyboard
import com.robertvokac.lexicon.ui.common.SectionHeader
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.Digits

/**
 * Every filter of the desktop table's filter row, stacked for a phone. Each
 * change applies at once; the server does the filtering.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FilterSheet(state: ItemsUiState, viewModel: ItemsViewModel, onDismiss: () -> Unit) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var editingProperty by rememberSaveable { mutableStateOf<Int?>(null) }
    var addingProperty by rememberSaveable { mutableStateOf(false) }
    val filters = state.filters

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        // The handle carries the sheet's expand, collapse and dismiss actions;
        // the default one is a 32 dp target, this one a 48 dp one.
        dragHandle = {
            Box(Modifier.sizeIn(minWidth = 48.dp, minHeight = 48.dp), contentAlignment = Alignment.Center) {
                BottomSheetDefaults.DragHandle()
            }
        },
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .navigationBarsPadding(),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Filters and sort", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                TextButton(onClick = viewModel::clearFilters, enabled = !filters.isEmpty) { Text("Clear filters") }
            }

            SectionHeader("Sort")
            ChoiceField(
                label = "Sort by",
                choices = state.sortColumns.map { Choice(it.first, it.second) },
                selected = state.sortColumn,
                onSelected = { viewModel.setSort(it, state.sortOrder) },
            )
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                SortOrder.entries.forEachIndexed { index, order ->
                    SegmentedButton(
                        selected = state.sortOrder == order,
                        onClick = { viewModel.setSort(state.sortColumn, order) },
                        shape = SegmentedButtonDefaults.itemShape(index, SortOrder.entries.size),
                    ) { Text(order.name) }
                }
            }

            SectionHeader("Group and type")
            ChoiceField(
                label = "Group",
                choices = listOf(Choice<Int?>(null, "All groups")) + state.groups.map { Choice(it.id, it.name) },
                selected = filters.groupId,
                onSelected = viewModel::setGroup,
            )
            ChoiceField(
                label = "Type",
                choices = listOf(Choice<Int?>(null, "All types")) +
                    state.types.map { Choice(it.id, it.displayName, it.description.ifEmpty { null }) },
                selected = filters.typeId,
                onSelected = viewModel::setType,
            )
            if (filters.typeId != null && state.typeFields.isNotEmpty()) {
                SectionHeader("Type fields")
                state.typeFields.forEach { field ->
                    ValueFilter(field, field.id?.let { filters.values[it] }.orEmpty()) { viewModel.setValueFilter(field, it) }
                }
            }

            SectionHeader("Columns")
            SyncedTextField(
                value = filters.id,
                onValueChange = viewModel::setIdFilter,
                label = "Id",
                accept = Digits,
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth(),
            )
            TextFilter("Title", filters.title, viewModel::setTitleFilter)
            TextFilter("Disambiguation", filters.disambiguation, viewModel::setDisambiguationFilter)
            TextFilter("Alias", filters.alias, viewModel::setAliasFilter)
            ChoiceField(
                label = "Tag",
                choices = listOf(Choice("", "All tags")) + state.tags.map { Choice(it, it) },
                selected = filters.tag,
                onSelected = viewModel::setTag,
            )
            ChoiceField(
                label = "Flag",
                choices = listOf(Choice("", "All flags")) + state.flags.map { Choice(it, it) },
                selected = filters.flag,
                onSelected = viewModel::setFlag,
            )
            ChoiceField(
                label = "Status",
                choices = listOf(Choice<ItemStatus?>(null, "All statuses")) + ItemStatus.entries.map { Choice(it, it.label) },
                selected = filters.status,
                onSelected = viewModel::setStatus,
            )
            ChoiceField(
                label = "Understanding",
                choices = listOf(Choice<UnderstandingLevel?>(null, "All levels")) +
                    UnderstandingLevel.entries.map { Choice(it, it.label, it.description) },
                selected = filters.understanding,
                onSelected = viewModel::setUnderstanding,
            )
            ChoiceField(
                label = "Pinned",
                choices = listOf(Choice<Boolean?>(null, "All"), Choice(true, "Pinned"), Choice(false, "Not pinned")),
                selected = filters.pinned,
                onSelected = viewModel::setPinned,
            )

            SectionHeader("Properties")
            Text(
                "All filters must match. Keys are exact; values contain the entered text. " +
                    "Leave a value empty to match any value for that key.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            filters.properties.forEachIndexed { index, property ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        if (property.value.isEmpty()) "${property.key} = (any)" else "${property.key} = ${property.value}",
                        modifier = Modifier.weight(1f),
                    )
                    IconButton(onClick = { editingProperty = index }) {
                        Icon(Icons.Filled.Edit, contentDescription = "Edit property filter ${property.key}")
                    }
                    IconButton(onClick = { viewModel.removePropertyFilter(index) }) {
                        Icon(Icons.Filled.Delete, contentDescription = "Remove property filter ${property.key}")
                    }
                }
            }
            OutlinedButton(onClick = { addingProperty = true }) {
                Icon(Icons.Filled.Add, contentDescription = null)
                Text("Add property filter")
            }
            TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End).padding(bottom = 16.dp)) {
                Text("Done")
            }
        }
    }

    if (addingProperty) {
        PropertyFilterDialog(
            title = "Add property filter",
            initial = PropertyFilter("", ""),
            onConfirm = {
                viewModel.addPropertyFilter(it)
                addingProperty = false
            },
            onDismiss = { addingProperty = false },
        )
    }
    editingProperty?.let { index ->
        filters.properties.getOrNull(index)?.let { current ->
            PropertyFilterDialog(
                title = "Edit property filter",
                initial = current,
                onConfirm = {
                    viewModel.replacePropertyFilter(index, it)
                    editingProperty = null
                },
                onDismiss = { editingProperty = null },
            )
        }
    }
}

@Composable
private fun TextFilter(label: String, value: String, onChange: (String) -> Unit) {
    SyncedTextField(
        value = value,
        onValueChange = onChange,
        label = label,
        singleLine = true,
        keyboardOptions = LiteralTextKeyboard,
        modifier = Modifier.fillMaxWidth(),
    )
}

/** Enum and Boolean fields filter by choice, everything else by text. */
@Composable
private fun ValueFilter(field: ItemField, value: String, onChange: (String) -> Unit) {
    when (field.dataType) {
        FieldDataType.Boolean -> ChoiceField(
            label = field.name,
            choices = listOf(Choice("", "Any"), Choice("false", "False"), Choice("true", "True")),
            selected = value,
            onSelected = onChange,
        )
        FieldDataType.Enum -> ChoiceField(
            label = field.name,
            choices = listOf(Choice("", "Any")) + field.enumOptions.map { Choice(it, it) },
            selected = value,
            onSelected = onChange,
        )
        else -> SyncedTextField(
            value = value,
            onValueChange = onChange,
            label = field.name,
            placeholder = placeholderFor(field.dataType),
            supportingText = if (field.dataType.filtersExactly) "Matches exactly" else "Contains",
            singleLine = true,
            keyboardOptions = when (field.dataType) {
                FieldDataType.Integer -> KeyboardOptions(keyboardType = KeyboardType.Number)
                FieldDataType.Float -> KeyboardOptions(keyboardType = KeyboardType.Decimal)
                else -> LiteralTextKeyboard
            },
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

private fun placeholderFor(type: FieldDataType): String? = when (type) {
    FieldDataType.Date -> "YYYY-MM-DD"
    FieldDataType.Time -> "HH:MM:SS"
    FieldDataType.Timestamp -> "YYYY-MM-DDTHH:MM:SS"
    FieldDataType.Blob -> "SHA-256"
    else -> null
}

@Composable
private fun PropertyFilterDialog(
    title: String,
    initial: PropertyFilter,
    onConfirm: (PropertyFilter) -> Unit,
    onDismiss: () -> Unit,
) {
    var key by rememberSaveable { mutableStateOf(initial.key) }
    var value by rememberSaveable { mutableStateOf(initial.value) }
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
                    label = "Value contains",
                    singleLine = true,
                    keyboardOptions = LiteralTextKeyboard,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (key.isBlank()) error = "Key cannot be empty." else onConfirm(PropertyFilter(key.trim(), value.trim()))
            }) { Text("OK") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
