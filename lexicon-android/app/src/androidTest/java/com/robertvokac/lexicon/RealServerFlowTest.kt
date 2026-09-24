package com.robertvokac.lexicon

import android.net.Uri
import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.isSelected
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.core.app.ActivityOptionsCompat
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.model.CardWrite
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.FieldWrite
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemWrite
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.OutgoingLinkWrite
import com.robertvokac.lexicon.model.SaveItemRequest
import com.robertvokac.lexicon.model.TypeWrite
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * The app on a device against a real LexiconServer: login, Quick Add, open,
 * edit and save, search, a Blob round trip through document URIs, delete and
 * logout; and cards with their quiz. Runs only when
 * scripts/run-device-tests.sh supplies a server.
 */
@RunWith(AndroidJUnit4::class)
class RealServerFlowTest {
    @get:Rule(order = 0)
    val compose = createAndroidComposeRule<ComponentActivity>()

    @get:Rule(order = 1)
    val dump = DumpOnFailure { compose }

    private val container get() = (compose.activity.application as LexiconApplication).container
    private val title = "Android device test ${System.currentTimeMillis()}"
    private var nextUri: Uri? = null
    private val registryOwner = object : ActivityResultRegistryOwner {
        override val activityResultRegistry = object : ActivityResultRegistry() {
            override fun <I, O> onLaunch(requestCode: Int, contract: ActivityResultContract<I, O>, input: I, options: ActivityOptionsCompat?) {
                dispatchResult(requestCode, android.app.Activity.RESULT_OK, android.content.Intent().setData(nextUri))
            }
        }
    }

    @Before
    fun setUp() {
        DeviceServer.require()
        container.sessions.logout().let { runBlocking { it.join() } }
        val requests = LaunchRequests()
        compose.setContent {
            CompositionLocalProvider(LocalActivityResultRegistryOwner provides registryOwner) {
                LexiconRoot(container, requests, onShareFinished = {}, onExit = {})
            }
        }
    }

    @After
    fun tearDown() {
        runBlocking {
            if (container.sessions.current == null) {
                container.sessions.login(DeviceServer.url, DeviceServer.user, DeviceServer.password)
            }
            val leftovers = container.api.queryItems(ItemQuery(searchText = "Android device test", limit = 100))
            leftovers.items.forEach { item -> item.id?.let { container.api.deleteItem(it) } }
            container.sessions.logout().join()
        }
    }

    private fun field(label: String) = compose.onNode(hasSetTextAction() and hasText(label))

    private fun button(text: String) = compose.onNode(hasText(text) and hasClickAction())

    private fun logIn() {
        compose.waitForText("Log in")
        field("Server URL").performTextReplacement(DeviceServer.url)
        field("User name").performTextReplacement(DeviceServer.user)
        field("Password").performTextInput(DeviceServer.password)
        button("Log in").performClick()
        compose.waitFor(hasContentDescription("Quick add item"))
    }

    /** Show answer, then Yes or No. */
    private fun answer(yes: Boolean) {
        compose.waitFor(hasText("Show answer") and hasClickAction())
        button("Show answer").performScrollTo().performClick()
        val label = if (yes) "Yes" else "No"
        compose.waitFor(hasText(label) and hasClickAction())
        button(label).performScrollTo().performClick()
    }

    private fun addCard(question: String, answer: String) {
        compose.onNodeWithContentDescription("Add card").performClick()
        compose.waitForText("Add card")
        field("Question").performTextInput(question)
        field("Answer").performTextInput(answer)
        compose.onNode(hasText("Save") and hasClickAction() and hasAnyAncestor(isDialog())).performClick()
        compose.waitUntilGone(hasText("Add card"))
    }

