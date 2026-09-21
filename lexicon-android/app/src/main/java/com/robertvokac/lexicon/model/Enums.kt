package com.robertvokac.lexicon.model

import kotlinx.serialization.Serializable

// The REST API names these values symbolically. The constant names below are
// exactly those names; ordinals never cross the wire. An unknown name in a
// response fails decoding and surfaces as ApiException.Incompatible.

@Serializable
enum class ItemStatus(val label: String) {
    None("None"),
    Draft("Draft"),
    Completed("Completed"),
}

@Serializable
enum class UnderstandingLevel(val label: String, val description: String) {
    Unknown("Unknown", "Never encountered"),
    Recognized("Recognized", "Seen before, can identify"),
    Understood("Understood", "Conceptually grasped"),
    Practiced("Practiced", "Can apply in real situations"),
    Mastered("Mastered", "Fully internalized, can teach or innovate"),
}

@Serializable
enum class LinkType(val label: String) {
    None("Link"),
    IsA("Is A"),
    PartOf("Part Of"),
    Uses("Uses"),
    DependsOn("Depends On"),
    Implements("Implements"),
    Related("Related"),
    Contrasts("Contrasts"),
    AlternativeTo("Alternative To"),
    ParentOf("Parent Of"),
    Custom("Custom"),
    ;

    companion object {
        /** Every type a link can be saved with. `None` is not a link type. */
        val persistable: List<LinkType> = entries.filter { it != None }
    }
}

@Serializable
enum class FieldDataType {
    Integer,
    Float,
    Text,
    Date,
    Time,
    Timestamp,
    Boolean,
    Enum,
    Blob,
    Other,
    ;

    /** Value filters on these types match exactly; the others match contained text. */
    val filtersExactly: kotlin.Boolean get() = this != Text && this != Other
}

@Serializable
enum class SortOrder { Ascending, Descending }
