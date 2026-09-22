package com.robertvokac.lexicon.api

import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.model.AlarmEnvelope
import com.robertvokac.lexicon.model.AlarmWrite
import com.robertvokac.lexicon.model.AlarmsEnvelope
import com.robertvokac.lexicon.model.CountEnvelope
import com.robertvokac.lexicon.model.DefaultGroupEnvelope
import com.robertvokac.lexicon.model.FieldEnvelope
import com.robertvokac.lexicon.model.FieldWrite
import com.robertvokac.lexicon.model.FieldsEnvelope
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.GroupEnvelope
import com.robertvokac.lexicon.model.GroupWrite
import com.robertvokac.lexicon.model.GroupsEnvelope
import com.robertvokac.lexicon.model.Health
import com.robertvokac.lexicon.model.ImportEnvelope
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemEnvelope
import com.robertvokac.lexicon.model.ItemGraph
import com.robertvokac.lexicon.model.ReviewQueue
import com.robertvokac.lexicon.model.ReviewRating
import com.robertvokac.lexicon.model.ReviewRequest
import com.robertvokac.lexicon.model.ImportReport
import com.robertvokac.lexicon.model.ItemBundle
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemIdEnvelope
import com.robertvokac.lexicon.model.ItemPage
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemType
import com.robertvokac.lexicon.model.LoginRequest
import com.robertvokac.lexicon.model.LoginResponse
import com.robertvokac.lexicon.model.Me
import com.robertvokac.lexicon.model.SaveItemRequest
import com.robertvokac.lexicon.model.SavedItem
import com.robertvokac.lexicon.model.StringsEnvelope
import com.robertvokac.lexicon.model.TypeEnvelope
import com.robertvokac.lexicon.model.TypeWrite
import com.robertvokac.lexicon.model.TypesEnvelope
import com.robertvokac.lexicon.model.UploadedBlob
import com.robertvokac.lexicon.model.UsageEnvelope
import com.robertvokac.lexicon.model.UsageValue
import java.io.InputStream
import java.io.OutputStream

/** Typed access to every /api/v1 endpoint the app uses. */
class LexiconApi(private val client: ApiClient) {

    // Health and session ------------------------------------------------------

    suspend fun health(server: ServerUrl): Health =
        client.get("health", Health.serializer(), ApiClient.Auth.Anonymous(server))

    suspend fun login(server: ServerUrl, username: String, password: String): LoginResponse =
        client.post(
            "auth/login",
            LoginRequest(username, password),
            LoginRequest.serializer(),
            LoginResponse.serializer(),
            ApiClient.Auth.Anonymous(server),
        )

    suspend fun logout(session: Session) = client.send("POST", "auth/logout", ApiClient.Auth.Explicit(session))

    suspend fun me(session: Session): Me =
        client.get("auth/me", Me.serializer(), ApiClient.Auth.Explicit(session))

    // Groups ------------------------------------------------------------------

    suspend fun groups(): List<Group> = client.get("groups", GroupsEnvelope.serializer()).groups

    suspend fun defaultGroupId(): Int = client.get("groups/default", DefaultGroupEnvelope.serializer()).groupId

    suspend fun createGroup(group: GroupWrite): Group =
        client.post("groups", group, GroupWrite.serializer(), GroupEnvelope.serializer()).group

    suspend fun updateGroup(id: Int, group: GroupWrite): Group =
        client.put("groups/$id", group, GroupWrite.serializer(), GroupEnvelope.serializer()).group

    suspend fun deleteGroup(id: Int) = client.send("DELETE", "groups/$id")

    // Types and fields --------------------------------------------------------

    /** The types usable in [groupId], which include those available in all groups. */
    suspend fun types(groupId: Int? = null): List<ItemType> =
        client.get(
            "types",
            TypesEnvelope.serializer(),
            query = if (groupId != null) mapOf("groupId" to groupId.toString()) else emptyMap(),
        ).types

    suspend fun createType(type: TypeWrite): ItemType =
        client.post("types", type, TypeWrite.serializer(), TypeEnvelope.serializer()).type

    suspend fun updateType(id: Int, type: TypeWrite): ItemType =
        client.put("types/$id", type, TypeWrite.serializer(), TypeEnvelope.serializer()).type

    suspend fun deleteType(id: Int) = client.send("DELETE", "types/$id")

    suspend fun typeItemCount(id: Int): Int = client.get("types/$id/item-count", CountEnvelope.serializer()).count

    suspend fun fields(typeId: Int): List<ItemField> =
        client.get("types/$typeId/fields", FieldsEnvelope.serializer()).fields

    suspend fun createField(typeId: Int, field: FieldWrite): ItemField =
        client.post("types/$typeId/fields", field, FieldWrite.serializer(), FieldEnvelope.serializer()).field

    suspend fun updateField(id: Int, field: FieldWrite): ItemField =
        client.put("fields/$id", field, FieldWrite.serializer(), FieldEnvelope.serializer()).field

    suspend fun deleteField(id: Int) = client.send("DELETE", "fields/$id")

    suspend fun fieldValueCount(id: Int): Int =
        client.get("fields/$id/value-count", CountEnvelope.serializer()).count

    // Items -------------------------------------------------------------------

    suspend fun queryItems(query: ItemQuery): ItemPage =
        client.post("items/query", query, ItemQuery.serializer(), ItemPage.serializer())

    suspend fun item(id: Int, withLinks: Boolean = false): ItemBundle =
        client.get(
            "items/$id",
            ItemBundle.serializer(),
            query = if (withLinks) mapOf("include" to "links,backlinks") else emptyMap(),
        )

