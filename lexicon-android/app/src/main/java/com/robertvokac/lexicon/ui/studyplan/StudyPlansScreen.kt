package com.robertvokac.lexicon.ui.studyplan

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.StudyPlan
import com.robertvokac.lexicon.model.StudyPlanOverview
import com.robertvokac.lexicon.model.StudyPlanType
import com.robertvokac.lexicon.model.StudyUnitType
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.item.DateDialog
import java.time.LocalDate
import java.util.Locale
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class StudyPlansState(
    val loading: Boolean = true,
    val error: String? = null,
    val plans: List<StudyPlanOverview> = emptyList(),
    val date: String = "",
    val busy: Boolean = false,
    val saveError: String? = null,
    val message: String? = null,
)

class StudyPlansViewModel(container: AppContainer) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(StudyPlansState())
    val state = _state.asStateFlow()

    init { load() }

    fun load() {
        val date = LocalDate.now().toString()
        viewModelScope.launch {
            try {
                val plans = api.studyPlanOverview(date)
                _state.update { it.copy(loading = false, error = null, plans = plans, date = date) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage()) }
            }
        }
    }

    fun save(plan: StudyPlan, onDone: () -> Unit = {}) {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true, saveError = null) }
        viewModelScope.launch {
            try {
                if (plan.id == null) api.createStudyPlan(plan) else api.updateStudyPlan(plan.id, plan)
                val date = LocalDate.now().toString()
                val plans = api.studyPlanOverview(date)
                _state.update { it.copy(loading = false, plans = plans, date = date, busy = false, saveError = null) }
                onDone()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, saveError = failure.userMessage()) }
            }
        }
    }

    fun delete(plan: StudyPlan) {
        val id = plan.id ?: return
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                api.deleteStudyPlan(id)
                val date = LocalDate.now().toString()
                val plans = api.studyPlanOverview(date)
                _state.update { it.copy(plans = plans, date = date, busy = false, message = "Deleted ${plan.item}.") }
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun clearSaveError() = _state.update { it.copy(saveError = null) }
    fun clearMessage() = _state.update { it.copy(message = null) }
}

private fun pace(value: Double): String = String.format(Locale.getDefault(), "%.2f", value)
private val weekdays = listOf("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")

private class StudyBrowseState(expanded: Boolean = false, query: String = "", page: Int = 0) {
    var expanded by mutableStateOf(expanded)
    var query by mutableStateOf(query)
    var page by mutableIntStateOf(page)

    companion object {
        val Saver = listSaver<StudyBrowseState, Any>(
            save = { listOf(it.expanded, it.query, it.page) },
            restore = { StudyBrowseState(it[0] as Boolean, it[1] as String, it[2] as Int) },
        )
    }
}

