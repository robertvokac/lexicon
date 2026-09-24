package com.robertvokac.lexicon.model

import kotlinx.serialization.Serializable

// Kotlin mirrors of the /api/v1 JSON representations (docs/rest-api.md and
// lexicon-http/Transport.cpp). Absent IDs are null, never -1 or 0.

@Serializable
data class Health(
    val status: String,
    val apiVersion: Int,
    val application: String = "",
)

@Serializable
data class LoginRequest(val username: String, val password: String) {
    override fun toString(): String = "LoginRequest(username=$username, password=<redacted>)"
}

@Serializable
data class LoginResponse(
    val token: String,
    val username: String,
    val apiVersion: Int,
    val idleTimeoutSeconds: Long = 0,
    val absoluteLifetimeSeconds: Long = 0,
) {
    override fun toString(): String =
        "LoginResponse(username=$username, apiVersion=$apiVersion, token=<redacted>)"
}

@Serializable
data class Me(val username: String, val apiVersion: Int)

@Serializable
data class Group(
    val id: Int? = null,
    val name: String,
    val description: String = "",
    val position: Int = 0,
)

@Serializable
data class ItemType(
    val id: Int? = null,
    val groupId: Int? = null,
    val groupName: String = "",
    val name: String,
    val description: String = "",
) {
    /** "All groups" for a type usable everywhere, otherwise its group. */
    val scopeLabel: String get() = if (groupId == null) "All groups" else groupName

    val displayName: String get() = "$name ($scopeLabel)"
}

@Serializable
data class ItemField(
    val id: Int? = null,
    val itemTypeId: Int? = null,
    val name: String,
    val dataType: FieldDataType,
    val position: Int = 0,
    val enumOptions: List<String> = emptyList(),
)

@Serializable
data class Property(val key: String, val value: String = "")

@Serializable
data class Link(
    val id: Int? = null,
    val fromItemId: Int? = null,
    val toItemId: Int? = null,
    val linkType: LinkType,
    val position: Int = 0,
    val customValue: String = "",
    val fromItemTitle: String = "",
    val toItemTitle: String = "",
)

@Serializable
data class Item(
    val id: Int? = null,
    val groupId: Int? = null,
    val groupName: String = "",
    val itemTypeId: Int? = null,
    val itemTypeName: String = "",
    /** Keyed by field ID as a string, because JSON object keys are strings. */
    val fieldValues: Map<String, String> = emptyMap(),
    val properties: List<Property> = emptyList(),
    val title: String,
    val disambiguation: String = "",
    val aliases: List<String> = emptyList(),
    val tags: List<String> = emptyList(),
    val flags: List<String> = emptyList(),
    val status: ItemStatus = ItemStatus.None,
    val understanding: UnderstandingLevel = UnderstandingLevel.Unknown,
    val pinned: Boolean = false,
    val content: String = "",
    /** Moves on with every change to the item, its values or its links. */
    val revision: Int = 0,
    /** Last review and next one, UTC "YYYY-MM-DDTHH:MM:SSZ"; null when never reviewed (due now). */
    val reviewedAt: String? = null,
    val reviewDueAt: String? = null,
    /** Why a search found this item: the piece of content around the match; null otherwise. */
    val matchSnippet: String? = null,
) {
    val displayTitle: String get() = formatItemTitle(title, disambiguation)

    fun fieldValue(fieldId: Int): String = fieldValues[fieldId.toString()].orEmpty()
}

@Serializable
data class UsageValue(val value: String, val usageCount: Int)

@Serializable
data class ColumnFilters(
    val id: String = "",
    val title: String = "",
    val disambiguation: String = "",
    val alias: String = "",
)

@Serializable
data class PropertyFilter(val key: String, val value: String = "")

@Serializable
data class ValueFilter(val fieldId: Int, val value: String, val exact: Boolean)

/** The body of POST /api/v1/items/query. Every field is sent explicitly. */
@Serializable
data class ItemQuery(
    val groupId: Int? = null,
    val typeId: Int? = null,
    val searchText: String = "",
    val columnFilters: ColumnFilters = ColumnFilters(),
    val propertyFilters: List<PropertyFilter> = emptyList(),
    val valueFilters: List<ValueFilter> = emptyList(),
    val tagFilter: String = "",
    val flagFilter: String = "",
    val understandingFilter: UnderstandingLevel? = null,
    val statusFilter: ItemStatus? = null,
    val pinnedFilter: Boolean? = null,
    val limit: Int = 20,
    val offset: Int = 0,
    val sortColumn: Int = SortColumns.TITLE,
    val sortOrder: SortOrder = SortOrder.Ascending,
)

