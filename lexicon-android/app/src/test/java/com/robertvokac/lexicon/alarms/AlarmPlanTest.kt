package com.robertvokac.lexicon.alarms

import com.robertvokac.lexicon.model.Alarm
import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant

class AlarmPlanTest {
    private val now = Instant.parse("2026-09-22T12:00:00Z")

    @Test
    fun upcomingAlarmsAreScheduledAndThoseGoneOffRingUntilDismissed() {
        val alarms = listOf(
            Alarm(1, "Tea", firesAt = "2026-09-22T11:59:00Z"),
            Alarm(2, "Call", firesAt = "2026-09-22T12:00:00Z"),
            Alarm(3, "Old", firesAt = "2026-09-20T08:00:00Z", dismissedAt = "2026-09-20T08:01:00Z"),
            Alarm(4, "Dentist", firesAt = "2026-10-02T08:30:00Z"),
            Alarm(5, "Dismissed here, not yet told", firesAt = "2026-09-22T10:00:00Z"),
            Alarm(null, "Unsaved", firesAt = "2026-09-22T11:00:00Z"),
            Alarm(6, "Broken", firesAt = "soon"),
        )
        val plan = AlarmPlan.of(alarms, now, dismissedHere = setOf(5))
        assertEquals(listOf(4), plan.upcoming.map { it.id })
        assertEquals(listOf(1, 2), plan.ringing.map { it.id })
    }

    @Test
    fun aDismissedAlarmMovedIntoTheFutureIsScheduledAgain() {
        val plan = AlarmPlan.of(listOf(Alarm(1, "Tea", firesAt = "2026-09-22T12:10:00Z", dismissedAt = "2026-09-22T11:00:00Z")), now)
        assertEquals(listOf(1), plan.upcoming.map { it.id })
        assertEquals(emptyList<Alarm>(), plan.ringing)
    }
}
