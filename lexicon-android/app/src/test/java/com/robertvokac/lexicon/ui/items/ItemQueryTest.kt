package com.robertvokac.lexicon.ui.items

import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.PropertyFilter
import com.robertvokac.lexicon.model.SortColumns
import com.robertvokac.lexicon.model.SortOrder
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.ValueFilter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ItemQueryTest {
    private val fields = listOf(
        ItemField(4, 2, "Difficulty", FieldDataType.Enum, 0, listOf("easy", "hard")),
        ItemField(5, 2, "Notes", FieldDataType.Text, 1),
        ItemField(6, 2, "Year", FieldDataType.Integer, 2),
        ItemField(7, 2, "Kind", FieldDataType.Other, 3),
    )

    @Test
    fun everyFilterReachesTheQuery() {
        val filters = ItemFilters(
            groupId = 3, typeId = 2, id = "12a", title = " Mon ", disambiguation = "alg", alias = "semi",
            tag = "algebra", flag = "todo", status = ItemStatus.Draft, understanding = UnderstandingLevel.Practiced,
            pinned = true, values = mapOf(4 to "hard", 5 to " prose ", 6 to "", 7 to "x"),
            properties = listOf(PropertyFilter(" source ", " folk "), PropertyFilter(" ", "ignored")),
        )
        val query = buildItemQuery(" monoid ", filters, fields, SortColumns.TITLE, SortOrder.Descending, 50, 100)
        assertEquals(3, query.groupId)
        assertEquals(2, query.typeId)
        assertEquals("monoid", query.searchText)
        assertEquals("12", query.columnFilters.id)
        assertEquals("Mon", query.columnFilters.title)
        assertEquals("algebra", query.tagFilter)
        assertEquals("todo", query.flagFilter)
        assertEquals(ItemStatus.Draft, query.statusFilter)
        assertEquals(UnderstandingLevel.Practiced, query.understandingFilter)
        assertEquals(true, query.pinnedFilter)
        assertEquals(listOf(PropertyFilter("source", "folk")), query.propertyFilters)
        // Exact for every data type but Text and Other, as in the other clients.
        assertEquals(
            listOf(ValueFilter(4, "hard", true), ValueFilter(5, "prose", false), ValueFilter(7, "x", false)),
            query.valueFilters,
        )
        assertEquals(50, query.limit)
        assertEquals(100, query.offset)
        assertEquals(SortOrder.Descending, query.sortOrder)
        // Eleven column filters, three typed values and two property filters.
        assertEquals(16, filters.activeCount)
    }

    @Test
    fun valueFiltersNeedTheirType() {
        val query = buildItemQuery("", ItemFilters(values = mapOf(4 to "hard")), fields, 11, SortOrder.Ascending, 20, 0)
        assertTrue(query.valueFilters.isEmpty())
        // A type field column without a type falls back to Title.
        assertEquals(SortColumns.TITLE, query.sortColumn)
    }

    @Test
    fun aFieldColumnIsKeptWhileItsTypeIsSelected() {
        val query = buildItemQuery("", ItemFilters(typeId = 2), fields, 13, SortOrder.Ascending, 20, 0)
        assertEquals(13, query.sortColumn)
        val gone = buildItemQuery("", ItemFilters(typeId = 2), fields.take(1), 13, SortOrder.Ascending, 20, 0)
        assertEquals(SortColumns.TITLE, gone.sortColumn)
    }

    @Test
    fun noFiltersMeansAnEmptyQuery() {
        val filters = ItemFilters()
        assertTrue(filters.isEmpty)
        val query = buildItemQuery("", filters, emptyList(), SortColumns.ID, SortOrder.Ascending, 20, 0)
        assertEquals(null, query.groupId)
        assertEquals(null, query.pinnedFilter)
        assertEquals("", query.tagFilter)
    }

    @Test
    fun namedExactlyMatchesTitleFullTitleAndAliasIgnoringCase() {
        val item = Item(id = 1, title = "Monoid", disambiguation = "algebra", aliases = listOf("Semigroup with unit"))
        assertTrue(ItemsViewModel.namedExactly(item, "monoid"))
        assertTrue(ItemsViewModel.namedExactly(item, "MONOID [ALGEBRA]"))
        assertTrue(ItemsViewModel.namedExactly(item, "semigroup WITH unit"))
        assertFalse(ItemsViewModel.namedExactly(item, "mono"))
    }
}
