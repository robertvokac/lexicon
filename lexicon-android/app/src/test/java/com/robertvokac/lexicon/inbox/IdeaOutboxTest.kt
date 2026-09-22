package com.robertvokac.lexicon.inbox

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.signInDirectly
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class IdeaOutboxTest {
    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var container: AppContainer
    private val outbox get() = container.outbox
    private val server get() = fake.baseUrl
    private val user get() = fake.username

    @Before
    fun setUp() {
        fake = FakeLexiconServer().start()
        environment = TestEnvironment()
        container = environment.container(ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver)
        signInDirectly(container, fake)
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    private fun inDefault(title: String) = fake.items.values.filter { it.groupId == 1 && it.title == title }

    @Test
    fun ideasWaitWhileTheServerIsAwayAndGoOutInOrderWhenItIsBack() = runBlocking {
        fake.unavailable = true
        outbox.add("Lock-free queue", "Try a ring buffer.\nMeasure it first.", server, user)
        outbox.add("Arena allocator", "", server, user)
        assertEquals(0, outbox.flush(server, user))
        assertEquals(2, outbox.ideas.value.size)
        assertTrue("nothing is refused for a server that is away", outbox.ideas.value.all { it.problem == null })

        fake.unavailable = false
        assertEquals(2, outbox.flush(server, user))
        assertTrue(outbox.ideas.value.isEmpty())
        val queue = inDefault("Lock-free queue").single()
        assertEquals("Try a ring buffer.\nMeasure it first.", queue.content)
        assertNull(queue.itemTypeId)
        assertTrue(queue.id!! < inDefault("Arena allocator").single().id!!)
    }

    @Test
    fun ideasSurviveARestartOfTheApp() = runBlocking {
        outbox.add("Kept", "on the phone", server, user)
        val again = IdeaOutbox(File(environment.directory, "inbox-outbox.json")) { container.api }
        assertEquals(listOf("Kept"), again.ideas.value.map { it.title })
    }

    @Test
    fun anIdeaWhoseTitleIsTakenWaitsForAnEdit() = runBlocking {
        fake.addItem(Item(title = "Lock-free queue", groupId = 1, content = "Something else."))
        val idea = outbox.add("Lock-free queue", "Try a ring buffer.", server, user)
        assertEquals(0, outbox.flush(server, user))
        val refused = outbox.ideas.value.single()
        assertTrue(refused.problem!!.contains("already in Default"))
        assertEquals("a refused idea is not sent again by itself", 0, outbox.flush(server, user))

        outbox.update(idea.id, "Lock-free queue, bounded", "Try a ring buffer.")
        assertNull(outbox.ideas.value.single().problem)
        assertEquals(1, outbox.flush(server, user))
        assertEquals("Try a ring buffer.", inDefault("Lock-free queue, bounded").single().content)
    }

    @Test
    fun anIdeaSavedWhoseAnswerWasLostIsNotSentTwice() = runBlocking {
        // The server stored it, but the phone never heard back.
        fake.addItem(Item(title = "Arena allocator", groupId = 1, content = "Bump pointer."))
        outbox.add("Arena allocator", "Bump pointer.", server, user)
        assertEquals(1, outbox.flush(server, user))
        assertTrue(outbox.ideas.value.isEmpty())
        assertEquals(1, inDefault("Arena allocator").size)
    }

    @Test
    fun ideasGoOnlyToTheAccountTheyWereCaughtFor() = runBlocking {
        outbox.add("Someone else's", "", server, "another-user")
        outbox.add("Mine", "", server, user)
        assertEquals(1, outbox.flush(server, user))
        assertEquals(listOf("Someone else's"), outbox.ideas.value.map { it.title })
        assertEquals(listOf("Someone else's"), outbox.waitingFor(server, "another-user").map { it.title })
    }
}
