package com.robertvokac.lexicon.alarms

import android.Manifest
import android.app.AlarmManager
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.net.toUri
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.MainActivity
import com.robertvokac.lexicon.R
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconJson
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.share.LaunchIntents
import com.robertvokac.lexicon.ui.alarms.AlarmTimes
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.builtins.SetSerializer
import kotlinx.serialization.builtins.serializer
import java.time.Instant

/**
 * Rings the signed-in person's alarms on this phone, whether or not the app
 * is open: alarms still to come are handed to the system AlarmManager, and one
 * that goes off becomes a notification with Dismiss and Snooze. A dismissal
 * goes to the server, so every client stops ringing; one made without a
 * connection is sent on the next sync. The alarms last seen from the server
 * are kept here, so they ring after a restart without the network.
 */
class AlarmRinger(private val context: Context, private val container: AppContainer) {
    private val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
    private val alarmManager = context.getSystemService(AlarmManager::class.java)
    private val notifications = NotificationManagerCompat.from(context)

    // What this phone knows ------------------------------------------------------

    /** The alarms as last seen from the server, with local snoozes applied. */
    fun known(): List<Alarm> = read(KEY_ALARMS, ListSerializer(Alarm.serializer())) ?: emptyList()

    private fun dismissedHere(): Set<Int> = read(KEY_DISMISSED, SetSerializer(Int.serializer())) ?: emptySet()

    private fun <T> read(key: String, serializer: kotlinx.serialization.KSerializer<T>): T? =
        preferences.getString(key, null)?.let { runCatching { LexiconJson.decodeFromString(serializer, it) }.getOrNull() }

    private fun <T> write(key: String, serializer: kotlinx.serialization.KSerializer<T>, value: T) =
        preferences.edit { putString(key, LexiconJson.encodeToString(serializer, value)) }

    /** True when the system lets alarms go off at the minute; otherwise they may be a few minutes late. */
    fun ringsOnTime(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || alarmManager.canScheduleExactAlarms()

    fun notificationsAllowed(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            return false
        }
        return notifications.areNotificationsEnabled()
    }

    // With the server ----------------------------------------------------------

    /** Fetches the alarms and rings them here. False when nobody is signed in or the server is away. */
    suspend fun sync(): Boolean {
        if (!awaitSignedIn()) return false
        sendDismissals()
        val alarms = try {
            container.api.alarms()
        } catch (_: ApiException) {
            return false
        }
        apply(alarms)
        return true
    }

    /** Stops the alarm ringing here at once and, through the server, everywhere. */
    suspend fun dismiss(id: Int) {
        notifications.cancel(TAG, id)
        write(KEY_DISMISSED, SetSerializer(Int.serializer()), dismissedHere() + id)
        sendDismissals()
    }

    /** Rings again in [minutes], here even without a connection, and tells the server. */
    suspend fun snooze(id: Int, minutes: Int = SNOOZE_MINUTES) {
        notifications.cancel(TAG, id)
        known().firstOrNull { it.id == id }?.let { alarm ->
            remember(alarm.copy(firesAt = AlarmTimes.format(Instant.now().plusSeconds(minutes * 60L)), dismissedAt = null))
        }
        if (!awaitSignedIn()) return
        try {
            remember(container.api.snoozeAlarm(id, minutes))
        } catch (_: ApiException) {
            // The local snooze rings anyway; the next sync settles it.
        }
    }

    private suspend fun sendDismissals() {
        val pending = dismissedHere()
        if (pending.isEmpty() || !awaitSignedIn()) return
        val sent = pending.filterTo(mutableSetOf()) { id ->
            try {
                container.api.dismissAlarm(id)
                true
            } catch (_: ApiException.NotFound) {
                true // Deleted meanwhile: nothing left to dismiss.
            } catch (_: ApiException) {
                false
            }
        }
        write(KEY_DISMISSED, SetSerializer(Int.serializer()), dismissedHere() - sent)
    }

    private suspend fun awaitSignedIn(): Boolean {
        val sessions = container.sessions
        sessions.start()
        val state = withTimeoutOrNull(SESSION_WAIT_MS) { sessions.state.first { it !is SessionState.Restoring } }
        return state is SessionState.SignedIn
    }

