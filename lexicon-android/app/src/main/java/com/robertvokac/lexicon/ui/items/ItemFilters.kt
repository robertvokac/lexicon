package com.robertvokac.lexicon.ui.items

import com.robertvokac.lexicon.model.ColumnFilters
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.PropertyFilter
import com.robertvokac.lexicon.model.SortColumns
import com.robertvokac.lexicon.model.SortOrder
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.ValueFilter

/**
 * Every filter the desktop table offers, held as values. The server applies
 * them; nothing is filtered on the device.
 */
data class ItemFilters(
    val groupId: Int? = null,
    val typeId: Int? = null,
    val id: String = "",
    val title: String = "",
    val disambiguation: String = "",
    val alias: String = "",
    val tag: String = "",
    val flag: String = "",
    val status: ItemStatus? = null,
    val understanding: UnderstandingLevel? = null,
    val pinned: Boolean? = null,
    /** Type field filters by field ID. Only fields of [typeId] apply. */
    val values: Map<Int, String> = emptyMap(),
    val properties: List<PropertyFilter> = emptyList(),
) {
    /** How many filters narrow the list, for the badge on the filter button. */
    val activeCount: Int
        get() = listOf(
            groupId != null, typeId != null, id.isNotBlank(), title.isNotBlank(),
            disambiguation.isNotBlank(), alias.isNotBlank(), tag.isNotEmpty(), flag.isNotEmpty(),
            status != null, understanding != null, pinned != null,
        ).count { it } + values.values.count { it.isNotBlank() } + properties.size

    val isEmpty: Boolean get() = activeCount == 0
}

/**
 * The query body for one page. [fields] are the fields of the selected type:
 * the value filters are sent for those only, exactly for every data type but
 * Text and Other, as in the other clients.
 */
fun buildItemQuery(
    searchText: String,
    filters: ItemFilters,
    fields: List<ItemField>,
    sortColumn: Int,
    sortOrder: SortOrder,
    limit: Int,
    offset: Int,
): ItemQuery {
    val valueFilters = if (filters.typeId == null) emptyList() else fields.mapNotNull { field ->
        val id = field.id ?: return@mapNotNull null
        val value = filters.values[id]?.trim().orEmpty()
        if (value.isEmpty()) null else ValueFilter(id, value, field.dataType.filtersExactly)
    }
    val column = if (sortColumn >= SortColumns.FIRST_FIELD &&
        (filters.typeId == null || sortColumn - SortColumns.FIRST_FIELD >= fields.size)
    ) {
        SortColumns.TITLE
    } else {
        sortColumn
    }
    return ItemQuery(
        groupId = filters.groupId,
        typeId = filters.typeId,
        searchText = searchText.trim(),
        columnFilters = ColumnFilters(
            id = filters.id.filter(Char::isDigit),
            title = filters.title.trim(),
            disambiguation = filters.disambiguation.trim(),
            alias = filters.alias.trim(),
        ),
        propertyFilters = filters.properties
            .map { PropertyFilter(it.key.trim(), it.value.trim()) }
            .filter { it.key.isNotEmpty() },
        valueFilters = valueFilters,
        tagFilter = filters.tag,
        flagFilter = filters.flag,
        understandingFilter = filters.understanding,
        statusFilter = filters.status,
        pinnedFilter = filters.pinned,
        limit = limit,
        offset = offset,
        sortColumn = column,
        sortOrder = sortOrder,
    )
}
