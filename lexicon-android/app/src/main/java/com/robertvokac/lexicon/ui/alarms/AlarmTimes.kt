package com.robertvokac.lexicon.ui.alarms

import java.time.Instant
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.LocalTime
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import java.time.format.TextStyle
import java.time.temporal.ChronoUnit
import java.util.Locale

/**
 * An alarm's time: stored as UTC "YYYY-MM-DDTHH:MM:SSZ", edited and shown as a
 * local date and time.
 */
object AlarmTimes {
    private val utcFormat = DateTimeFormatter.ofPattern("uuuu-MM-dd'T'HH:mm:ss'Z'", Locale.ROOT).withZone(ZoneOffset.UTC)
    private val timeFormat = DateTimeFormatter.ofPattern("HH:mm", Locale.ROOT)

    fun parse(utc: String): Instant? = try {
        Instant.parse(utc)
    } catch (_: DateTimeParseException) {
        null
    }

    fun format(instant: Instant): String = utcFormat.format(instant.truncatedTo(ChronoUnit.SECONDS))

    /** The local date "YYYY-MM-DD" and time "HH:MM" of [utc]. */
    fun toLocal(utc: String, zone: ZoneId = ZoneId.systemDefault()): Pair<String, String>? {
        val local = parse(utc)?.atZone(zone) ?: return null
        return local.toLocalDate().toString() to timeFormat.format(local)
    }

    /** A local date and time as UTC, or null when either is not valid. */
    fun toUtc(date: String, time: String, zone: ZoneId = ZoneId.systemDefault()): String? = try {
        val local = LocalDateTime.of(LocalDate.parse(date.trim()), LocalTime.parse(time.trim()))
        format(local.atZone(zone).toInstant())
    } catch (_: DateTimeParseException) {
        null
    }

    /** "Wed 2030-01-02 10:15" in local time. */
    fun describe(utc: String, zone: ZoneId = ZoneId.systemDefault(), locale: Locale = Locale.getDefault()): String {
        val local = parse(utc)?.atZone(zone) ?: return utc
        return "${local.dayOfWeek.getDisplayName(TextStyle.SHORT, locale)} ${local.toLocalDate()} ${timeFormat.format(local)}"
    }

    fun hasGoneOff(utc: String, now: Instant = Instant.now()): Boolean = parse(utc)?.let { !it.isAfter(now) } ?: false

    /** Where a new alarm starts: the next full hour. */
    fun nextFullHour(now: Instant = Instant.now(), zone: ZoneId = ZoneId.systemDefault()): String =
        format(now.atZone(zone).truncatedTo(ChronoUnit.HOURS).plusHours(1).toInstant())
}
