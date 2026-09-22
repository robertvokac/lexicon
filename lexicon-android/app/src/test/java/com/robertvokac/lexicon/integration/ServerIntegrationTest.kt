package com.robertvokac.lexicon.integration

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.api.ApiClient
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.api.Session
import com.robertvokac.lexicon.api.SessionAccess
import com.robertvokac.lexicon.auth.SessionManager
import com.robertvokac.lexicon.auth.SessionState
import com.robertvokac.lexicon.model.AlarmWrite
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.FieldWrite
import com.robertvokac.lexicon.model.GroupWrite
import com.robertvokac.lexicon.model.ImageValues
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemStatus
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.Property
import com.robertvokac.lexicon.model.PropertyFilter
import com.robertvokac.lexicon.model.ReviewRating
import com.robertvokac.lexicon.model.TypeWrite
import com.robertvokac.lexicon.model.UnderstandingLevel
import com.robertvokac.lexicon.model.ValueFilter
import com.robertvokac.lexicon.testing.TestEnvironment
import com.robertvokac.lexicon.testing.TestImages
import com.robertvokac.lexicon.ui.item.EditorFields
import com.robertvokac.lexicon.ui.item.EditorRules
import com.robertvokac.lexicon.ui.item.LinkEntry
import com.robertvokac.lexicon.ui.items.ItemFilters
import com.robertvokac.lexicon.ui.items.buildItemQuery
import com.robertvokac.lexicon.model.SortColumns
import com.robertvokac.lexicon.model.SortOrder
import kotlinx.coroutines.runBlocking
import org.junit.AfterClass
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.BeforeClass
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.net.ServerSocket
import java.net.URI
import java.nio.file.Files
import java.util.concurrent.TimeUnit

/**
 * The Android REST layer against a real LexiconServer, when the build is given
 * one: `LEXICON_SERVER_BINARY=/path/to/LexiconServer ./gradlew test`.
 * A temporary database and user are created and removed again.
 */
class ServerIntegrationTest {
    companion object {
        private const val USER = "android-test"
        private const val PASSWORD = "integration password 123"
        private const val MAX_BLOB_BYTES = 200_000

        private var directory: File? = null
        private var process: Process? = null
        private var baseUrl = ""

        @BeforeClass
        @JvmStatic
        fun startServer() {
            val binary = System.getProperty("lexicon.serverBinary")?.takeIf { it.isNotBlank() }
            assumeTrue("LEXICON_SERVER_BINARY is not set; skipping the real-server test", binary != null)
            val dir = Files.createTempDirectory("lexicon-android-it").toFile().also { directory = it }
            val database = File(dir, "lexicon.db").absolutePath
            val setUser = ProcessBuilder(binary, "auth", "set-user", "--database", database)
                .redirectErrorStream(true)
                .start()
            setUser.outputStream.bufferedWriter().use { it.write("$USER\n$PASSWORD\n$PASSWORD\n") }
            val output = setUser.inputStream.bufferedReader().readText()
            check(setUser.waitFor(60, TimeUnit.SECONDS) && setUser.exitValue() == 0) { "auth set-user failed: $output" }
            val port = ServerSocket(0).use { it.localPort }
            process = ProcessBuilder(
                binary, "--database", database, "--listen", "127.0.0.1", "--port", port.toString(),
                // A small blob limit makes the oversized upload cheap; the server
                // wants it no smaller than the JSON limit.
                "--max-json-bytes", "100000", "--max-blob-bytes", MAX_BLOB_BYTES.toString(), "--quiet",
            ).redirectErrorStream(true).redirectOutput(File(dir, "server.log")).start()
            baseUrl = "http://127.0.0.1:$port"
            val deadline = System.currentTimeMillis() + 15_000
            while (true) {
                val up = runCatching { URI("$baseUrl/api/v1/health").toURL().readText().contains("\"apiVersion\":1") }.getOrDefault(false)
                if (up) break
                check(System.currentTimeMillis() < deadline) { "LexiconServer did not start: ${File(dir, "server.log").readText()}" }
                Thread.sleep(100)
            }
        }

        @AfterClass
        @JvmStatic
        fun stopServer() {
            process?.let {
                it.destroy()
                it.waitFor(10, TimeUnit.SECONDS)
            }
            directory?.deleteRecursively()
        }
    }

