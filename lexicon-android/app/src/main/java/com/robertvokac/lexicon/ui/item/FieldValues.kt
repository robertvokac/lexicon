package com.robertvokac.lexicon.ui.item

import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ItemField
import java.time.LocalDate
import java.time.LocalTime
import java.time.ZoneOffset
import java.time.format.DateTimeParseException
import java.util.Locale

/**
 * The string forms the REST API stores for each data type: YYYY-MM-DD,
 * HH:MM[:SS], YYYY-MM-DDTHH:MM[:SS], true/false, an enum option, a SHA-256.
 * The server validates every value; these helpers only convert picker
 * choices into those forms and hint at a typo before Save.
 */
object FieldValues {
    private val integer = Regex("^[+-]?[0-9]+$")
    private val decimal = Regex("^[+-]?([0-9]+\\.?[0-9]*|\\.[0-9]+)([eE][+-]?[0-9]+)?$")
    private val date = Regex("^\\d{4}-\\d{2}-\\d{2}$")
    private val time = Regex("^(\\d{2}):(\\d{2})(?::(\\d{2})(?:\\.\\d{1,3})?)?$")
    private val timestamp = Regex("^(\\d{4}-\\d{2}-\\d{2})T(\\d{2}:\\d{2}(?::\\d{2}(?:\\.\\d{1,3})?)?)(Z|[+-]\\d{2}:\\d{2})?$")
    private val sha256 = Regex("^[0-9a-f]{64}$")

    /** A hint when [value] is not in the form the server accepts, else null. Empty is always fine. */
    fun problem(field: ItemField, value: String): String? {
        val text = value.trim()
        if (text.isEmpty()) return null
        val ok = when (field.dataType) {
            FieldDataType.Integer -> integer.matches(text)
            FieldDataType.Float -> decimal.matches(text)
            FieldDataType.Date -> isDate(text)
            FieldDataType.Time -> isTime(text)
            FieldDataType.Timestamp -> timestamp.matchEntire(text)?.let { isDate(it.groupValues[1]) && isTime(it.groupValues[2]) } ?: false
            FieldDataType.Boolean -> text == "true" || text == "false"
            FieldDataType.Enum -> text in field.enumOptions
            FieldDataType.Blob -> sha256.matches(text)
            FieldDataType.Text, FieldDataType.Other -> true
        }
        if (ok) return null
        return when (field.dataType) {
            FieldDataType.Integer -> "Enter a whole number."
            FieldDataType.Float -> "Enter a number such as 3.14."
            FieldDataType.Date -> "Use YYYY-MM-DD."
            FieldDataType.Time -> "Use HH:MM or HH:MM:SS."
            FieldDataType.Timestamp -> "Use YYYY-MM-DDTHH:MM:SS."
            FieldDataType.Enum -> "Choose one of the options."
            else -> "This value is not valid for the field."
        }
    }

    private fun isDate(text: String): Boolean =
        date.matches(text) && try {
            LocalDate.parse(text)
            true
        } catch (_: DateTimeParseException) {
            false
        }

    private fun isTime(text: String): Boolean {
        val match = time.matchEntire(text) ?: return false
        val (hours, minutes, seconds) = match.destructured
        return hours.toInt() < 24 && minutes.toInt() < 60 && (seconds.isEmpty() || seconds.toInt() < 60)
    }

    /** Keeps what a numeric keyboard can mean; a decimal comma becomes a point. */
    fun sanitize(type: FieldDataType, input: String): String = when (type) {
        FieldDataType.Integer -> input.filterIndexed { index, ch -> ch.isDigit() || (index == 0 && (ch == '-' || ch == '+')) }
        FieldDataType.Float -> input.replace(',', '.').filter { it.isDigit() || it in "+-.eE" }
        else -> input
    }

    /** The DatePicker speaks UTC milliseconds at midnight. */
    fun dateToPickerMillis(value: String): Long? = value.takeIf(::isDate)?.let {
        LocalDate.parse(it).atStartOfDay().toInstant(ZoneOffset.UTC).toEpochMilli()
    }

    fun pickerMillisToDate(millis: Long): String =
        java.time.Instant.ofEpochMilli(millis).atZone(ZoneOffset.UTC).toLocalDate().toString()

    fun timeParts(value: String): Pair<Int, Int>? {
        val match = time.matchEntire(value.trim()) ?: return null
        return match.groupValues[1].toInt() to match.groupValues[2].toInt()
    }

    /** HH:MM:SS, the form the desktop editor shows as its placeholder. */
    fun formatTime(hour: Int, minute: Int): String = String.format(Locale.ROOT, "%02d:%02d:00", hour, minute)

    fun timestampDate(value: String): String? = timestamp.matchEntire(value.trim())?.groupValues?.get(1)

    fun timestampTime(value: String): String? = timestamp.matchEntire(value.trim())?.groupValues?.get(2)

    fun timestamp(date: String, time: String): String = "${date}T$time"

    fun today(): String = LocalDate.now().toString()

    fun now(): String = LocalTime.now().let { formatTime(it.hour, it.minute) }
}
