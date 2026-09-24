package com.robertvokac.lexicon.ui.graph

import androidx.activity.compose.BackHandler
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
import androidx.compose.material.icons.filled.FitScreen
import androidx.compose.material.icons.filled.Fullscreen
import androidx.compose.material.icons.filled.FullscreenExit
import androidx.compose.material.icons.filled.Quiz
import androidx.compose.material.icons.filled.ZoomIn
import androidx.compose.material.icons.filled.ZoomOut
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.SmallFloatingActionButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
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

/** The node circle and caption room, in dp, at zoom 1. */
private const val RADIUS_DP = 16f
private const val ZOOM_STEP = 1.3f
private const val MIN_ZOOM = 0.4f
private const val MAX_ZOOM = 6f

/** The view onto the graph: the person's zoom and pan on top of the fit to the canvas. */
private class GraphView {
    var zoom by mutableFloatStateOf(1f)
    var pan by mutableStateOf(Offset.Zero)

    /** Zooms about [focus], a point relative to the canvas centre, which stays where it is. */
    fun zoomBy(factor: Float, focus: Offset = Offset.Zero) {
        val next = (zoom * factor).coerceIn(MIN_ZOOM, MAX_ZOOM)
        pan = focus - (focus - pan) * (next / zoom)
        zoom = next
    }

    fun reset() {
        zoom = 1f
        pan = Offset.Zero
    }
}

