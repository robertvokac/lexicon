# Lexicon REST API v1

`LexiconServer` exposes JSON over HTTP and nothing else. It never serves HTML,
CSS, JavaScript, images or any other web asset, and it has no SPA fallback: the
web client is deployed separately as static files.

- Base path: `/api/v1`
- Content type: `application/json; charset=utf-8` (UTF-8 everywhere)
- Authentication: `Authorization: Bearer <token>` on every endpoint except
  `GET /api/v1/health` and `POST /api/v1/auth/login`

## Conventions

**Enums are names, never numbers.** The API uses stable symbolic values so it
survives changes to the C++ enum order:

| Concept | Values |
| --- | --- |
| `status` | `None`, `Draft`, `Completed` |
| `understanding` | `Unknown`, `Recognized`, `Understood`, `Practiced`, `Mastered` |
| `linkType` | `None`, `IsA`, `PartOf`, `Uses`, `DependsOn`, `Implements`, `Related`, `Contrasts`, `AlternativeTo`, `ParentOf`, `Custom` |
| `dataType` | `Integer`, `Float`, `Text`, `Date`, `Time`, `Timestamp`, `Boolean`, `Enum`, `Blob`, `Other` |
| `sortOrder` | `Ascending`, `Descending` |

An unknown name is rejected with HTTP 400.

**Absent IDs are `null`,** not `-1` or `0`. A type that is available in every
group has `"groupId": null`; an item without a type has `"itemTypeId": null`.

**IDs in the path win.** `PUT /api/v1/items/7` saves item 7 whatever the body
says, and `POST` rejects a body that already carries an ID.

### Errors

Every failure uses one envelope:

```json
{ "error": { "code": "validation", "message": "Type name cannot be empty." } }
```

| Status | Typical `code` | Meaning |
| --- | --- | --- |
| 400 | `validation`, `malformed_json` | Invalid body, unknown enum, malformed ID |
| 401 | `unauthorized` | Missing, unknown or expired token; wrong credentials |
| 403 | `origin_not_allowed` | The `Origin` header is not in the allowlist |
| 404 | `not_found` | No such record or endpoint |
| 413 | `payload_too_large` | Body over `--max-json-bytes` or `--max-blob-bytes` |
| 415 | `unsupported_media_type` | Wrong `Content-Type` |
| 429 | `too_many_requests` | Login rate limit; carries `Retry-After` |
| 500 | `storage`, `internal` | The server could not complete the operation |

Application errors map as `Validation → 400`, `NotFound → 404`,
`Storage → 500`. Storage messages stay on the server: SQL, file system paths,
stack traces, passwords and tokens never appear in a response.

## Health

```http
GET /api/v1/health
```

```json
{ "status": "ok", "apiVersion": 1, "application": "Lexicon" }
```

Unauthenticated, and deliberately free of configuration detail. Clients check
`apiVersion` before doing anything else.

## Authentication

```http
POST /api/v1/auth/login
{ "username": "...", "password": "..." }
```

```json
{
  "token": "3Qv...43 characters of base64url...",
  "username": "robert",
  "apiVersion": 1,
  "idleTimeoutSeconds": 28800,
  "absoluteLifetimeSeconds": 604800
}
```

The token is 256 bits of cryptographically random material. Failures return a
generic 401 that does not reveal whether the user name exists; repeated
failures return 429 with `Retry-After`.

```http
POST /api/v1/auth/logout   → 204, the token stops working immediately
GET  /api/v1/auth/me       → { "username": "robert", "apiVersion": 1 }
```

## Groups

```http
GET    /api/v1/groups           → { "groups": [ ... ] }
GET    /api/v1/groups/default   → { "groupId": 1 }
POST   /api/v1/groups           → 201 { "group": { ... } }
PUT    /api/v1/groups/{id}      → 200 { "group": { ... } }
DELETE /api/v1/groups/{id}      → 204
```

`GET /groups/default` returns the `Default` group, creating it if a historical
database has none. Deleting a group deletes the items inside it.

