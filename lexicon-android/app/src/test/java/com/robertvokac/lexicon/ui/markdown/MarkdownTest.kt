package com.robertvokac.lexicon.ui.markdown

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MarkdownTest {
    @Test
    fun parsesTheBlocksTheEditorInserts() {
        val blocks = Markdown.parse(
            """
            # Title
            Some **bold** and *italic* and `code`, ~~gone~~.

            - one
            - two

            3. three

            > quoted

            ---

            ```cpp
            int main() {}
            ```

            | A | B |
            | :-: | --: |
            | 1 | 2 |
            """.trimIndent(),
        )
        assertEquals(
            listOf(
                MdBlock.Heading::class, MdBlock.Paragraph::class, MdBlock.Bullets::class, MdBlock.Numbered::class,
                MdBlock.Quote::class, MdBlock.Rule::class, MdBlock.CodeBlock::class, MdBlock.Table::class,
            ),
            blocks.map { it::class },
        )
        assertEquals("cpp", (blocks[6] as MdBlock.CodeBlock).language)
        assertEquals("int main() {}", (blocks[6] as MdBlock.CodeBlock).code)
        assertEquals(3, (blocks[3] as MdBlock.Numbered).start)
        val table = blocks[7] as MdBlock.Table
        assertEquals(listOf(CellAlignment.Center, CellAlignment.End), table.alignments)
        assertEquals(1, table.rows.size)
    }

    @Test
    fun rawHtmlStaysText() {
        val blocks = Markdown.parse("<script>alert(1)</script>\n\nText <b onclick=\"x\">bold</b>")
        assertTrue(blocks[0] is MdBlock.RawHtml)
        assertEquals("<script>alert(1)</script>", (blocks[0] as MdBlock.RawHtml).literal)
        val paragraph = blocks[1] as MdBlock.Paragraph
        assertTrue(paragraph.inlines.contains(MdInline.Plain("<b onclick=\"x\">")))
    }

    @Test
    fun bareUrlsBecomeLinks() {
        val paragraph = Markdown.parse("See https://example.com/a for more.").single() as MdBlock.Paragraph
        assertTrue(paragraph.inlines.any { it is MdInline.LinkTo && it.destination == "https://example.com/a" })
    }

    @Test
    fun onlySafeLinkSchemesAreFollowed() {
        assertTrue(SafeLinks.isAllowed("https://example.com/page"))
        assertTrue(SafeLinks.isAllowed("http://example.com"))
        assertTrue(SafeLinks.isAllowed("mailto:someone@example.com"))
        assertTrue(SafeLinks.isAllowed("tel:+420123456789"))
        assertFalse(SafeLinks.isAllowed("javascript:alert(1)"))
        assertFalse(SafeLinks.isAllowed("JAVASCRIPT:alert(1)"))
        assertFalse(SafeLinks.isAllowed(" javascript:alert(1)"))
        assertFalse(SafeLinks.isAllowed("java\tscript:alert(1)"))
        assertFalse(SafeLinks.isAllowed("intent://scan/#Intent;scheme=zxing;end"))
        assertFalse(SafeLinks.isAllowed("file:///data/data/com.robertvokac.lexicon/files"))
        assertFalse(SafeLinks.isAllowed("content://com.android.contacts/contacts"))
        assertFalse(SafeLinks.isAllowed("data:text/html,<script>alert(1)</script>"))
        assertFalse(SafeLinks.isAllowed("#section"))
        assertFalse(SafeLinks.isAllowed("relative/page"))
        assertFalse(SafeLinks.isAllowed("https://"))
    }

    @Test
    fun formattingWrapsTheSelectionOrInsertsASample() {
        val bold = MarkdownFormatting.edit(FormattingAction.Bold, "a word here", 2, 6)
        assertEquals(TextEdit(2, 6, "**word**", 4, 8), bold)
        val italic = MarkdownFormatting.edit(FormattingAction.Italic, "", 0, 0)
        assertEquals("*italic text*", italic.replacement)
        assertEquals(1 to 12, italic.selectionStart to italic.selectionEnd)
        val heading = MarkdownFormatting.edit(FormattingAction.Heading2, "x", 1, 1)
        assertEquals("\n## Header 2", heading.replacement)
        val link = MarkdownFormatting.edit(FormattingAction.Link, "Lexicon", 0, 7)
        assertEquals("[Lexicon](https://)", link.replacement)
        val code = MarkdownFormatting.edit(FormattingAction.CodeBlock, "", 0, 0, codeLanguage = " cpp ")
        assertEquals("\n```cpp\ncode block\n```\n", code.replacement)
        val table = MarkdownFormatting.edit(FormattingAction.Table, "", 0, 0)
        assertTrue(table.replacement.contains("| --- | --- |"))
        // Every toolbar action of the desktop and web editors is here.
        assertEquals(13, FormattingAction.entries.size)
    }

    @Test
    fun highlightsCppLikeTheOtherClients() {
        val source = "#include <vector>\n// note\nint x = 1'000; auto s = R\"(a\"b)\"; /* c */ return \"s\";"
        val tokens = CppHighlighter.tokenize(source)
        assertEquals(source, tokens.joinToString("") { it.text })
        fun kinds(kind: CppHighlighter.Kind) = tokens.filter { it.kind == kind }.map { it.text }
        assertEquals(listOf("#include"), kinds(CppHighlighter.Kind.Preprocessor))
        assertEquals(listOf("<vector>", "R\"(a\"b)\"", "\"s\""), kinds(CppHighlighter.Kind.StringLiteral))
        assertEquals(listOf("// note", "/* c */"), kinds(CppHighlighter.Kind.Comment))
        assertEquals(listOf("int", "auto", "return"), kinds(CppHighlighter.Kind.Keyword))
        assertTrue(CppHighlighter.handles("C++"))
        assertFalse(CppHighlighter.handles("python"))
    }
}
