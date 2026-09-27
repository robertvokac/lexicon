package com.robertvokac.lexicon.model

import kotlinx.serialization.Serializable

@Serializable
enum class StudyPlanType { Book, Course, Lesson, Documentation, Article, Video, Practice, Other }

@Serializable
enum class StudyUnitType { Page, Lesson, Chapter, Section, Module, Video, Exercise, Minute, Other }

/** currentProgress is the last completed absolute unit, or zero for none. */
@Serializable
data class StudyPlan(
    val id: Int? = null,
    val item: String,
    val type: StudyPlanType,
    val unitType: StudyUnitType,
    val currentProgress: Int = 0,
    val startDate: String,
    val endDate: String,
    val note: String = "",
    val firstUnit: Int = 1,
    val lastUnit: Int = 1,
    val studyDaysMask: Int = 127,
    val customUnit: String = "",
) {
    fun unitLabel(count: Int): String {
        val singular = if (unitType == StudyUnitType.Other) customUnit.trim() else unitType.name.lowercase()
        return if (count == 1) singular else "${singular}s"
    }
}

@Serializable
data class StudyPlanOverview(
    val plan: StudyPlan,
    val date: String,
    val status: String,
    val upcoming: Boolean,
    val active: Boolean,
    val ended: Boolean,
    val complete: Boolean,
    val studyDay: Boolean,
    val totalUnits: Int,
    val completedUnits: Int,
    val remainingUnits: Int,
    val totalStudyDays: Int,
    val elapsedStudyDays: Int,
    val remainingStudyDays: Int,
    val plannedUnitsPerStudyDay: Double,
    val requiredUnitsPerRemainingStudyDay: Double,
    val expectedUnits: Int,
    val deficitUnits: Int,
    val todayFirst: Int,
    val todayLast: Int,
    val recommendedFirst: Int,
    val recommendedLast: Int,
) {
    fun range(first: Int, last: Int): String = if (first == 0 || last == 0) "No units scheduled today"
        else "${plan.unitLabel(last - first + 1)} $first${if (first == last) "" else "–$last"}"
}

@Serializable internal data class StudyPlansEnvelope(val studyPlans: List<StudyPlan>)
@Serializable internal data class StudyPlanEnvelope(val studyPlan: StudyPlan)
@Serializable internal data class StudyPlanOverviewsEnvelope(val date: String, val plans: List<StudyPlanOverview>)