@Serializable
data class ItemPage(val items: List<Item>, val totalCount: Int)

/** GET /items/{id}?include=links,backlinks */
@Serializable
data class ItemBundle(
    val item: Item,
    val links: List<Link> = emptyList(),
    val backlinks: List<Link> = emptyList(),
)

@Serializable
data class SavedItem(val id: Int, val item: Item)

/** An outgoing link in a save request. fromItemId is filled in by the server. */
@Serializable
data class OutgoingLinkWrite(
    val id: Int?,
    val toItemId: Int,
    val linkType: LinkType,
    val customValue: String = "",
    val position: Int = 0,
)

/** An incoming link in a save request. toItemId is filled in by the server. */
@Serializable
data class IncomingLinkWrite(
    val id: Int?,
    val fromItemId: Int,
    val linkType: LinkType,
    val customValue: String = "",
    val position: Int = 0,
)

/** The item fields a save request carries. Read-only projections are left out. */
@Serializable
data class ItemWrite(
    val groupId: Int,
    val itemTypeId: Int? = null,
    val title: String,
    val disambiguation: String = "",
    val status: ItemStatus = ItemStatus.None,
    val understanding: UnderstandingLevel = UnderstandingLevel.Unknown,
    val pinned: Boolean = false,
    val content: String = "",
    val tags: List<String> = emptyList(),
    val flags: List<String> = emptyList(),
    val aliases: List<String> = emptyList(),
    val properties: List<Property> = emptyList(),
    val fieldValues: Map<String, String> = emptyMap(),
    /** The revision the edit started from; the server refuses the save with 409 once it has moved on. 0 skips the check. */
    val revision: Int = 0,
)

/**
 * The complete desired state of an item and both link directions, saved by
 * the server in one unit of work (POST /items, PUT /items/{id}).
 */
@Serializable
data class SaveItemRequest(
    val item: ItemWrite,
    val links: List<OutgoingLinkWrite> = emptyList(),
    val backlinks: List<IncomingLinkWrite> = emptyList(),
) {
    init {
        require(item.title.isNotBlank()) { "Title cannot be empty." }
        (links.map { it.linkType to it.customValue } + backlinks.map { it.linkType to it.customValue })
            .forEach { (type, custom) ->
                require(type != LinkType.None) { "A link needs a link type." }
                require(type != LinkType.Custom || custom.isNotBlank()) { "Custom links need a value." }
                require(type == LinkType.Custom || custom.isEmpty()) { "Only Custom links carry a value." }
            }
    }
}

@Serializable
data class GroupWrite(val name: String, val description: String = "", val position: Int = 0)

@Serializable
data class TypeWrite(val name: String, val description: String = "", val groupId: Int? = null)

@Serializable
data class FieldWrite(
    val name: String,
    val dataType: FieldDataType,
    val position: Int = 0,
    val enumOptions: List<String> = emptyList(),
    /** Required by PUT /fields/{id}; ignored by POST /types/{id}/fields. */
    val itemTypeId: Int? = null,
)

/** What POST /import did: created, skipped and why. */
@Serializable
data class ImportReport(
    val groupsCreated: Int = 0,
    val typesCreated: Int = 0,
    val fieldsCreated: Int = 0,
    val itemsCreated: Int = 0,
    val itemsSkipped: Int = 0,
    val linksCreated: Int = 0,
    val blobsImported: Int = 0,
    val alarmsCreated: Int = 0,
    val cardsCreated: Int = 0,
    val warnings: List<String> = emptyList(),
) {
    val summary: String
        get() = "Imported $itemsCreated item(s), $linksCreated link(s), $blobsImported file(s), $alarmsCreated alarm(s) " +
            "and $cardsCreated card(s); $itemsSkipped item(s) were already here. Created $groupsCreated group(s), " +
            "$typesCreated type(s) and $fieldsCreated field(s)."
}

/** How well an item was remembered in a review (POST /items/{id}/review). */
enum class ReviewRating(val step: Int) {
    Again(-1),
    Hard(0),
    Good(1),
    Easy(2),
    ;

    /** The understanding a review with this rating leaves an item at, as the server computes it. */
    fun levelAfter(current: UnderstandingLevel): UnderstandingLevel =
        UnderstandingLevel.entries[(current.ordinal + step).coerceIn(0, UnderstandingLevel.entries.size - 1)]

    companion object {
        /** Days until the next review, by the understanding after one (lexicon-core/Review.h). */
        fun intervalDays(level: UnderstandingLevel): Int = when (level) {
            UnderstandingLevel.Unknown -> 1
            UnderstandingLevel.Recognized -> 2
            UnderstandingLevel.Understood -> 5
            UnderstandingLevel.Practiced -> 12
            UnderstandingLevel.Mastered -> 30
        }
    }
}

