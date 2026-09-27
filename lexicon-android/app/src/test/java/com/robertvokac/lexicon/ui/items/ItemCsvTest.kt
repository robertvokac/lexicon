package com.robertvokac.lexicon.ui.items

import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemField
import org.junit.Assert.assertEquals
import org.junit.Test

class ItemCsvTest {
    @Test
    fun quotesUtf8CommasQuotesAndNewlinesAndFormatsVisibleValues() {
        val fields = listOf(
            ItemField(4, 2, "Ready", FieldDataType.Boolean),
            ItemField(5, 2, "Note", FieldDataType.Text),
        )
        val item = Item(
            id = 7,
            groupName = "C++, JVM",
            itemTypeName = "Term",
            title = "Příliš \"žluťoučký\"",
            tags = listOf("one", "two"),
            fieldValues = mapOf("4" to "true", "5" to "Line\nbreak"),
        )
        assertEquals(
            "\uFEFF\"Id\",\"Group\",\"Type\",\"Title\",\"Disambiguation\",\"Tags\",\"Flags\",\"Aliases\",\"Status\",\"Understanding\",\"Pinned\",\"Ready\",\"Note\"\r\n" +
                "\"7\",\"C++, JVM\",\"Term\",\"Příliš \"\"žluťoučký\"\"\",\"\",\"one, two\",\"\",\"\",\"None\",\"Unknown\",\"No\",\"Yes\",\"Line\nbreak\"\r\n",
            ItemCsv.encode(listOf(item), fields),
        )
    }
}