    @Test
    fun loginAddEditSearchBlobDeleteLogout() {
        compose.waitForText("Log in")
        field("Server URL").performTextReplacement(DeviceServer.url)
        field("User name").performTextReplacement(DeviceServer.user)
        field("Password").performTextInput(DeviceServer.password)
        compose.onNode(hasText("Log in") and hasClickAction()).performClick()
        compose.waitFor(hasContentDescription("Quick add item"))

        // Quick Add into Default.
        compose.onNodeWithContentDescription("Quick add item").performClick()
        compose.waitForText("Quick add")
        field("Title").performTextReplacement(title)
        compose.onNode(hasText("Add") and hasClickAction() and hasAnyAncestor(isDialog())).performClick()
        compose.waitUntilGone(hasText("Quick add"))
        val created = runBlocking { container.api.queryItems(ItemQuery(searchText = title)).items.single() }
        assertEquals("Default", created.groupName)
        val createdId = checkNotNull(created.id)

        // A Blob type, so the item can carry a file.
        val (typeId, fieldId) = runBlocking {
            val type = checkNotNull(container.api.createType(TypeWrite("Android test type ${System.currentTimeMillis()}")).id)
            type to checkNotNull(container.api.createField(type, FieldWrite("Attachment", FieldDataType.Blob)).id)
        }

        // Search on the server, open, edit.
        compose.onNode(hasSetTextAction() and hasContentDescription("Search items")).performTextReplacement(title)
        compose.onNode(hasSetTextAction() and hasContentDescription("Search items")).performImeAction()
        // The row, not the search field that holds the same text.
        compose.waitFor(hasText(title) and hasClickAction() and !hasSetTextAction())
        compose.onNode(hasText(title) and hasClickAction() and !hasSetTextAction()).performClick()
        compose.waitForText("This item has no content yet.")
        compose.onNodeWithContentDescription("Edit item").performClick()
        compose.waitFor(hasSetTextAction() and hasText("Title"))
        compose.onNode(hasText("Type") and hasText("None")).performClick()
        compose.waitFor(hasText("Android test type", substring = true) and hasClickAction())
        compose.onNode(hasText("Android test type", substring = true) and hasClickAction()).performClick()
        compose.onNode(hasText("Values") and hasClickAction()).performClick()

        val context = compose.activity
        val source = File(context.cacheDir, "upload.bin")
        val bytes = ByteArray(300_000) { (it * 13 % 256).toByte() }
        source.writeBytes(bytes)
        nextUri = Uri.fromFile(source)
        compose.onNode(hasText("Upload…") and hasClickAction()).performClick()
        // "upload.bin (293.0 KB)" once the upload has finished.
        compose.waitForText("upload.bin (", substring = true)
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        // Back on the item page once the save has gone through.
        compose.waitUntilGone(hasContentDescription("Close editor"))
        compose.waitForText("Save as…")

        val saved = runBlocking { container.api.item(createdId).item }
        assertEquals(typeId, saved.itemTypeId)
        val hash = saved.fieldValue(fieldId)
        assertEquals(64, hash.length)

        val target = File(context.cacheDir, "download.bin").also { it.delete() }
        nextUri = Uri.fromFile(target)
        compose.onNode(hasText("Save as…") and hasClickAction()).performClick()
        compose.waitUntil(20_000) { target.length() == bytes.size.toLong() }
        assertArrayEquals(bytes, target.readBytes())

        // Delete with the desktop's confirmation.
        compose.onNodeWithContentDescription("More actions").performClick()
        compose.onNode(hasText("Delete") and hasClickAction()).performClick()
        compose.waitForText("Delete item '$title'?")
        compose.onNode(hasText("Delete") and hasClickAction() and hasAnyAncestor(isDialog())).performClick()
        compose.waitUntilGone(hasText("This item has no content yet."))
        assertTrue(runBlocking { container.api.queryItems(ItemQuery(searchText = title)).items.isEmpty() })
        runBlocking { container.api.deleteType(typeId) }

        // Logout returns to the login screen.
        compose.onNodeWithContentDescription("Open navigation").performClick()
        compose.onNode(hasText("Log out") and hasClickAction()).performClick()
        compose.waitForText("Log in")
    }

    @Test
    fun cardsAreAddedQuizzedAndDeleted() {
        logIn()
        // The item and a linked neighbour with a card of its own, through the API.
        val (itemId, neighbourId) = runBlocking {
            val groupId = container.api.defaultGroupId()
            val item = container.api.createItem(SaveItemRequest(ItemWrite(groupId = groupId, title = title))).id
            val neighbour = container.api.createItem(
                SaveItemRequest(
                    ItemWrite(groupId = groupId, title = "$title neighbour"),
                    links = listOf(OutgoingLinkWrite(id = null, toItemId = item, linkType = LinkType.Related)),
                ),
            ).id
            container.api.createCard(neighbour, CardWrite("Neighbour question", "Neighbour answer"))
            item to neighbour
        }

        compose.onNode(hasSetTextAction() and hasContentDescription("Search items")).performTextReplacement(title)
        compose.onNode(hasSetTextAction() and hasContentDescription("Search items")).performImeAction()
        compose.waitFor(hasText(title) and hasClickAction() and !hasSetTextAction())
        compose.onNode(hasText(title) and hasClickAction() and !hasSetTextAction()).performClick()
        compose.waitForText("This item has no content yet.")
        compose.onNodeWithContentDescription("More actions").performClick()
        compose.waitFor(hasText("Cards") and hasClickAction())
        button("Cards").performClick()
        compose.waitForText("No cards yet. Add one with the + button.")

        addCard("Co znamená řetězec?", "Příliš žluťoučký kůň\n指针")
        compose.waitFor(hasContentDescription("Delete card Co znamená řetězec?"))
        addCard("What is std::uint64_t?", "An unsigned 64-bit integer.")
        compose.waitFor(hasContentDescription("Delete card What is std::uint64_t?"))
        val (first, second) = runBlocking { container.api.cards(itemId) }
        assertEquals("Příliš žluťoučký kůň\n指针", first.answer)

        // One Yes and one No; the server counts them.
        compose.onNodeWithContentDescription("Card quiz").performClick()
        compose.waitForText("1 / 2")
        answer(yes = true)
        compose.waitForText("2 / 2")
        answer(yes = false)
        compose.waitForText("Quiz finished")
        compose.waitForText("Yes: 1")
        compose.waitForText("No: 1")
        val counted = runBlocking { container.api.cards(itemId) }
        assertEquals(listOf(1L to 0L, 0L to 1L), counted.map { it.successCount to it.failureCount })
        assertTrue(counted.all { it.lastAttempt?.endsWith("Z") == true })
        assertEquals(listOf(first.id, second.id), counted.map { it.id })

        // The neighbourhood brings the linked item's card.
        button("Neighborhood").performClick()
        compose.waitForText("1 / 3")
        compose.waitFor(hasText("2 links") and isSelected())
        compose.waitForText("3 card(s) from 2 item(s)", substring = true)

        // Back in the list: the new counts, and a delete that asks first.
        compose.onNodeWithContentDescription("Back").performClick()
        compose.waitForText("Success: 1 · Failure: 0", substring = true)
        compose.onNodeWithContentDescription("Delete card What is std::uint64_t?").performClick()
        compose.waitForText("Delete the card 'What is std::uint64_t?' and its counts?")
        compose.onNode(hasText("Delete") and hasClickAction() and hasAnyAncestor(isDialog())).performClick()
        compose.waitUntilGone(hasContentDescription("Delete card What is std::uint64_t?"))
        assertEquals(listOf(first.id), runBlocking { container.api.cards(itemId) }.map { it.id })
        assertEquals(1, runBlocking { container.api.cards(neighbourId) }.size)
    }
}
