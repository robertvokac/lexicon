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
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.core.app.ActivityOptionsCompat
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.FieldWrite
import com.robertvokac.lexicon.model.ItemQuery
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
 * logout. Runs only when scripts/run-device-tests.sh supplies a server.
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
}
