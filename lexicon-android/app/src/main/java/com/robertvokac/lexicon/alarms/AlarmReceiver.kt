package com.robertvokac.lexicon.alarms

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.robertvokac.lexicon.LexiconApplication
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

/** The app's own alarm broadcasts: an alarm going off, Dismiss, Snooze and the periodic sync. Not exported. */
class AlarmReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val application = context.applicationContext as? LexiconApplication ?: return
        val ringer = AlarmRinger(application, application.container)
        when (intent.action) {
            AlarmRinger.ACTION_FIRE -> {
                // At once, from what the schedule carries; then the server,
                // which may know it was dismissed or moved meanwhile.
                AlarmRinger.alarmFrom(intent)?.let(ringer::show)
                later(application) { ringer.sync() }
            }
            AlarmRinger.ACTION_DISMISS -> AlarmRinger.idFrom(intent)?.let { id -> later(application) { ringer.dismiss(id) } }
            AlarmRinger.ACTION_SNOOZE -> AlarmRinger.idFrom(intent)?.let { id -> later(application) { ringer.snooze(id) } }
            AlarmRinger.ACTION_SYNC -> later(application) {
                ringer.sync()
                // Ideas caught offline go out too, even with the app closed.
                application.container.flushOutbox()
            }
        }
    }
}

/**
 * System broadcasts: after a restart or an update the system has forgotten
 * the schedule, and a newly granted exact-alarm permission lets alarms ring
 * on time. Exported for the system; any other sender only causes a resync.
 */
class AlarmSystemReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action !in SYSTEM_ACTIONS) return
        val application = context.applicationContext as? LexiconApplication ?: return
        val ringer = AlarmRinger(application, application.container)
        ringer.restore()
        later(application) { ringer.sync() }
    }

    companion object {
        private val SYSTEM_ACTIONS = setOf(
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_MY_PACKAGE_REPLACED,
            // AlarmManager.ACTION_SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED, from
            // Android 12; older systems simply never send it.
            "android.app.action.SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED",
        )
    }
}

/** Network work from a receiver: it may run a few seconds past onReceive. */
private fun BroadcastReceiver.later(application: LexiconApplication, block: suspend () -> Unit) {
    val result = goAsync()
    application.container.applicationScope.launch {
        try {
            withTimeoutOrNull(9_000) { block() }
        } finally {
            result?.finish()
        }
    }
}
