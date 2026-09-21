package com.robertvokac.lexicon.ui.items

import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf
import java.time.Duration

/** Server-side paging: pages append without duplicates and old answers never win. */
@RunWith(AndroidJUnit4::class)
class ItemsViewModelTest {
    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var container: AppContainer

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        for (number in 1..120) fake.addItem(Item(title = "Item %03d".format(number)))
        environment = TestEnvironment()
        container = environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)
        signInDirectly(container, fake)
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    /** Runs the main looper, where ViewModel coroutines resume, until [condition] holds. */
    private fun awaitState(viewModel: ItemsViewModel, timeoutMillis: Long = 10_000, condition: (ItemsUiState) -> Boolean): ItemsUiState {
        val deadline = System.currentTimeMillis() + timeoutMillis
        while (System.currentTimeMillis() < deadline) {
            shadowOf(Looper.getMainLooper()).idle()
            val state = viewModel.state.value
            if (condition(state)) return state
            Thread.sleep(10)
        }
        throw AssertionError("Timed out; last state: ${viewModel.state.value.copy(items = emptyList())}")
    }

    private fun queries() = fake.requestsTo("POST", "/api/v1/items/query")

    @Test
    fun pagesLoadOnDemandWithoutDuplicates() {
        val viewModel = ItemsViewModel(container)
        var state = awaitState(viewModel) { !it.loading && it.items.isNotEmpty() }
        assertEquals(50, state.items.size)
        assertEquals(120, state.totalCount)
        assertTrue(queries().last().body!!.utf8().contains("\"limit\":50"))

        // Someone adds an item that sorts first: the next page's offset now
        // starts one row earlier, and that row must not show twice.
        fake.addItem(Item(title = "Item 000"))
        viewModel.loadMore()
        state = awaitState(viewModel) { !it.loadingMore && it.items.size > 50 }
        assertEquals(99, state.items.size)
        assertTrue(queries().last().body!!.utf8().contains("\"offset\":50"))

        viewModel.loadMore()
        state = awaitState(viewModel) { !it.loadingMore && it.endReached }
        viewModel.loadMore()
        state = awaitState(viewModel) { !it.loadingMore && it.endReached }
        assertEquals(state.items.map { it.id }.distinct(), state.items.map { it.id })
        assertEquals(121, state.totalCount)
        // Nothing asks for more once the end is reached.
        val count = queries().size
        viewModel.loadMore()
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals(count, queries().size)
    }

    @Test
    fun aNewerSearchWinsOverASlowerOlderOne() {
        val viewModel = ItemsViewModel(container)
        awaitState(viewModel) { !it.loading && it.items.isNotEmpty() }
        fake.slowQuery = "Item 1"
        viewModel.setSearch("Item 1")
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(ItemsViewModel.TYPING_DEBOUNCE_MS + 50))
        // The slow request is on its way when the person types on.
        Thread.sleep(200)
        viewModel.setSearch("Item 00")
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(ItemsViewModel.TYPING_DEBOUNCE_MS + 50))
        val state = awaitState(viewModel) { !it.loading && it.search == "Item 00" && it.items.isNotEmpty() }
        assertEquals((1..9).map { "Item 00$it" }, state.items.map { it.title })
        // Long after the slow answer arrived, the newer result still stands.
        Thread.sleep(fake.slowQueryMillis)
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals((1..9).map { "Item 00$it" }, viewModel.state.value.items.map { it.title })
    }

    @Test
    fun typingIsDebounced() {
        val viewModel = ItemsViewModel(container)
        awaitState(viewModel) { !it.loading && it.items.isNotEmpty() }
        val before = queries().size
        "Item 12".forEach { _ ->
            viewModel.setSearch(viewModel.state.value.search + "x")
            shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(50))
        }
        viewModel.setSearch("Item 12")
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(ItemsViewModel.TYPING_DEBOUNCE_MS + 50))
        awaitState(viewModel) { !it.loading && it.items.size == 1 }
        // One request for the whole burst of typing.
        assertEquals(before + 1, queries().size)
    }
}