private fun LazyListScope.studyBrowseSection(key: String, title: String, values: List<StudyPlanOverview>,
    browse: StudyBrowseState, card: @Composable (StudyPlanOverview) -> Unit) {
    item(key = "$key-heading") {
        TextButton(onClick = { browse.expanded = !browse.expanded }, modifier = Modifier.fillMaxWidth()) {
            Text("${if (browse.expanded) "Hide" else "Show"} $title (${values.size})",
                style = MaterialTheme.typography.titleLarge)
        }
    }
    if (!browse.expanded) return
    val result = studyPlanPage(values, browse.query, browse.page)
    item(key = "$key-search") {
        Column(Modifier.padding(horizontal = 16.dp)) {
            if (key == "finished") Text("Completed plans and plans past their deadline.",
                style = MaterialTheme.typography.bodySmall)
            OutlinedTextField(value = browse.query, onValueChange = { browse.query = it; browse.page = 0 },
                label = { Text("Search ${title.lowercase()} plans") },
                placeholder = { Text("Title, group or note") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        }
    }
    item(key = "$key-pages") {
        Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
            val first = if (result.total == 0) 0 else result.page * STUDY_PAGE_SIZE + 1
            val last = minOf((result.page + 1) * STUDY_PAGE_SIZE, result.total)
            Text("$first–$last of ${result.total} · Page ${result.page + 1} of ${result.pageCount}",
                style = MaterialTheme.typography.bodySmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(enabled = result.page > 0, onClick = { browse.page = result.page - 1 }) { Text("Previous") }
                OutlinedButton(enabled = result.page + 1 < result.pageCount,
                    onClick = { browse.page = result.page + 1 }) { Text("Next") }
            }
        }
    }
    if (result.values.isEmpty()) item(key = "$key-empty") {
        Text(if (values.isEmpty()) "No plans here." else "No matching plans.", modifier = Modifier.padding(16.dp))
    }
    items(result.values, key = { "$key-${it.plan.id}" }) { card(it) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StudyPlansScreen(viewModel: StudyPlansViewModel, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var editing by remember { mutableStateOf<StudyPlan?>(null) }
    var adding by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<StudyPlan?>(null) }
    var completing by remember { mutableStateOf<StudyPlan?>(null) }
    var progress by remember { mutableStateOf<StudyPlan?>(null) }
    val upcomingBrowse = rememberSaveable(saver = StudyBrowseState.Saver) { StudyBrowseState() }
    val finishedBrowse = rememberSaveable(saver = StudyBrowseState.Saver) { StudyBrowseState() }

    LifecycleResumeEffect(Unit) { viewModel.load(); onPauseOrDispose { } }
    state.message?.let { message -> LaunchedEffect(message) { viewModel.clearMessage(); snackbar.showSnackbar(message) } }
    Scaffold(
        topBar = { TopAppBar(title = { Text("Study Plan") },
            navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") } }) },
        floatingActionButton = { FloatingActionButton(onClick = { adding = true }) { Icon(Icons.Filled.Add, "Add Study Plan") } },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.error != null -> ErrorBox(state.error.orEmpty(), onRetry = viewModel::load, modifier = Modifier.padding(padding))
            else -> {
                val priority = mapOf("At risk" to 0, "Behind" to 1, "On track" to 2, "Completed" to 3)
                val active = state.plans.filter { it.active && !it.complete }.sortedWith(compareBy({ priority[it.status] ?: 4 }, { it.plan.item }))
                val upcoming = state.plans.filter { it.upcoming && !it.complete }
                    .sortedWith(compareBy({ it.plan.startDate }, { it.plan.item }, { it.plan.id }))
                val past = state.plans.filter { it.ended || it.complete }
                    .sortedWith(compareByDescending<StudyPlanOverview> { it.plan.endDate }.thenBy { it.plan.item }.thenBy { it.plan.id })
                LaunchedEffect(upcoming, upcomingBrowse.query) {
                    upcomingBrowse.page = studyPlanPage(upcoming, upcomingBrowse.query, upcomingBrowse.page).page
                }
                LaunchedEffect(past, finishedBrowse.query) {
                    finishedBrowse.page = studyPlanPage(past, finishedBrowse.query, finishedBrowse.page).page
                }
                LazyColumn(Modifier.padding(padding).fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp),
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    item { Text("Today · ${state.date}", style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(16.dp)) }
                    item { Text("Active (${active.size})", style = MaterialTheme.typography.titleLarge, modifier = Modifier.padding(horizontal = 16.dp)) }
                    if (active.isEmpty()) item { Text("No active plans today.", modifier = Modifier.padding(horizontal = 16.dp)) }
                    items(active, key = { "active-${it.plan.id}" }) { value ->
                        StudyCard(value, state.busy, onEdit = { editing = it }, onDelete = { deleting = it },
                            onProgress = { progress = it }, onTarget = { plan, target -> viewModel.save(plan.copy(currentProgress = target)) },
                            onComplete = { completing = it })
                    }
                    studyBrowseSection("upcoming", "Upcoming", upcoming, upcomingBrowse) { value ->
                        StudyCard(value, state.busy, { editing = it }, { deleting = it }, { progress = it },
                            { plan, target -> viewModel.save(plan.copy(currentProgress = target)) }, { completing = it })
                    }
                    studyBrowseSection("finished", "Finished", past, finishedBrowse) { value ->
                        StudyCard(value, state.busy, { editing = it }, { deleting = it }, { progress = it },
                            { plan, target -> viewModel.save(plan.copy(currentProgress = target)) }, { completing = it })
                    }
                    item { androidx.compose.foundation.layout.Spacer(Modifier.padding(bottom = 88.dp)) }
                }
            }
        }
    }
    if (adding || editing != null) StudyPlanEditor(editing, state.busy, state.saveError,
        onDismiss = { adding = false; editing = null; viewModel.clearSaveError() },
        onSave = { value -> viewModel.save(value) { adding = false; editing = null } })
    deleting?.let { plan -> ConfirmDialog(title = "Delete Study Plan", message = "Delete study plan \"${plan.item}\"?",
        confirmLabel = "Delete", destructive = true, onDismiss = { deleting = null },
        onConfirm = { deleting = null; viewModel.delete(plan) }) }
    completing?.let { plan -> ConfirmDialog(title = "Complete Study Plan", message = "Mark \"${plan.item}\" complete?",
        confirmLabel = "Complete", onDismiss = { completing = null },
        onConfirm = { completing = null; viewModel.save(plan.copy(currentProgress = plan.lastUnit)) }) }
    progress?.let { plan -> ProgressDialog(plan, state.busy, state.saveError, onDismiss = {
        progress = null; viewModel.clearSaveError()
    }, onSave = { target -> viewModel.save(plan.copy(currentProgress = target)) { progress = null } }) }
}

@Composable
private fun StudyCard(value: StudyPlanOverview, busy: Boolean, onEdit: (StudyPlan) -> Unit,
    onDelete: (StudyPlan) -> Unit, onProgress: (StudyPlan) -> Unit,
    onTarget: (StudyPlan, Int) -> Unit, onComplete: (StudyPlan) -> Unit) {
    val plan = value.plan
    val warning = value.status in listOf("Behind", "At risk", "Overdue")
    Card(Modifier.widthIn(max = 800.dp).fillMaxWidth().padding(horizontal = 16.dp)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text(plan.item, style = MaterialTheme.typography.titleMedium)
            Text("${if (plan.group.isNotBlank()) "${plan.group} · " else ""}${plan.type} · ${plan.unitLabel(2)} · ${plan.startDate} – ${plan.endDate}", style = MaterialTheme.typography.bodySmall)
            Text("Current progress: ${if (plan.currentProgress == 0) "0" else "${plan.unitLabel(1)} ${plan.currentProgress}"}")
            Text("Expected progress: ${if (value.expectedProgress == 0) "0" else "${plan.unitLabel(1)} ${value.expectedProgress}"}")
            Text(value.differenceText())
            Text("Expected unit range today: ${value.expectedRangeText()}")
            Text("Recommended today: ${if (value.recommendedFirst > 0) value.range(value.recommendedFirst, value.recommendedLast) else "—"}")
            Text("Planned pace: ${pace(value.plannedUnitsPerStudyDay)} ${plan.unitLabel(2)}/day")
            Text("Required now: ${value.requiredPaceText()}")
            Text("Status: ${value.status}",
                color = if (warning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary)
            if (plan.note.isNotBlank()) Text(plan.note, style = MaterialTheme.typography.bodySmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { onProgress(plan) }, enabled = !busy) { Text("Update progress") }
                OutlinedButton(onClick = { onEdit(plan) }, enabled = !busy) { Text("Edit") }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { if (value.canMarkToday()) onTarget(plan, maxOf(plan.currentProgress, value.expectedUnitEnd ?: 0, value.recommendedLast)) },
                    enabled = !busy && value.canMarkToday()) { Text("Mark today") }
                if (!value.complete) OutlinedButton(onClick = { onComplete(plan) }, enabled = !busy) { Text("Complete") }
                TextButton(onClick = { onDelete(plan) }, enabled = !busy) { Text("Delete") }
            }
        }
    }
}

