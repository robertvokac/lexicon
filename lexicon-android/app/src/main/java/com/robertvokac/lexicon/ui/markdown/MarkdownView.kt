package com.robertvokac.lexicon.ui.markdown

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import com.robertvokac.lexicon.ui.theme.CodeColors
import com.robertvokac.lexicon.ui.theme.LocalCodeColors

/** Colors and the link handler inline text needs; built once per composition. */
@Immutable
private class InlineStyle(
    val link: Color,
    val code: Color,
    val onLink: (String) -> Unit,
)

@Composable
private fun rememberInlineStyle(): InlineStyle {
    val context = LocalContext.current
    val link = MaterialTheme.colorScheme.primary
    val code = LocalCodeColors.current.background
    return remember(link, code, context) { InlineStyle(link, code) { SafeLinks.open(context, it) } }
}

/** Renders [blocks] in a column; for content inside a scrolling parent. */
@Composable
fun MarkdownBlocks(blocks: List<MdBlock>, modifier: Modifier = Modifier) {
    val style = rememberInlineStyle()
    SelectionContainer(modifier) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            blocks.forEach { Block(it, style) }
        }
    }
}

/** Adds one lazy item per top-level block, so long notes compose only what is visible. */
fun LazyListScope.markdownItems(blocks: List<MdBlock>, keyPrefix: String, modifier: Modifier = Modifier) {
    itemsIndexed(blocks, key = { index, _ -> "$keyPrefix-$index" }, contentType = { _, block -> block::class }) { _, block ->
        val style = rememberInlineStyle()
        SelectionContainer(modifier.padding(vertical = 4.dp)) { Block(block, style) }
    }
}

@Composable
private fun Block(block: MdBlock, style: InlineStyle) {
    val typography = MaterialTheme.typography
    when (block) {
        is MdBlock.Heading -> Text(
            annotated(block.inlines, style),
            style = when (block.level) {
                1 -> typography.headlineSmall
                2 -> typography.titleLarge
                3 -> typography.titleMedium
                else -> typography.titleSmall
            },
            modifier = Modifier.padding(top = 8.dp).semantics { heading() },
        )
        is MdBlock.Paragraph -> Text(annotated(block.inlines, style), style = typography.bodyLarge)
        is MdBlock.CodeBlock -> CodeBlockView(block, LocalCodeColors.current)
        is MdBlock.Quote -> Row(Modifier.height(IntrinsicSize.Min)) {
            Box(
                Modifier
                    .width(3.dp)
                    .fillMaxHeight()
                    .background(MaterialTheme.colorScheme.outline),
            )
            Column(Modifier.padding(start = 12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                block.blocks.forEach { Block(it, style) }
            }
        }
        is MdBlock.Bullets -> ListView(block.items.map { "•" to it }, style)
        is MdBlock.Numbered -> ListView(block.items.mapIndexed { index, item -> "${block.start + index}." to item }, style)
        MdBlock.Rule -> HorizontalDivider(Modifier.padding(vertical = 8.dp))
        is MdBlock.Table -> TableView(block, style)
        is MdBlock.RawHtml -> Text(
            block.literal,
            style = typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun ListView(items: List<Pair<String, List<MdBlock>>>, style: InlineStyle) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        items.forEach { (marker, blocks) ->
            Row {
                Text(marker, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.widthIn(min = 24.dp).padding(end = 6.dp))
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    blocks.forEach { Block(it, style) }
                }
            }
        }
    }
}

@Composable
private fun CodeBlockView(block: MdBlock.CodeBlock, colors: CodeColors) {
    val text = remember(block, colors) {
        if (CppHighlighter.handles(block.language)) highlighted(block.code, colors) else AnnotatedString(block.code)
    }
    Surface(color = colors.background, shape = MaterialTheme.shapes.small, modifier = Modifier.fillMaxWidth()) {
        Text(
            text,
            style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = MaterialTheme.typography.bodyMedium.fontSize),
            softWrap = false,
            modifier = Modifier.horizontalScroll(rememberScrollState()).padding(12.dp),
        )
    }
}