    // On this phone ------------------------------------------------------------

    /**
     * Rings [alarms] from now on: upcoming ones are scheduled, those gone off
     * and not dismissed are shown, and everything else is taken back.
     */
    fun apply(alarms: List<Alarm>, now: Instant = Instant.now()) {
        val plan = AlarmPlan.of(alarms, now, dismissedHere())
        val upcoming = plan.upcoming.mapNotNull { it.id }.toSet()
        for (alarm in known()) alarm.id?.takeIf { it !in upcoming }?.let(::unschedule)
        plan.upcoming.forEach(::schedule)
        val ringing = plan.ringing.mapNotNull { it.id }.toSet()
        val showing = showing()
        for (alarm in plan.ringing) if (alarm.id !in showing) show(alarm)
        for (id in showing) if (id !in ringing) notifications.cancel(TAG, id)
        write(KEY_ALARMS, ListSerializer(Alarm.serializer()), alarms)
        scheduleSync()
    }

    /** After a restart or an update: the system forgot the schedule, this phone did not. */
    fun restore() = apply(known())

    /** Signing out: nothing of this person's rings here any more. */
    fun clear() {
        for (alarm in known()) alarm.id?.let(::unschedule)
        alarmManager.cancel(syncIntent())
        for (id in showing()) notifications.cancel(TAG, id)
        preferences.edit { clear() }
    }

    private fun remember(alarm: Alarm) {
        val id = alarm.id ?: return
        val alarms = known().filter { it.id != id } + alarm
        write(KEY_ALARMS, ListSerializer(Alarm.serializer()), alarms)
        if (AlarmTimes.parse(alarm.firesAt)?.isAfter(Instant.now()) == true) schedule(alarm)
    }

