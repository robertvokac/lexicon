package com.robertvokac.lexicon.model

/** "Title [disambiguation]", the format both other clients use. */
fun formatItemTitle(title: String, disambiguation: String): String =
    if (disambiguation.isEmpty()) title else "$title [$disambiguation]"

/** The sortColumn indexes of the query endpoint, shared with Qt and web. */
object SortColumns {
    const val ID = 0
    const val GROUP = 1
    const val TYPE = 2
    const val TITLE = 3
    const val DISAMBIGUATION = 4
    const val TAGS = 5
    const val FLAGS = 6
    const val ALIASES = 7
    const val STATUS = 8
    const val UNDERSTANDING = 9
    const val PINNED = 10

    /** The first column of the selected type's fields, in display order. */
    const val FIRST_FIELD = 11

    val base: List<Pair<Int, String>> = listOf(
        ID to "Id",
        GROUP to "Group",
        TYPE to "Type",
        TITLE to "Title",
        DISAMBIGUATION to "Disambiguation",
        TAGS to "Tags",
        FLAGS to "Flags",
        ALIASES to "Aliases",
        STATUS to "Status",
        UNDERSTANDING to "Understanding",
        PINNED to "Pinned",
    )

    /** The base columns followed by the fields of the selected type. */
    fun available(fields: List<ItemField>): List<Pair<Int, String>> =
        base + fields.mapIndexed { index, field -> FIRST_FIELD + index to field.name }
}

/** ASCII-only case folding, the comparison policy of the Lexicon core. */
fun asciiFold(value: String): String = buildString(value.length) {
    for (ch in value) append(if (ch in 'A'..'Z') ch + ('a' - 'A') else ch)
}
