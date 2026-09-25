package com.robertvokac.lexicon.testing

import com.robertvokac.lexicon.api.LexiconJson
import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.model.Card
import com.robertvokac.lexicon.model.FieldDataType
import com.robertvokac.lexicon.model.ImageValues
import com.robertvokac.lexicon.model.Group
import com.robertvokac.lexicon.model.Item
import com.robertvokac.lexicon.model.ItemField
import com.robertvokac.lexicon.model.ItemQuery
import com.robertvokac.lexicon.model.ItemType
import com.robertvokac.lexicon.model.Link
import com.robertvokac.lexicon.model.LinkType
import com.robertvokac.lexicon.model.ReviewRating
import com.robertvokac.lexicon.model.SortColumns
import com.robertvokac.lexicon.model.SortOrder
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.serializer
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import mockwebserver3.Dispatcher
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import mockwebserver3.RecordedRequest
import java.security.MessageDigest
import java.util.concurrent.CopyOnWriteArrayList

/** The type every Inbox idea gets, as on the real server. */
const val INBOX_TYPE = "Inbox"

/**
 * An in-memory stand-in for LexiconServer, for tests only. It speaks the
 * /api/v1 JSON shapes of docs/rest-api.md closely enough to drive the app's
 * screens; the real server's own tests cover its semantics.
 */
class FakeLexiconServer : Dispatcher() {
    val server = MockWebServer()
    val requests = CopyOnWriteArrayList<RecordedRequest>()

    var apiVersion = 1
    var username = "robert"
    var password = "correct horse battery staple"
    val tokens = mutableSetOf<String>()
    val refreshSecrets = mutableMapOf<String, String>()
    private var nextToken = 1

    val groups = mutableListOf(Group(1, "Default", "Default group for new items when no group is selected.", 0))
    val types = mutableListOf<ItemType>()
    val fields = mutableListOf<ItemField>()
    val items = linkedMapOf<Int, Item>()
    val links = mutableListOf<Link>()
    val reads = CopyOnWriteArrayList<Int>()
    val blobs = mutableMapOf<String, ByteArray>()

    /** What GET /export answers, and the bodies POST /import received. */
    var exportDocument = """{"format":"lexicon-export","version":1,"groups":[],"types":[],"items":[],"links":[]}"""
    val imports = mutableListOf<ByteArray>()
    val reviews = mutableListOf<Pair<Int, ReviewRating>>()
    val alarms = mutableListOf<Alarm>()

    /** Every item's cards, in the order they were added. */
    val cards = mutableListOf<Card>()

    /** What the server's clock says when a card is attempted. */
    var attemptTime = "2026-09-24T14:00:00Z"

    /** Card attempts answer only after this long. */
    @Volatile var attemptDelayMillis = 0L

    /** Card attempts answer 503, like a server that is briefly away. */
    @Volatile var attemptsFail = false

    /** Dismiss and snooze answer 503, like a server that is briefly away. */
    @Volatile var alarmActionsFail = false
    private var nextId = 100

    /** Queries with this search text answer only after [slowQueryMillis]. */
    @Volatile var slowQuery: String? = null
    @Volatile var slowQueryMillis = 1_500L

    /** Blob uploads answer only after this long. */
    @Volatile var blobDelayMillis = 0L

    fun start(): FakeLexiconServer {
        server.dispatcher = this
        server.start()
        return this
    }

    override fun close() = server.close()

    val baseUrl: String get() = server.url("/").toString().trimEnd('/')

    fun addItem(item: Item): Item {
        val id = item.id ?: nextId++
        val group = groups.first { it.id == (item.groupId ?: 1) }
        val stored = item.copy(id = id, groupId = group.id, groupName = group.name, revision = maxOf(1, item.revision))
        items[id] = stored
        return stored
    }

    /** Another client saves [id]: its revision moves on. */
    fun changeElsewhere(id: Int, change: (Item) -> Item) = synchronized(this) {
        val item = items.getValue(id)
        items[id] = change(item).copy(revision = item.revision + 1)
    }

    fun addType(name: String, groupId: Int? = null): ItemType {
        val type = ItemType(nextId++, groupId, groups.firstOrNull { it.id == groupId }?.name.orEmpty(), name, "")
        types += type
        return type
    }

    fun addField(typeId: Int, name: String, dataType: FieldDataType, options: List<String> = emptyList()): ItemField {
        val field = ItemField(nextId++, typeId, name, dataType, fields.count { it.itemTypeId == typeId }, options)
        fields += field
        return field
    }

    fun addCard(itemId: Int, question: String, answer: String, successCount: Long = 0, failureCount: Long = 0, lastAttempt: String? = null): Card =
        synchronized(this) {
            Card(nextId++, itemId, question, answer, successCount, failureCount, lastAttempt).also { cards += it }
        }

