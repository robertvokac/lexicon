package com.robertvokac.lexicon.ui.item

import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemBundle
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.Link
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.Property
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class EditorRulesTest {
    private fun link(key: Long, id: Int?, itemId: Int, title: String, type: LinkType, custom: String = "", position: Int = 0) =
        LinkEntry(key, id, itemId, title, type, custom, position)

    @Test
    fun aConflictNamesWhatDiffers() {
        val fields = EditorFields(groupId = 1, title = "Monoid", tags = listOf("algebra"), revision = 3,
            links = listOf(link(1, 10, 2, "Semigroup", LinkType.Related)))
        val newer = ItemBundle(
            item = Item(id = 7, groupId = 1, title = "Monoid", tags = listOf("algebra", "maths"), content = "Changed", revision = 5),
            links = listOf(Link(id = 10, fromItemId = 7, toItemId = 2, linkType = LinkType.Related)),
        )
        assertEquals(listOf("Content", "Tags"), EditorRules.conflictDifferences(fields, "Original", newer))
        assertEquals(emptyList<String>(), EditorRules.conflictDifferences(fields.copy(tags = listOf("maths", "algebra")), "Changed", newer))
    }

    @Test
    fun overwritingTakesTheNewRevisionAndRecreatesRemovedLinks() {
        val fields = EditorFields(groupId = 1, title = "Monoid", revision = 3,
            links = listOf(link(1, 10, 2, "Semigroup", LinkType.Related), link(2, 11, 3, "Group", LinkType.PartOf)))
        val newer = ItemBundle(
            item = Item(id = 7, groupId = 1, title = "Monoid", revision = 5),
            links = listOf(Link(id = 10, fromItemId = 7, toItemId = 2, linkType = LinkType.Related)),
        )
        val overwriting = EditorRules.overwriting(fields, newer)
        assertEquals(5, overwriting.revision)
        assertEquals(listOf(10, null), overwriting.links.map { it.id })
        assertEquals(5, EditorRules.saveRequest(overwriting, "", emptyList()).item.revision)
    }

    @Test
    fun theSaveRequestIsTheCompleteState() {
        val fields = listOf(ItemField(4, 2, "Difficulty", FieldDataType.Enum), ItemField(5, 2, "Notes", FieldDataType.Text))
        val editor = EditorFields(
            groupId = 1, typeId = 2, title = "  Monoid ", disambiguation = " algebra ", status = ItemStatus.Draft,
            pinned = true, tags = listOf("algebra"), properties = listOf(Property("source", "folklore")),
            values = mapOf(4 to "hard", 5 to "  ", 99 to "left over from another type"),
            links = listOf(link(1, 10, 12, "Semigroup", LinkType.IsA, custom = "stale", position = 1)),
            backlinks = listOf(link(2, null, 9, "Group", LinkType.Custom, custom = " generalizes ")),
        )
        val request = EditorRules.saveRequest(editor, "# Monoid", fields)
        assertEquals("Monoid", request.item.title)
        assertEquals("algebra", request.item.disambiguation)
        assertEquals(mapOf("4" to "hard"), request.item.fieldValues)
        assertEquals("# Monoid", request.item.content)
        val outgoing = request.links.single()
        assertEquals(10, outgoing.id)
        assertEquals(12, outgoing.toItemId)
        // Only Custom links carry a value.
        assertEquals("", outgoing.customValue)
        val incoming = request.backlinks.single()
        assertNull(incoming.id)
        assertEquals(9, incoming.fromItemId)
        assertEquals("generalizes", incoming.customValue)
    }

    @Test
    fun anItemWithoutATypeSendsNoValues() {
        val request = EditorRules.saveRequest(EditorFields(groupId = 1, title = "x", values = mapOf(4 to "hard")), "", emptyList())
        assertEquals(emptyMap<String, String>(), request.item.fieldValues)
    }

    @Test
    fun aCustomLinkWithoutAValueCannotBeSaved() {
        val editor = EditorFields(groupId = 1, title = "x", links = listOf(link(1, null, 2, "y", LinkType.Custom, custom = " ")))
        assertThrows(IllegalArgumentException::class.java) { EditorRules.saveRequest(editor, "", emptyList()) }
    }

    @Test
    fun aGrouplessItemCannotBeSaved() {
        assertThrows(IllegalArgumentException::class.java) { EditorRules.saveRequest(EditorFields(title = "x"), "", emptyList()) }
    }

    @Test
    fun typeChangesCountTheValuesTheyRemove() {
        val displayed = listOf(ItemField(4, 2, "A", FieldDataType.Text), ItemField(5, 2, "B", FieldDataType.Text), ItemField(6, 2, "C", FieldDataType.Text))
        // Typed values count, whatever the original type was.
        assertEquals(1, EditorRules.affectedByTypeChange(displayed, mapOf(4 to "x"), 2, 3, emptyMap(), false))
        // Saved values count while the original type is displayed and unconfirmed.
        assertEquals(2, EditorRules.affectedByTypeChange(displayed, mapOf(4 to "x"), 2, 2, mapOf(5 to "saved"), false))
        assertEquals(1, EditorRules.affectedByTypeChange(displayed, mapOf(4 to "x"), 2, 2, mapOf(5 to "saved"), true))
        assertEquals(0, EditorRules.affectedByTypeChange(displayed, mapOf(4 to " "), 2, 3, emptyMap(), false))
    }

    @Test
    fun tagsAndFlagsTakeSeveralValuesAliasesOne() {
        assertEquals(listOf("a", "b", "c"), EditorRules.withValues(listOf("b"), " c, a ,, b "))
        assertEquals(listOf("std::map<K, V>", "x"), EditorRules.withAlias(listOf("x"), " std::map<K, V> "))
        assertEquals(listOf("x"), EditorRules.withAlias(listOf("x"), "x"))
        assertEquals(listOf("a", "z"), EditorRules.replaced(listOf("a", "b"), 1, " z "))
        assertEquals(listOf("a", "b"), EditorRules.replaced(listOf("a", "b"), 1, " "))
    }

    @Test
    fun propertyKeysAreUniqueIgnoringAsciiCase() {
        val properties = listOf(Property("Source", "a"), Property("page", "3"))
        assertEquals("Property key cannot be empty.", EditorRules.propertyProblem(properties, " ", -1))
        assertEquals("Property key must be unique in this item.", EditorRules.propertyProblem(properties, "source", -1))
        assertNull(EditorRules.propertyProblem(properties, "SOURCE", 0))
        assertNull(EditorRules.propertyProblem(properties, "Édition", -1))
    }

    @Test
    fun linksSortByPositionThenTitle() {
        val sorted = EditorRules.sortedLinks(
            listOf(link(1, null, 1, "beta", LinkType.Uses, position = 1), link(2, null, 2, "Alpha", LinkType.Uses, position = 1), link(3, null, 3, "zeta", LinkType.Uses, position = 0)),
        )
        assertEquals(listOf("zeta", "Alpha", "beta"), sorted.map { it.title })
    }
}
