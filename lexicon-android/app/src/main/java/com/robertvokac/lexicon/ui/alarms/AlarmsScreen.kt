package com.robertvokac.lexicon.ui.alarms

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.provider.Settings
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.alarms.AlarmRinger
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.model.AlarmWrite
import com.robertvokac.lexicon.ui.common.ConfirmDialog
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.SyncedTextField
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.item.DateDialog
import com.robertvokac.lexicon.ui.item.TimeDialog
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class AlarmsState(
    val loading: Boolean = true,
    val error: String? = null,
    val alarms: List<Alarm> = emptyList(),
    val busy: Boolean = false,
    val message: String? = null,
    /** Why the alarm being edited was not saved; the dialog stays open. */
    val saveError: String? = null,
)

/**
 * The counterpart of the desktop AlarmsDialog: every alarm, the soonest first.
 * Each list from the server also goes to [ringer], so the phone rings what
 * the list shows.
 */
class AlarmsViewModel(container: AppContainer, private val ringer: AlarmRinger? = null) : ViewModel() {
    private val api = container.api
    private val _state = MutableStateFlow(AlarmsState())
    val state: StateFlow<AlarmsState> = _state.asStateFlow()

    init {
        load()
    }

    fun load() {
        viewModelScope.launch {
            try {
                val alarms = api.alarms()
                ringer?.apply(alarms)
                _state.update { it.copy(loading = false, error = null, alarms = alarms) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage()) }
            }
        }
    }

    /** Creates or updates; [onDone] runs after success so the dialog can close. */
    fun save(id: Int?, alarm: AlarmWrite, onDone: () -> Unit) {
        if (_state.value.busy) return
        _state.update { it.copy(busy = true, saveError = null) }
        viewModelScope.launch {
            try {
                if (id == null) api.createAlarm(alarm) else api.updateAlarm(id, alarm)
                val alarms = api.alarms()
                ringer?.apply(alarms)
                _state.update { it.copy(busy = false, alarms = alarms) }
                onDone()
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, saveError = failure.userMessage()) }
            }
        }
    }

    fun delete(alarm: Alarm) {
        val id = alarm.id ?: return
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                api.deleteAlarm(id)
                val alarms = api.alarms()
                ringer?.apply(alarms)
                _state.update { it.copy(busy = false, alarms = alarms, message = "Deleted alarm '${alarm.title}'.") }
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    /** Stops a ringing alarm, in every client. */
    fun dismiss(alarm: Alarm) = ringing(alarm) { api.dismissAlarm(it) }

    fun snooze(alarm: Alarm, minutes: Int = AlarmRinger.SNOOZE_MINUTES) = ringing(alarm) { api.snoozeAlarm(it, minutes) }

    private fun ringing(alarm: Alarm, request: suspend (Int) -> Alarm) {
        val id = alarm.id ?: return
        if (_state.value.busy) return
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                request(id)
                val alarms = api.alarms()
                ringer?.apply(alarms)
                _state.update { it.copy(busy = false, alarms = alarms) }
            } catch (failure: ApiException) {
                _state.update { it.copy(busy = false, message = failure.userMessage()) }
            }
        }
    }

    fun notificationsAllowed(): Boolean = ringer?.notificationsAllowed() ?: true

    fun ringsOnTime(): Boolean = ringer?.ringsOnTime() ?: true

    fun saveErrorShown() = _state.update { it.copy(saveError = null) }

    fun messageShown() = _state.update { it.copy(message = null) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AlarmsScreen(viewModel: AlarmsViewModel, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var editing by remember { mutableStateOf<Alarm?>(null) }
    var adding by rememberSaveable { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<Alarm?>(null) }

    state.message?.let { message ->
        LaunchedEffect(message) {
            viewModel.messageShown()
            snackbar.showSnackbar(message)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Alarms") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { adding = true }) { Icon(Icons.Filled.Add, contentDescription = "Add alarm") }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        when {
            state.loading -> LoadingBox(Modifier.padding(padding))
            state.error != null -> ErrorBox(state.error.orEmpty(), onRetry = viewModel::load, modifier = Modifier.padding(padding))
            state.alarms.isEmpty() -> Column(Modifier.padding(padding)) {
                RingingPermissions(viewModel)
                Text(
                    "No alarms yet. Add one with the + button.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(16.dp),
                )
            }
            else -> LazyColumn(Modifier.padding(padding).fillMaxSize(), contentPadding = PaddingValues(bottom = 88.dp)) {
                val upcoming = state.alarms.count { !AlarmTimes.hasGoneOff(it.firesAt) }
                item { RingingPermissions(viewModel) }
                item {
                    Text(
                        "${state.alarms.size} alarm(s), $upcoming still to go off. Times are in your local time.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(16.dp),
                    )
                }
                items(state.alarms, key = { it.id ?: 0 }) { alarm ->
                    AlarmRow(
                        alarm,
                        busy = state.busy,
                        onEdit = { editing = alarm },
                        onDelete = { deleting = alarm },
                        onDismiss = { viewModel.dismiss(alarm) },
                        onSnooze = { viewModel.snooze(alarm) },
                        onSnoozeLong = { viewModel.snooze(alarm, AlarmRinger.SNOOZE_LONG_MINUTES) },
                    )
                    HorizontalDivider()
                }
            }
        }
    }

    if (adding || editing != null) {
        val current = editing
        AlarmDialog(
            initial = current,
            busy = state.busy,
            serverError = state.saveError,
            onConfirm = { write ->
                viewModel.save(current?.id, write) {
                    adding = false
                    editing = null
                }
            },
            onEdited = viewModel::saveErrorShown,
            onDismiss = {
                viewModel.saveErrorShown()
                adding = false
                editing = null
            },
        )
    }
    deleting?.let { alarm ->
        ConfirmDialog(
            title = "Delete alarm",
            message = "Delete the alarm '${alarm.title}'?",
            confirmLabel = "Delete",
            onConfirm = {
                deleting = null
                viewModel.delete(alarm)
            },
            onDismiss = { deleting = null },
            destructive = true,
        )
    }
}

/**
 * What stops alarms ringing on this phone, each with the way to allow it:
 * notifications, and on Android 12 and later alarms at the exact minute.
 */
@Composable
private fun RingingPermissions(viewModel: AlarmsViewModel) {
    val context = LocalContext.current
    var notificationsAllowed by remember { mutableStateOf(viewModel.notificationsAllowed()) }
    var ringsOnTime by remember { mutableStateOf(viewModel.ringsOnTime()) }
    LifecycleResumeEffect(Unit) {
        notificationsAllowed = viewModel.notificationsAllowed()
        ringsOnTime = viewModel.ringsOnTime()
        onPauseOrDispose { }
    }
    val askForNotifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
        notificationsAllowed = viewModel.notificationsAllowed()
    }
    val openSettings: (Intent) -> Unit = { intent ->
        runCatching { context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
    }
    if (!notificationsAllowed) {
        PermissionNote(
            text = "Alarms cannot ring on this phone until Lexicon may show notifications.",
            action = "Allow notifications",
        ) {
            val granted = Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
                ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
            if (!granted) {
                askForNotifications.launch(Manifest.permission.POST_NOTIFICATIONS)
            } else {
                // Granted, but switched off in the system settings.
                openSettings(Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName))
            }
        }
    }
    if (!ringsOnTime && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        PermissionNote(
            text = "Alarms may ring a few minutes late while the phone saves power.",
            action = "Allow exact alarms",
        ) {
            openSettings(Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, "package:${context.packageName}".toUri()))
        }
    }
}

