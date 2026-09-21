package com.robertvokac.lexicon.ui.item

import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ItemField
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class FieldValuesTest {
    private fun field(type: FieldDataType, options: List<String> = emptyList()) = ItemField(1, 1, "f", type, 0, options)

    @Test
    fun acceptsTheStoredRepresentations() {
        assertNull(FieldValues.problem(field(FieldDataType.Integer), "-42"))
        assertNull(FieldValues.problem(field(FieldDataType.Float), "3.14"))
        assertNull(FieldValues.problem(field(FieldDataType.Float), "1e-3"))
        assertNull(FieldValues.problem(field(FieldDataType.Date), "2024-02-29"))
        assertNull(FieldValues.problem(field(FieldDataType.Time), "23:59"))
        assertNull(FieldValues.problem(field(FieldDataType.Time), "23:59:59"))
        assertNull(FieldValues.problem(field(FieldDataType.Timestamp), "2024-02-29T08:30:00"))
        assertNull(FieldValues.problem(field(FieldDataType.Timestamp), "2024-02-29T08:30Z"))
        assertNull(FieldValues.problem(field(FieldDataType.Boolean), "true"))
        assertNull(FieldValues.problem(field(FieldDataType.Enum, listOf("easy", "hard")), "hard"))
        assertNull(FieldValues.problem(field(FieldDataType.Blob), "0123456789abcdef".repeat(4)))
        assertNull(FieldValues.problem(field(FieldDataType.Text), "anything at all"))
        assertNull(FieldValues.problem(field(FieldDataType.Integer), ""))
    }

    @Test
    fun hintsAtWhatTheServerWouldRefuse() {
        assertNotNull(FieldValues.problem(field(FieldDataType.Integer), "4.5"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Float), "3,14"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Date), "2023-02-29"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Date), "29.2.2024"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Time), "24:00"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Timestamp), "2024-02-29 08:30"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Enum, listOf("easy")), "hard"))
        assertNotNull(FieldValues.problem(field(FieldDataType.Blob), "ABC"))
    }

    @Test
    fun pickersProduceTheStoredForms() {
        val millis = FieldValues.dateToPickerMillis("2024-02-29")!!
        assertEquals("2024-02-29", FieldValues.pickerMillisToDate(millis))
        assertEquals("08:05:00", FieldValues.formatTime(8, 5))
        assertEquals(8 to 5, FieldValues.timeParts("08:05:30"))
        assertEquals("2024-02-29T08:05:00", FieldValues.timestamp("2024-02-29", FieldValues.formatTime(8, 5)))
        assertEquals("2024-02-29", FieldValues.timestampDate("2024-02-29T08:05:00"))
        assertEquals("08:05:00", FieldValues.timestampTime("2024-02-29T08:05:00"))
    }

    @Test
    fun numericInputKeepsWhatANumberCanBe() {
        assertEquals("-12", FieldValues.sanitize(FieldDataType.Integer, "-1a2"))
        assertEquals("12", FieldValues.sanitize(FieldDataType.Integer, "1-2"))
        assertEquals("3.14", FieldValues.sanitize(FieldDataType.Float, "3,14"))
        assertEquals("abc", FieldValues.sanitize(FieldDataType.Text, "abc"))
    }
}
