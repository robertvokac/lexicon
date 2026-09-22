package com.robertvokac.lexicon.ui.graph

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.hypot

class GraphLayoutTest {
    @Test
    fun itemsStayApartTheCentreStaysAndTheLayoutRepeats() {
        val depths = listOf(0, 1, 1, 1, 2, 2, 2, 2, 2)
        val edges = listOf(0 to 1, 0 to 2, 0 to 3, 1 to 4, 1 to 5, 2 to 6, 3 to 7, 3 to 8)
        val points = GraphLayout.layout(depths, edges)
        assertEquals(GraphLayout.Point(0.0, 0.0), points[0])
        var closest = Double.MAX_VALUE
        for (i in points.indices) for (j in i + 1 until points.size) {
            closest = minOf(closest, hypot(points[i].x - points[j].x, points[i].y - points[j].y))
        }
        assertTrue("closest $closest", closest > 60)
        assertEquals(points, GraphLayout.layout(depths, edges))
    }
}