```json
{ "id": 1, "name": "Default", "description": "...", "position": 0 }
```

## Types and fields

```http
GET    /api/v1/types?groupId=3        → { "types": [ ... ] }
POST   /api/v1/types                  → 201 { "type": { ... } }
PUT    /api/v1/types/{id}             → 200 { "type": { ... } }
DELETE /api/v1/types/{id}             → 204
GET    /api/v1/types/{id}/item-count  → { "count": 12 }

GET    /api/v1/types/{id}/fields      → { "fields": [ ... ] }
POST   /api/v1/types/{id}/fields      → 201 { "field": { ... } }
PUT    /api/v1/fields/{id}            → 200 { "field": { ... } }
DELETE /api/v1/fields/{id}            → 204
GET    /api/v1/fields/{id}/value-count → { "count": 4 }
```

`groupId` filters to the types usable in that group, which includes the types
available in all groups. The two count endpoints exist so a client can warn
before a destructive change, exactly as the desktop dialogs do: deleting a type
clears the field values of every item using it, and changing a field's data
type or enum options clears its stored values.

```json
{ "id": 4, "itemTypeId": 2, "name": "Difficulty", "dataType": "Enum",
  "position": 0, "enumOptions": ["easy", "hard"] }
```

## Items

Filtering needs too many parameters for a query string, so the list endpoint is
a POST with a body:

```http
POST /api/v1/items/query
```

```json
{
  "groupId": null,
  "typeId": null,
  "searchText": "",
  "columnFilters": { "id": "", "title": "", "disambiguation": "", "alias": "" },
  "propertyFilters": [{ "key": "source", "value": "folklore" }],
  "valueFilters": [{ "fieldId": 4, "value": "hard", "exact": true }],
  "tagFilter": "",
  "flagFilter": "",
  "understandingFilter": null,
  "statusFilter": null,
  "pinnedFilter": null,
  "limit": 20,
  "offset": 0,
  "sortColumn": 3,
  "sortOrder": "Ascending"
}
```

Every field is optional. The semantics are the ones the application already
implements: property keys match exactly and their values are a contains match,
an empty property value matches any value for that key, `columnFilters.id` is
an exact ID, the other column filters and `searchText` are contains matches.
`sortColumn` is the column index used by both clients - 0 `Id`, 1 `Group`,
2 `Type`, 3 `Title`, 4 `Disambiguation`, 5 `Tags`, 6 `Flags`, 7 `Aliases`,
8 `Status`, 9 `Understanding`, 10 `Pinned`, and 11 onwards the fields of the
selected type in their display order. `limit` is capped at 1000.

The response carries the page and the total so clients need no second request:

```json
{ "items": [ ... ], "totalCount": 137 }
```

```http
GET    /api/v1/items/{id}                        → { "item": { ... } }
GET    /api/v1/items/{id}?include=links,backlinks
POST   /api/v1/items                             → 201 { "id": 7, "item": { ... } }
PUT    /api/v1/items/{id}                        → 200 { "id": 7, "item": { ... } }
DELETE /api/v1/items/{id}                        → 204
POST   /api/v1/items/{id}/read                   → 204 (records a read in the log)
GET    /api/v1/items/{id}/links                  → { "links": [ ... ] }
GET    /api/v1/items/{id}/backlinks              → { "backlinks": [ ... ] }
GET    /api/v1/items/resolve?title=&disambiguation= → { "itemId": 7 }
```

An item:

```json
{
  "id": 7,
  "groupId": 1,
  "groupName": "Default",
  "itemTypeId": 2,
  "itemTypeName": "Concept",
  "fieldValues": { "4": "hard" },
  "properties": [{ "key": "source", "value": "folklore" }],
  "title": "Monoid",
  "disambiguation": "algebra",
  "aliases": ["Semigroup with unit"],
  "tags": ["algebra"],
  "flags": ["todo"],
  "status": "Draft",
  "understanding": "Practiced",
  "pinned": false,
  "content": "# Monoid\n\nMarkdown source."
}
```

