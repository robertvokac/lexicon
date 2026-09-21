package com.robertvokac.lexicon.ui.markdown

import org.commonmark.ext.autolink.AutolinkExtension
import org.commonmark.ext.gfm.strikethrough.Strikethrough
import org.commonmark.ext.gfm.strikethrough.StrikethroughExtension
import org.commonmark.ext.gfm.tables.TableBlock
import org.commonmark.ext.gfm.tables.TableBody
import org.commonmark.ext.gfm.tables.TableCell
import org.commonmark.ext.gfm.tables.TableHead
import org.commonmark.ext.gfm.tables.TableRow
import org.commonmark.ext.gfm.tables.TablesExtension
import org.commonmark.node.BlockQuote
import org.commonmark.node.BulletList
import org.commonmark.node.Code
import org.commonmark.node.Emphasis
import org.commonmark.node.FencedCodeBlock
import org.commonmark.node.HardLineBreak
import org.commonmark.node.Heading
import org.commonmark.node.HtmlBlock
import org.commonmark.node.HtmlInline
import org.commonmark.node.Image
import org.commonmark.node.IndentedCodeBlock
import org.commonmark.node.Link
import org.commonmark.node.ListItem
import org.commonmark.node.Node
import org.commonmark.node.OrderedList
import org.commonmark.node.Paragraph
import org.commonmark.node.SoftLineBreak
import org.commonmark.node.StrongEmphasis
import org.commonmark.node.Text
import org.commonmark.node.ThematicBreak
import org.commonmark.parser.Parser

// Markdown is parsed into this small immutable model and drawn with native
// Compose text. Nothing is ever rendered as HTML: raw HTML in the source is
// shown as the literal text it is, and links are only followed after
// SafeLinks has approved their scheme.

sealed interface MdInline {
    data class Plain(val text: String) : MdInline
    data class InlineCode(val code: String) : MdInline
    data class Emph(val children: List<MdInline>) : MdInline
    data class Strong(val children: List<MdInline>) : MdInline
    data class Strike(val children: List<MdInline>) : MdInline
    data class LinkTo(val destination: String, val children: List<MdInline>) : MdInline
    data class ImageRef(val destination: String, val alt: String) : MdInline
    data object SoftBreak : MdInline
    data object HardBreak : MdInline
}

enum class CellAlignment { Start, Center, End }

sealed interface MdBlock {
    data class Heading(val level: Int, val inlines: List<MdInline>) : MdBlock
    data class Paragraph(val inlines: List<MdInline>) : MdBlock
    data class CodeBlock(val language: String, val code: String) : MdBlock
    data class Quote(val blocks: List<MdBlock>) : MdBlock
    data class Bullets(val items: List<List<MdBlock>>) : MdBlock
    data class Numbered(val start: Int, val items: List<List<MdBlock>>) : MdBlock
    data object Rule : MdBlock
    data class Table(
        val alignments: List<CellAlignment>,
        val header: List<List<MdInline>>,
        val rows: List<List<List<MdInline>>>,
    ) : MdBlock

    /** Raw HTML, shown as text and never interpreted. */
    data class RawHtml(val literal: String) : MdBlock
}

object Markdown {
    private val parser: Parser = Parser.builder()
        .extensions(listOf(TablesExtension.create(), StrikethroughExtension.create(), AutolinkExtension.create()))
        .build()

    /** GitHub-flavoured Markdown, as the web client renders it. */
    fun parse(source: String): List<MdBlock> = blocks(parser.parse(source))

    private fun children(node: Node): Sequence<Node> = generateSequence(node.firstChild) { it.next }

    private fun blocks(parent: Node): List<MdBlock> = children(parent).mapNotNull(::block).toList()

    private fun block(node: Node): MdBlock? = when (node) {
        is Heading -> MdBlock.Heading(node.level, inlines(node))
        is Paragraph -> MdBlock.Paragraph(inlines(node))
        is FencedCodeBlock -> MdBlock.CodeBlock(node.info.orEmpty().trim().substringBefore(' '), node.literal.removeSuffix("\n"))
        is IndentedCodeBlock -> MdBlock.CodeBlock("", node.literal.removeSuffix("\n"))
        is BlockQuote -> MdBlock.Quote(blocks(node))
        is BulletList -> MdBlock.Bullets(children(node).filterIsInstance<ListItem>().map { blocks(it) }.toList())
        is OrderedList -> MdBlock.Numbered(
            node.markerStartNumber ?: 1,
            children(node).filterIsInstance<ListItem>().map { blocks(it) }.toList(),
        )
        is ThematicBreak -> MdBlock.Rule
        is HtmlBlock -> MdBlock.RawHtml(node.literal.trimEnd())
        is TableBlock -> table(node)
        else -> null
    }

    private fun table(node: TableBlock): MdBlock.Table {
        val header = mutableListOf<List<MdInline>>()
        val alignments = mutableListOf<CellAlignment>()
        val rows = mutableListOf<List<List<MdInline>>>()
        children(node).forEach { section ->
            children(section).filterIsInstance<TableRow>().forEach { row ->
                val cells = children(row).filterIsInstance<TableCell>().toList()
                when (section) {
                    is TableHead -> {
                        cells.forEach { cell ->
                            header += inlines(cell)
                            alignments += when (cell.alignment) {
                                TableCell.Alignment.CENTER -> CellAlignment.Center
                                TableCell.Alignment.RIGHT -> CellAlignment.End
                                else -> CellAlignment.Start
                            }
                        }
                    }
                    is TableBody -> rows += cells.map { inlines(it) }
                }
            }
        }
        return MdBlock.Table(alignments, header, rows)
    }

    private fun inlines(parent: Node): List<MdInline> = children(parent).mapNotNull(::inline).toList()

    private fun inline(node: Node): MdInline? = when (node) {
        is Text -> MdInline.Plain(node.literal)
        is Code -> MdInline.InlineCode(node.literal)
        is Emphasis -> MdInline.Emph(inlines(node))
        is StrongEmphasis -> MdInline.Strong(inlines(node))
        is Strikethrough -> MdInline.Strike(inlines(node))
        is Link -> MdInline.LinkTo(node.destination.orEmpty(), inlines(node))
        is Image -> MdInline.ImageRef(node.destination.orEmpty(), plainText(node))
        is SoftLineBreak -> MdInline.SoftBreak
        is HardLineBreak -> MdInline.HardBreak
        is HtmlInline -> MdInline.Plain(node.literal)
        else -> plainText(node).takeIf { it.isNotEmpty() }?.let { MdInline.Plain(it) }
    }

    private fun plainText(node: Node): String = buildString {
        children(node).forEach { child ->
            when (child) {
                is Text -> append(child.literal)
                is Code -> append(child.literal)
                else -> append(plainText(child))
            }
        }
    }
}
