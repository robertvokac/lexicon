package com.robertvokac.lexicon.ui.graph

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.rememberTransformableState
import androidx.compose.foundation.gestures.transformable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.model.GraphNode
import com.robertvokac.lexicon.model.ItemGraph
import com.robertvokac.lexicon.ui.common.ErrorBox
import com.robertvokac.lexicon.ui.common.LoadingBox
import com.robertvokac.lexicon.ui.common.userMessage
import com.robertvokac.lexicon.ui.item.linkDescription
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlin.math.hypot

data class GraphState(
    val loading: Boolean = true,
    val error: String? = null,
    val centre: Int,
    val depth: Int = 2,
    val graph: ItemGraph? = null,
    val points: List<GraphLayout.Point> = emptyList(),
    val selected: Int? = null,
)

/** The items around one item, and the one the person picked. */
class GraphViewModel(private val container: AppContainer, itemId: Int) : ViewModel() {
    private val _state = MutableStateFlow(GraphState(centre = itemId))
    val state: StateFlow<GraphState> = _state.asStateFlow()

    init {
        load()
    }

    fun load() {
        val current = _state.value
        _state.update { it.copy(loading = true, error = null) }
        viewModelScope.launch {
            try {
                val graph = container.api.itemGraph(current.centre, current.depth)
                val index = graph.nodes.withIndex().associate { (position, node) -> node.id to position }
                val edges = graph.edges.mapNotNull { link ->
                    val from = index[link.fromItemId] ?: return@mapNotNull null
                    val to = index[link.toItemId] ?: return@mapNotNull null
                    from to to
                }
                val points = GraphLayout.layout(graph.nodes.map { it.depth }, edges)
                _state.update { it.copy(loading = false, graph = graph, points = points, selected = null) }
            } catch (failure: ApiException) {
                _state.update { it.copy(loading = false, error = failure.userMessage() ?: "Sign in to continue.") }
            }
        }
    }

    fun setDepth(depth: Int) {
        _state.update { it.copy(depth = depth) }
        load()
    }

    fun select(itemId: Int?) = _state.update { it.copy(selected = itemId) }

    fun centreOn(itemId: Int) {
        _state.update { it.copy(centre = itemId) }
        load()
    }
}

private const val RADIUS = 26f

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GraphScreen(viewModel: GraphViewModel, onBack: () -> Unit, onOpenItem: (Int) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Relationship graph") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
            )
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            SingleChoiceSegmentedButtonRow(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                listOf(1, 2, 3).forEachIndexed { index, depth ->
                    SegmentedButton(
                        selected = state.depth == depth,
                        onClick = { viewModel.setDepth(depth) },
                        shape = SegmentedButtonDefaults.itemShape(index, 3),
                    ) { Text(if (depth == 1) "1 link" else "$depth links") }
                }
            }
            val graph = state.graph
            when {
                state.loading && graph == null -> LoadingBox()
                state.error != null -> ErrorBox(checkNotNull(state.error), onRetry = viewModel::load)
                graph != null -> {
                    Text(
                        "${graph.nodes.size} item(s), ${graph.edges.size} link(s)" + if (graph.truncated) ", more not shown" else "",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 16.dp),
                    )
                    GraphCanvas(graph, state.points, state.selected, viewModel::select, Modifier.fillMaxWidth().weight(1f))
                    val selected = graph.nodes.firstOrNull { it.id == state.selected }
                    if (selected != null) {
                        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(selected.displayTitle, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                            if (selected.id != state.centre) TextButton(onClick = { viewModel.centreOn(selected.id) }) { Text("Centre here") }
                            TextButton(onClick = { onOpenItem(selected.id) }) { Text("Open") }
                        }
                    }
                    HorizontalDivider()
                    // The same items as a list: what TalkBack reads, and a way
                    // to pick one on a small screen.
                    LazyColumn(Modifier.fillMaxWidth().heightIn(max = 220.dp)) {
                        items(graph.nodes, key = { it.id }) { node -> NodeRow(node, node.id == state.selected) { viewModel.select(node.id) } }
                    }
                }
            }
        }
    }
}

