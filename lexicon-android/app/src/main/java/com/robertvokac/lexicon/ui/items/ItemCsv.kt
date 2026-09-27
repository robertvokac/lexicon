package com.robertvokac.lexicon.ui.items

import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ImageValues
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemField

/** CSV for the columns represented by the current item list/table. */
object ItemCsv {
    private fun quoted(value: Any?): String = "\"${value?.toString().orEmpty().replace("\"", "\"\"")}\""

    fun encode(items: List<Item>, fields: List<ItemField>): String {
        val headers = listOf(
            "Id", "Group", "Type", "Title", "Disambiguation", "Tags", "Flags", "Aliases",
            "Status", "Understanding", "Pinned",
        ) + fields.map { it.name }
        val rows = items.map { item ->
            listOf(
                item.id?.toString().orEmpty(), item.groupName, item.itemTypeName, item.title,
                item.disambiguation, item.tags.joinToString(", "), item.flags.joinToString(", "),
                item.aliases.joinToString(", "), item.status.label, item.understanding.label,
                if (item.pinned) "Yes" else "No",
            ) + fields.map { field ->
                val value = field.id?.let(item::fieldValue).orEmpty()
                when {
                    field.dataType == FieldDataType.Image -> ImageValues.describe(value) ?: value
                    field.dataType == FieldDataType.Boolean && value.isNotEmpty() ->
                        if (value == "true") "Yes" else "No"
                    else -> value
                }
            }
        }
        return "\uFEFF" + (listOf(headers) + rows).joinToString("\r\n") { row ->
            row.joinToString(",", transform = ::quoted)
        } + "\r\n"
    }
}
