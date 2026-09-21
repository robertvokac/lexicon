package com.robertvokac.lexicon.ui.item

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import com.robertvokac.lexicon.api.ApiException
import com.robertvokac.lexicon.api.LexiconApi
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import java.io.FileNotFoundException
import java.util.Locale

/**
 * Moves Blob field bytes between documents the person picked through the
 * Storage Access Framework and the server. No file system path is ever used
 * and no storage permission is needed: the picker grants access to exactly
 * the chosen document.
 */
class BlobTransfer(private val api: LexiconApi, private val resolver: ContentResolver) {
    data class Document(val name: String, val size: Long)

    /** The display name and size of a picked document, when the provider knows them. */
    suspend fun describe(uri: Uri): Document = withContext(Dispatchers.IO) {
        var name = uri.lastPathSegment.orEmpty()
        var size = -1L
        resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val nameColumn = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                val sizeColumn = cursor.getColumnIndex(OpenableColumns.SIZE)
                if (nameColumn >= 0 && !cursor.isNull(nameColumn)) name = cursor.getString(nameColumn)
                if (sizeColumn >= 0 && !cursor.isNull(sizeColumn)) size = cursor.getLong(sizeColumn)
            }
        }
        Document(name, size)
    }

    /** Streams the document to the server and returns its SHA-256. */
    suspend fun upload(uri: Uri, size: Long, onProgress: (Long, Long) -> Unit): String {
        try {
            return api.uploadBlob(
                size = size,
                open = { resolver.openInputStream(uri) ?: throw FileNotFoundException("The document cannot be read.") },
                onProgress = onProgress,
            )
        } catch (failure: ApiException.Network) {
            // A server that refuses an oversized body may close the connection
            // before the answer can be read.
            throw ApiException.Network(
                "The upload was interrupted. If the file is large, it may exceed the server's --max-blob-bytes limit.",
                failure,
            )
        }
    }

    /**
     * Writes the blob into the document the person created. A document left
     * incomplete by a failure or a cancel is deleted again.
     */
    suspend fun download(hash: String, target: Uri, onProgress: (Long, Long) -> Unit) {
        try {
            api.downloadBlob(
                hash,
                output = { resolver.openOutputStream(target, "wt") ?: throw FileNotFoundException("The document cannot be written.") },
                onProgress = onProgress,
            )
        } catch (failure: Throwable) {
            withContext(NonCancellable + Dispatchers.IO) {
                runCatching { DocumentsContract.deleteDocument(resolver, target) }
            }
            if (failure is CancellationException || failure is ApiException) throw failure
            throw ApiException.Network("The file could not be written: ${failure.message}", failure)
        }
    }

    companion object {
        /** A file name for Save as: the field name and the start of the hash. */
        fun suggestedName(fieldName: String, hash: String): String {
            val safe = fieldName.replace(Regex("[^A-Za-z0-9._-]+"), "_").trim('_').ifEmpty { "blob" }
            return "$safe-${hash.take(12)}.bin"
        }

        fun formatSize(bytes: Long): String = when {
            bytes < 0 -> "unknown size"
            bytes < 1024 -> "$bytes bytes"
            bytes < 1024 * 1024 -> String.format(Locale.ROOT, "%.1f KB", bytes / 1024.0)
            else -> String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0))
        }
    }
}
