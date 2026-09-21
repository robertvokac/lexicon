package com.robertvokac.lexicon.model

import com.robertvokac.lexicon.api.LexiconJson
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** The JSON shapes of docs/rest-api.md, decoded and encoded exactly. */
class ModelsSerializationTest {
    @Test
    fun decodesTheDocumentedItem() {
        val item = LexiconJson.decodeFromString(
            Item.serializer(),
            """
            {
              "id": 7, "groupId": 1, "groupName": "Default", "itemTypeId": 2, "itemTypeName": "Concept",
              "fieldValues": { "4": "hard" },
              "properties": [{ "key": "source", "value": "folklore" }],
              "title": "Monoid", "disambiguation": "algebra",
              "aliases": ["Semigroup with unit"], "tags": ["algebra"], "flags": ["todo"],
              "status": "Draft", "understanding": "Practiced", "pinned": false,
              "content": "# Monoid\n\nMarkdown source."
            }
            """,
        )
        assertEquals(7, item.id)
        assertEquals("hard", item.fieldValue(4))
        assertEquals(ItemStatus.Draft, item.status)
        assertEquals(UnderstandingLevel.Practiced, item.understanding)
        assertEquals(listOf(Property("source", "folklore")), item.properties)
        assertEquals("Monoid [algebra]", item.displayTitle)
        assertEquals("# Monoid\n\nMarkdown source.", item.content)
    }

    @Test
    fun absentIdsAreNull() {
        val type = LexiconJson.decodeFromString(
            ItemType.serializer(),
            """{ "id": 2, "groupId": null, "groupName": "", "name": "Concept", "description": "" }""",
        )
        assertNull(type.groupId)
        assertEquals("All groups", type.scopeLabel)
        assertEquals("Concept (All groups)", type.displayName)
        val item = LexiconJson.decodeFromString(Item.serializer(), """{ "id": 3, "itemTypeId": null, "title": "RAII" }""")
        assertNull(item.itemTypeId)
    }

    @Test
    fun enumsTravelAsNamesNeverOrdinals() {
        val field = ItemField(4, 2, "Difficulty", FieldDataType.Enum, 0, listOf("easy", "hard"))
        val json = LexiconJson.encodeToJsonElement(ItemField.serializer(), field).jsonObject
        assertEquals("Enum", json["dataType"]!!.jsonPrimitive.content)
        assertTrue(json["dataType"]!!.jsonPrimitive.isString)

        for (status in ItemStatus.entries) {
            assertEquals("\"${status.name}\"", LexiconJson.encodeToString(ItemStatus.serializer(), status))
        }
        assertEquals(listOf("None", "Draft", "Completed"), ItemStatus.entries.map { it.name })
        assertEquals(
            listOf("Unknown", "Recognized", "Understood", "Practiced", "Mastered"),
            UnderstandingLevel.entries.map { it.name },
        )
        assertEquals(
            listOf("None", "IsA", "PartOf", "Uses", "DependsOn", "Implements", "Related", "Contrasts", "AlternativeTo", "ParentOf", "Custom"),
            LinkType.entries.map { it.name },
        )
        assertEquals(
            listOf("Integer", "Float", "Text", "Date", "Time", "Timestamp", "Boolean", "Enum", "Blob", "Other"),
            FieldDataType.entries.map { it.name },
        )
        assertFalse(LinkType.None in LinkType.persistable)
        assertEquals(10, LinkType.persistable.size)
    }

    @Test
    fun anUnknownEnumNameIsAnError() {
        assertThrows(SerializationException::class.java) {
            LexiconJson.decodeFromString(Item.serializer(), """{ "title": "x", "status": "Archived" }""")
        }
        assertThrows(SerializationException::class.java) {
            LexiconJson.decodeFromString(Link.serializer(), """{ "linkType": 3 }""")
        }
    }

    @Test
    fun unknownFieldsFromANewerServerAreIgnored() {
        val group = LexiconJson.decodeFromString(
            Group.serializer(),
            """{ "id": 1, "name": "Default", "description": "", "position": 0, "colour": "blue" }""",
        )
        assertEquals("Default", group.name)
    }

