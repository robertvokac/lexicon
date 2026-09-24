# Lexicon export format

A Lexicon export is the whole dictionary - groups, types with their fields,
items, links, alarms, cards and, optionally, the files that values refer to -
as one UTF-8 JSON document. Every client writes and reads the same format:

| Where | Export | Import |
| --- | --- | --- |
| Desktop | `File -> Export...` | `File -> Import...` |
| Web | `File -> Export...` | `File -> Import...` |
| Android | Settings, **Export…** | Settings, **Import…** |
| Server machine | `LexiconServer export --output FILE [--with-files]` | `LexiconServer import --input FILE` |
| REST | `GET /api/v1/export?blobs=true` | `POST /api/v1/import` |

The command line works on the database file directly, so it can export a
dictionary while the server runs; SQLite coordinates the two connections.

**An export is for moving a dictionary, not for backing up a large one.**
With files included, every file is read into memory, grows by a third as
base64 and becomes part of one JSON document, which is also built in memory;
the REST import accepts at most `--max-blob-bytes` (64 MB by default). That
is fine for notes and a few images, and wrong for gigabytes of PDFs or video.
To keep such a dictionary safe, use the server's automatic backups (see
[server.md](server.md#backups-while-the-server-runs)): they copy the database
and keep the files as files, shared between backups.

## The document

```json
{
  "format": "lexicon-export",
  "version": 2,
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
  "alarms": [
    { "id": 4, "title": "Dentist", "description": "Bring the card.", "firesAt": "2026-10-02T08:30:00Z" }
  ],
  "cards": [
    { "id": 12, "itemId": 7, "question": "What is a monoid?", "answer": "A semigroup with a unit.",
      "successCount": 4, "failureCount": 2, "lastAttempt": "2026-09-24T14:00:00Z" }
  ],
  "blobs": [
    { "hash": "6c7dbba2...99d98ca", "data": "iVBORw0KGgo..." }
  ]
}
```

- `format` and `version` come first in meaning: an import refuses a document
  whose `format` is not `lexicon-export` or whose `version` it does not know,
  before anything is written. This document describes version 2.
- **Versions.** The version goes up whenever a reader of the previous one
  would import a new document by leaving part of it out - silently losing
  data - rather than refusing it:

  | Version | Written by | Adds |
  | --- | --- | --- |
  | 1 | Lexicon before cards | groups, types, fields, items, links, alarms, files |
  | 2 | Lexicon with cards | `cards` |

  A reader takes every version up to its own and refuses a newer one: an
  older Lexicon says it does not know version 2 instead of importing it
  without the cards. A version 1 document has no cards; the few that
  development builds wrote with cards have them read all the same.
- Groups, types, fields, items and links have the shapes of the
  [REST API](rest-api.md), with the same symbolic enum names. Read-only parts
  such as `groupName`, `itemTypeName` and `revision` are written but ignored
  on import.
- The `id`s are those of the exporting database. They only connect the records
  of one document: `groupId`, `itemTypeId`, the keys of `fieldValues`,
  `fromItemId` and `toItemId` refer to them.
- `alarms` holds every alarm, with `firesAt` and `dismissedAt` in UTC, so an
  alarm that was dismissed does not ring again after an import. Documents
  written before alarms existed have no `alarms`; they import as before.
- `cards` holds every card with its `itemId`, `question`, `answer` and
  statistics - `successCount`, `failureCount` and `lastAttempt` (UTC, or
  `null` for a card never answered) - so a quiz history survives the move.
  The counts are whole numbers and never negative, and `lastAttempt` is
  given exactly when one of them is above zero. Documents written before
  cards existed have no `cards`; they import as before, with none.
- `blobs` is present when files were included. `data` is standard base64 with
  padding; `hash` is the SHA-256 of the decoded bytes, the value `Blob` fields
  store and the part after the colon of an `Image` value (`image/png:<hash>`).

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
- **Alarms** are created unless an alarm with the same title already goes off
  at the same moment.
- **Cards** are created for the items this import created, attached to them
  by their new IDs and with their statistics as exported. An item that was
  already present keeps the cards it has and gets none from the file. Cards
  are never merged: two cards asking the same question are two cards. A card
  of an item missing from the file is left out with a warning; a card with a
  blank question or answer, a negative count, a `lastAttempt` that is not a
  UTC time, or answers without a `lastAttempt` (or the other way round) fails
  the import.

Importing the same document twice therefore changes nothing the second time.
The import answers with a report:

```json
{ "groupsCreated": 1, "typesCreated": 2, "fieldsCreated": 3, "itemsCreated": 3,
  "itemsSkipped": 0, "linksCreated": 2, "blobsImported": 1, "alarmsCreated": 0,
  "cardsCreated": 4, "warnings": [] }
```

An import is a merge, not a restore: it never deletes, renames or edits what
is already in the dictionary. To restore a dictionary exactly, import into an
empty database - or back up and restore the database file itself (see
[server.md](server.md#backups-while-the-server-runs)).
