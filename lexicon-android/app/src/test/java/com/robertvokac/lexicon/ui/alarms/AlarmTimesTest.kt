package com.robertvokac.lexicon.ui.alarms

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Instant
import java.time.ZoneId
import java.util.Locale

class AlarmTimesTest {
    private val prague = ZoneId.of("Europe/Prague")
    private val kolkata = ZoneId.of("Asia/Kolkata")

    @Test
    fun aUtcTimeIsEditedInLocalTime() {
        assertEquals("2030-01-02" to "10:15", AlarmTimes.toLocal("2030-01-02T09:15:00Z", prague))
        assertEquals("2030-07-02" to "11:15", AlarmTimes.toLocal("2030-07-02T09:15:00Z", prague))
        assertEquals("2030-01-02" to "14:45", AlarmTimes.toLocal("2030-01-02T09:15:00Z", kolkata))
        assertNull(AlarmTimes.toLocal("tomorrow", prague))
    }

    @Test
    fun aLocalTimeIsStoredInUtcWithSeconds() {
        assertEquals("2030-01-02T09:15:00Z", AlarmTimes.toUtc("2030-01-02", "10:15", prague))
        assertEquals("2030-01-02T09:15:00Z", AlarmTimes.toUtc(" 2030-01-02 ", "14:45", kolkata))
        for (utc in listOf("2030-01-02T09:15:00Z", "2026-03-29T00:30:00Z", "2026-10-25T00:30:00Z")) {
            val (date, time) = AlarmTimes.toLocal(utc, prague)!!
            assertEquals(utc, AlarmTimes.toUtc(date, time, prague))
        }
        // 02:30 happens twice the night the clocks go back; it means the first.
        assertEquals("2026-10-25" to "02:30", AlarmTimes.toLocal("2026-10-25T01:30:00Z", prague))
        assertEquals("2026-10-25T00:30:00Z", AlarmTimes.toUtc("2026-10-25", "02:30", prague))
    }

    @Test
    fun anImpossibleDateOrTimeIsNoTime() {
        assertNull(AlarmTimes.toUtc("2030-02-30", "10:00", prague))
        assertNull(AlarmTimes.toUtc("2030-01-02", "25:99", prague))
        assertNull(AlarmTimes.toUtc("", "10:00", prague))
    }

    @Test
    fun theListShowsTheWeekdayAndWhatHasGoneOff() {
        assertEquals("Wed 2030-01-02 10:15", AlarmTimes.describe("2030-01-02T09:15:00Z", prague, Locale.ENGLISH))
        val now = Instant.parse("2026-09-22T12:00:00Z")
        assertTrue(AlarmTimes.hasGoneOff("2026-09-22T12:00:00Z", now))
        assertFalse(AlarmTimes.hasGoneOff("2026-09-22T12:01:00Z", now))
        assertFalse(AlarmTimes.hasGoneOff("garbage", now))
    }

    @Test
    fun aNewAlarmStartsAtTheNextFullHour() {
        assertEquals("2026-09-22T11:00:00Z", AlarmTimes.nextFullHour(Instant.parse("2026-09-22T10:37:12Z"), prague))
        // A half-hour zone's full hour is not UTC's.
        assertEquals("2026-09-22T10:30:00Z", AlarmTimes.nextFullHour(Instant.parse("2026-09-22T10:07:00Z"), kolkata))
    }
}
