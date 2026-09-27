package com.robertvokac.lexicon.ui.studyplan

import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.StudyPlan
import com.robertvokac.lexicon.model.StudyPlanType
import com.robertvokac.lexicon.model.StudyUnitType
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import java.time.LocalDate
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf

@RunWith(AndroidJUnit4::class)
class StudyPlansViewModelTest {
    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var container: AppContainer

    @Before fun setUp() {
        fake = FakeLexiconServer().start()
        environment = TestEnvironment()
        container = environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)
        signInDirectly(container, fake)
    }
    @After fun tearDown() { fake.close(); environment.close() }
    private fun awaitState(viewModel: StudyPlansViewModel, condition: (StudyPlansState) -> Boolean): StudyPlansState {
        val deadline = System.currentTimeMillis() + 10_000
        while (System.currentTimeMillis() < deadline) {
            shadowOf(Looper.getMainLooper()).idle()
            val state = viewModel.state.value
            if (condition(state)) return state
            Thread.sleep(10)
        }
        throw AssertionError("Timed out: ${viewModel.state.value}")
    }

    @Test fun localDateAndCrudRefreshTheServerOverview() {
        val viewModel = StudyPlansViewModel(container)
        assertTrue(awaitState(viewModel) { !it.loading }.plans.isEmpty())
        val firstRequest = fake.requestsTo("GET", "/api/v1/study-plans/overview").first()
        assertEquals(LocalDate.now().toString(), firstRequest.url.queryParameter("date"))
        val plan = StudyPlan(item = "Katas", group = "Practice", type = StudyPlanType.Practice, unitType = StudyUnitType.Other,
            customUnit = "kata", firstUnit = 101, lastUnit = 200,
            startDate = LocalDate.now().toString(), endDate = LocalDate.now().plusDays(7).toString())
        viewModel.save(plan)
        val created = awaitState(viewModel) { !it.busy && it.plans.size == 1 }.plans.single().plan
        assertEquals("Katas", created.item)
        assertEquals("Practice", created.group)
        viewModel.save(created.copy(currentProgress = 145))
        val updated = awaitState(viewModel) { !it.busy && it.plans.singleOrNull()?.plan?.currentProgress == 145 }
        assertEquals(45, updated.plans.single().completedUnits)
        viewModel.delete(created)
        assertTrue(awaitState(viewModel) { !it.busy && it.plans.isEmpty() }.plans.isEmpty())
        assertEquals(4, fake.requestsTo("GET", "/api/v1/study-plans/overview").size)
    }
}
