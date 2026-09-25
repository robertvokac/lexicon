package com.robertvokac.lexicon.inbox

import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import com.robertvokac.lexicon.api.LexiconJson
import com.robertvokac.lexicon.model.ColumnFilters
import com.robertvokac.lexicon.model.ItemQuery
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.builtins.ListSerializer
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.Files
import java.nio.file.NoSuchFileException
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.time.Instant
import java.time.temporal.ChronoUnit
import java.util.UUID

/** An Inbox idea caught while the server could not be reached. */
@Serializable
data class PendingIdea(
    val id: String,
    val title: String,
    val content: String = "",
    /** UTC, when it was caught. */
    val createdAt: String,
    /** The server and account it belongs to: it is never sent anywhere else. */
    val server: String,
    val username: String,
    /** Why the server refused it; it waits for an edit instead of being sent again. */
    val problem: String? = null,
)

/**
 * Inbox ideas kept on this phone until the server takes them: caught offline,
 * with the server away, or with the session expired. They live in one file of
 * app-private storage that is never backed up, and are sent oldest first as
 * soon as the account they belong to is signed in with the server in reach.
 */
class IdeaOutbox(
    private val file: File,
    private val replaceFile: (Path, Path) -> Unit = { source, target ->
        Files.move(source, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
    },
    private val api: () -> LexiconApi,
) {
    private val mutex = Mutex()
    private val loaded = runCatching { read() }
    private val _ideas = MutableStateFlow(loaded.getOrDefault(emptyList()))
    val ideas: StateFlow<List<PendingIdea>> = _ideas.asStateFlow()
    val storageError: String? = loaded.exceptionOrNull()?.let {
        "Offline Inbox could not be read. Its file is preserved; new ideas cannot be saved on this phone yet."
    }

    private val _delivered = MutableStateFlow(0)

    /**
     * Ideas delivered that nobody has told the person about yet: a flush at
     * sign-in may run before any screen listens.
     */
    val delivered: StateFlow<Int> = _delivered.asStateFlow()

    /** The screen has said how many ideas arrived. */
    fun deliveredShown() = _delivered.update { 0 }

    fun waitingFor(server: String, username: String): List<PendingIdea> =
        _ideas.value.filter { it.server == server && it.username == username }

    suspend fun add(title: String, content: String, server: String, username: String): PendingIdea = mutex.withLock {
        val idea = PendingIdea(
            id = UUID.randomUUID().toString(),
            title = title.trim(),
            content = content,
            createdAt = Instant.now().truncatedTo(ChronoUnit.SECONDS).toString(),
            server = server,
            username = username,
        )
        save(_ideas.value + idea)
        idea
    }

    /** A changed idea is sent again at the next chance. */
    suspend fun update(id: String, title: String, content: String) = mutex.withLock {
        save(_ideas.value.map { if (it.id == id) it.copy(title = title.trim(), content = content, problem = null) else it })
    }

    suspend fun remove(id: String) = mutex.withLock { save(_ideas.value.filter { it.id != id }) }

    /**
     * Sends the waiting ideas of this account, oldest first, and returns how
     * many arrived. It stops at the first sign the server is still away; an
     * idea the server refuses keeps the reason and waits for an edit.
     */
    suspend fun flush(server: String, username: String): Int = mutex.withLock {
        val waiting = _ideas.value.filter { it.server == server && it.username == username && it.problem == null }
        if (waiting.isEmpty()) return@withLock 0
        var delivered = 0
        try {
            val groupId = api().defaultGroupId()
            for (idea in waiting) {
                val outcome = try {
                    deliver(groupId, idea)
                } catch (failure: ApiException) {
                    if (keepsForLater(failure)) break
                    idea.copy(problem = failure.message ?: "The server refused this idea.")
                }
                if (outcome == null) {
                    delivered++
                    save(_ideas.value.filter { it.id != idea.id })
                } else {
                    save(_ideas.value.map { if (it.id == idea.id) outcome else it })
                }
            }
        } catch (failure: ApiException) {
            if (!keepsForLater(failure)) throw failure
        }
        if (delivered > 0) _delivered.update { it + delivered }
        delivered
    }

    /** Null once the idea is in the dictionary; otherwise the idea with why it is not. */
    private suspend fun deliver(groupId: Int, idea: PendingIdea): PendingIdea? {
        // A save whose answer was lost on the way is already there: sending
        // it again would only be refused as a duplicate.
        val sameTitle = api().queryItems(ItemQuery(groupId = groupId, columnFilters = ColumnFilters(title = idea.title), limit = 50))
            .items.filter { it.title == idea.title && it.disambiguation.isEmpty() }
        for (existing in sameTitle) {
            val id = existing.id ?: continue
            if (api().item(id).item.content == idea.content) return null
        }
        if (sameTitle.isNotEmpty()) {
            return idea.copy(problem = "An item titled '${idea.title}' is already in Default. Change the title to send this idea.")
        }
        api().captureIdea(idea.title, idea.content)
        return null
    }

    private fun read(): List<PendingIdea> {
        // Recover a complete staging file left by an older app version if its
        // delete-then-rename fallback removed the original before a crash.
        val legacyStaging = File(file.parentFile, "${file.name}.new")
        fun readIfPresent(candidate: File): String? = try {
            Files.readAllBytes(candidate.toPath()).toString(Charsets.UTF_8)
        } catch (_: NoSuchFileException) {
            null
        }
        val contents = readIfPresent(file) ?: readIfPresent(legacyStaging) ?: return emptyList()
        return LexiconJson.decodeFromString(ListSerializer(PendingIdea.serializer()), contents)
    }

    // Never delete the previous list. A failed write or rename leaves it and
    // the in-memory list unchanged. Both files are on the same filesystem.
    private suspend fun save(ideas: List<PendingIdea>) {
        if (storageError != null)
            throw IOException(storageError, loaded.exceptionOrNull())
        withContext(Dispatchers.IO) {
            val parent = file.absoluteFile.parentFile ?: throw IOException("Offline Inbox has no parent directory.")
            Files.createDirectories(parent.toPath())
            val staged = Files.createTempFile(parent.toPath(), ".${file.name}-", ".tmp")
            try {
                val bytes = LexiconJson.encodeToString(ListSerializer(PendingIdea.serializer()), ideas)
                    .toByteArray(Charsets.UTF_8)
                FileOutputStream(staged.toFile()).use { output ->
                    output.write(bytes)
                    output.fd.sync()
                }
                replaceFile(staged, file.toPath())
                _ideas.value = ideas
            } finally {
                Files.deleteIfExists(staged)
            }
        }
    }

    companion object {
        /**
         * True when the failure says nothing about the idea itself - no
         * connection, the server or a proxy down, no session - so it is kept
         * and sent later. A refusal of the idea itself is not.
         */
        fun keepsForLater(failure: ApiException): Boolean = when (failure) {
            is ApiException.Validation, is ApiException.Conflict, is ApiException.PayloadTooLarge,
            is ApiException.Forbidden, is ApiException.NotFound,
            -> false
            else -> true
        }
    }
}