@Composable
private fun ProgressDialog(plan: StudyPlan, busy: Boolean, serverError: String?, onDismiss: () -> Unit, onSave: (Int) -> Unit) {
    var value by remember(plan.id) { mutableStateOf(plan.currentProgress.toString()) }
    var error by remember(plan.id) { mutableStateOf<String?>(null) }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Update progress · ${plan.item}") },
        text = { Column { OutlinedTextField(value, { value = it; error = null }, label = { Text("Last completed unit (0 = none)") },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), isError = error != null);
            if (error != null || serverError != null) Text(error ?: serverError.orEmpty(), color = MaterialTheme.colorScheme.error) } },
        confirmButton = { TextButton(enabled = !busy, onClick = {
            val number = value.toIntOrNull()
            if (number == null || number != 0 && number !in plan.firstUnit..plan.lastUnit) error = "Enter 0 or ${plan.firstUnit}–${plan.lastUnit}."
            else onSave(number)
        }) { Text("Save") } }, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}

@Composable
private fun <T> EnumPicker(label: String, value: T, options: List<T>, onChoose: (T) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Column {
        Text(label, style = MaterialTheme.typography.labelMedium)
        OutlinedButton(onClick = { expanded = true }) { Text(value.toString()) }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { option -> DropdownMenuItem(text = { Text(option.toString()) }, onClick = { onChoose(option); expanded = false }) }
        }
    }
}

