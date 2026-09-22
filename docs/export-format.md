# Lexicon export format

A Lexicon export is the whole dictionary - groups, types with their fields,
items, links and, optionally, the files that values refer to - as one UTF-8
JSON document. Every client writes and reads the same format:

| Where | Export | Import |
| --- | --- | --- |
| Desktop | `File -> Export...` | `File -> Import...` |
| Web | `File -> Export...` | `File -> Import...` |
| Android | Settings, **Export…** | Settings, **Import…** |
| Server machine | `LexiconServer export --output FILE [--with-files]` | `LexiconServer import --input FILE` |
| REST | `GET /api/v1/export?blobs=true` | `POST /api/v1/import` |

The command line works on the database file directly, so it can back a
dictionary up while the server runs; SQLite coordinates the two connections.

## The document

```json
{
  "format": "lexicon-export",
  "version": 1,
  "exportedAt": "2026-09-22T08:00:00Z",
  "groups": [
    { "id": 1, "name": "Default", "description": "...", "position": 0 },
    { "id": 2, "name": "Maths", "description": "", "position": 1 }
  ],
  "types": [
    {
      "id": 3, "groupId": 2, "groupName": "Maths", "name": "Concept", "description": "",
      "fields": [
        { "id": 4, "itemTypeId": 3, "name": "Difficulty", "dataType": "Enum",
          "position": 0, "enumOptions": ["easy", "hard"] }
      ]
    }
  ],
  "items": [
    { "id": 7, "groupId": 2, "itemTypeId": 3, "title": "Monoid", "fieldValues": { "4": "hard" }, "...": "..." }
  ],
  "links": [
    { "fromItemId": 7, "toItemId": 8, "linkType": "IsA", "position": 1, "customValue": "" }
  ],
  "blobs": [
    { "hash": "6c7dbba2...99d98ca", "data": "iVBORw0KGgo..." }
  ]
}
```

- `format` and `version` come first in meaning: an import refuses a document
  whose `format` is not `lexicon-export` or whose `version` it does not know,
  before anything is written. This document describes version 1.
- Groups, types, fields, items and links have the shapes of the
  [REST API](rest-api.md), with the same symbolic enum names. Read-only parts
  such as `groupName`, `itemTypeName` and `revision` are written but ignored
  on import.
- The `id`s are those of the exporting database. They only connect the records
  of one document: `groupId`, `itemTypeId`, the keys of `fieldValues`,
  `fromItemId` and `toItemId` refer to them.
- `blobs` is present when files were included. `data` is standard base64 with
  padding; `hash` is the SHA-256 of the decoded bytes, the value `Blob` fields
  store.

## Importing

An import merges the document into the dictionary, in one unit of work: if any
part fails, nothing is written.

- **Groups** are matched by name; a missing one is created.
- **Types** are matched by name, ignoring ASCII case, within their scope - all
  groups, or the matched group. **Fields** are matched by name within their
  type. A field that exists but holds another kind of value keeps its data
  type, and the values meant for it are left out with a warning. Existing
  types and fields are never changed, so an import cannot clear values.
- **Items** already present - the same group, title and disambiguation - are
  left exactly as they are. Every other item is created with its values,
  properties, aliases, tags, flags, status, understanding, pinned state and
  content. A value that does not fit its field here, or whose file is neither
  in the document nor in this database, is left out with a warning.
- **Links** are created where at least one end is an item this import created,
  unless the same link is already there. Links between two items that were
  already present are theirs to keep as they are.
- **Files** in `blobs` are stored under their SHA-256; one whose bytes do not
  match its hash is left out.

Importing the same document twice therefore changes nothing the second time.
The import answers with a report:

```json
{ "groupsCreated": 1, "typesCreated": 2, "fieldsCreated": 3, "itemsCreated": 3,
  "itemsSkipped": 0, "linksCreated": 2, "blobsImported": 1, "warnings": [] }
```

An import is a merge, not a restore: it never deletes, renames or edits what
is already in the dictionary. To restore a dictionary exactly, import into an
empty database - or back up and restore the database file itself (see
[server.md](server.md#backups-while-the-server-runs)).