@Composable
private fun PermissionNote(text: String, action: String, onAction: () -> Unit) {
    Surface(
        color = MaterialTheme.colorScheme.secondaryContainer,
        shape = MaterialTheme.shapes.medium,
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(text, color = MaterialTheme.colorScheme.onSecondaryContainer, style = MaterialTheme.typography.bodyMedium)
            TextButton(onClick = onAction) { Text(action) }
        }
    }
}

@Composable
@OptIn(ExperimentalLayoutApi::class)
private fun AlarmRow(
    alarm: Alarm,
    busy: Boolean,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
    onDismiss: () -> Unit,
    onSnooze: () -> Unit,
    onSnoozeLong: () -> Unit,
) {
    val gone = AlarmTimes.hasGoneOff(alarm.firesAt)
    val ringing = gone && alarm.dismissedAt == null
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(role = Role.Button, onClickLabel = "Edit") { onEdit() }
            .padding(start = 16.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                AlarmTimes.describe(alarm.firesAt) + when {
                    ringing -> " · ringing"
                    gone -> " · gone off"
                    else -> ""
                },
                style = MaterialTheme.typography.labelLarge,
                color = when {
                    ringing -> MaterialTheme.colorScheme.error
                    gone -> muted
                    else -> MaterialTheme.colorScheme.primary
                },
            )
            Text(
                alarm.title,
                style = MaterialTheme.typography.titleMedium,
                color = if (gone && !ringing) muted else MaterialTheme.colorScheme.onSurface,
            )
            if (alarm.description.isNotEmpty()) {
                Text(
                    alarm.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = muted,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            if (ringing) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onDismiss, enabled = !busy) { Text("Dismiss") }
                    OutlinedButton(onClick = onSnooze, enabled = !busy) { Text("Snooze ${AlarmRinger.SNOOZE_MINUTES} min") }
                    OutlinedButton(onClick = onSnoozeLong, enabled = !busy) { Text(AlarmRinger.SNOOZE_LONG_LABEL) }
                }
            }
        }
        IconButton(onClick = onDelete, enabled = !busy) {
            Icon(Icons.Filled.Delete, contentDescription = "Delete alarm ${alarm.title}")
        }
    }
}

