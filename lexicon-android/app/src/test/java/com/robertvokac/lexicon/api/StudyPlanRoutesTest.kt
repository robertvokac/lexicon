package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.model.StudyPlan
import com.robertvokac.lexicon.model.StudyPlanType
import com.robertvokac.lexicon.model.StudyUnitType
import kotlinx.coroutines.runBlocking
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StudyPlanRoutesTest {
    private val plan = StudyPlan(id = 7, item = "Katas", type = StudyPlanType.Practice, unitType = StudyUnitType.Other,
        customUnit = "kata", firstUnit = 101, lastUnit = 200, currentProgress = 145,
        startDate = "2026-09-27", endDate = "2026-10-27", studyDaysMask = 31)

    @Test fun modelsAndRoutesUseNamesAndAnExplicitLocalDate() = runBlocking {
        MockWebServer().use { server ->
            server.start()
            val url = (ServerUrl.parse(server.url("/").toString(), allowCleartextDevelopmentHosts = true) as ServerUrl.Parsed.Valid).url
            val sessions = object : SessionAccess {
                override val current: Session = Session(url, "robert", "token")
                override fun onUnauthorized(session: Session) = Unit
            }
            val api = LexiconApi(ApiClient(AppContainer.httpClient(), sessions))
            val json = LexiconJson.encodeToString(StudyPlan.serializer(), plan)
            assertTrue(json.contains("\"type\":\"Practice\""))
            assertTrue(json.contains("\"unitType\":\"Other\""))
            assertTrue(json.contains("\"customUnit\":\"kata\""))
            fun response(body: String, code: Int = 200) = MockResponse.Builder().code(code)
                .setHeader("Content-Type", "application/json; charset=utf-8").body(body).build()
            server.enqueue(response("""{"studyPlans":[$json]}"""))
            assertEquals(1, api.studyPlans().size)
            assertEquals("GET", server.takeRequest().method)
            val overview = """{"date":"2026-09-27","plans":[{"plan":$json,"date":"2026-09-27","status":"Behind","upcoming":false,"active":true,"ended":false,"complete":false,"studyDay":true,"totalUnits":100,"completedUnits":45,"remainingUnits":55,"totalStudyDays":23,"elapsedStudyDays":1,"remainingStudyDays":23,"plannedUnitsPerStudyDay":4.347826,"requiredUnitsPerRemainingStudyDay":2.3913,"expectedUnits":4,"deficitUnits":-41,"todayFirst":101,"todayLast":104,"recommendedFirst":0,"recommendedLast":0}]}"""
            server.enqueue(response(overview))
            val values = api.studyPlanOverview("2026-09-27")
            assertEquals("katas 101–104", values.single().range(101, 104))
            val overviewRequest = server.takeRequest()
            assertEquals("/api/v1/study-plans/overview", overviewRequest.url.encodedPath)
            assertEquals("2026-09-27", overviewRequest.url.queryParameter("date"))
            server.enqueue(response("""{"studyPlan":$json}""", 201))
            assertEquals(7, api.createStudyPlan(plan.copy(id = null)).id)
            val post = server.takeRequest()
            assertEquals("POST", post.method)
            assertEquals("/api/v1/study-plans", post.url.encodedPath)
            assertTrue(post.body!!.utf8().contains("\"currentProgress\":145"))
            server.enqueue(response("""{"studyPlan":$json}"""))
            api.updateStudyPlan(7, plan)
            assertEquals("PUT", server.takeRequest().method)
            server.enqueue(MockResponse.Builder().code(204).build())
            api.deleteStudyPlan(7)
            assertEquals("DELETE", server.takeRequest().method)
        }
    }
}
