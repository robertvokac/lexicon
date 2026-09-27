package com.robertvokac.lexicon.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StudyPlanOverviewTest {
    private val plan = StudyPlan(item = "Book", type = StudyPlanType.Book, unitType = StudyUnitType.Page,
        firstUnit = 101, lastUnit = 300, currentProgress = 187,
        startDate = "2026-09-28", endDate = "2026-10-04")

    private fun overview() = StudyPlanOverview(plan, "2026-09-30", "At risk",
        upcoming = false, active = true, ended = false, complete = false, studyDay = true,
        totalUnits = 200, completedUnits = 87, remainingUnits = 113,
        totalStudyDays = 5, elapsedStudyDays = 3, remainingStudyDays = 3,
        plannedUnitsPerStudyDay = 40.0, requiredUnitsPerRemainingStudyDay = 113.0 / 3,
        expectedCompletedUnits = 120, expectedProgress = 220,
        expectedUnitStart = 181, expectedUnitEnd = 220, expectedUnits = 120,
        deficitUnits = 33, todayFirst = 181, todayLast = 220,
        recommendedFirst = 188, recommendedLast = 225)

    @Test fun presentationKeepsAbsoluteProgressAndDailyRangeDistinct() {
        val value = overview()
        assertEquals(220, value.expectedProgress)
        assertEquals("pages 181–220", value.expectedRangeText())
        assertEquals("Behind by: 33 pages", value.differenceText())
        assertTrue(value.canMarkToday())
        assertEquals("37.67 pages/day", value.requiredPaceText())
    }

    @Test fun unavailableStatesAndCompletedActions() {
        val noDay = overview().copy(studyDay = false, expectedUnitStart = null, expectedUnitEnd = null,
            requiredUnitsPerRemainingStudyDay = null)
        assertEquals("No study scheduled", noDay.expectedRangeText())
        assertEquals("N/A — no study days remaining", noDay.requiredPaceText())
        assertFalse(noDay.canMarkToday())
        assertEquals("No units scheduled today", noDay.copy(studyDay = true).expectedRangeText())
        assertFalse(noDay.copy(studyDay = true).canMarkToday())
        val overdue = noDay.copy(active = false, ended = true, expectedProgress = 300)
        assertEquals("—", overdue.expectedRangeText())
        assertEquals("N/A — deadline passed", overdue.requiredPaceText())
        val completed = overview().copy(plan = plan.copy(currentProgress = 300), complete = true,
            requiredUnitsPerRemainingStudyDay = 0.0)
        assertFalse(completed.canMarkToday())
        assertEquals("0.00 pages/day", completed.requiredPaceText())
    }
}