    @Test
    fun theQueryBodySendsEveryFieldExplicitly() {
        val json = LexiconJson.encodeToJsonElement(ItemQuery.serializer(), ItemQuery(limit = 20)).jsonObject
        assertEquals(
            setOf(
                "groupId", "typeId", "searchText", "columnFilters", "propertyFilters", "valueFilters", "tagFilter",
                "flagFilter", "understandingFilter", "statusFilter", "pinnedFilter", "limit", "offset", "sortColumn", "sortOrder",
            ),
            json.keys,
        )
        assertEquals("null", json["groupId"].toString())
        assertEquals("null", json["pinnedFilter"].toString())
        assertEquals("Ascending", json["sortOrder"]!!.jsonPrimitive.content)
        assertEquals(3, json["sortColumn"]!!.jsonPrimitive.content.toInt())
    }

    @Test
    fun theSaveRequestCarriesBothLinkDirections() {
        val request = SaveItemRequest(
            item = ItemWrite(groupId = 1, title = "Group"),
            links = listOf(OutgoingLinkWrite(id = null, toItemId = 12, linkType = LinkType.PartOf, position = 1)),
            backlinks = listOf(IncomingLinkWrite(id = 33, fromItemId = 9, linkType = LinkType.Custom, customValue = "generalizes")),
        )
        val json = LexiconJson.encodeToJsonElement(SaveItemRequest.serializer(), request).jsonObject
        val link = json["links"]!!.jsonArray.single().jsonObject
        assertEquals("null", link["id"].toString())
        assertEquals(12, link["toItemId"]!!.jsonPrimitive.content.toInt())
        assertEquals("PartOf", link["linkType"]!!.jsonPrimitive.content)
        assertNull(link["fromItemId"])
        val backlink = json["backlinks"]!!.jsonArray.single().jsonObject
        assertEquals("Custom", backlink["linkType"]!!.jsonPrimitive.content)
        assertEquals("generalizes", backlink["customValue"]!!.jsonPrimitive.content)
        assertEquals(9, backlink["fromItemId"]!!.jsonPrimitive.content.toInt())
        assertNull(json["item"]!!.jsonObject["id"])
    }

    @Test
    fun aSaveRequestRefusesLinksTheServerWouldRefuse() {
        val item = ItemWrite(groupId = 1, title = "x")
        assertThrows(IllegalArgumentException::class.java) {
            SaveItemRequest(item, links = listOf(OutgoingLinkWrite(null, 2, LinkType.None)))
        }
        assertThrows(IllegalArgumentException::class.java) {
            SaveItemRequest(item, backlinks = listOf(IncomingLinkWrite(null, 2, LinkType.Custom, customValue = " ")))
        }
        assertThrows(IllegalArgumentException::class.java) {
            SaveItemRequest(item.copy(title = "  "))
        }
    }

    @Test
    fun secretsNeverAppearInToString() {
        val login = LoginRequest("robert", "hunter2hunter2")
        assertFalse(login.toString().contains("hunter2"))
        val response = LoginResponse("secret-token-value", "robert", 1)
        assertFalse(response.toString().contains("secret-token-value"))
    }

    @Test
    fun loginResponseDecodes() {
        val response = LexiconJson.decodeFromString(
            LoginResponse.serializer(),
            """{ "token": "3Qv", "username": "robert", "apiVersion": 1, "idleTimeoutSeconds": 28800, "absoluteLifetimeSeconds": 604800 }""",
        )
        assertEquals("3Qv", response.token)
        assertEquals(28800, response.idleTimeoutSeconds)
        assertEquals(604800, response.absoluteLifetimeSeconds)
    }

    @Test
    fun sortColumnsMatchTheOtherClients() {
        assertEquals((0..10).toList(), SortColumns.base.map { it.first })
        val fields = listOf(ItemField(4, 2, "Difficulty", FieldDataType.Enum), ItemField(5, 2, "Year", FieldDataType.Integer))
        assertEquals(listOf(11 to "Difficulty", 12 to "Year"), SortColumns.available(fields).drop(11))
    }

    @Test
    fun asciiFoldingLeavesOtherLettersAlone() {
        assertEquals("abcÉ", asciiFold("ABCÉ"))
    }
}
