package com.robertvokac.lexicon.flows

import android.net.Uri
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasAnyDescendant
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasParent
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.isPopup
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.printToString
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.core.app.ActivityOptionsCompat
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.LexiconApplication
import com.robertvokac.lexicon.api.LexiconJson
import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.ReviewRating
import com.robertvokac.lexicon.ui.LaunchRequests
import com.robertvokac.lexicon.ui.LexiconRoot
import com.robertvokac.lexicon.testing.FakeLexiconServer
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.waitFor
import com.robertvokac.lexicon.testing.waitForCondition
import com.robertvokac.lexicon.testing.waitForText
import com.robertvokac.lexicon.testing.waitUntilGone
import com.robertvokac.lexicon.ui.alarms.AlarmTimes
import kotlinx.coroutines.runBlocking
import java.time.LocalDateTime
import java.time.ZoneId
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import org.junit.runner.RunWith
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.GraphicsMode
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream

/**
 * The important flows, driven through the real Compose UI against an
 * in-memory server. Pixels are not compared; semantics and requests are.
 */
@RunWith(AndroidJUnit4::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class LexiconFlowsTest {
    @get:Rule(order = 0)
    val compose = createComposeRule()

    /** On a failure, what was on screen and what the server saw. */
    @get:Rule(order = 1)
    val dumpOnFailure = object : TestWatcher() {
        override fun failed(e: Throwable?, description: Description?) {
            runCatching { println("SCREEN\n" + compose.onAllNodes(isRoot()).printToString(maxDepth = Int.MAX_VALUE)) }
            runCatching { println("REQUESTS " + fake.requests.map { "${it.method} ${it.url.encodedPath}" }) }
        }
    }

    private lateinit var fake: FakeLexiconServer
    private lateinit var environment: TestEnvironment
    private lateinit var container: AppContainer
    private lateinit var raii: Item
    private lateinit var lifetime: Item

    /** Answers every document picker at once with [nextUri], like a person choosing a file. */
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
        fake = FakeLexiconServer().start()
        raii = fake.addItem(Item(title = "RAII", tags = listOf("cpp"), content = "Resource acquisition is initialization."))
        lifetime = fake.addItem(Item(title = "Object lifetime", pinned = true))
        fake.addItem(Item(title = "Pointer provenance", flags = listOf("todo")))
        environment = TestEnvironment()
        val application = ApplicationProvider.getApplicationContext<LexiconApplication>()
        container = environment.container(application.contentResolver)
        val requests = LaunchRequests()
        compose.setContent {
            CompositionLocalProvider(LocalActivityResultRegistryOwner provides registryOwner) {
                LexiconRoot(container, requests, onShareFinished = {}, onExit = {})
            }
        }
    }

    @After
    fun tearDown() {
        fake.close()
        environment.close()
    }

    private fun field(label: String) = compose.onNode(hasSetTextAction() and hasText(label))

    private fun inDialog(text: String) = compose.onNode(hasText(text) and hasClickAction() and hasAnyAncestor(isDialog()))

    private fun inPopup(text: String) = compose.onNode(hasText(text) and hasAnyAncestor(isPopup()))

    private fun lastBody(method: String, path: String): JsonObject =
        LexiconJson.parseToJsonElement(fake.requestsTo(method, path).last().body!!.utf8()).jsonObject

    private fun login() {
        compose.waitForText("Log in")
        field("Server URL").performTextReplacement(fake.baseUrl)
        field("User name").performTextReplacement(fake.username)
        field("Password").performTextInput(fake.password)
        compose.onNode(hasText("Log in") and hasClickAction()).performClick()
        compose.waitForText("Pointer provenance")
    }

    /** From an open item page: the editor, once its form has loaded. */
    private fun openEditor() {
        compose.onNodeWithContentDescription("Edit item").performClick()
        compose.waitFor(hasSetTextAction() and hasText("Title"))
    }

    private fun openItem(title: String, waitFor: String) {
        compose.onNode(hasText(title) and hasClickAction()).performClick()
        compose.waitForText(waitFor)
    }

    private fun openDrawer(entry: String) {
        compose.onNodeWithContentDescription("Open navigation").performClick()
        compose.waitForText(entry)
        compose.onNode(hasText(entry) and hasClickAction()).performClick()
    }

    @Test
    fun loginChecksTheApiVersionAndShowsTheItems() {
        login()
        val paths = fake.requests.map { it.url.encodedPath }
        assertEquals(listOf("/api/v1/health", "/api/v1/auth/login"), paths.take(2))
        compose.onNodeWithText("RAII").assertIsDisplayed()
        compose.onNodeWithText("Object lifetime").assertIsDisplayed()
        compose.onNodeWithText("3 items").assertIsDisplayed()
        // The token is kept, encrypted; the password is not kept anywhere.
        val stored = runBlocking { environment.tokenStore.load() }
        assertEquals("token-1", stored?.token)
    }

    @Test
    fun aWrongPasswordIsExplained() {
        compose.waitForText("Log in")
        field("Server URL").performTextReplacement(fake.baseUrl)
        field("User name").performTextReplacement(fake.username)
        field("Password").performTextInput("not the password")
        compose.onNode(hasText("Log in") and hasClickAction()).performClick()
        compose.waitForText("Invalid user name or password.")
    }

    @Test
    fun searchAsksTheServer() {
        login()
        compose.onNode(hasSetTextAction() and hasContentDescription("Search items")).performTextInput("raii")
        compose.onNode(hasSetTextAction() and hasContentDescription("Search items")).performImeAction()
        compose.waitUntilGone(hasText("Pointer provenance"))
        compose.onNodeWithText("RAII").assertIsDisplayed()
        assertEquals("raii", lastBody("POST", "/api/v1/items/query")["searchText"]!!.jsonPrimitive.content)
    }

    @Test
    fun quickAddUsesTheDefaultGroup() {
        login()
        compose.onNodeWithContentDescription("Quick add item").performClick()
        compose.waitFor(hasText("Quick add"))
        field("Title").performTextReplacement("Move semantics")
        inDialog("Add").performClick()
        compose.waitForCondition { fake.requestsTo("POST", "/api/v1/items").isNotEmpty() }
        val item = lastBody("POST", "/api/v1/items")["item"]!!.jsonObject
        assertEquals("Move semantics", item["title"]!!.jsonPrimitive.content)
        assertEquals(1, item["groupId"]!!.jsonPrimitive.int)
        assertEquals("null", item["itemTypeId"].toString())
        compose.waitForText("Move semantics")
    }

    @Test
    fun theInboxSavesAnIdeaToDefaultWithoutAType() {
        login()
        compose.onNodeWithContentDescription("Inbox: save an idea").performClick()
        compose.waitForText("Saved to Default, without a type. Sort it out later.")
        inDialog("Save").performClick()
        compose.waitForText("Enter a title.")
        assertTrue(fake.requestsTo("POST", "/api/v1/items").isEmpty())
        field("Title").performTextInput("Lock-free queue")
        field("Idea").performTextInput("Try a ring buffer.\nMeasure it first.")
        inDialog("Save").performClick()
        compose.waitForCondition { fake.requestsTo("POST", "/api/v1/items").isNotEmpty() }
        val item = lastBody("POST", "/api/v1/items")["item"]!!.jsonObject
        assertEquals("Lock-free queue", item["title"]!!.jsonPrimitive.content)
        assertEquals("Try a ring buffer.\nMeasure it first.", item["content"]!!.jsonPrimitive.content)
        assertEquals(1, item["groupId"]!!.jsonPrimitive.int)
        assertEquals("null", item["itemTypeId"].toString())
        compose.waitForText("Lock-free queue")
    }

    @Test
    fun quickAddShowsAnItemThatAlreadyExistsInsteadOfAddingIt() {
        login()
        compose.onNodeWithContentDescription("Quick add item").performClick()
        compose.waitFor(hasText("Quick add"))
        field("Title").performTextInput("raii")
        inDialog("Add").performClick()
        compose.waitForText("Already in Lexicon")
        assertTrue(fake.requestsTo("POST", "/api/v1/items").isEmpty())
        compose.onNode(hasText("Cancel") and hasAnyAncestor(isDialog() and hasAnyDescendant(hasText("Already in Lexicon")))).performClick()
        compose.waitUntilGone(hasText("Already in Lexicon"))
        assertTrue(fake.requestsTo("POST", "/api/v1/items").isEmpty())
    }

    @Test
    fun openingAnItemRecordsOneRead() {
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        compose.waitForCondition { fake.reads.isNotEmpty() }
        repeat(3) { compose.waitForIdle() }
        assertEquals(listOf(raii.id), fake.reads.toList())
    }

    @Test
    fun editingAndSavingUsesOneRequest() {
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        openEditor()
        field("Title").performTextReplacement("RAII idiom")
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForCondition { fake.requestsTo("PUT", "/api/v1/items/${raii.id}").isNotEmpty() }
        val body = lastBody("PUT", "/api/v1/items/${raii.id}")
        assertEquals("RAII idiom", body["item"]!!.jsonObject["title"]!!.jsonPrimitive.content)
        assertTrue(body.containsKey("links") && body.containsKey("backlinks"))
        compose.waitForText("RAII idiom")
    }

    @Test
    fun aSaveOverAnotherClientsChangeAsksFirst() {
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        openEditor()
        fake.changeElsewhere(raii.id!!) { it.copy(content = "Changed on the desktop.") }
        field("Title").performTextReplacement("RAII idiom")
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForText("Item changed elsewhere")
        compose.onNodeWithText("Your version differs in: Title, Content.").assertIsDisplayed()
        assertEquals("Changed on the desktop.", fake.items.getValue(raii.id!!).content)

        inDialog("Overwrite").performClick()
        compose.waitForCondition { fake.items.getValue(raii.id!!).title == "RAII idiom" }
        val puts = fake.requestsTo("PUT", "/api/v1/items/${raii.id}")
        assertEquals(2, puts.size)
        val revisions = puts.map {
            LexiconJson.parseToJsonElement(it.body!!.utf8()).jsonObject["item"]!!.jsonObject["revision"]!!.jsonPrimitive.int
        }
        assertEquals(listOf(1, 2), revisions)
        assertEquals("Resource acquisition is initialization.", fake.items.getValue(raii.id!!).content)
    }

    @Test
    fun reloadingAfterAConflictShowsTheNewerVersion() {
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        openEditor()
        fake.changeElsewhere(raii.id!!) { it.copy(title = "RAII (desktop)") }
        field("Title").performTextReplacement("RAII idiom")
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForText("Item changed elsewhere")
        inDialog("Reload").performClick()
        compose.waitFor(hasSetTextAction() and hasText("RAII (desktop)"))
        assertEquals(1, fake.requestsTo("PUT", "/api/v1/items/${raii.id}").size)
    }

    @Test
    fun aRefusedSaveKeepsWhatWasTyped() {
        fake.addItem(Item(title = "Taken"))
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        openEditor()
        field("Title").performTextReplacement("Taken")
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForText("Not saved: 'Taken' already exists in this group.")
        field("Title").assertIsDisplayed()
        compose.onNode(hasSetTextAction() and hasText("Taken")).assertIsDisplayed()
    }

    @Test
    fun filtersGoToTheServerAndCanBeCleared() {
        login()
        compose.onNodeWithContentDescription("Filters and sort").performClick()
        compose.waitForText("Filters and sort")
        compose.onNode(hasText("Tag") and hasText("All tags")).performScrollTo().performClick()
        inPopup("cpp").performClick()
        compose.waitForCondition { lastBody("POST", "/api/v1/items/query")["tagFilter"]?.jsonPrimitive?.content == "cpp" }
        compose.onNode(hasText("Done") and hasClickAction()).performScrollTo().performClick()
        compose.waitUntilGone(hasText("Pointer provenance"))
        compose.onNodeWithContentDescription("Filter Tag: cpp. Remove").assertIsDisplayed()
        compose.onNodeWithContentDescription("Filters and sort, 1 active").performClick()
        compose.waitForText("Clear filters")
        compose.onNode(hasText("Clear filters") and hasClickAction()).performClick()
        compose.waitForCondition { lastBody("POST", "/api/v1/items/query")["tagFilter"]?.jsonPrimitive?.content == "" }
    }

    @Test
    fun linksAreSavedWithTheItem() {
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        openEditor()
        compose.onNode(hasText("Links") and hasClickAction()).performClick()
        compose.onNode(hasText("Add link") and hasClickAction()).performClick()
        inDialog("Choose…").performClick()
        compose.waitForText("Search items")
        field("Search items").performTextInput("lifetime")
        compose.waitFor(hasText("Object lifetime") and hasAnyAncestor(isDialog()) and hasClickAction())
        compose.onNode(hasText("Object lifetime") and hasAnyAncestor(isDialog()) and hasClickAction()).performClick()
        inDialog("OK").performClick()
        compose.waitForText("Links (1)")
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForCondition { fake.requestsTo("PUT", "/api/v1/items/${raii.id}").isNotEmpty() }
        val link = lastBody("PUT", "/api/v1/items/${raii.id}")["links"]!!.jsonArray.single().jsonObject
        assertEquals(lifetime.id, link["toItemId"]!!.jsonPrimitive.int)
        assertEquals(LinkType.Related.name, link["linkType"]!!.jsonPrimitive.content)
        assertEquals("null", link["id"].toString())
        // The detail page of the target now shows the backlink.
        compose.waitForText("Links")
        assertEquals(1, fake.links.size)
    }

    @Test
    fun aBlobIsUploadedAssignedSavedAndDownloadedAgain() {
        val type = fake.addType("Document")
        val blobField = fake.addField(type.id!!, "Attachment", FieldDataType.Blob)
        val item = fake.addItem(Item(title = "Spec", itemTypeId = type.id))
        val bytes = ByteArray(70_000) { (it * 7 % 256).toByte() }
        val source = Uri.parse("content://com.example.documents/spec.pdf")
        val resolver = ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver
        shadowOf(resolver).registerInputStream(source, ByteArrayInputStream(bytes))
        val target = Uri.parse("content://com.example.documents/saved.bin")
        val saved = ByteArrayOutputStream()
        shadowOf(resolver).registerOutputStream(target, saved)

        login()
        compose.onNodeWithText("Spec").performClick()
        compose.waitForText("Not set")
        openEditor()
        compose.onNode(hasText("Values") and hasClickAction()).performClick()
        nextUri = source
        compose.onNode(hasText("Upload…") and hasClickAction()).performClick()
        compose.waitForCondition { fake.blobs.isNotEmpty() }
        val hash = fake.blobs.keys.single()
        compose.waitForText(hash)
        assertArrayEquals(bytes, fake.blobs.getValue(hash))

        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForCondition { fake.requestsTo("PUT", "/api/v1/items/${item.id}").isNotEmpty() }
        assertEquals(hash, fake.items.getValue(item.id!!).fieldValue(blobField.id!!))

        // Reloaded from the server, then saved to a document the person chose.
        compose.waitForText("Save as…")
        nextUri = target
        compose.onNode(hasText("Save as…") and hasClickAction()).performClick()
        compose.waitForCondition { saved.size() == bytes.size }
        assertArrayEquals(bytes, saved.toByteArray())
    }

    @Test
    fun theDictionaryIsExportedToADocumentAndImportedFromOne() {
        val resolver = ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver
        val target = Uri.parse("content://com.example.documents/lexicon.json")
        val saved = ByteArrayOutputStream()
        shadowOf(resolver).registerOutputStream(target, saved)
        val source = Uri.parse("content://com.example.documents/other.json")
        val document = """{"format":"lexicon-export","version":1,"groups":[],"types":[],"items":[],"links":[]}""".toByteArray()
        shadowOf(resolver).registerInputStream(source, ByteArrayInputStream(document))

        login()
        openDrawer("Settings")
        compose.waitForText("Export and import")
        nextUri = target
        compose.onNode(hasText("Export…") and hasClickAction()).performScrollTo().performClick()
        compose.waitForText("The dictionary was exported.")
        assertEquals(fake.exportDocument, saved.toString(Charsets.UTF_8))
        assertEquals("blobs=true", fake.requestsTo("GET", "/api/v1/export").single().url.encodedQuery)

        nextUri = source
        compose.onNode(hasText("Import…") and hasClickAction()).performScrollTo().performClick()
        compose.waitFor(hasText("Merge this export", substring = true))
        inDialog("Import").performClick()
        compose.waitFor(hasText("Field 'Year' holds another kind of value here.", substring = true))
        assertArrayEquals(document, fake.imports.single())
        assertEquals("application/json; charset=utf-8", fake.requestsTo("POST", "/api/v1/import").single().headers["Content-Type"])
        compose.onNode(hasText("Imported 2 item(s)", substring = true) and hasAnyAncestor(isDialog())).assertIsDisplayed()
    }

    @Test
    fun reviewShowsTheAnswerOnRequestAndRatesIt() {
        login()
        openDrawer("Review")
        compose.waitForText("3 due")
        compose.onNodeWithText("RAII").assertIsDisplayed()
        compose.onNodeWithText("Resource acquisition is initialization.").assertDoesNotExist()
        compose.onNode(hasText("Show answer") and hasClickAction()).performClick()
        compose.waitForText("Resource acquisition is initialization.")
        compose.waitFor(hasText("Good (2 days)") and hasClickAction())
        compose.onNode(hasText("Good (2 days)") and hasClickAction()).performScrollTo().performClick()
        compose.waitForText("Object lifetime")
        assertEquals(listOf(raii.id!! to ReviewRating.Good), fake.reviews.toList())
        assertEquals(UnderstandingLevel.Recognized, fake.items.getValue(raii.id!!).understanding)

        compose.onNode(hasText("Show answer") and hasClickAction()).performClick()
        compose.waitFor(hasText("Again (1 day)") and hasClickAction())
        compose.onNode(hasText("Again (1 day)") and hasClickAction()).performScrollTo().performClick()
        compose.waitForText("Pointer provenance")
        compose.onNode(hasText("Skip") and hasClickAction()).performClick()
        // Forgotten, so it comes back in the same sitting.
        compose.waitForText("Object lifetime")
        compose.onNode(hasText("Skip") and hasClickAction()).performClick()
        compose.waitForText("2 reviewed", substring = true)
    }

    @Test
    fun aWikiLinkOpensItsItemOrOffersToCreateIt() {
        fake.changeElsewhere(raii.id!!) { it.copy(content = "Tied to the [[object lifetime]]; see [[Ownership]].") }
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Tied to the object lifetime; see Ownership.")
        // The links of a text are its clickable children, in order.
        val links = hasClickAction() and hasParent(hasText("Tied to the object lifetime; see Ownership."))
        compose.onAllNodes(links)[0].performClick()
        compose.waitForText("This item has no content yet.")
        assertTrue(fake.requests.any { it.url.encodedPath == "/api/v1/items/${lifetime.id}" })

        compose.onNodeWithContentDescription("Back").performClick()
        compose.waitForText("Tied to the object lifetime; see Ownership.")
        compose.onAllNodes(links)[1].performClick()
        compose.waitForText("No item is called 'Ownership'. Create it?")
        inDialog("Create").performClick()
        compose.waitFor(hasSetTextAction() and hasText("Ownership"))
    }

    @Test
    fun linksAreAddedForTheItemsTheContentNames() {
        fake.changeElsewhere(raii.id!!) { it.copy(content = "Tied to the [[object lifetime]], not to [[Ghost]].") }
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Tied to the object lifetime, not to Ghost.")
        openEditor()
        compose.onNode(hasText("Links", substring = true) and hasClickAction()).performClick()
        compose.onNode(hasText("Add links from content") and hasClickAction()).performScrollTo().performClick()
        compose.waitForText("1 link(s) added. No item is called: Ghost.")
        compose.onNodeWithText("Object lifetime").assertIsDisplayed()
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForCondition { fake.requestsTo("PUT", "/api/v1/items/${raii.id}").isNotEmpty() }
        val link = lastBody("PUT", "/api/v1/items/${raii.id}")["links"]!!.jsonArray.single().jsonObject
        assertEquals(lifetime.id, link["toItemId"]!!.jsonPrimitive.int)
        assertEquals("Related", link["linkType"]!!.jsonPrimitive.content)
    }

    @Test
    fun theGraphShowsTheItemsAroundOneAndOpensThem() {
        fake.links += com.robertvokac.lexicon.model.Link(id = 900, fromItemId = raii.id, toItemId = lifetime.id, linkType = LinkType.DependsOn)
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        compose.onNodeWithContentDescription("More actions").performClick()
        compose.onNode(hasText("Relationship graph") and hasClickAction()).performClick()
        compose.waitForText("2 item(s), 1 link(s)")
        compose.onNode(hasText("Object lifetime") and hasClickAction()).performClick()
        compose.onNode(hasText("Open") and hasClickAction()).performClick()
        compose.waitForText("This item has no content yet.")
    }

    @Test
    fun saveWaitsForARunningUpload() {
        val type = fake.addType("Document")
        fake.addField(checkNotNull(type.id), "Attachment", FieldDataType.Blob)
        fake.addItem(Item(title = "Spec", itemTypeId = type.id))
        val source = Uri.parse("content://com.example.documents/slow.pdf")
        val resolver = ApplicationProvider.getApplicationContext<LexiconApplication>().contentResolver
        shadowOf(resolver).registerInputStream(source, ByteArrayInputStream(ByteArray(10_000) { 1 }))
        fake.blobDelayMillis = 2_000

        login()
        openItem("Spec", waitFor = "Not set")
        openEditor()
        compose.onNode(hasText("Values") and hasClickAction()).performClick()
        nextUri = source
        compose.onNode(hasText("Upload\u2026") and hasClickAction()).performClick()
        compose.waitForText("Uploading", substring = true)
        // Saving now would leave the file out of the item.
        compose.onNode(hasText("Save") and hasClickAction()).assertIsNotEnabled()
        compose.waitForText("slow.pdf (", substring = true)
        compose.onNode(hasText("Save") and hasClickAction()).assertIsEnabled()
    }

    @Test
    fun alarmsAreListedAddedChangedAndDeleted() {
        fake.alarms += Alarm(7, "Old call", "", "2001-05-06T07:08:00Z")
        login()
        openDrawer("Alarms")
        compose.waitForText("Old call")
        compose.waitForText("gone off", substring = true)
        compose.onNodeWithContentDescription("Add alarm").performClick()
        compose.waitForText("Add alarm")
        inDialog("Save").performClick()
        compose.waitForText("Enter a title.")
        field("Title").performTextInput("Dentist")
        field("Date").performTextReplacement("2030-01-02")
        field("Time").performTextReplacement("25:99")
        inDialog("Save").performClick()
        compose.waitForText("Enter a date as YYYY-MM-DD and a time as HH:MM.")
        field("Time").performTextReplacement("10:15")
        field("Description").performTextInput("Bring the card.")
        inDialog("Save").performClick()
        compose.waitForCondition { fake.requestsTo("POST", "/api/v1/alarms").isNotEmpty() }
        // Typed in local time, sent in UTC.
        val expected = AlarmTimes.format(LocalDateTime.of(2030, 1, 2, 10, 15).atZone(ZoneId.systemDefault()).toInstant())
        val created = lastBody("POST", "/api/v1/alarms")
        assertEquals("Dentist", created["title"]!!.jsonPrimitive.content)
        assertEquals(expected, created["firesAt"]!!.jsonPrimitive.content)
        assertEquals("Bring the card.", created["description"]!!.jsonPrimitive.content)
        compose.waitFor(hasContentDescription("Delete alarm Dentist"))
        compose.onNodeWithText("2030-01-02 10:15", substring = true).assertIsDisplayed()

        compose.onNode(hasText("Dentist") and hasClickAction()).performClick()
        compose.waitForText("Edit alarm")
        field("Time").performTextReplacement("08:00")
        inDialog("Save").performClick()
        compose.waitForCondition { fake.requestsTo("PUT", "/api/v1/alarms/${fake.alarms.first { it.title == "Dentist" }.id}").isNotEmpty() }
        compose.waitForText("2030-01-02 08:00", substring = true)

        compose.onNodeWithContentDescription("Delete alarm Dentist").performClick()
        compose.waitForText("Delete the alarm 'Dentist'?")
        inDialog("Delete").performClick()
        compose.waitUntilGone(hasContentDescription("Delete alarm Dentist"))
        assertTrue(fake.alarms.none { it.title == "Dentist" })
    }

    @Test
    fun groupsCanBeAddedAndDeletedWithTheDesktopWarning() {
        login()
        openDrawer("Groups")
        compose.waitForText("Default")
        compose.onNodeWithContentDescription("Add group").performClick()
        field("Name").performTextInput("C++")
        inDialog("Save").performClick()
        compose.waitFor(hasContentDescription("Delete group C++"))
        assertEquals("C++", lastBody("POST", "/api/v1/groups")["name"]!!.jsonPrimitive.content)
        compose.onNodeWithContentDescription("Delete group C++").performClick()
        compose.waitForText("Delete group 'C++'? All items inside it will also be deleted.")
        inDialog("Delete").performClick()
        compose.waitUntilGone(hasText("C++"))
        assertTrue(fake.groups.none { it.name == "C++" })
    }

    @Test
    fun typesAndFieldsAreManagedWithCountedWarnings() {
        login()
        openDrawer("Types")
        compose.waitForText("No types yet.")
        compose.onNodeWithContentDescription("Add type").performClick()
        field("Name").performTextInput("Term")
        inDialog("Save").performClick()
        // The row, not the dialog's name field.
        compose.waitFor(hasText("Term") and hasText("All groups") and hasClickAction())
        compose.onNode(hasText("Term") and hasText("All groups") and hasClickAction()).performClick()
        compose.waitForText("This type has no fields yet.")
        compose.onNodeWithContentDescription("Add field").performClick()
        field("Name").performTextInput("Difficulty")
        compose.onNode(hasText("Text") and hasAnyAncestor(isDialog())).performClick()
        inPopup("Enum").performClick()
        field("Enum options").performTextInput("easy\nhard")
        inDialog("Save").performClick()
        compose.waitFor(hasContentDescription("Delete field Difficulty"))
        val field = fake.fields.single()
        assertEquals(FieldDataType.Enum, field.dataType)
        assertEquals(listOf("easy", "hard"), field.enumOptions)

        fake.addItem(Item(title = "Uses the type", itemTypeId = fake.types.single().id, fieldValues = mapOf(field.id.toString() to "hard")))
        compose.onNodeWithContentDescription("Delete field Difficulty").performClick()
        compose.waitForText("Delete field 'Difficulty'? This will remove its value from 1 item(s). Continue?")
        compose.onNode(hasText("Cancel") and hasClickAction()).performClick()
        compose.onNodeWithContentDescription("Delete type").performClick()
        compose.waitForText("for all 1 item(s) using it", substring = true)
        inDialog("Delete").performClick()
        compose.waitForText("No types yet.")
        assertTrue(fake.types.isEmpty())
    }

    @Test
    fun overviewsShowUsageCounts() {
        fake.addItem(Item(title = "Move", tags = listOf("cpp", "cpp11")))
        login()
        openDrawer("All tags")
        compose.waitForText("Usage count")
        compose.onNodeWithContentDescription("cpp, used 2 times").assertIsDisplayed()
        compose.onNodeWithContentDescription("cpp11, used 1 time").assertIsDisplayed()
    }

    @Test
    fun logoutEndsTheSessionEverywhere() {
        login()
        openDrawer("Log out")
        compose.waitForText("Log in")
        compose.waitForCondition { fake.requestsTo("POST", "/api/v1/auth/logout").isNotEmpty() }
        assertTrue(fake.tokens.isEmpty())
        compose.waitForCondition { runBlocking { environment.tokenStore.load() } == null }
        assertNull(container.sessions.current)
    }

    @Test
    fun anExpiredSessionReturnsToLoginAndKeepsTheEditor() {
        login()
        compose.onNodeWithText("RAII").performClick()
        compose.waitForText("Resource acquisition is initialization.")
        openEditor()
        field("Title").performTextReplacement("RAII, kept")
        // The server ends every session, as a new password does.
        fake.tokens.clear()
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForText("Your session has expired or was ended on the server. Sign in again.")
        field("Password").performTextInput(fake.password)
        compose.onNode(hasText("Log in") and hasClickAction()).performClick()
        compose.waitForText("RAII, kept")
        compose.onNode(hasText("Save") and hasClickAction()).performClick()
        compose.waitForCondition { fake.items.getValue(raii.id!!).title == "RAII, kept" }
        // One 401, no retry loop.
        assertEquals(1, fake.requests.count { it.method == "PUT" && it.headers["Authorization"] == "Bearer token-1" })
    }
}
