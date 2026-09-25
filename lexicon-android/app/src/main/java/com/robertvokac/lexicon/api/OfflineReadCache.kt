package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.auth.SecretCipher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.file.Files
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.StandardCopyOption
import java.security.MessageDigest

/** Encrypted, app-private copies of successful read responses for one account. */
class OfflineReadCache(private val directory: File, private val cipher: SecretCipher) {
    private fun digest(text: String): String = MessageDigest.getInstance("SHA-256")
        .digest(text.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }

    private fun identity(session: Session): String = digest("${session.server.value}\n${session.username}")
    private fun file(session: Session, request: String): File =
        File(directory, "${identity(session)}-${digest(request)}.cache")

    fun hasIdentity(server: ServerUrl, username: String): Boolean {
        val prefix = digest("${server.value}\n$username") + "-"
        return directory.listFiles()?.any { it.name.startsWith(prefix) && it.name.endsWith(".cache") } == true
    }

    suspend fun read(session: Session, request: String): String? = withContext(Dispatchers.IO) {
        val file = file(session, request)
        runCatching {
            val sealed = file.readBytes()
            cipher.decrypt(sealed, "${identity(session)}\n$request".toByteArray(Charsets.UTF_8)).toString(Charsets.UTF_8)
        }.getOrNull()
    }

    suspend fun write(session: Session, request: String, response: String) = withContext(Dispatchers.IO) {
        if (response.length > MAX_RESPONSE_CHARS) return@withContext
        runCatching {
            directory.mkdirs()
            val target = file(session, request)
            val sealed = cipher.encrypt(response.toByteArray(Charsets.UTF_8),
                "${identity(session)}\n$request".toByteArray(Charsets.UTF_8))
            val temporary = File.createTempFile(".read-", ".tmp", directory)
            try {
                temporary.writeBytes(sealed)
                try {
                    Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING)
                } catch (_: AtomicMoveNotSupportedException) {
                    Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
                }
            } finally {
                temporary.delete()
            }
            val entries = directory.listFiles { candidate -> candidate.name.endsWith(".cache") }
                ?.sortedByDescending { it.lastModified() }.orEmpty()
            entries.drop(MAX_ENTRIES).forEach { it.delete() }
        }
        Unit
    }

    private companion object {
        const val MAX_RESPONSE_CHARS = 2_000_000
        const val MAX_ENTRIES = 500
    }
}