    private fun schedule(alarm: Alarm) {
        val at = AlarmTimes.parse(alarm.firesAt)?.toEpochMilli() ?: return
        val intent = fireIntent(alarm) ?: return
        try {
            if (ringsOnTime()) {
                alarmManager.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, intent)
            } else {
                alarmManager.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, intent)
            }
        } catch (_: SecurityException) {
            // The exact-alarm permission was taken back in between.
            alarmManager.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, intent)
        }
    }

    private fun unschedule(id: Int) {
        val intent = Intent(context, AlarmReceiver::class.java).setAction(ACTION_FIRE).setData(alarmUri(id))
        PendingIntent.getBroadcast(context, id, intent, PendingIntent.FLAG_NO_CREATE or PendingIntent.FLAG_IMMUTABLE)?.let {
            alarmManager.cancel(it)
            it.cancel()
        }
    }

    /** A look at the server every half hour, for alarms added or moved elsewhere. */
    private fun scheduleSync() {
        alarmManager.setInexactRepeating(
            AlarmManager.RTC,
            System.currentTimeMillis() + AlarmManager.INTERVAL_HALF_HOUR,
            AlarmManager.INTERVAL_HALF_HOUR,
            syncIntent(),
        )
    }

    private fun syncIntent(): PendingIntent = PendingIntent.getBroadcast(
        context,
        0,
        Intent(context, AlarmReceiver::class.java).setAction(ACTION_SYNC),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    private fun fireIntent(alarm: Alarm): PendingIntent? {
        val id = alarm.id ?: return null
        val intent = Intent(context, AlarmReceiver::class.java)
            .setAction(ACTION_FIRE)
            .setData(alarmUri(id))
            .putExtra(EXTRA_ID, id)
            .putExtra(EXTRA_TITLE, alarm.title)
            .putExtra(EXTRA_DESCRIPTION, alarm.description)
            .putExtra(EXTRA_FIRES_AT, alarm.firesAt)
        return PendingIntent.getBroadcast(context, id, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    }

    private fun actionIntent(action: String, id: Int): PendingIntent = PendingIntent.getBroadcast(
        context,
        id,
        Intent(context, AlarmReceiver::class.java).setAction(action).setData(alarmUri(id)).putExtra(EXTRA_ID, id),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    // Notifications ------------------------------------------------------------

    private fun showing(): Set<Int> =
        context.getSystemService(NotificationManager::class.java).activeNotifications
            .filter { it.tag == TAG }
            .mapTo(mutableSetOf()) { it.id }

    /** The alarm as a notification: Dismiss, both snoozes, and a swipe dismisses it too. */
    fun show(alarm: Alarm) {
        val id = alarm.id ?: return
        if (!notificationsAllowed()) return
        notifications.createNotificationChannel(
            NotificationChannelCompat.Builder(CHANNEL, NotificationManagerCompat.IMPORTANCE_HIGH)
                .setName("Alarms")
                .setDescription("Your Lexicon alarms, when their time comes")
                .build(),
        )
        val open = PendingIntent.getActivity(
            context,
            id,
            Intent(context, MainActivity::class.java).setAction(LaunchIntents.ACTION_ALARMS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val text = alarm.description.ifBlank { AlarmTimes.describe(alarm.firesAt) }
        val notification = NotificationCompat.Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification_alarm)
            .setContentTitle(alarm.title)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setCategory(NotificationCompat.CATEGORY_REMINDER)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setWhen(AlarmTimes.parse(alarm.firesAt)?.toEpochMilli() ?: System.currentTimeMillis())
            .setShowWhen(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(open)
            .setDeleteIntent(actionIntent(ACTION_DISMISS, id))
            .addAction(0, "Dismiss", actionIntent(ACTION_DISMISS, id))
            .addAction(0, "Snooze $SNOOZE_MINUTES min", actionIntent(ACTION_SNOOZE, id))
            .addAction(0, SNOOZE_LONG_LABEL, actionIntent(ACTION_SNOOZE_LONG, id))
            .build()
        try {
            notifications.notify(TAG, id, notification)
        } catch (_: SecurityException) {
            // The permission was taken back in between; the list still shows it ringing.
        }
    }

    companion object {
        const val ACTION_FIRE = "com.robertvokac.lexicon.alarm.FIRE"
        const val ACTION_DISMISS = "com.robertvokac.lexicon.alarm.DISMISS"
        const val ACTION_SNOOZE = "com.robertvokac.lexicon.alarm.SNOOZE"
        // Its own action, not an extra: two PendingIntents that differ only in
        // their extras are the same intent to Android, and the second would
        // overwrite the first.
        const val ACTION_SNOOZE_LONG = "com.robertvokac.lexicon.alarm.SNOOZE_LONG"
        const val ACTION_SYNC = "com.robertvokac.lexicon.alarm.SYNC"
        const val EXTRA_ID = "id"
        const val EXTRA_TITLE = "title"
        const val EXTRA_DESCRIPTION = "description"
        const val EXTRA_FIRES_AT = "firesAt"
        const val TAG = "lexicon-alarm"
        const val CHANNEL = "alarms"
        const val SNOOZE_MINUTES = 10
        const val SNOOZE_LONG_MINUTES = 60
        const val SNOOZE_LONG_LABEL = "Snooze 1 hour"
        private const val PREFERENCES = "alarms"
        private const val KEY_ALARMS = "alarms"
        private const val KEY_DISMISSED = "dismissed"
        private const val SESSION_WAIT_MS = 8_000L

        private fun alarmUri(id: Int): Uri = "lexicon-alarm://alarm/$id".toUri()

        /** The alarm a FIRE broadcast carries, so it rings without the network. */
        fun alarmFrom(intent: Intent): Alarm? {
            val id = intent.getIntExtra(EXTRA_ID, -1).takeIf { it > 0 } ?: return null
            return Alarm(
                id = id,
                title = intent.getStringExtra(EXTRA_TITLE) ?: return null,
                description = intent.getStringExtra(EXTRA_DESCRIPTION).orEmpty(),
                firesAt = intent.getStringExtra(EXTRA_FIRES_AT) ?: return null,
            )
        }

        fun idFrom(intent: Intent): Int? = intent.getIntExtra(EXTRA_ID, -1).takeIf { it > 0 }
    }
}