    /** Creates the item and its links in one unit of work. */
    suspend fun createItem(request: SaveItemRequest): SavedItem =
        client.post("items", request, SaveItemRequest.serializer(), SavedItem.serializer())

    /** Saves the item and the complete state of both link directions in one unit of work. */
    suspend fun updateItem(id: Int, request: SaveItemRequest): SavedItem =
        client.put("items/$id", request, SaveItemRequest.serializer(), SavedItem.serializer())

    suspend fun deleteItem(id: Int) = client.send("DELETE", "items/$id")

    /** Records a read in the server's log. */
    suspend fun logItemRead(id: Int) = client.send("POST", "items/$id/read")

    suspend fun resolveItem(title: String, disambiguation: String): Int =
        client.get(
            "items/resolve",
            ItemIdEnvelope.serializer(),
            query = buildMap {
                put("title", title)
                if (disambiguation.isNotEmpty()) put("disambiguation", disambiguation)
            },
        ).itemId

    // Search and usage --------------------------------------------------------

    suspend fun suggestions(): List<String> = client.get("search/suggestions", StringsEnvelope.serializer()).values

    suspend fun itemTitles(): List<String> = client.get("search/item-titles", StringsEnvelope.serializer()).values

    suspend fun tagUsage(): List<UsageValue> = client.get("usage/tags", UsageEnvelope.serializer()).values

    suspend fun flagUsage(): List<UsageValue> = client.get("usage/flags", UsageEnvelope.serializer()).values

    suspend fun aliasUsage(): List<UsageValue> = client.get("usage/aliases", UsageEnvelope.serializer()).values

    // Blobs -------------------------------------------------------------------

    /** Uploads the bytes and returns the SHA-256 to store in a Blob field. */
    suspend fun uploadBlob(size: Long, open: () -> InputStream, onProgress: (Long, Long) -> Unit): String =
        uploadBlobDetailed(size, open, onProgress).hash

    /** Uploads the bytes; the answer also says whether they are an image, and which kind. */
    suspend fun uploadBlobDetailed(size: Long, open: () -> InputStream, onProgress: (Long, Long) -> Unit): UploadedBlob =
        client.upload("blobs", size, open, onProgress, UploadedBlob.serializer())

    /** A stored file in memory, for pictures; larger than [limit] bytes is refused. */
    suspend fun blobBytes(hash: String, limit: Int = 48 * 1024 * 1024): ByteArray {
        val bytes = java.io.ByteArrayOutputStream()
        downloadBlob(hash, output = { bytes }) { received, _ ->
            if (received > limit) throw ApiException.PayloadTooLarge("The image is too large to show.")
        }
        return bytes.toByteArray()
    }

    suspend fun downloadBlob(hash: String, output: () -> OutputStream, onProgress: (Long, Long) -> Unit) {
        require(isBlobHash(hash)) { "A blob is addressed by its lowercase SHA-256 hash." }
        client.download("blobs/$hash", output, onProgress)
    }

    suspend fun itemGraph(id: Int, depth: Int = 2, limit: Int = 100): ItemGraph =
        client.get("items/$id/graph", ItemGraph.serializer(), query = mapOf("depth" to depth.toString(), "limit" to limit.toString()))

    // Alarms ------------------------------------------------------------------

    /** Every alarm, the soonest first. */
    suspend fun alarms(): List<Alarm> = client.get("alarms", AlarmsEnvelope.serializer()).alarms

    suspend fun createAlarm(alarm: AlarmWrite): Alarm =
        client.post("alarms", alarm, AlarmWrite.serializer(), AlarmEnvelope.serializer()).alarm

    suspend fun updateAlarm(id: Int, alarm: AlarmWrite): Alarm =
        client.put("alarms/$id", alarm, AlarmWrite.serializer(), AlarmEnvelope.serializer()).alarm

    suspend fun deleteAlarm(id: Int) = client.send("DELETE", "alarms/$id")

    // Review ------------------------------------------------------------------

    suspend fun reviewQueue(groupId: Int?, limit: Int = 20): ReviewQueue =
        client.get(
            "review",
            ReviewQueue.serializer(),
            query = buildMap {
                put("limit", limit.toString())
                if (groupId != null) put("groupId", groupId.toString())
            },
        )

    /** Records a review; the answer is the item with its new understanding and next review. */
    suspend fun reviewItem(id: Int, rating: ReviewRating): Item =
        client.post("items/$id/review", ReviewRequest(rating), ReviewRequest.serializer(), ItemEnvelope.serializer()).item

    // Export and import -------------------------------------------------------

    /** Streams the whole dictionary, as the documented export file, into [output]. */
    suspend fun exportDictionary(includeFiles: Boolean, output: () -> OutputStream, onProgress: (Long, Long) -> Unit) =
        client.download("export", output, onProgress, query = mapOf("blobs" to includeFiles.toString()), accept = "application/json")

    /** Merges an export into the server's dictionary. The server needs its size up front. */
    suspend fun importDictionary(size: Long, open: () -> InputStream, onProgress: (Long, Long) -> Unit): ImportReport =
        client.upload("import", size, open, onProgress, ImportEnvelope.serializer(), json = true).report

    companion object {
        const val API_VERSION = 1

        private val blobHash = Regex("^[0-9a-f]{64}$")

        fun isBlobHash(value: String): Boolean = blobHash.matches(value)
    }
}