private fun highlighted(code: String, colors: CodeColors): AnnotatedString = buildAnnotatedString {
    CppHighlighter.tokenize(code).forEach { token ->
        val color = when (token.kind) {
            CppHighlighter.Kind.Keyword -> colors.keyword
            CppHighlighter.Kind.StringLiteral -> colors.string
            CppHighlighter.Kind.Comment -> colors.comment
            CppHighlighter.Kind.Preprocessor -> colors.preprocessor
            null -> null
        }
        if (color == null) append(token.text) else withStyle(SpanStyle(color = color)) { append(token.text) }
    }
}

@Composable
private fun TableView(table: MdBlock.Table, style: InlineStyle) {
    val columns = maxOf(table.header.size, table.rows.maxOfOrNull { it.size } ?: 0)
    if (columns == 0) return
    val outline = MaterialTheme.colorScheme.outlineVariant
    Box(Modifier.horizontalScroll(rememberScrollState())) {
        Layout(
            content = {
                val rows = listOf(table.header) + table.rows
                rows.forEachIndexed { rowIndex, row ->
                    for (column in 0 until columns) {
                        val cell = row.getOrNull(column).orEmpty()
                        Text(
                            annotated(cell, style),
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = if (rowIndex == 0) FontWeight.Bold else null,
                            textAlign = when (table.alignments.getOrNull(column)) {
                                CellAlignment.Center -> TextAlign.Center
                                CellAlignment.End -> TextAlign.End
                                else -> TextAlign.Start
                            },
                            modifier = Modifier.border(0.5.dp, outline).padding(horizontal = 8.dp, vertical = 6.dp),
                        )
                    }
                }
            },
        ) { measurables, _ ->
            // Intrinsics size the grid first, so each cell is measured once:
            // columns take their widest cell, rows their tallest.
            val maxCell = 280.dp.roundToPx()
            val rowCount = measurables.size / columns
            val widths = IntArray(columns) { column ->
                (0 until rowCount).maxOf { row ->
                    measurables[row * columns + column].maxIntrinsicWidth(Constraints.Infinity).coerceAtMost(maxCell)
                }
            }
            val heights = IntArray(rowCount) { row ->
                (0 until columns).maxOf { column -> measurables[row * columns + column].minIntrinsicHeight(widths[column]) }
            }
            val remeasured = measurables.mapIndexed { index, measurable ->
                measurable.measure(Constraints.fixed(widths[index % columns], heights[index / columns]))
            }
            layout(widths.sum(), heights.sum()) {
                var y = 0
                for (row in 0 until rowCount) {
                    var x = 0
                    for (column in 0 until columns) {
                        remeasured[row * columns + column].placeRelative(x, y)
                        x += widths[column]
                    }
                    y += heights[row]
                }
            }
        }
    }
}

private fun annotated(inlines: List<MdInline>, style: InlineStyle): AnnotatedString = buildAnnotatedString {
    fun appendAll(nodes: List<MdInline>) {
        nodes.forEach { node ->
            when (node) {
                is MdInline.Plain -> append(node.text)
                is MdInline.InlineCode -> withStyle(SpanStyle(fontFamily = FontFamily.Monospace, background = style.code)) {
                    append(node.code)
                }
                is MdInline.Emph -> withStyle(SpanStyle(fontStyle = FontStyle.Italic)) { appendAll(node.children) }
                is MdInline.Strong -> withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { appendAll(node.children) }
                is MdInline.Strike -> withStyle(SpanStyle(textDecoration = TextDecoration.LineThrough)) { appendAll(node.children) }
                is MdInline.LinkTo -> if (SafeLinks.isAllowed(node.destination)) {
                    withLink(link(node.destination, style)) { appendAll(node.children) }
                } else {
                    appendAll(node.children)
                }
                is MdInline.ImageRef -> {
                    // Images are not loaded: a note never makes the device fetch
                    // from wherever its source points.
                    val label = "[image: ${node.alt.ifEmpty { node.destination }}]"
                    if (SafeLinks.isAllowed(node.destination)) withLink(link(node.destination, style)) { append(label) }
                    else append(label)
                }
                MdInline.SoftBreak -> append(' ')
                MdInline.HardBreak -> append('\n')
            }
        }
    }
    appendAll(inlines)
}

private fun link(destination: String, style: InlineStyle) = LinkAnnotation.Url(
    destination,
    TextLinkStyles(SpanStyle(color = style.link, textDecoration = TextDecoration.Underline)),
) { style.onLink(destination) }
