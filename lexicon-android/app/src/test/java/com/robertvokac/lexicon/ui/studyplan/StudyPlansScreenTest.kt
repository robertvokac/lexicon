package com.robertvokac.lexicon.ui.studyplan

import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.printToString
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.StudyPlan
import com.robertvokac.lexicon.model.StudyPlanType
import com.robertvokac.lexicon.model.StudyUnitType
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import com.robertvokac.lexicon.testing.waitForText
import java.time.LocalDate
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import org.junit.runner.RunWith
import org.robolectric.annotation.GraphicsMode

@RunWith(AndroidJUnit4::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class StudyPlansScreenTest {
    @get:Rule(order = 0) val compose = createComposeRule()
    @get:Rule(order = 1) val dumpOnFailure = object : TestWatcher() {
        override fun failed(e: Throwable?, description: Description?) {
            runCatching { println(compose.onAllNodes(isRoot()).printToString(maxDepth = Int.MAX_VALUE)) }
        }
    }
    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var restoration: StateRestorationTester

    @Before fun setUp() {
        fake = FakeLexiconServer().start()
        environment = TestEnvironment()
        val today = LocalDate.now()
        fake.studyPlans += (0..202).map { index ->
            StudyPlan(id = index + 1, item = "Book %03d".format(index),
                group = if (index == 87) "C++ Library" else "",
                note = if (index == 88) "Read templates next" else "",
                type = StudyPlanType.Book, unitType = StudyUnitType.Page, lastUnit = 300,
                currentProgress = if (index >= 103) 300 else 0,
                startDate = today.plusDays(if (index in 3..102) 10 else 0).toString(),
                endDate = today.plusDays(30).toString())
        }
        val container = environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)
        signInDirectly(container, fake)
        val viewModel = StudyPlansViewModel(container)
        restoration = StateRestorationTester(compose)
        restoration.setContent { StudyPlansScreen(viewModel, onBack = {}) }
    }

    @After fun tearDown() { fake.close(); environment.close() }

    private fun scrollTo(text: String) {
        compose.onNode(androidx.compose.ui.test.hasScrollToNodeAction()).performScrollToNode(hasText(text))
    }
    private fun button(text: String) = compose.onNode(hasText(text) and hasClickAction())
    private fun search(title: String) = compose.onNode(hasText("Search $title plans") and hasSetTextAction())

    @Test fun onlyActiveBooksStartVisibleAndEachArchiveHasIndependentPagesAndSearch() {
        compose.waitForText("Active (3)")
        scrollTo("Show Upcoming (100)")
        compose.onNodeWithText("Book 003").assertDoesNotExist()
        button("Show Upcoming (100)").performClick()
        compose.waitForText("1–10 of 100 · Page 1 of 10")
        button("Next").performClick()
        compose.waitForText("11–20 of 100 · Page 2 of 10")
        search("upcoming").performTextReplacement("c++ library")
        compose.waitForText("1–1 of 1 · Page 1 of 1")
        scrollTo("Book 087")
        scrollTo("Search upcoming plans")
        search("upcoming").performTextReplacement("TEMPLATES")
        compose.waitForText("Book 088")
        restoration.emulateSavedInstanceStateRestore()
        scrollTo("Search upcoming plans")
        compose.waitForText("TEMPLATES")
        scrollTo("Show Finished (100)")
        button("Show Finished (100)").performClick()
        scrollTo("Search finished plans")
        search("finished").performTextReplacement("Book 20")
        scrollTo("1–3 of 3 · Page 1 of 1")
        compose.waitForText("1–3 of 3 · Page 1 of 1")
        scrollTo("Search upcoming plans")
        compose.waitForText("TEMPLATES")
        search("upcoming").performTextReplacement("missing")
        scrollTo("0–0 of 0 · Page 1 of 1")
        compose.waitForText("0–0 of 0 · Page 1 of 1")
        scrollTo("Hide Upcoming (100)")
        button("Hide Upcoming (100)").performClick()
        compose.onNodeWithText("Search upcoming plans").assertDoesNotExist()
    }
}