    private fun newSessionManager(environment: TestEnvironment): Pair<SessionManager, LexiconApi> {
        lateinit var manager: SessionManager
        val api = LexiconApi(ApiClient(AppContainer.httpClient(), object : SessionAccess {
            override val current: Session? get() = manager.current
            override fun onUnauthorized(session: Session) = manager.onUnauthorized(session)
        }))
        manager = SessionManager({ api }, environment.tokenStore, environment.settings, environment.scope, allowCleartextDevelopmentHosts = true)
        return manager to api
    }

    @Test
    fun theWholeItemLifecycleOverRealRest() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)

            val refused = sessions.login(baseUrl, USER, "wrong password")
            assertEquals(SessionManager.LoginResult.Failure("Invalid user name or password."), refused)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            assertTrue(sessions.state.value is SessionState.SignedIn)

            // Quick Add semantics: the Default group, created on demand.
            val defaultGroupId = api.defaultGroupId()
            assertEquals("Default", api.groups().single { it.id == defaultGroupId }.name)

            val group = api.createGroup(GroupWrite("C++", "Notes on C++", 1))
            val type = api.createType(TypeWrite("Term", "", groupId = null))
            val typeId = checkNotNull(type.id)
            val difficulty = api.createField(typeId, FieldWrite("Difficulty", FieldDataType.Enum, 0, listOf("easy", "hard")))
            val attachment = api.createField(typeId, FieldWrite("Attachment", FieldDataType.Blob, 1))
            val year = api.createField(typeId, FieldWrite("Year", FieldDataType.Integer, 2))
            val difficultyId = checkNotNull(difficulty.id)
            val attachmentId = checkNotNull(attachment.id)
            val yearId = checkNotNull(year.id)

            // A blob travels as bytes and comes back identical.
            val bytes = ByteArray(150_000) { (it * 31 % 256).toByte() }
            val hash = api.uploadBlob(bytes.size.toLong(), { ByteArrayInputStream(bytes) }) { _, _ -> }
            assertTrue(LexiconApi.isBlobHash(hash))

            val target = api.createItem(EditorRules.saveRequest(EditorFields(groupId = group.id, title = "Object lifetime"), "", emptyList()))
            val source = api.createItem(EditorRules.saveRequest(EditorFields(groupId = group.id, title = "Constructor"), "", emptyList()))

            // Create with values, metadata and both link directions in one request.
            val fields = listOf(difficulty, attachment, year)
            val editor = EditorFields(
                groupId = group.id, typeId = type.id, title = "RAII", disambiguation = "idiom",
                status = ItemStatus.Draft, understanding = UnderstandingLevel.Practiced, pinned = true,
                tags = listOf("cpp", "resources"), flags = listOf("todo"), aliases = listOf("Scope-bound resource management"),
                properties = listOf(Property("source", "Stroustrup")),
                values = mapOf(difficultyId to "hard", attachmentId to hash, yearId to "1984"),
                links = listOf(LinkEntry(1, null, target.id, "Object lifetime", LinkType.DependsOn, "", 0)),
                backlinks = listOf(LinkEntry(2, null, source.id, "Constructor", LinkType.Custom, "acquires in", 1)),
            )
            val created = api.createItem(EditorRules.saveRequest(editor, "# RAII\n\nČeština and ✓ survive UTF-8.", fields))
            val loaded = api.item(created.id, withLinks = true)
            assertEquals("RAII", loaded.item.title)
            assertEquals("Term", loaded.item.itemTypeName.removeSuffix(" (All groups)"))
            assertEquals("hard", loaded.item.fieldValue(difficultyId))
            assertEquals(hash, loaded.item.fieldValue(attachmentId))
            assertEquals("# RAII\n\nČeština and ✓ survive UTF-8.", loaded.item.content)
            assertEquals(LinkType.DependsOn, loaded.links.single().linkType)
            assertEquals(target.id, loaded.links.single().toItemId)
            assertEquals("acquires in", loaded.backlinks.single().customValue)
            assertEquals(source.id, loaded.backlinks.single().fromItemId)

            val downloaded = ByteArrayOutputStream()
            api.downloadBlob(loaded.item.fieldValue(attachmentId), { downloaded }) { _, _ -> }
            assertArrayEquals(bytes, downloaded.toByteArray())

            // The complete link state: dropping the backlink deletes it.
            val update = editor.copy(
                title = "RAII idiom",
                backlinks = emptyList(),
                links = listOf(LinkEntry(1, loaded.links.single().id, target.id, "Object lifetime", LinkType.Related, "", 3)),
            )
            api.updateItem(created.id, EditorRules.saveRequest(update, "Updated.", fields))
            val updated = api.item(created.id, withLinks = true)
            assertEquals("RAII idiom", updated.item.title)
            assertTrue(updated.backlinks.isEmpty())
            assertEquals(LinkType.Related, updated.links.single().linkType)
            assertEquals(loaded.links.single().id, updated.links.single().id)

            // The server stays authoritative about values and duplicates.
            try {
                api.updateItem(created.id, EditorRules.saveRequest(update.copy(values = mapOf(yearId to "nineteen")), "", fields))
                fail("expected Validation")
            } catch (failure: ApiException.Validation) {
                assertTrue(failure.message!!.contains("Invalid value"))
            }
            try {
                api.createItem(EditorRules.saveRequest(EditorFields(groupId = group.id, title = "Object lifetime"), "", emptyList()))
                fail("expected Validation")
            } catch (failure: ApiException.Validation) {
                assertTrue(failure.message!!.contains("Object lifetime"))
            }

            // Server-side query semantics, as the filter sheet builds them.
            val query = buildItemQuery(
                "idiom",
                ItemFilters(
                    groupId = group.id, typeId = type.id, tag = "cpp", pinned = true, status = ItemStatus.Draft,
                    values = mapOf(difficultyId to "hard"), properties = listOf(PropertyFilter("SOURCE", "strou")),
                ),
                fields, SortColumns.TITLE, SortOrder.Ascending, 20, 0,
            )
            assertEquals(listOf(ValueFilter(difficultyId, "hard", true)), query.valueFilters)
            val page = api.queryItems(query)
            assertEquals(1, page.totalCount)
            assertEquals(created.id, page.items.single().id)
            assertEquals(0, api.queryItems(query.copy(tagFilter = "cp")).totalCount)
            val paged = api.queryItems(ItemQuery(groupId = group.id, limit = 2, offset = 0, sortColumn = SortColumns.TITLE))
            val second = api.queryItems(ItemQuery(groupId = group.id, limit = 2, offset = 2, sortColumn = SortColumns.TITLE))
            assertEquals(3, paged.totalCount)
            assertEquals(3, (paged.items + second.items).map { it.id }.distinct().size)

            // Overviews and destructive-change counts.
            assertEquals(1, api.tagUsage().single { it.value == "cpp" }.usageCount)
            assertEquals(1, api.typeItemCount(typeId))
            assertEquals(1, api.fieldValueCount(difficultyId))

            api.logItemRead(created.id)
            api.deleteItem(created.id)
            try {
                api.item(created.id)
                fail("expected NotFound")
            } catch (_: ApiException.NotFound) {
            }

            // Logout ends the session on the server too.
            val stale = sessions.current!!
            sessions.logout().join()
            assertNull(environment.tokenStore.load())
            try {
                api.me(stale)
                fail("expected Unauthorized")
            } catch (_: ApiException.Unauthorized) {
            }
        } finally {
            environment.close()
        }
    }

    @Test
    fun aSaveBasedOnAnOldRevisionIsAConflict() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            val groupId = api.defaultGroupId()
            val created = api.createItem(EditorRules.saveRequest(EditorFields(groupId = groupId, title = "Conflict ${System.nanoTime()}"), "", emptyList()))
            val opened = EditorFields(groupId = groupId, title = created.item.title, revision = created.item.revision)
            val first = api.updateItem(created.id, EditorRules.saveRequest(opened, "From the phone.", emptyList()))
            assertTrue(first.item.revision > created.item.revision)
            try {
                api.updateItem(created.id, EditorRules.saveRequest(opened, "From the desktop.", emptyList()))
                fail("expected a conflict")
            } catch (_: ApiException.Conflict) {
            }
            val newer = api.item(created.id, withLinks = true)
            assertEquals("From the phone.", newer.item.content)
            assertEquals(listOf("Content"), EditorRules.conflictDifferences(opened, "From the desktop.", newer))
            val overwritten = api.updateItem(created.id, EditorRules.saveRequest(EditorRules.overwriting(opened, newer), "From the desktop.", emptyList()))
            assertEquals("From the desktop.", overwritten.item.content)
            api.deleteItem(created.id)
        } finally {
            environment.close()
        }
    }

    @Test
    fun theDictionaryExportsAndImportsOverRealRest() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            val title = "Exported ${System.nanoTime()}"
            val created = api.createItem(EditorRules.saveRequest(EditorFields(groupId = api.defaultGroupId(), title = title), "Content.", emptyList()))
            val document = ByteArrayOutputStream()
            api.exportDictionary(includeFiles = true, output = { document }, onProgress = { _, _ -> })
            val text = document.toString(Charsets.UTF_8)
            assertTrue(text.contains("\"format\": \"lexicon-export\""))
            assertTrue(text.contains(title))
            val bytes = document.toByteArray()
            val report = api.importDictionary(bytes.size.toLong(), { ByteArrayInputStream(bytes) }) { _, _ -> }
            assertEquals(0, report.itemsCreated)
            assertTrue(report.itemsSkipped >= 1)
            api.deleteItem(created.id)
            val restored = api.importDictionary(bytes.size.toLong(), { ByteArrayInputStream(bytes) }) { _, _ -> }
            assertEquals(1, restored.itemsCreated)
            assertEquals(title, api.item(api.resolveItem(title, "")).item.title)
        } finally {
            environment.close()
        }
    }

    @Test
    fun reviewOverRealRest() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            val groupId = api.defaultGroupId()
            val title = "Reviewed ${System.nanoTime()}"
            val created = api.createItem(EditorRules.saveRequest(EditorFields(groupId = groupId, title = title), "", emptyList()))
            val before = api.reviewQueue(groupId, 200)
            assertTrue(before.items.any { it.id == created.id })
            val reviewed = api.reviewItem(created.id, ReviewRating.Easy)
            assertEquals(ReviewRating.Easy.levelAfter(UnderstandingLevel.Unknown), reviewed.understanding)
            val reviewedAt = checkNotNull(reviewed.reviewedAt)
            assertTrue(reviewedAt.endsWith("Z"))
            assertTrue(checkNotNull(reviewed.reviewDueAt) > reviewedAt)
            val after = api.reviewQueue(groupId, 200)
            assertTrue(after.items.none { it.id == created.id })
            assertEquals(before.dueCount - 1, after.dueCount)
            api.deleteItem(created.id)
        } finally {
            environment.close()
        }
    }

    @Test
    fun alarmsOverRealRest() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            val title = "Dentist ${System.nanoTime()}"
            val created = api.createAlarm(AlarmWrite(" $title ", "Bring the card.", "2030-01-02T09:15:00Z"))
            val id = checkNotNull(created.id)
            assertEquals(title, created.title)
            assertTrue(api.alarms().any { it.id == id && it.firesAt == "2030-01-02T09:15:00Z" })
            val moved = api.updateAlarm(id, AlarmWrite(title, "", "2030-01-03T08:00:00Z"))
            assertEquals("2030-01-03T08:00:00Z", moved.firesAt)
            assertEquals("", moved.description)
            try {
                api.createAlarm(AlarmWrite("Never", "", "2030-02-30T08:00:00Z"))
                fail("expected a refusal")
            } catch (failure: ApiException.Validation) {
                assertTrue(failure.message!!.contains("UTC"))
            }
            api.deleteAlarm(id)
            assertTrue(api.alarms().none { it.id == id })
        } finally {
            environment.close()
        }
    }

    @Test
    fun imagesOverRealRest() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            val groupId = api.defaultGroupId()
            val type = api.createType(TypeWrite("Figure ${System.nanoTime()}", groupId = groupId))
            val field = api.createField(checkNotNull(type.id), FieldWrite("Diagram", FieldDataType.Image))
            assertEquals(FieldDataType.Image, field.dataType)
            val png = TestImages.png(12, 8)
            val uploaded = api.uploadBlobDetailed(png.size.toLong(), { ByteArrayInputStream(png) }) { _, _ -> }
            assertEquals("image/png", uploaded.mediaType)
            val text = "no picture".toByteArray()
            assertNull(api.uploadBlobDetailed(text.size.toLong(), { ByteArrayInputStream(text) }) { _, _ -> }.mediaType)
            val value = ImageValues.format("image/png", uploaded.hash)
            val fieldKey = checkNotNull(field.id)
            val created = api.createItem(
                EditorRules.saveRequest(
                    EditorFields(groupId = groupId, typeId = type.id, title = "Pipeline ${System.nanoTime()}", values = mapOf(fieldKey to value)),
                    "",
                    listOf(field),
                ),
            )
            assertEquals(value, api.item(created.id).item.fieldValue(fieldKey))
            assertArrayEquals(png, api.blobBytes(uploaded.hash))
            try {
                api.createItem(
                    EditorRules.saveRequest(
                        EditorFields(groupId = groupId, typeId = type.id, title = "Wrong ${System.nanoTime()}", values = mapOf(fieldKey to "image/gif:${uploaded.hash}")),
                        "",
                        listOf(field),
                    ),
                )
                fail("expected a refusal")
            } catch (failure: ApiException.Validation) {
                assertTrue(failure.message!!.contains("image/png, not image/gif"))
            }
            api.deleteItem(created.id)
            api.deleteType(checkNotNull(type.id))
        } finally {
            environment.close()
        }
    }

    @Test
    fun anOversizedBlobIsRefused() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            val bytes = ByteArray(MAX_BLOB_BYTES + 50_000)
            try {
                api.uploadBlob(bytes.size.toLong(), { ByteArrayInputStream(bytes) }) { _, _ -> }
                fail("expected a refusal")
            } catch (failure: ApiException.PayloadTooLarge) {
                assertTrue(failure.message!!.contains("--max-blob-bytes"))
            }
        } finally {
            environment.close()
        }
    }

    @Test
    fun aSessionEndedOnTheServerEndsInTheLoginScreen() = runBlocking {
        val environment = TestEnvironment()
        try {
            val (sessions, api) = newSessionManager(environment)
            assertEquals(SessionManager.LoginResult.Success, sessions.login(baseUrl, USER, PASSWORD))
            // A session the server ended - here by a logout elsewhere - is simply unknown.
            val session = sessions.current!!
            api.logout(session)
            try {
                api.groups()
                fail("expected Unauthorized")
            } catch (_: ApiException.Unauthorized) {
            }
            assertTrue(sessions.state.value is SessionState.SignedOut)
            assertNull(sessions.current)
        } finally {
            environment.close()
        }
    }
}