@Composable
private fun NodeRow(node: GraphNode, selected: Boolean, onSelect: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .clickable(onClick = onSelect)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(
            node.displayTitle,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
        Text(
            when (node.depth) {
                0 -> "centre"
                1 -> "1 link away"
                else -> "${node.depth} links away"
            },
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun GraphCanvas(
    graph: ItemGraph,
    points: List<GraphLayout.Point>,
    selected: Int?,
    onSelect: (Int?) -> Unit,
    modifier: Modifier,
) {
    var scale by remember(graph) { mutableFloatStateOf(1f) }
    var offset by remember(graph) { mutableStateOf(Offset.Zero) }
    var size by remember { mutableStateOf(IntSize.Zero) }
    val transform = rememberTransformableState { centroid, zoom, pan, _ ->
        // Zoom around the fingers: the point under them stays under them.
        val origin = Offset(size.width / 2f, size.height / 2f) + offset
        val newScale = (scale * zoom).coerceIn(0.3f, 4f)
        val world = (centroid - origin) / scale
        offset = centroid - world * newScale - Offset(size.width / 2f, size.height / 2f) + pan
        scale = newScale
    }
    val measurer = rememberTextMeasurer()
    val colors = MaterialTheme.colorScheme
    val captionStyle = TextStyle(color = colors.onSurface, fontSize = 12.sp)
    val labelStyle = TextStyle(color = colors.onSurfaceVariant, fontSize = 10.sp)
    val index = remember(graph) { graph.nodes.withIndex().associate { (position, node) -> node.id to position } }
    Box(
        modifier
            .onSizeChanged { size = it }
            .clearAndSetSemantics { contentDescription = "Graph of ${graph.nodes.size} items; the list below names them" }
            .transformable(transform)
            .pointerInput(graph, points) {
                detectTapGestures { tap ->
                    val centre = Offset(size.width / 2f, size.height / 2f) + offset
                    val hit = points.indexOfFirst { point ->
                        val x = centre.x + point.x.toFloat() * scale
                        val y = centre.y + point.y.toFloat() * scale
                        hypot(tap.x - x, tap.y - y) <= RADIUS * scale * 1.3f
                    }
                    onSelect(graph.nodes.getOrNull(hit)?.id)
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val origin = Offset(size.width / 2f, size.height / 2f) + offset
            withTransform({
                translate(origin.x, origin.y)
                scale(scale, scale, pivot = Offset.Zero)
            }) {
                graph.edges.forEach { link ->
                    val from = points[index[link.fromItemId] ?: return@forEach]
                    val to = points[index[link.toItemId] ?: return@forEach]
                    val dx = (to.x - from.x).toFloat()
                    val dy = (to.y - from.y).toFloat()
                    val length = hypot(dx, dy).coerceAtLeast(1f)
                    val start = Offset(from.x.toFloat() + dx / length * RADIUS, from.y.toFloat() + dy / length * RADIUS)
                    val end = Offset(to.x.toFloat() - dx / length * RADIUS, to.y.toFloat() - dy / length * RADIUS)
                    drawLine(colors.outline, start, end, strokeWidth = 1.5f)
                    val back = Offset(-dx / length * 10, -dy / length * 10)
                    val side = Offset(-dy / length * 5, dx / length * 5)
                    drawPath(
                        Path().apply {
                            moveTo(end.x, end.y)
                            lineTo(end.x + back.x + side.x, end.y + back.y + side.y)
                            lineTo(end.x + back.x - side.x, end.y + back.y - side.y)
                            close()
                        },
                        colors.outline,
                    )
                    val label = measurer.measure(linkDescription(link.linkType, link.customValue), labelStyle)
                    drawText(label, topLeft = (start + end) / 2f - Offset(label.size.width / 2f, label.size.height / 2f))
                }
                graph.nodes.forEachIndexed { position, node ->
                    val point = Offset(points[position].x.toFloat(), points[position].y.toFloat())
                    val centre = node.depth == 0
                    drawCircle(if (centre) colors.primary else colors.surfaceVariant, RADIUS, point)
                    drawCircle(
                        if (node.id == selected) colors.tertiary else if (centre) colors.primary else colors.outline,
                        RADIUS,
                        point,
                        style = Stroke(width = if (node.id == selected || centre) 4f else 1.5f),
                    )
                    val title = if (node.title.length > 24) node.title.take(23) + "…" else node.title
                    val caption = measurer.measure(title, captionStyle)
                    drawText(caption, topLeft = point + Offset(-caption.size.width / 2f, RADIUS + 4f))
                }
            }
        }
    }
}
