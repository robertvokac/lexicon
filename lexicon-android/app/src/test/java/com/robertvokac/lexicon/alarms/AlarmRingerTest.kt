package com.robertvokac.lexicon.alarms

import android.Manifest
import android.app.AlarmManager
import android.app.Notification
import android.app.NotificationManager
import android.content.Intent
import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowAlarmManager
import java.time.Instant

@RunWith(AndroidJUnit4::class)
class AlarmRingerTest {
    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var container: AppContainer
    private lateinit var application: LexiconApplication
    private lateinit var ringer: AlarmRinger
    private val alarmManager get() = shadowOf(application.getSystemService(AlarmManager::class.java))
    private val notifications get() = shadowOf(application.getSystemService(NotificationManager::class.java))

    private val soon = Instant.now().plusSeconds(3600).truncatedTo(java.time.temporal.ChronoUnit.SECONDS)

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        fake.alarms += Alarm(1, "Tea", "Green, two minutes.", "2020-01-01T10:00:00Z")
        fake.alarms += Alarm(2, "Dentist", "Bring the card.", soon.toString())
        fake.alarms += Alarm(3, "Old call", "", "2019-05-06T07:08:00Z", dismissedAt = "2019-05-06T07:10:00Z")
        environment = TestEnvironment()
        application = ApplicationProvider.getApplicationContext()
        container = environment.container(application.contentResolver)
        application.container = container
        shadowOf(application).grantPermissions(Manifest.permission.POST_NOTIFICATIONS)
        ShadowAlarmManager.setCanScheduleExactAlarms(true)
        signInDirectly(container, fake)
        ringer = AlarmRinger(application, container)
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    // Robolectric 4.17 has no getter for the operation yet.
    @Suppress("DEPRECATION")
    private fun intentOf(alarm: ShadowAlarmManager.ScheduledAlarm): Intent? = alarm.operation?.let { shadowOf(it).savedIntent }

    private fun fireAlarms() = alarmManager.scheduledAlarms.filter { intentOf(it)?.action == AlarmRinger.ACTION_FIRE }

    private fun shown(id: Int): Notification? = notifications.getNotification(AlarmRinger.TAG, id)

    /** Delivers a broadcast to the manifest receivers and waits for their network work. */
    private fun broadcast(intent: Intent, until: () -> Boolean) {
        application.sendBroadcast(intent.setPackage(application.packageName))
        val deadline = System.currentTimeMillis() + 10_000
        while (!until()) {
            shadowOf(Looper.getMainLooper()).idle()
            check(System.currentTimeMillis() < deadline) { "The broadcast was not handled in time." }
            Thread.sleep(20)
        }
    }

    @Test
    fun anAlarmStillToComeIsScheduledAndOneGoneOffRings() = runBlocking {
        assertTrue(ringer.sync())
        val scheduled = fireAlarms()
        assertEquals(1, scheduled.size)
        assertEquals(soon.toEpochMilli(), scheduled.single().triggerAtMs)
        assertEquals(2, intentOf(scheduled.single())!!.getIntExtra(AlarmRinger.EXTRA_ID, -1))
        val tea = shown(1)!!
        assertEquals("Tea", tea.extras.getString(Notification.EXTRA_TITLE))
        assertEquals("Green, two minutes.", tea.extras.getCharSequence(Notification.EXTRA_TEXT).toString())
        assertEquals(listOf("Dismiss", "Snooze 10 min"), tea.actions.map { it.title.toString() })
        assertNull("a dismissed alarm stays quiet", shown(3))
        assertTrue(alarmManager.scheduledAlarms.any { intentOf(it)?.action == AlarmRinger.ACTION_SYNC })
    }

