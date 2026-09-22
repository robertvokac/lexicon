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

    @Test
    fun theWholeGraphFitsTheCanvasButASmallOneIsNotBlownUp() {
        val points = listOf(GraphLayout.Point(-150.0, -50.0), GraphLayout.Point(150.0, 50.0), GraphLayout.Point(0.0, 0.0))
        val fit = GraphLayout.fit(points, width = 1000, height = 600, margin = 50.0, maxScale = 10.0)
        // 900 px for 300 units across, 500 px for 100 units down: the narrower wins.
        assertEquals(3.0, fit.scale, 1e-9)
        assertEquals(0.0, fit.centreX, 1e-9)
        assertEquals(0.0, fit.centreY, 1e-9)
        val offCentre = GraphLayout.fit(listOf(GraphLayout.Point(100.0, 20.0), GraphLayout.Point(300.0, 60.0)), 1000, 600, 50.0, 10.0)
        assertEquals(200.0, offCentre.centreX, 1e-9)
        assertEquals(40.0, offCentre.centreY, 1e-9)
        assertEquals(2.0, GraphLayout.fit(points, 1000, 600, 50.0, maxScale = 2.0).scale, 1e-9)
        assertEquals("a lone item gets the largest scale", 2.0,
            GraphLayout.fit(listOf(GraphLayout.Point(0.0, 0.0)), 1000, 600, 50.0, 2.0).scale, 1e-9)
    }
}