@Composable
private fun AlarmDialog(
    initial: Alarm?,
    busy: Boolean,
    serverError: String?,
    onConfirm: (AlarmWrite) -> Unit,
    onEdited: () -> Unit,
    onDismiss: () -> Unit,
) {
    val start = AlarmTimes.toLocal(initial?.firesAt ?: AlarmTimes.nextFullHour())
    var title by rememberSaveable { mutableStateOf(initial?.title.orEmpty()) }
    var description by rememberSaveable { mutableStateOf(initial?.description.orEmpty()) }
    var date by rememberSaveable { mutableStateOf(start?.first.orEmpty()) }
    var time by rememberSaveable { mutableStateOf(start?.second.orEmpty()) }
    var titleError by rememberSaveable { mutableStateOf<String?>(null) }
    var timeError by rememberSaveable { mutableStateOf<String?>(null) }
    var pickingDate by rememberSaveable { mutableStateOf(false) }
    var pickingTime by rememberSaveable { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = { if (!busy) onDismiss() },
        title = { Text(if (initial == null) "Add alarm" else "Edit alarm") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                SyncedTextField(
                    value = title,
                    onValueChange = {
                        title = it
                        titleError = null
                        onEdited()
                    },
                    label = "Title",
                    singleLine = true,
                    isError = titleError != null,
                    supportingText = titleError,
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = date,
                    onValueChange = {
                        date = it
                        timeError = null
                    },
                    label = "Date",
                    singleLine = true,
                    placeholder = "YYYY-MM-DD",
                    trailingIcon = {
                        IconButton(onClick = { pickingDate = true }) { Icon(Icons.Filled.CalendarMonth, contentDescription = "Choose the date") }
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = time,
                    onValueChange = {
                        time = it
                        timeError = null
                    },
                    label = "Time",
                    singleLine = true,
                    placeholder = "HH:MM",
                    isError = timeError != null,
                    supportingText = timeError ?: "In your local time.",
                    trailingIcon = {
                        IconButton(onClick = { pickingTime = true }) { Icon(Icons.Filled.Schedule, contentDescription = "Choose the time") }
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
                SyncedTextField(
                    value = description,
                    onValueChange = { description = it },
                    label = "Description",
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth(),
                )
                if (serverError != null) {
                    Text(serverError, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = !busy,
                onClick = {
                    val firesAt = AlarmTimes.toUtc(date, time)
                    when {
                        title.isBlank() -> titleError = "Enter a title."
                        firesAt == null -> timeError = "Enter a date as YYYY-MM-DD and a time as HH:MM."
                        else -> onConfirm(AlarmWrite(title.trim(), description, firesAt))
                    }
                },
            ) { Text(if (busy) "Saving…" else "Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !busy) { Text("Cancel") } },
    )
    if (pickingDate) {
        DateDialog(
            initial = date,
            onPicked = {
                date = it
                timeError = null
                pickingDate = false
            },
            onDismiss = { pickingDate = false },
        )
    }
    if (pickingTime) {
        TimeDialog(
            initial = time,
            onPicked = {
                time = it.take(5)
                timeError = null
                pickingTime = false
            },
            onDismiss = { pickingTime = false },
        )
    }
}