@Composable
private fun StudyPlanEditor(initial: StudyPlan?, busy: Boolean, serverError: String?, onDismiss: () -> Unit, onSave: (StudyPlan) -> Unit) {
    val today = LocalDate.now().toString()
    var item by remember(initial?.id) { mutableStateOf(initial?.item.orEmpty()) }
    var group by remember(initial?.id) { mutableStateOf(initial?.group.orEmpty()) }
    var type by remember(initial?.id) { mutableStateOf(initial?.type ?: StudyPlanType.Book) }
    var unit by remember(initial?.id) { mutableStateOf(initial?.unitType ?: StudyUnitType.Page) }
    var custom by remember(initial?.id) { mutableStateOf(initial?.customUnit.orEmpty()) }
    var first by remember(initial?.id) { mutableStateOf((initial?.firstUnit ?: 1).toString()) }
    var last by remember(initial?.id) { mutableStateOf((initial?.lastUnit ?: 1).toString()) }
    var progress by remember(initial?.id) { mutableStateOf((initial?.currentProgress ?: 0).toString()) }
    var start by remember(initial?.id) { mutableStateOf(initial?.startDate ?: today) }
    var end by remember(initial?.id) { mutableStateOf(initial?.endDate ?: today) }
    var mask by remember(initial?.id) { mutableStateOf(initial?.studyDaysMask ?: 127) }
    var note by remember(initial?.id) { mutableStateOf(initial?.note.orEmpty()) }
    var error by remember(initial?.id) { mutableStateOf<String?>(null) }
    var pickingStart by remember { mutableStateOf(false) }
    var pickingEnd by remember { mutableStateOf(false) }
    AlertDialog(onDismissRequest = { if (!busy) onDismiss() }, title = { Text(if (initial == null) "Add Study Plan" else "Edit Study Plan") },
        text = { Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(item, { item = it; error = null }, label = { Text("Item") }, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(group, { group = it }, label = { Text("Group") }, modifier = Modifier.fillMaxWidth())
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                EnumPicker("Type", type, StudyPlanType.entries, { type = it })
                EnumPicker("Unit", unit, StudyUnitType.entries, { unit = it })
            }
            if (unit == StudyUnitType.Other) OutlinedTextField(custom, { custom = it }, label = { Text("Custom unit (e.g. kata)") })
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(first, { first = it }, label = { Text("First") }, modifier = Modifier.weight(1f),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
                OutlinedTextField(last, { last = it }, label = { Text("Last") }, modifier = Modifier.weight(1f),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
            }
            OutlinedTextField(progress, { progress = it }, label = { Text("Last completed (0 = none)") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                OutlinedButton(onClick = { pickingStart = true }) { Text("Start: $start") }
                OutlinedButton(onClick = { pickingEnd = true }) { Text("End: $end") }
            }
            Text("Study days", style = MaterialTheme.typography.labelLarge)
            weekdays.chunked(3).forEachIndexed { row, days -> Row {
                days.forEachIndexed { offset, day ->
                    val bit = 1 shl (row * 3 + offset)
                    Row { Checkbox(checked = mask and bit != 0, onCheckedChange = { checked ->
                        mask = if (checked) mask or bit else mask and bit.inv()
                    }); Text(day, modifier = Modifier.padding(top = 14.dp)) }
                }
            } }
            OutlinedTextField(note, { note = it }, label = { Text("Note") }, minLines = 3, modifier = Modifier.fillMaxWidth())
            if (error != null || serverError != null) Text(error ?: serverError.orEmpty(), color = MaterialTheme.colorScheme.error)
        } },
        confirmButton = { TextButton(enabled = !busy, onClick = {
            val firstNumber = first.toIntOrNull(); val lastNumber = last.toIntOrNull(); val progressNumber = progress.toIntOrNull()
            error = when {
                item.isBlank() -> "Enter an item."
                firstNumber == null || firstNumber < 1 || lastNumber == null || lastNumber < firstNumber -> "Enter a valid unit range."
                progressNumber == null || progressNumber != 0 && progressNumber !in firstNumber..lastNumber -> "Progress must be 0 or in the unit range."
                start > end -> "End date must be on or after start."
                mask == 0 -> "Select at least one study day."
                unit == StudyUnitType.Other && custom.isBlank() -> "Enter a custom unit."
                else -> null
            }
            if (error == null) onSave(StudyPlan(id = initial?.id, item = item.trim(), group = group.trim(), type = type, unitType = unit,
                customUnit = custom.trim(), firstUnit = firstNumber!!, lastUnit = lastNumber!!,
                currentProgress = progressNumber!!, startDate = start, endDate = end, studyDaysMask = mask, note = note))
        }) { Text("Save") } }, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
    if (pickingStart) DateDialog(start, onPicked = { start = it; pickingStart = false }, onDismiss = { pickingStart = false })
    if (pickingEnd) DateDialog(end, onPicked = { end = it; pickingEnd = false }, onDismiss = { pickingEnd = false })
}