`fieldValues` is keyed by field ID as a string, because JSON object keys are
strings. Values keep the representation the database stores: `YYYY-MM-DD` for
`Date`, `HH:MM[:SS]` for `Time`, `YYYY-MM-DDTHH:MM[:SS]` for `Timestamp`,
`true`/`false` for `Boolean`, one of `enumOptions` for `Enum`, and a 64
character lowercase SHA-256 for `Blob`.

### Saving an item with its links

`POST /api/v1/items` and `PUT /api/v1/items/{id}` accept the item together with
both link directions and save them in one unit of work, the same transactional
path the desktop item dialog uses:

```json
{
  "item": { "groupId": 1, "title": "Group" },
  "links": [{ "id": null, "toItemId": 12, "linkType": "PartOf", "position": 1 }],
  "backlinks": [{ "id": 33, "fromItemId": 9, "linkType": "Uses", "position": 0 }]
}
```

The lists are the complete desired state: links present in the database but
missing from the request are deleted, entries with an `id` are updated, and
entries without one are created. `fromItemId` on outgoing links and `toItemId`
on backlinks are filled in from the saved item. If any part fails, nothing is
written. `Custom` links require a non-empty `customValue`; other types store
none; `None` is not a persistable link type.

## Links

Supplementary to the atomic save above:

```http
POST   /api/v1/links       → 201 { "links": [ ... ] }   (links of the source item)
PUT    /api/v1/links/{id}  → 200 { "links": [ ... ] }
DELETE /api/v1/links/{id}  → 204
```

## Search and usage

```http
GET /api/v1/search/suggestions  → { "values": ["Monoid", "Semigroup with unit"] }
GET /api/v1/search/item-titles  → { "values": ["Monoid [algebra]"] }
GET /api/v1/usage/tags          → { "values": [{ "value": "algebra", "usageCount": 2 }] }
GET /api/v1/usage/flags         → { "values": [ ... ] }
GET /api/v1/usage/aliases       → { "values": [ ... ] }
```

## Blobs

A browser cannot hand the server a local path, so blobs travel as bytes. The
server stages uploads in a file it names itself inside the database directory,
imports it through `BlobService`, and removes the staging file; no request ever
names a server-side path.

```http
POST /api/v1/blobs
Content-Type: application/octet-stream
<raw bytes>
```

```json
{ "hash": "6c7dbba2...99d98ca" }
```

```http
GET /api/v1/blobs/{hash}
→ application/octet-stream, Content-Disposition: attachment, X-Content-Type-Options: nosniff
```

The hash is the value to store in a `Blob` field. Uploads over
`--max-blob-bytes` are rejected with 413; a hash that is not 64 lowercase hex
characters is rejected with 400; an unknown hash is 404.

## CORS

The API is used from another origin, so CORS is explicit and exact:

- `--allowed-origin` may be repeated; matching is exact, and `*` is never used.
- An allowed origin gets `Access-Control-Allow-Origin: <that origin>` plus
  `Vary: Origin`.
- `OPTIONS` preflights answer 204 with `Access-Control-Allow-Methods:
  GET, POST, PUT, DELETE, OPTIONS` and `Access-Control-Allow-Headers:
  Authorization, Content-Type`.
- A request carrying an `Origin` that is not allowed is refused with 403.
- Credentials are never allowed, because the session travels in an explicit
  `Authorization` header rather than an automatically sent cookie. That is also
  why the API needs no CSRF token.

## Response headers

Every response carries `X-Content-Type-Options: nosniff`,
`Cache-Control: no-store`, `Referrer-Policy: no-referrer`,
`X-Frame-Options: DENY` and a restrictive `Content-Security-Policy`. When the
server terminates TLS itself it adds `Strict-Transport-Security`.

## Not exposed

- **Configuration.** The desktop keeps UI preferences in the database; the web
  client keeps its own in the browser, so neither overwrites the other.
- **Blob maintenance.** Scanning and garbage collecting the blob directory is
  local file system maintenance and stays with the desktop client and the
  server machine.