    @Test
    fun aDismissalGoesToTheServerAndStopsTheNotification() = runBlocking {
        ringer.sync()
        assertNotNull(shown(1))
        broadcast(Intent(application, AlarmReceiver::class.java).setAction(AlarmRinger.ACTION_DISMISS).putExtra(AlarmRinger.EXTRA_ID, 1)) {
            fake.alarms.first { it.id == 1 }.dismissedAt != null
        }
        assertNull(shown(1))
        assertTrue(fake.requestsTo("POST", "/api/v1/alarms/1/dismiss").isNotEmpty())
        ringer.sync()
        assertNull("a dismissed alarm does not come back", shown(1))
    }

    @Test
    fun aDismissalWithoutTheServerIsSentLater() = runBlocking {
        ringer.sync()
        fake.alarmActionsFail = true
        ringer.dismiss(1)
        assertNull(shown(1))
        assertNull(fake.alarms.first { it.id == 1 }.dismissedAt)
        // The server still says it rings, but this phone knows better.
        ringer.sync()
        assertNull(shown(1))
        // Back again: the next sync sends the dismissal first.
        fake.alarmActionsFail = false
        ringer.sync()
        assertNotNull(fake.alarms.first { it.id == 1 }.dismissedAt)
        assertNull(shown(1))
    }

    @Test
    fun aSnoozeRingsAgainLaterHereAndOnTheServer() = runBlocking {
        ringer.sync()
        ringer.snooze(1)
        assertNull(shown(1))
        val snoozed = fake.alarms.first { it.id == 1 }
        val at = Instant.parse(snoozed.firesAt)
        assertTrue(at.isAfter(Instant.now().plusSeconds(9 * 60)))
        val local = fireAlarms().first { intentOf(it)!!.getIntExtra(AlarmRinger.EXTRA_ID, -1) == 1 }
        assertEquals(at.toEpochMilli(), local.triggerAtMs)
    }

    @Test
    fun anAlarmGoingOffRingsWithoutTheNetworkAndDismissedElsewhereStops() = runBlocking {
        ringer.sync()
        fake.alarms.replaceAll { if (it.id == 2) it.copy(firesAt = "2020-02-02T10:00:00Z") else it }
        val fire = intentOf(fireAlarms().single())!!
        broadcast(Intent(fire)) { shown(2) != null && fake.requestsTo("GET", "/api/v1/alarms").size >= 2 }
        assertEquals("Dentist", shown(2)!!.extras.getString(Notification.EXTRA_TITLE))
        // Dismissed in the web client: the next look takes the notification back.
        fake.alarms.replaceAll { if (it.id == 2) it.copy(dismissedAt = "2020-02-02T10:05:00Z") else it }
        ringer.sync()
        assertNull(shown(2))
    }

    @Test
    fun signingOutTakesEverythingBack() = runBlocking {
        ringer.sync()
        ringer.clear()
        assertTrue(fireAlarms().isEmpty())
        assertTrue(alarmManager.scheduledAlarms.none { intentOf(it)?.action == AlarmRinger.ACTION_SYNC })
        assertNull(shown(1))
        assertTrue(ringer.known().isEmpty())
    }

    @Test
    fun afterARestartTheKnownAlarmsAreScheduledAgain() = runBlocking {
        ringer.sync()
        // A restart: the system forgets every scheduled alarm.
        @Suppress("DEPRECATION")
        for (alarm in alarmManager.scheduledAlarms) alarm.operation?.let(application.getSystemService(AlarmManager::class.java)::cancel)
        assertTrue(fireAlarms().isEmpty())
        broadcast(Intent(Intent.ACTION_BOOT_COMPLETED).setClass(application, AlarmSystemReceiver::class.java)) {
            fireAlarms().isNotEmpty()
        }
        assertEquals(soon.toEpochMilli(), fireAlarms().single().triggerAtMs)
    }

    @Test
    fun withoutPermissionNothingIsShownButTheScheduleStands() = runBlocking {
        shadowOf(application).denyPermissions(Manifest.permission.POST_NOTIFICATIONS)
        assertTrue(!ringer.notificationsAllowed())
        ringer.sync()
        assertNull(shown(1))
        assertEquals(1, fireAlarms().size)
    }
}
