package com.robertvokac.lexicon.ui.markdown

/**
 * The formatting toolbar of the desktop and web editors. Each action wraps
 * the selection, or inserts a sample to type over, and selects the result.
 */
enum class FormattingAction(
    val label: String,
    val description: String,
    val prefix: String = "",
    val suffix: String = "",
    val sample: String = "",
) {
    Bold("B", "Bold", "**", "**", "bold text"),
    Italic("I", "Italic", "*", "*", "italic text"),
    Heading2("H2", "Heading 2", "\n## ", "", "Header 2"),
    Heading3("H3", "Heading 3", "\n### ", "", "Header 3"),
    Heading4("H4", "Heading 4", "\n#### ", "", "Header 4"),
    BulletList("List", "Unordered list", "\n- ", "", "list item"),
    NumberedList("1.", "Ordered list", "\n1. ", "", "list item"),
    Quote("“", "Quote", "\n> ", "", "quote"),
    Rule("---", "Horizontal line", "\n---\n", "", ""),
    InlineCode("Code", "Inline code", "`", "`", "code"),
    CodeBlock("Block", "Code block"),
    Link("Link", "Insert link", "[", "](https://)", "link text"),
    Table("Table", "Insert table", "\n| Header 1 | Header 2 |\n| --- | --- |\n| Cell 1 | Cell 2 |\n", "", ""),
}

/** Replace [start] until [end] with [replacement], then select [selectionStart] until [selectionEnd]. */
data class TextEdit(
    val start: Int,
    val end: Int,
    val replacement: String,
    val selectionStart: Int,
    val selectionEnd: Int,
)

object MarkdownFormatting {
    /**
     * The edit [action] makes to [text] with the selection [selectionStart]
     * until [selectionEnd]. [codeLanguage] labels a code block.
     */
    fun edit(action: FormattingAction, text: CharSequence, selectionStart: Int, selectionEnd: Int, codeLanguage: String = ""): TextEdit {
        val start = minOf(selectionStart, selectionEnd).coerceIn(0, text.length)
        val end = maxOf(selectionStart, selectionEnd).coerceIn(0, text.length)
        val (prefix, suffix, sample) = when (action) {
            FormattingAction.CodeBlock -> Triple("\n```${codeLanguage.trim()}\n", "\n```\n", "code block")
            FormattingAction.Table -> Triple(action.prefix, "", "")
            else -> Triple(action.prefix, action.suffix, action.sample)
        }
        val selected = text.subSequence(start, end).toString()
        val inserted = if (action == FormattingAction.Table) "" else selected.ifEmpty { sample }
        val keep = if (action == FormattingAction.Table) selected else ""
        val caret = start + prefix.length
        return TextEdit(
            start = start,
            end = end,
            replacement = prefix + inserted + suffix + keep,
            selectionStart = caret,
            selectionEnd = caret + inserted.length,
        )
    }
}
