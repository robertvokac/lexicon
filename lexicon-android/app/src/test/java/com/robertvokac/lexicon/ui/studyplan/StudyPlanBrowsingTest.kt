package com.robertvokac.lexicon.ui.studyplan

import com.robertvokac.lexicon.model.StudyPlan
import com.robertvokac.lexicon.model.StudyPlanOverview
import com.robertvokac.lexicon.model.StudyPlanType
import com.robertvokac.lexicon.model.StudyUnitType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StudyPlanBrowsingTest {
    private fun book(id: Int) = StudyPlanOverview(
        plan = StudyPlan(id = id, item = "Book $id", group = if (id == 87) "C++ Library" else "",
            note = if (id == 88) "Read templates next" else "", type = StudyPlanType.Book,
            unitType = StudyUnitType.Page, startDate = "2026-10-01", endDate = "2026-10-31"),
        date = "2026-09-30", status = "Upcoming", upcoming = true, active = false,
        ended = false, complete = false, studyDay = false, totalUnits = 1,
        completedUnits = 0, remainingUnits = 1, totalStudyDays = 31, elapsedStudyDays = 0,
        remainingStudyDays = 31, plannedUnitsPerStudyDay = 1.0 / 31,
        requiredUnitsPerRemainingStudyDay = 1.0 / 31, expectedCompletedUnits = 0,
        expectedProgress = 0, expectedUnitStart = null, expectedUnitEnd = null,
        expectedUnits = 0, deficitUnits = 0, todayFirst = 0, todayLast = 0,
        recommendedFirst = 0, recommendedLast = 0,
    )

    @Test fun aHundredBooksHaveTenPagesWithoutMissingOrRepeatingBooks() {
        val books = (1..100).map(::book)
        val visited = (0..9).flatMap { page ->
            val result = studyPlanPage(books, page = page)
            assertEquals(10, result.values.size)
            assertEquals(10, result.pageCount)
            assertEquals(100, result.total)
            result.values.map { it.plan.id }
        }
        assertEquals((1..100).toList(), visited)
    }

    @Test fun searchCoversAllPagesAndDeletedLastPagesAreClamped() {
        val books = (1..100).map(::book)
        assertEquals(listOf(100), studyPlanPage(books, " Book 100 ", 9).values.map { it.plan.id })
        assertEquals(listOf(87), studyPlanPage(books, "c++ library").values.map { it.plan.id })
        assertEquals(listOf(88), studyPlanPage(books, "TEMPLATES").values.map { it.plan.id })
        val empty = studyPlanPage(books, "missing", 9)
        assertTrue(empty.values.isEmpty())
        assertEquals(0, empty.page)
        assertEquals(1, empty.pageCount)
        val afterDelete = studyPlanPage(books.take(90), page = 9)
        assertEquals(8, afterDelete.page)
        assertEquals(10, afterDelete.values.size)
        assertEquals(0, studyPlanPage(emptyList(), page = 9).page)
    }
}