    fun card(id: Int): Card = synchronized(this) { cards.single { it.id == id } }

    fun requestsTo(method: String, path: String): List<RecordedRequest> =
        requests.filter { it.method == method && it.url.encodedPath == path }

    /** Every request answers 503, like a server behind a proxy that is down. */
    @Volatile var unavailable = false

    override fun dispatch(request: RecordedRequest): MockResponse {
        if (unavailable) {
            requests += request
            return error(503, "unavailable", "The server is not available.")
        }
        if (request.url.encodedPath.endsWith("/blobs") && blobDelayMillis > 0) Thread.sleep(blobDelayMillis)
        if (request.url.encodedPath.endsWith("/attempt") && attemptDelayMillis > 0) Thread.sleep(attemptDelayMillis)
        val slow = slowQuery
        if (slow != null && request.url.encodedPath.endsWith("/items/query") && request.body?.utf8()?.contains("\"searchText\":\"$slow\"") == true) {
            Thread.sleep(slowQueryMillis)
        }
        return respond(request)
    }

    private fun respond(request: RecordedRequest): MockResponse = synchronized(this) {
        requests += request
        try {
            route(request)
        } catch (failure: IllegalArgumentException) {
            error(400, "validation", failure.message ?: "Invalid request.")
        }
    }

    private fun route(request: RecordedRequest): MockResponse {
        val path = request.url.encodedPath.removePrefix("/api/v1")
        val method = request.method
        if (path == "/health" && method == "GET") {
            return json(buildJsonObject {
                put("status", "ok")
                put("apiVersion", apiVersion)
                put("application", "Lexicon")
            })
        }
        if (path == "/auth/login" && method == "POST") {
            val body = bodyObject(request)
            if (body["username"]?.jsonPrimitive?.contentOrNull != username || body["password"]?.jsonPrimitive?.contentOrNull != password) {
                return error(401, "unauthorized", "Invalid user name or password.")
            }
            val token = "token-${nextToken++}"
            tokens += token
            val remembered = body["rememberDevice"]?.jsonPrimitive?.booleanOrNull == true
            val refresh = if (remembered) "refresh-${nextToken++}" else ""
            if (remembered) refreshSecrets[refresh] = "device-${nextToken++}"
            return json(buildJsonObject {
                put("token", token)
                put("refreshToken", refresh)
                put("username", username)
                put("apiVersion", apiVersion)
                put("idleTimeoutSeconds", 28800)
                put("absoluteLifetimeSeconds", 604800)
            })
        }
        if (path == "/auth/refresh" && method == "POST") {
            val secret = bodyObject(request)["refreshToken"]?.jsonPrimitive?.contentOrNull.orEmpty()
            val device = refreshSecrets.remove(secret) ?: return error(401, "unauthorized", "Device not remembered.")
            val token = "token-${nextToken++}"
            val next = "refresh-${nextToken++}"
            tokens += token
            refreshSecrets[next] = device
            return json(buildJsonObject {
                put("token", token); put("refreshToken", next); put("username", username)
                put("apiVersion", apiVersion); put("idleTimeoutSeconds", 28800)
                put("absoluteLifetimeSeconds", 604800)
            })
        }
        if (path == "/auth/forget-device" && method == "POST") {
            val secret = bodyObject(request)["refreshToken"]?.jsonPrimitive?.contentOrNull.orEmpty()
            refreshSecrets.remove(secret)
            return noContent()
        }
        val token = request.headers["Authorization"]?.removePrefix("Bearer ")
        if (token == null || token !in tokens) return error(401, "unauthorized", "Authentication is required.")

        val segments = path.trim('/').split('/')
        return when {
            path == "/auth/logout" && method == "POST" -> {
                tokens.remove(token)
                noContent()
            }
            path == "/auth/me" -> json(buildJsonObject {
                put("username", username)
                put("apiVersion", apiVersion)
            })
            path == "/auth/devices" && method == "GET" -> json(buildJsonObject {
                put("devices", buildJsonArray {
                    refreshSecrets.values.distinct().forEach { id -> add(buildJsonObject {
                        put("id", id); put("createdAtSeconds", 1); put("lastUsedSeconds", 1)
                    }) }
                })
            })
            segments.size == 3 && segments[0] == "auth" && segments[1] == "devices" && method == "DELETE" -> {
                refreshSecrets.entries.removeIf { it.value == segments[2] }
                noContent()
            }
            path == "/groups" && method == "GET" -> json(buildJsonObject { put("groups", encode(groups.sortedWith(compareBy({ it.position }, { it.name })))) })
            path == "/groups/default" -> json(buildJsonObject { put("groupId", 1) })
            path == "/groups" && method == "POST" -> {
                val body = bodyObject(request)
                val name = requireNotNull(body["name"]?.jsonPrimitive?.contentOrNull?.trim()?.takeIf { it.isNotEmpty() }) { "Group name cannot be empty." }
                val group = Group(nextId++, name, body["description"]?.jsonPrimitive?.contentOrNull.orEmpty(), body["position"]?.jsonPrimitive?.intOrNull ?: 0)
                groups += group
                json(buildJsonObject { put("group", encode(group)) }, 201)
            }
            segments.size == 2 && segments[0] == "groups" && method == "PUT" -> {
                val id = segments[1].toInt()
                val body = bodyObject(request)
                val index = groups.indexOfFirst { it.id == id }.takeIf { it >= 0 } ?: return notFound()
                groups[index] = groups[index].copy(
                    name = body["name"]!!.jsonPrimitive.content,
                    description = body["description"]?.jsonPrimitive?.contentOrNull.orEmpty(),
                    position = body["position"]?.jsonPrimitive?.intOrNull ?: 0,
                )
                json(buildJsonObject { put("group", encode(groups[index])) })
            }
            segments.size == 2 && segments[0] == "groups" && method == "DELETE" -> {
                val id = segments[1].toInt()
                if (!groups.removeIf { it.id == id }) return notFound()
                items.values.removeIf { it.groupId == id }
                cards.removeIf { it.itemId !in items }
                noContent()
            }
            path == "/alarms" && method == "GET" -> json(buildJsonObject { put("alarms", encode(alarms.sortedWith(compareBy({ it.firesAt }, { it.id })))) })
            path == "/alarms" && method == "POST" -> {
                val alarm = alarmFrom(bodyObject(request), nextId++) ?: return error(400, "validation", "An alarm needs a title and a UTC time.")
                alarms += alarm
                json(buildJsonObject { put("alarm", encode(alarm)) }, 201)
            }
            segments.size == 2 && segments[0] == "alarms" && method == "PUT" -> {
                val id = segments[1].toInt()
                val index = alarms.indexOfFirst { it.id == id }.takeIf { it >= 0 } ?: return notFound()
                val alarm = alarmFrom(bodyObject(request), id) ?: return error(400, "validation", "An alarm needs a title and a UTC time.")
                // A new time rings again; a new title does not.
                val previous = alarms[index]
                alarms[index] = alarm.copy(dismissedAt = if (alarm.firesAt == previous.firesAt) previous.dismissedAt else null)
                json(buildJsonObject { put("alarm", encode(alarms[index])) })
            }
            segments.size == 3 && segments[0] == "alarms" && alarmActionsFail -> error(503, "unavailable", "Try again later.")
            segments.size == 3 && segments[0] == "alarms" && segments[2] == "dismiss" && method == "POST" -> {
                val index = alarms.indexOfFirst { it.id == segments[1].toInt() }.takeIf { it >= 0 } ?: return notFound()
                alarms[index] = alarms[index].copy(dismissedAt = alarms[index].dismissedAt ?: utcNow())
                json(buildJsonObject { put("alarm", encode(alarms[index])) })
            }
            segments.size == 3 && segments[0] == "alarms" && segments[2] == "snooze" && method == "POST" -> {
                val index = alarms.indexOfFirst { it.id == segments[1].toInt() }.takeIf { it >= 0 } ?: return notFound()
                val minutes = bodyObject(request)["minutes"]?.jsonPrimitive?.intOrNull?.takeIf { it in 1..1440 }
                    ?: return error(400, "validation", "Snooze for 1 minute to 24 hours.")
                val until = java.time.Instant.now().plusSeconds(minutes * 60L).truncatedTo(java.time.temporal.ChronoUnit.SECONDS)
                alarms[index] = alarms[index].copy(firesAt = until.toString(), dismissedAt = null)
                json(buildJsonObject { put("alarm", encode(alarms[index])) })
            }
            segments.size == 2 && segments[0] == "alarms" && method == "DELETE" -> {
                val id = segments[1].toInt()
                if (!alarms.removeIf { it.id == id }) return notFound()
                noContent()
            }
            path == "/types" && method == "GET" -> {
                val groupId = request.url.queryParameter("groupId")?.toInt()
                val list = types.filter { groupId == null || it.groupId == null || it.groupId == groupId }
                json(buildJsonObject { put("types", encode(list)) })
            }
            path == "/types" && method == "POST" -> {
                val body = bodyObject(request)
                val groupId = body["groupId"]?.jsonPrimitive?.intOrNull
                val type = ItemType(nextId++, groupId, groups.firstOrNull { it.id == groupId }?.name.orEmpty(), body["name"]!!.jsonPrimitive.content, body["description"]?.jsonPrimitive?.contentOrNull.orEmpty())
                types += type
                json(buildJsonObject { put("type", encode(type)) }, 201)
            }
            segments.size == 2 && segments[0] == "types" && method == "PUT" -> {
                val id = segments[1].toInt()
                val body = bodyObject(request)
                val index = types.indexOfFirst { it.id == id }.takeIf { it >= 0 } ?: return notFound()
                val groupId = body["groupId"]?.jsonPrimitive?.intOrNull
                types[index] = types[index].copy(
                    name = body["name"]!!.jsonPrimitive.content,
                    description = body["description"]?.jsonPrimitive?.contentOrNull.orEmpty(),
                    groupId = groupId,
                    groupName = groups.firstOrNull { it.id == groupId }?.name.orEmpty(),
                )
                json(buildJsonObject { put("type", encode(types[index])) })
            }
            segments.size == 2 && segments[0] == "types" && method == "DELETE" -> {
                val id = segments[1].toInt()
                types.removeIf { it.id == id }
                val removed = fields.filter { it.itemTypeId == id }.mapNotNull { it.id }.map(Int::toString).toSet()
                fields.removeIf { it.itemTypeId == id }
                items.replaceAll { _, item -> if (item.itemTypeId == id) item.copy(itemTypeId = null, itemTypeName = "", fieldValues = item.fieldValues - removed) else item }
                noContent()
            }
            segments.size == 3 && segments[0] == "types" && segments[2] == "item-count" -> {
                val id = segments[1].toInt()
                json(buildJsonObject { put("count", items.values.count { it.itemTypeId == id }) })
            }
            segments.size == 3 && segments[0] == "types" && segments[2] == "fields" && method == "GET" -> {
                val id = segments[1].toInt()
                json(buildJsonObject { put("fields", encode(fields.filter { it.itemTypeId == id }.sortedBy { it.position })) })
            }
            segments.size == 3 && segments[0] == "types" && segments[2] == "fields" && method == "POST" -> {
                val typeId = segments[1].toInt()
                val body = bodyObject(request)
                val field = ItemField(
                    nextId++, typeId, body["name"]!!.jsonPrimitive.content,
                    FieldDataType.valueOf(body["dataType"]!!.jsonPrimitive.content),
                    body["position"]?.jsonPrimitive?.intOrNull ?: 0,
                    body["enumOptions"]?.jsonArray?.map { it.jsonPrimitive.content }.orEmpty(),
                )
                fields += field
                json(buildJsonObject { put("field", encode(field)) }, 201)
            }
            segments.size == 2 && segments[0] == "fields" && method == "PUT" -> {
                val id = segments[1].toInt()
                val body = bodyObject(request)
                val index = fields.indexOfFirst { it.id == id }.takeIf { it >= 0 } ?: return notFound()
                val old = fields[index]
                fields[index] = old.copy(
                    name = body["name"]!!.jsonPrimitive.content,
                    dataType = FieldDataType.valueOf(body["dataType"]!!.jsonPrimitive.content),
                    position = body["position"]?.jsonPrimitive?.intOrNull ?: 0,
                    enumOptions = body["enumOptions"]?.jsonArray?.map { it.jsonPrimitive.content }.orEmpty(),
                )
                if (fields[index].dataType != old.dataType || fields[index].enumOptions != old.enumOptions) {
                    items.replaceAll { _, item -> item.copy(fieldValues = item.fieldValues - id.toString()) }
                }
                json(buildJsonObject { put("field", encode(fields[index])) })
            }
            segments.size == 2 && segments[0] == "fields" && method == "DELETE" -> {
                val id = segments[1].toInt()
                fields.removeIf { it.id == id }
                items.replaceAll { _, item -> item.copy(fieldValues = item.fieldValues - id.toString()) }
                noContent()
            }
            segments.size == 3 && segments[0] == "fields" && segments[2] == "value-count" -> {
                val id = segments[1]
                json(buildJsonObject { put("count", items.values.count { it.fieldValues.containsKey(id) }) })
            }
            path == "/items/query" -> query(request)
            path == "/items/resolve" && method == "GET" -> {
                val title = request.url.queryParameter("title").orEmpty()
                val disambiguation = request.url.queryParameter("disambiguation").orEmpty()
                val found = items.values.firstOrNull {
                    it.title.equals(title, ignoreCase = true) && it.disambiguation.equals(disambiguation, ignoreCase = true)
                } ?: items.values.firstOrNull { item -> disambiguation.isEmpty() && item.aliases.any { it.equals(title, ignoreCase = true) } }
                    ?: return error(404, "not_found", "Item not found.")
                json(buildJsonObject { put("itemId", found.id!!) })
            }
            path == "/items" && method == "POST" -> saveItem(request, null)
            path == "/inbox" && method == "POST" -> captureIdea(request)
            segments.size == 2 && segments[0] == "items" && method == "GET" -> {
                val id = segments[1].toIntOrNull() ?: return notFound()
                val item = items[id] ?: return error(404, "not_found", "Item not found.")
                val include = request.url.queryParameter("include").orEmpty()
                json(buildJsonObject {
                    put("item", encode(item))
                    if ("links" in include) put("links", encode(links.filter { it.fromItemId == id }.map(::withTitles)))
                    if ("backlinks" in include) put("backlinks", encode(links.filter { it.toItemId == id }.map(::withTitles)))
                })
            }
            segments.size == 2 && segments[0] == "items" && method == "PUT" -> saveItem(request, segments[1].toInt())
            segments.size == 2 && segments[0] == "items" && method == "DELETE" -> {
                val id = segments[1].toInt()
                items.remove(id) ?: return notFound()
                links.removeIf { it.fromItemId == id || it.toItemId == id }
                cards.removeIf { it.itemId == id }
                noContent()
            }
            segments.size == 3 && segments[0] == "items" && segments[2] == "read" -> {
                reads += segments[1].toInt()
                noContent()
            }
            path == "/usage/tags" -> usage(items.values.flatMap { it.tags })
            path == "/usage/flags" -> usage(items.values.flatMap { it.flags })
            path == "/usage/aliases" -> usage(items.values.flatMap { it.aliases })
            path == "/search/suggestions" -> json(buildJsonObject { put("values", buildJsonArray { items.values.forEach { add(JsonPrimitive(it.title)) } }) })
            segments.size == 3 && segments[0] == "items" && segments[2] == "graph" && method == "GET" -> {
                val id = segments[1].toInt()
                val centre = items[id] ?: return notFound()
                val around = links.filter { it.fromItemId == id || it.toItemId == id }
                val others = around.map { if (it.fromItemId == id) it.toItemId else it.fromItemId }.distinct()
                json(buildJsonObject {
                    put("nodes", buildJsonArray {
                        add(buildJsonObject { put("id", id); put("title", centre.title); put("depth", 0) })
                        others.forEach { other ->
                            add(buildJsonObject { put("id", other!!); put("title", items.getValue(other).title); put("depth", 1) })
                        }
                    })
                    put("edges", encode(around.map(::withTitles)))
                    put("truncated", false)
                })
            }
            segments.size == 3 && segments[0] == "items" && segments[2] == "cards" && method == "GET" -> {
                val id = segments[1].toInt()
                if (id !in items) return error(404, "not_found", "Item not found.")
                json(buildJsonObject { put("cards", encode(cards.filter { it.itemId == id })) })
            }
            segments.size == 3 && segments[0] == "items" && segments[2] == "cards" && method == "POST" -> {
                val id = segments[1].toInt()
                if (id !in items) return error(404, "not_found", "Item not found.")
                val (question, answer) = cardText(bodyObject(request))
                val card = Card(nextId++, id, question, answer)
                cards += card
                json(buildJsonObject { put("card", encode(card)) }, 201)
            }
            segments.size == 3 && segments[0] == "items" && segments[2] == "quiz-cards" && method == "GET" -> {
                val id = segments[1].toInt()
                if (id !in items) return error(404, "not_found", "Item not found.")
                val depth = request.url.queryParameter("depth")?.toIntOrNull() ?: 0
                val limit = request.url.queryParameter("limit")?.toIntOrNull() ?: 150
                require(depth in 0..3) { "depth must be 0 to 3." }
                require(limit in 1..300) { "limit must be 1 to 300." }
                val (reached, truncated) = neighbourhood(id, depth, limit)
                json(buildJsonObject {
                    put("cards", buildJsonArray {
                        reached.forEach { itemId ->
                            cards.filter { it.itemId == itemId }.forEach { card ->
                                add(buildJsonObject {
                                    encode(card).jsonObject.forEach { (key, value) -> put(key, value) }
                                    put("itemTitle", items.getValue(itemId).title)
                                })
                            }
                        }
                    })
                    put("itemCount", reached.size)
                    put("truncated", truncated)
                })
            }
            segments.size == 2 && segments[0] == "cards" && method == "GET" -> {
                val card = cards.firstOrNull { it.id == segments[1].toIntOrNull() } ?: return error(404, "not_found", "Card not found.")
                json(buildJsonObject { put("card", encode(card)) })
            }
            segments.size == 2 && segments[0] == "cards" && method == "PUT" -> {
                val index = cards.indexOfFirst { it.id == segments[1].toIntOrNull() }.takeIf { it >= 0 }
                    ?: return error(404, "not_found", "Card not found.")
                val (question, answer) = cardText(bodyObject(request))
                // Only the text changes; the counts are the server's.
                cards[index] = cards[index].copy(question = question, answer = answer)
                json(buildJsonObject { put("card", encode(cards[index])) })
            }
            segments.size == 2 && segments[0] == "cards" && method == "DELETE" -> {
                if (!cards.removeIf { it.id == segments[1].toIntOrNull() }) return error(404, "not_found", "Card not found.")
                noContent()
            }
            segments.size == 3 && segments[0] == "cards" && segments[2] == "attempt" && method == "POST" -> {
                if (attemptsFail) return error(503, "unavailable", "Try again later.")
                val index = cards.indexOfFirst { it.id == segments[1].toIntOrNull() }.takeIf { it >= 0 }
                    ?: return error(404, "not_found", "Card not found.")
                val success = (bodyObject(request)["success"] as? JsonPrimitive)?.takeIf { !it.isString }?.booleanOrNull
                    ?: return error(400, "validation", "An attempt needs \"success\": true or false.")
                val card = cards[index]
                cards[index] = card.copy(
                    successCount = card.successCount + if (success) 1 else 0,
                    failureCount = card.failureCount + if (success) 0 else 1,
                    lastAttempt = attemptTime,
                )
                json(buildJsonObject { put("card", encode(cards[index])) })
            }
            path == "/review" && method == "GET" -> {
                val due = items.values.filter { it.reviewedAt == null }.sortedBy { it.id }
                val limit = request.url.queryParameter("limit")?.toInt() ?: 20
                json(buildJsonObject {
                    put("items", encode(due.take(limit)))
                    put("dueCount", due.size)
                })
            }
            segments.size == 3 && segments[0] == "items" && segments[2] == "review" && method == "POST" -> {
                val id = segments[1].toInt()
                val item = items[id] ?: return notFound()
                val rating = ReviewRating.valueOf(bodyObject(request)["rating"]!!.jsonPrimitive.content)
                val level = rating.levelAfter(item.understanding)
                val reviewed = item.copy(
                    understanding = level,
                    reviewedAt = "2026-09-22T08:00:00Z",
                    reviewDueAt = "2026-09-2${2 + ReviewRating.intervalDays(level).coerceAtMost(7)}T08:00:00Z",
                    revision = item.revision + 1,
                )
                items[id] = reviewed
                reviews += id to rating
                json(buildJsonObject { put("item", encode(reviewed)) })
            }
            path == "/export" && method == "GET" -> MockResponse.Builder().code(200)
                .setHeader("Content-Type", "application/json; charset=utf-8")
                .body(exportDocument).build()
            path == "/import" && method == "POST" -> {
                imports += request.body?.toByteArray() ?: ByteArray(0)
                json(buildJsonObject {
                    put("report", buildJsonObject {
                        put("itemsCreated", 2)
                        put("itemsSkipped", 1)
                        put("linksCreated", 1)
                        put("cardsCreated", 3)
                        put("warnings", buildJsonArray { add(JsonPrimitive("Field 'Year' holds another kind of value here.")) })
                    })
                })
            }
            path == "/blobs" && method == "POST" -> {
                val bytes = request.body?.toByteArray() ?: ByteArray(0)
                require(bytes.isNotEmpty()) { "The upload is empty." }
                val hash = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
                blobs[hash] = bytes
                json(buildJsonObject {
                    put("hash", hash)
                    put("mediaType", ImageValues.sniff(bytes.copyOf(minOf(16, bytes.size)))?.let(::JsonPrimitive) ?: kotlinx.serialization.json.JsonNull)
                }, 201)
            }
            segments.size == 2 && segments[0] == "blobs" && method == "GET" -> {
                val bytes = blobs[segments[1]] ?: return error(404, "not_found", "Blob not found.")
                MockResponse.Builder().code(200).setHeader("Content-Type", "application/octet-stream")
                    .body(okio.Buffer().write(bytes)).build()
            }
            else -> error(404, "not_found", "The requested endpoint does not exist.")
        }
    }