/** An item in a relationship graph, [depth] links from its centre. */
@Serializable
data class GraphNode(
    val id: Int,
    val title: String,
    val disambiguation: String = "",
    val groupName: String = "",
    val itemTypeName: String = "",
    val depth: Int = 0,
) {
    val displayTitle: String get() = formatItemTitle(title, disambiguation)
}

/** GET /items/{id}/graph: the items around one, the centre first, and the links among them. */
@Serializable
data class ItemGraph(val nodes: List<GraphNode>, val edges: List<Link>, val truncated: Boolean = false)

/** GET /review: the items due now, and how many are due in all. */
@Serializable
data class ReviewQueue(val items: List<Item>, val dueCount: Int)

@Serializable
internal data class ReviewRequest(val rating: ReviewRating)

// Response envelopes -------------------------------------------------------

@Serializable internal data class ItemEnvelope(val item: Item)

@Serializable internal data class ImportEnvelope(val report: ImportReport)

/**
 * A reminder: [firesAt] is UTC "YYYY-MM-DDTHH:MM:SSZ". [dismissedAt] is set
 * once someone dismissed it after it went off, in any client.
 */
@Serializable
data class Alarm(
    val id: Int? = null,
    val title: String,
    val description: String = "",
    val firesAt: String,
    val dismissedAt: String? = null,
)

@Serializable
internal data class SnoozeRequest(val minutes: Int)

@Serializable
data class AlarmWrite(val title: String, val description: String = "", val firesAt: String)

@Serializable internal data class AlarmsEnvelope(val alarms: List<Alarm>)
@Serializable internal data class AlarmEnvelope(val alarm: Alarm)

/**
 * A question and its answer, for active recall; it belongs to one item and
 * goes when the item goes. The counts and [lastAttempt] are the server's:
 * only an attempt moves them, never an edit. Cards are not the item's Review
 * and never change its understanding or review dates.
 */
@Serializable
data class Card(
    val id: Int,
    val itemId: Int,
    val question: String,
    val answer: String,
    val successCount: Long = 0,
    val failureCount: Long = 0,
    /** UTC "YYYY-MM-DDTHH:MM:SSZ" by the server's clock; null when never attempted. */
    val lastAttempt: String? = null,
)

/** A card in a quiz, with the title of the item it belongs to. */
@Serializable
data class QuizCard(
    val id: Int,
    val itemId: Int,
    val itemTitle: String = "",
    val question: String,
    val answer: String,
    val successCount: Long = 0,
    val failureCount: Long = 0,
    val lastAttempt: String? = null,
)

/**
 * GET /items/{id}/quiz-cards: the cards of an item, or of the items around it,
 * the centre's first. [itemCount] is how many items the quiz covered, with
 * cards or without; [truncated] says more were in reach than the limit allowed.
 */
@Serializable
data class CardQuizSet(val cards: List<QuizCard>, val itemCount: Int = 0, val truncated: Boolean = false)

/** The body of POST /items/{id}/cards and PUT /cards/{id}: the only editable parts. */
@Serializable
data class CardWrite(val question: String, val answer: String)

/** The body of POST /cards/{id}/attempt: the answer to "Do you know?". */
@Serializable
internal data class CardAttempt(val success: Boolean)

@Serializable internal data class CardsEnvelope(val cards: List<Card>)
@Serializable internal data class CardEnvelope(val card: Card)

@Serializable internal data class GroupsEnvelope(val groups: List<Group>)
@Serializable internal data class GroupEnvelope(val group: Group)
@Serializable internal data class DefaultGroupEnvelope(val groupId: Int)
@Serializable internal data class TypesEnvelope(val types: List<ItemType>)
@Serializable internal data class TypeEnvelope(val type: ItemType)
@Serializable internal data class FieldsEnvelope(val fields: List<ItemField>)
@Serializable internal data class FieldEnvelope(val field: ItemField)
@Serializable internal data class CountEnvelope(val count: Int)
@Serializable internal data class ItemIdEnvelope(val itemId: Int)
@Serializable internal data class StringsEnvelope(val values: List<String>)
@Serializable internal data class UsageEnvelope(val values: List<UsageValue>)
/** An uploaded file: [mediaType] names the image type the server sees in its bytes, if any. */
@Serializable
data class UploadedBlob(val hash: String, val mediaType: String? = null)
@Serializable internal data class ErrorEnvelope(val error: ErrorBody)
@Serializable internal data class ErrorBody(val code: String = "error", val message: String = "")
