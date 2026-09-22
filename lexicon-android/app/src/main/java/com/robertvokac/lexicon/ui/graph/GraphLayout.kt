package com.robertvokac.lexicon.ui.graph

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * Places the nodes of a relationship graph: rings by distance from the centre
 * to start with, then a Fruchterman-Reingold pass that pushes every pair apart
 * and pulls linked items together. Deterministic, and the same steps as the
 * desktop (lexicon-qt/GraphLayout.h) and web (js/graphlayout.js) clients.
 */
object GraphLayout {
    data class Point(val x: Double, val y: Double)

    private const val RING = 150.0
    private const val IDEAL = 120.0
    private const val ITERATIONS = 250

    /** How the graph sits on a canvas: pixels per layout unit, and the layout point at the canvas centre. */
    data class Fit(val scale: Double, val centreX: Double, val centreY: Double)

    /**
     * Fits every point into [width] x [height] pixels with [margin] pixels to
     * spare on each side, for the node circles and captions, but never larger
     * than [maxScale], so two items do not fill a tablet.
     */
    fun fit(points: List<Point>, width: Int, height: Int, margin: Double, maxScale: Double): Fit {
        if (points.isEmpty() || width <= 0 || height <= 0) return Fit(maxScale, 0.0, 0.0)
        val minX = points.minOf { it.x }
        val maxX = points.maxOf { it.x }
        val minY = points.minOf { it.y }
        val maxY = points.maxOf { it.y }
        val roomX = max(1.0, width - 2 * margin)
        val roomY = max(1.0, height - 2 * margin)
        val scaleX = if (maxX > minX) roomX / (maxX - minX) else maxScale
        val scaleY = if (maxY > minY) roomY / (maxY - minY) else maxScale
        return Fit(min(min(scaleX, scaleY), maxScale), (minX + maxX) / 2, (minY + maxY) / 2)
    }

    /** [depths] of node i from the centre, node 0; [edges] as index pairs. */
    fun layout(depths: List<Int>, edges: List<Pair<Int, Int>>): List<Point> {
        val count = depths.size
        val xs = DoubleArray(count)
        val ys = DoubleArray(count)
        val perDepth = IntArray(4)
        val seen = IntArray(4)
        depths.forEach { perDepth[it.coerceIn(0, 3)]++ }
        for (i in 1 until count) {
            val depth = depths[i].coerceIn(1, 3)
            val angle = 2 * PI * seen[depth]++ / max(1, perDepth[depth]) + depth * 0.7
            xs[i] = RING * depth * cos(angle)
            ys[i] = RING * depth * sin(angle)
        }
        val shiftX = DoubleArray(count)
        val shiftY = DoubleArray(count)
        repeat(ITERATIONS) { step ->
            val temperature = 80.0 * (1.0 - step.toDouble() / ITERATIONS) + 2.0
            shiftX.fill(0.0)
            shiftY.fill(0.0)
            for (i in 0 until count) {
                for (j in i + 1 until count) {
                    val dx = xs[i] - xs[j]
                    val dy = ys[i] - ys[j]
                    val distance = max(hypot(dx, dy), 0.01)
                    val force = IDEAL * IDEAL / distance
                    shiftX[i] += dx / distance * force; shiftY[i] += dy / distance * force
                    shiftX[j] -= dx / distance * force; shiftY[j] -= dy / distance * force
                }
            }
            for ((a, b) in edges) {
                if (a == b) continue
                val dx = xs[a] - xs[b]
                val dy = ys[a] - ys[b]
                val distance = max(hypot(dx, dy), 0.01)
                val force = distance * distance / IDEAL
                shiftX[a] -= dx / distance * force; shiftY[a] -= dy / distance * force
                shiftX[b] += dx / distance * force; shiftY[b] += dy / distance * force
            }
            for (i in 1 until count) { // The centre stays where it is.
                val length = hypot(shiftX[i], shiftY[i])
                if (length < 1e-9) continue
                val move = min(length, temperature)
                xs[i] += shiftX[i] / length * move
                ys[i] += shiftY[i] / length * move
            }
        }
        return List(count) { Point(xs[it], ys[it]) }
    }
}