    private fun withTitles(link: Link): Link = link.copy(
        fromItemTitle = items[link.fromItemId]?.title.orEmpty(),
        toItemTitle = items[link.toItemId]?.title.orEmpty(),
    )

    private fun query(request: RecordedRequest): MockResponse {
        val query = LexiconJson.decodeFromString(ItemQuery.serializer(), request.body!!.utf8())
        var list = items.values.filter { item ->
            (query.groupId == null || item.groupId == query.groupId) &&
                (query.typeId == null || item.itemTypeId == query.typeId) &&
                (query.searchText.isEmpty() || listOf(item.title, item.disambiguation).plus(item.aliases).plus(item.tags).plus(item.flags)
                    .any { it.contains(query.searchText, ignoreCase = true) } ||
                    item.content.contains(query.searchText, ignoreCase = true)) &&
                (query.columnFilters.id.isEmpty() || item.id.toString() == query.columnFilters.id) &&
                (query.columnFilters.title.isEmpty() || item.title.contains(query.columnFilters.title, ignoreCase = true)) &&
                (query.columnFilters.alias.isEmpty() || item.aliases.any { it.contains(query.columnFilters.alias, ignoreCase = true) }) &&
                (query.tagFilter.isEmpty() || query.tagFilter in item.tags) &&
                (query.flagFilter.isEmpty() || query.flagFilter in item.flags) &&
                (query.statusFilter == null || item.status == query.statusFilter) &&
                (query.understandingFilter == null || item.understanding == query.understandingFilter) &&
                (query.pinnedFilter == null || item.pinned == query.pinnedFilter) &&
                query.valueFilters.all { filter ->
                    val value = item.fieldValue(filter.fieldId)
                    if (filter.exact) value == filter.value else value.contains(filter.value, ignoreCase = true)
                } &&
                query.propertyFilters.all { filter ->
                    item.properties.any { it.key.equals(filter.key, ignoreCase = true) && it.value.contains(filter.value, ignoreCase = true) }
                }
        }
        list = when (query.sortColumn) {
            SortColumns.ID -> list.sortedBy { it.id }
            else -> list.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it: Item -> it.title }.thenBy { it.id })
        }
        if (query.sortOrder == SortOrder.Descending) list = list.reversed()
        val page = list.drop(query.offset).let { if (query.limit > 0) it.take(query.limit) else it }
            .map { it.copy(content = "", properties = emptyList(), matchSnippet = snippetOf(it.content, query.searchText)) }
        return json(buildJsonObject {
            put("items", encode(page))
            put("totalCount", list.size)
        })
    }

    /** Why a search found an item, the way the server explains it: around the match, on one line. */
    private fun snippetOf(content: String, searchText: String): String? {
        if (searchText.isEmpty()) return null
        val at = content.indexOf(searchText, ignoreCase = true)
        if (at < 0) return null
        val start = maxOf(0, at - 40)
        val end = minOf(content.length, start + 120)
        val middle = content.substring(start, end).replace('\n', ' ').replace('\r', ' ').trim()
        return (if (start > 0) "…" else "") + middle + (if (end < content.length) "…" else "")
    }

    /** The Inbox: to Default with the type Inbox - one of all groups, or else of Default - made when there is none. */
    private fun captureIdea(request: RecordedRequest): MockResponse {
        val body = bodyObject(request)
        val title = body["title"]?.jsonPrimitive?.contentOrNull?.trim().orEmpty()
        require(title.isNotEmpty()) { "Item title cannot be empty." }
        require(items.values.none { it.groupId == 1 && it.title == title && it.disambiguation.isEmpty() }) {
            "An item titled '$title' already exists in this group."
        }
        val named = { type: ItemType, scope: Int? -> type.groupId == scope && type.name.equals(INBOX_TYPE, ignoreCase = true) }
        val type = types.firstOrNull { named(it, null) } ?: types.firstOrNull { named(it, 1) }
            ?: ItemType(nextId++, null, "", INBOX_TYPE, "Ideas caught in the Inbox, to sort out later.").also { types += it }
        val itemId = nextId++
        items[itemId] = Item(
            id = itemId,
            groupId = 1,
            groupName = groups.first { it.id == 1 }.name,
            itemTypeId = type.id,
            itemTypeName = type.name,
            title = title,
            content = body["content"]?.jsonPrimitive?.contentOrNull.orEmpty(),
            revision = 1,
        )
        return json(buildJsonObject {
            put("id", itemId)
            put("item", encode(items.getValue(itemId)))
        }, 201)
    }

    private fun saveItem(request: RecordedRequest, id: Int?): MockResponse {
        val body = bodyObject(request)
        val itemJson = body["item"]!!.jsonObject
        val title = itemJson["title"]!!.jsonPrimitive.content.trim()
        require(title.isNotEmpty()) { "Item title cannot be empty." }
        if (id != null && id !in items) return notFound()
        val decoded = LexiconJson.decodeFromJsonElement(Item.serializer(), itemJson)
        if (id != null && decoded.revision > 0 && decoded.revision != items.getValue(id).revision) {
            return error(409, "conflict", "This item was changed elsewhere after you opened it.")
        }
        val groupId = decoded.groupId ?: 1
        val duplicate = items.values.any { it.id != id && it.groupId == groupId && it.title == title && it.disambiguation == decoded.disambiguation }
        require(!duplicate) { "'$title' already exists in this group." }
        val itemId = id ?: nextId++
        val type = types.firstOrNull { it.id == decoded.itemTypeId }
        items[itemId] = decoded.copy(
            id = itemId,
            groupId = groupId,
            groupName = groups.first { it.id == groupId }.name,
            itemTypeName = type?.name.orEmpty(),
            title = title,
            revision = (items[itemId]?.revision ?: 0) + 1,
        )
        val outgoing = body["links"]?.jsonArray.orEmpty().map { it.jsonObject }
        val incoming = body["backlinks"]?.jsonArray.orEmpty().map { it.jsonObject }
        (outgoing + incoming).forEach { link ->
            val type = LinkType.valueOf(link["linkType"]!!.jsonPrimitive.content)
            require(type != LinkType.None) { "A valid link type is required." }
            require(type != LinkType.Custom || !link["customValue"]?.jsonPrimitive?.contentOrNull.isNullOrBlank()) { "Custom links need a value." }
        }
        links.removeIf { it.fromItemId == itemId || it.toItemId == itemId }
        outgoing.forEach { link -> links += linkFrom(link, from = itemId, to = link["toItemId"]!!.jsonPrimitive.int) }
        incoming.forEach { link -> links += linkFrom(link, from = link["fromItemId"]!!.jsonPrimitive.int, to = itemId) }
        return json(buildJsonObject {
            put("id", itemId)
            put("item", encode(items.getValue(itemId)))
        }, if (id == null) 201 else 200)
    }

    private fun linkFrom(json: JsonObject, from: Int, to: Int) = Link(
        id = json["id"]?.jsonPrimitive?.intOrNull ?: nextId++,
        fromItemId = from,
        toItemId = to,
        linkType = LinkType.valueOf(json["linkType"]!!.jsonPrimitive.content),
        position = json["position"]?.jsonPrimitive?.intOrNull ?: 0,
        customValue = json["customValue"]?.jsonPrimitive?.contentOrNull.orEmpty(),
    )

    /** A card's question and answer, which neither may be missing or blank. */
    private fun cardText(body: JsonObject): Pair<String, String> {
        val question = body["question"]?.jsonPrimitive?.contentOrNull
        val answer = body["answer"]?.jsonPrimitive?.contentOrNull
        require(!question.isNullOrBlank()) { "Question cannot be empty." }
        require(!answer.isNullOrBlank()) { "Answer cannot be empty." }
        return question to answer
    }

    /**
     * The items [depth] links around [centre], either way, breadth first and
     * the centre first, as the graph finds them; at most [limit]. The flag says
     * more were in reach.
     */
    private fun neighbourhood(centre: Int, depth: Int, limit: Int): Pair<List<Int>, Boolean> {
        val reached = mutableListOf(centre)
        var frontier = listOf(centre)
        var truncated = false
        repeat(depth) {
            val next = mutableListOf<Int>()
            for (id in frontier) {
                val around = links.mapNotNull { link ->
                    when (id) {
                        link.fromItemId -> link.toItemId
                        link.toItemId -> link.fromItemId
                        else -> null
                    }
                }
                for (other in around.distinct()) {
                    if (other !in items || other in reached || other in next) continue
                    if (reached.size + next.size < limit) next += other else truncated = true
                }
            }
            reached += next
            frontier = next
        }
        return reached to truncated
    }

    private fun usage(values: List<String>): MockResponse = json(buildJsonObject {
        put("values", buildJsonArray {
            values.groupingBy { it }.eachCount().toSortedMap(String.CASE_INSENSITIVE_ORDER).forEach { (value, count) ->
                add(buildJsonObject {
                    put("value", value)
                    put("usageCount", count)
                })
            }
        })
    })

    private fun utcNow(): String = java.time.Instant.now().truncatedTo(java.time.temporal.ChronoUnit.SECONDS).toString()

    /** As the server stores it: a trimmed title and a UTC time with seconds. */
    private fun alarmFrom(body: JsonObject, id: Int): Alarm? {
        val title = body["title"]?.jsonPrimitive?.contentOrNull?.trim()?.takeIf { it.isNotEmpty() } ?: return null
        val firesAt = body["firesAt"]?.jsonPrimitive?.contentOrNull?.takeIf { it.length == 20 && it.endsWith("Z") } ?: return null
        runCatching { java.time.Instant.parse(firesAt) }.getOrNull() ?: return null
        return Alarm(id, title, body["description"]?.jsonPrimitive?.contentOrNull.orEmpty(), firesAt)
    }

    private fun bodyObject(request: RecordedRequest): JsonObject =
        LexiconJson.parseToJsonElement(request.body?.utf8().orEmpty().ifEmpty { "{}" }).jsonObject

    private inline fun <reified T> encode(value: T): JsonElement = LexiconJson.encodeToJsonElement(serializer<T>(), value)

    private fun json(body: JsonElement, code: Int = 200): MockResponse = MockResponse.Builder()
        .code(code)
        .setHeader("Content-Type", "application/json; charset=utf-8")
        .body(body.toString())
        .build()

    private fun noContent(): MockResponse = MockResponse.Builder().code(204).build()

    private fun notFound(): MockResponse = error(404, "not_found", "No such record.")

    fun error(code: Int, errorCode: String, message: String): MockResponse = json(
        buildJsonObject {
            put("error", buildJsonObject {
                put("code", errorCode)
                put("message", message)
            })
        },
        code,
    )
}