/** [onQuizCards] starts a card quiz over the items the graph shows: its centre, as deep as it goes. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GraphScreen(
    viewModel: GraphViewModel,
    onBack: () -> Unit,
    onOpenItem: (Int) -> Unit,
    onQuizCards: ((itemId: Int, depth: Int) -> Unit)? = null,
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    // Full screen leaves the graph alone on the screen: no depth choice, no list.
    var fullScreen by rememberSaveable { mutableStateOf(false) }
    val view = remember(state.graph) { GraphView() }
    BackHandler(enabled = fullScreen) { fullScreen = false }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Relationship graph") },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") } },
                actions = {
                    if (onQuizCards != null) {
                        IconButton(onClick = { onQuizCards(state.centre, state.depth) }) {
                            Icon(Icons.Filled.Quiz, contentDescription = "Quiz cards")
                        }
                    }
                    IconButton(onClick = { fullScreen = !fullScreen }) {
                        Icon(
                            if (fullScreen) Icons.Filled.FullscreenExit else Icons.Filled.Fullscreen,
                            contentDescription = if (fullScreen) "Exit full screen" else "Full screen",
                        )
                    }
                },
            )
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            if (!fullScreen) {
                SingleChoiceSegmentedButtonRow(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                    listOf(1, 2, 3).forEachIndexed { index, depth ->
                        SegmentedButton(
                            selected = state.depth == depth,
                            onClick = { viewModel.setDepth(depth) },
                            shape = SegmentedButtonDefaults.itemShape(index, 3),
                        ) { Text(if (depth == 1) "1 link" else "$depth links") }
                    }
                }
            }
            val graph = state.graph
            when {
                state.loading && graph == null -> LoadingBox()
                state.error != null -> ErrorBox(checkNotNull(state.error), onRetry = viewModel::load)
                graph != null -> {
                    if (!fullScreen) {
                        Text(
                            "${graph.nodes.size} item(s), ${graph.edges.size} link(s)" + if (graph.truncated) ", more not shown" else "",
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(horizontal = 16.dp),
                        )
                    }
                    Box(Modifier.fillMaxWidth().weight(1f)) {
                        GraphCanvas(graph, state.points, state.selected, view, viewModel::select, Modifier.fillMaxSize())
                        ZoomButtons(view, Modifier.align(Alignment.BottomEnd).padding(12.dp))
                    }
                    val selected = graph.nodes.firstOrNull { it.id == state.selected }
                    if (selected != null) {
                        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(selected.displayTitle, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                            if (selected.id != state.centre) TextButton(onClick = { viewModel.centreOn(selected.id) }) { Text("Centre here") }
                            TextButton(onClick = { onOpenItem(selected.id) }) { Text("Open") }
                        }
                    }
                    if (!fullScreen) {
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
}

/** Zoom in, zoom out and fit, beside pinching. */
@Composable
private fun ZoomButtons(view: GraphView, modifier: Modifier) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SmallFloatingActionButton(onClick = { view.zoomBy(ZOOM_STEP) }) { Icon(Icons.Filled.ZoomIn, contentDescription = "Zoom in") }
        SmallFloatingActionButton(onClick = { view.zoomBy(1 / ZOOM_STEP) }) { Icon(Icons.Filled.ZoomOut, contentDescription = "Zoom out") }
        SmallFloatingActionButton(onClick = view::reset) { Icon(Icons.Filled.FitScreen, contentDescription = "Fit the graph") }
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
    view: GraphView,
    onSelect: (Int?) -> Unit,
    modifier: Modifier,
) {
    var size by remember { mutableStateOf(IntSize.Zero) }
    val density = LocalDensity.current.density
    val baseRadius = RADIUS_DP * density
    // The whole graph fills the canvas at zoom 1, whatever the screen's
    // density; the person zooms and pans from there.
    val fit = remember(points, size) {
        GraphLayout.fit(points, size.width, size.height, margin = 3.0 * baseRadius, maxScale = 1.5 * density)
    }
    val scale = (fit.scale * view.zoom).toFloat()
    val radius = baseRadius * view.zoom.coerceIn(0.7f, 1.6f)
    fun screen(point: GraphLayout.Point) = Offset(
        size.width / 2f + view.pan.x + ((point.x - fit.centreX) * scale).toFloat(),
        size.height / 2f + view.pan.y + ((point.y - fit.centreY) * scale).toFloat(),
    )
    val transform = rememberTransformableState { centroid, zoom, pan, _ ->
        view.zoomBy(zoom, centroid - Offset(size.width / 2f, size.height / 2f))
        view.pan += pan
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
            .pointerInput(graph, points, fit, view.zoom, view.pan) {
                detectTapGestures { tap ->
                    val hit = points.indexOfFirst { point -> (screen(point) - tap).getDistance() <= radius * 1.3f }
                    onSelect(graph.nodes.getOrNull(hit)?.id)
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val arrow = 8f * density
            graph.edges.forEach { link ->
                val from = screen(points[index[link.fromItemId] ?: return@forEach])
                val to = screen(points[index[link.toItemId] ?: return@forEach])
                val delta = to - from
                val length = delta.getDistance().coerceAtLeast(1f)
                val unit = delta / length
                val start = from + unit * radius
                val end = to - unit * radius
                drawLine(colors.outline, start, end, strokeWidth = 1.2f * density)
                val back = unit * -arrow
                val side = Offset(-unit.y, unit.x) * (arrow / 2)
                drawPath(
                    Path().apply {
                        moveTo(end.x, end.y)
                        lineTo(end.x + back.x + side.x, end.y + back.y + side.y)
                        lineTo(end.x + back.x - side.x, end.y + back.y - side.y)
                        close()
                    },
                    colors.outline,
                )
                // A label only where the link is long enough to carry it.
                val label = measurer.measure(linkDescription(link.linkType, link.customValue), labelStyle)
                if (length - 2 * radius > label.size.width + 8 * density) {
                    drawText(label, topLeft = (start + end) / 2f - Offset(label.size.width / 2f, label.size.height / 2f))
                }
            }
            graph.nodes.forEachIndexed { position, node ->
                val point = screen(points[position])
                val centre = node.depth == 0
                drawCircle(if (centre) colors.primary else colors.surfaceVariant, radius, point)
                drawCircle(
                    if (node.id == selected) colors.tertiary else if (centre) colors.primary else colors.outline,
                    radius,
                    point,
                    style = Stroke(width = if (node.id == selected || centre) 3f * density else 1.2f * density),
                )
                val title = if (node.title.length > 24) node.title.take(23) + "…" else node.title
                val caption = measurer.measure(title, captionStyle)
                drawText(caption, topLeft = point + Offset(-caption.size.width / 2f, radius + 4f * density))
            }
        }
    }
}
