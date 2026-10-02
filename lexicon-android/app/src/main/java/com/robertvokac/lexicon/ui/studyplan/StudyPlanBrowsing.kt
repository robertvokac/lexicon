package com.robertvokac.lexicon.ui.studyplan

import com.robertvokac.lexicon.model.StudyPlanOverview

internal const val STUDY_PAGE_SIZE = 10

internal data class StudyPlanPage(
    val values: List<StudyPlanOverview>,
    val page: Int,
    val pageCount: Int,
    val total: Int,
)

internal fun studyPlanPage(values: List<StudyPlanOverview>, query: String = "", page: Int = 0): StudyPlanPage {
    val needle = query.trim()
    val matches = values.filter { value ->
        listOf(value.plan.item, value.plan.group, value.plan.note).any { it.contains(needle, ignoreCase = true) }
    }
    val pageCount = maxOf(1, (matches.size + STUDY_PAGE_SIZE - 1) / STUDY_PAGE_SIZE)
    val currentPage = page.coerceIn(0, pageCount - 1)
    return StudyPlanPage(matches.drop(currentPage * STUDY_PAGE_SIZE).take(STUDY_PAGE_SIZE),
        currentPage, pageCount, matches.size)
}
