package com.robertvokac.lexicon.ui.item

import com.robertvokac.lexicon.model.IncomingLinkWrite
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.ItemWrite
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.OutgoingLinkWrite
import com.robertvokac.lexicon.model.Property
import com.robertvokac.lexicon.model.SaveItemRequest
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.asciiFold
import kotlinx.serialization.Serializable

/**
 * One link in the editor. [itemId] is the target of an outgoing link or the
 * source of a backlink; [id] is null until the server has saved it.
 */
@Serializable
data class LinkEntry(
    val key: Long,
    val id: Int?,
    val itemId: Int,
    val title: String,
    val linkType: LinkType,
    val customValue: String,
    val position: Int,
)

/** Everything the editor saves except the Markdown content. */
@Serializable
data class EditorFields(
    val groupId: Int? = null,
    val typeId: Int? = null,
    val title: String = "",
    val disambiguation: String = "",
    val status: ItemStatus = ItemStatus.None,
    val understanding: UnderstandingLevel = UnderstandingLevel.Unknown,
    val pinned: Boolean = false,
    val tags: List<String> = emptyList(),
    val flags: List<String> = emptyList(),
    val aliases: List<String> = emptyList(),
    val properties: List<Property> = emptyList(),
    /** Values by field ID, for the fields of the displayed type. */
    val values: Map<Int, String> = emptyMap(),
    val links: List<LinkEntry> = emptyList(),
    val backlinks: List<LinkEntry> = emptyList(),
)

/** The editor's saved-state snapshot: survives a configuration change or process death. */
@Serializable
data class EditorSnapshot(
    val fields: EditorFields,
    val content: String,
    val initialFields: EditorFields,
    val initialContent: String,
    val originalTypeId: Int?,
    val originalValues: Map<Int, String>,
    val typeChangeConfirmed: Boolean,
)

object EditorRules {
    /** Tags and flags accept several values separated by commas; the list stays sorted and unique. */
    fun withValues(values: List<String>, text: String): List<String> =
        (values + text.split(',').map { it.trim() }.filter { it.isNotEmpty() }).distinct().sorted()

    /** Aliases are whole: std::map<K, V> is one alias. */
    fun withAlias(values: List<String>, text: String): List<String> {
        val alias = text.trim()
        return if (alias.isEmpty() || alias in values) values else (values + alias).sorted()
    }

    fun replaced(values: List<String>, index: Int, text: String): List<String> {
        val value = text.trim()
        if (value.isEmpty() || index !in values.indices) return values
        return values.toMutableList().also { it[index] = value }.distinct().sorted()
    }

    /** Null when the property can be stored, or why not. Keys compare as the core compares them. */
    fun propertyProblem(properties: List<Property>, key: String, skipIndex: Int): String? {
        val trimmed = key.trim()
        if (trimmed.isEmpty()) return "Property key cannot be empty."
        val folded = asciiFold(trimmed)
        val duplicate = properties.withIndex().any { (index, property) -> index != skipIndex && asciiFold(property.key.trim()) == folded }
        return if (duplicate) "Property key must be unique in this item." else null
    }

    /** Links ordered as the server orders them: position, then title. */
    fun sortedLinks(links: List<LinkEntry>): List<LinkEntry> =
        links.sortedWith(compareBy<LinkEntry> { it.position }.thenBy(String.CASE_INSENSITIVE_ORDER) { it.title })

    /**
     * How many field values a switch away from the displayed type removes:
     * the values entered, plus the saved ones while the original type is
     * still displayed and its change has not been confirmed yet.
     */
    fun affectedByTypeChange(
        displayedFields: List<ItemField>,
        values: Map<Int, String>,
        displayedTypeId: Int?,
        originalTypeId: Int?,
        originalValues: Map<Int, String>,
        typeChangeConfirmed: Boolean,
    ): Int = displayedFields.mapNotNull { it.id }.count { fieldId ->
        values[fieldId].orEmpty().isNotBlank() ||
            (displayedTypeId == originalTypeId && !typeChangeConfirmed && originalValues[fieldId].orEmpty().isNotBlank())
    }

    /**
     * The complete save request: the item, and both link directions as their
     * complete desired state. Field values are sent for the displayed type's
     * fields only, trimmed, and empty values are left out.
     */
    fun saveRequest(fields: EditorFields, content: String, typeFields: List<ItemField>): SaveItemRequest {
        val groupId = requireNotNull(fields.groupId) { "Create at least one group first." }
        val values = if (fields.typeId == null) emptyMap() else typeFields.mapNotNull { field ->
            val id = field.id ?: return@mapNotNull null
            val value = fields.values[id]?.trim().orEmpty()
            if (value.isEmpty()) null else id.toString() to value
        }.toMap()
        return SaveItemRequest(
            item = ItemWrite(
                groupId = groupId,
                itemTypeId = fields.typeId,
                title = fields.title.trim(),
                disambiguation = fields.disambiguation.trim(),
                status = fields.status,
                understanding = fields.understanding,
                pinned = fields.pinned,
                content = content,
                tags = fields.tags,
                flags = fields.flags,
                aliases = fields.aliases,
                properties = fields.properties,
                fieldValues = values,
            ),
            links = fields.links.map {
                OutgoingLinkWrite(it.id, it.itemId, it.linkType, customValueFor(it), it.position)
            },
            backlinks = fields.backlinks.map {
                IncomingLinkWrite(it.id, it.itemId, it.linkType, customValueFor(it), it.position)
            },
        )
    }

    private fun customValueFor(link: LinkEntry): String =
        if (link.linkType == LinkType.Custom) link.customValue.trim() else ""
}
