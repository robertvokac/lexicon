# Lexicon

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)
[![CI](https://github.com/robertvokac/lexicon/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/robertvokac/lexicon/actions/workflows/ci.yml)

Lexicon is a knowledge dictionary for structured learning and technical note-taking, with groups, items, metadata, and typed links between concepts.
It has three clients over one long-lived core: a Qt Widgets desktop application, and a static web client and a native Android app, both talking to a Qt-free REST server. All of them work on the same SQLite database, which only the desktop client and the server open.

## Table of contents

- [Highlights](#highlights)
- [Screenshots](#screenshots)
- [Requirements](#requirements)
- [Build and run](#build-and-run)
- [Architecture](#architecture)
- [REST server and web client](#rest-server-and-web-client)
- [Extensive user manual](#extensive-user-manual)
- [Database model](#database-model)
- [Data location and backup](#data-location-and-backup)
- [Troubleshooting](#troubleshooting)
- [Recent updates](#recent-updates)
- [Roadmap](#roadmap)
- [License](#license)

## Highlights

- SQLite-backed local dictionary with content-addressed blob files
- Full CRUD for groups and items
- Custom group order using a numeric position
- Optional item types shared across groups or scoped to one group, with ordered typed fields
- Rich item metadata:
  - aliases
  - tags
  - flags
  - status
  - understanding level
  - pinned state
  - additional key/value properties
- Typed item relationships:
  - outgoing links
  - incoming links (backlinks)
  - link types like `Is A`, `Part Of`, `Depends On`, `Related`, and `Custom` with a label
- Markdown item content editor with formatting toolbar and live preview
- Global read-only overviews for all tags, flags, and aliases
- Fast filtering and search:
  - search in title, disambiguation, alias, tag, flag, and Markdown content
  - full-text search of the content that ignores diacritics (`prilis` finds `Příliš`), with the exact title first
  - per-column filters above the table headers, including type fields
  - property key/value filters via `Filter Properties...`
- Pagination for large datasets
- Column sorting in the item table
- Theme switch: light mode and dark mode
- Export and import of the whole dictionary as one documented JSON file, optionally with its files, from every client and from the command line
- Review with spaced repetition: the items due now, the answer on request, and a rating that moves the understanding and sets the next review
- `[[Title]]` links between items in the Markdown content, which open the item or offer to create it
- A relationship graph of the items around one item, up to three links away
- A Qt-free REST server and an independently deployable static web client with the same capabilities
- A native Android client (Kotlin, Jetpack Compose) for the same server

## Screenshots

The web and Android screenshots share one small sample dictionary: C++ terms,
a few database and algorithm notes, and a `Term` type with `Standard`,
`Difficulty` and `Reviewed` fields.

- [Desktop client](#desktop-client)
- [Web client](#web-client)
- [Android client](#android-client)

### Desktop client

#### Main window

![Main window](images/Screenshot.png)

The main screen combines filters, searchable item table, pagination, rendered Markdown content, and link/backlink preview.

#### Item editor — General tab

![General tab](images/Screenshot_General.png)

Basic identity and state fields for an item: group, optional type, title, disambiguation, status, understanding, and pinned flag.

#### Item editor — Values tab

Items with a type have a Values tab containing the fields defined by that type. Blob fields accept a file path (or Browse) and import the file when you click Import or save the item. The field stores its SHA-256 hash, and the file is kept in `blobs` next to the database.

#### Item editor — Content tab

![Content tab](images/Screenshot_Content.png)

Markdown editor on the left, live rendered preview on the right, plus a formatting toolbar.

#### Item editor — Metadata tab

![Metadata tab](images/Screenshot_Metadata.png)

Manage tags, flags, aliases, and additional key/value properties with dedicated add/edit/remove controls.

#### Item editor — Links tab

![Links tab](images/Screenshot_Links.png)

Create and maintain outgoing relationships to other items.

#### Item editor — Backlinks tab

![Backlinks tab](images/Screenshot_Backlinks.png)

Create and maintain incoming relationships (who references this item).

### Web client

`lexicon-web/` in a desktop browser: the filtered item table with the rendered
content, links and backlinks of the selected item.

![Web client: item table and preview](images/Screenshot_Web.png)

The six-tab item editor, with the Markdown source next to its live preview.

![Web client: item editor, Content tab](images/Screenshot_Web_Editor.png)

On a phone-sized screen the same page turns into a card list with a filter
panel; this one uses the dark theme.

<img src="images/Screenshot_Web_Phone.png" alt="Web client on a phone-sized screen, dark theme" width="300">

### Android client

`lexicon-android/`, the native app. The items list with server-side search,
an item with its values and rendered Markdown, and the editor's Content and
Values tabs:

<p>
  <img src="images/Screenshot_Android_Items.png" alt="Android: items list" width="200">
  <img src="images/Screenshot_Android_Item.png" alt="Android: item page" width="200">
  <img src="images/Screenshot_Android_Content.png" alt="Android: editor, Content tab" width="200">
  <img src="images/Screenshot_Android_Values.png" alt="Android: editor, Values tab" width="200">
</p>

Filters and sorting, the navigation drawer, and the dark theme:

<p>
  <img src="images/Screenshot_Android_Filters.png" alt="Android: filters and sort" width="200">
  <img src="images/Screenshot_Android_Navigation.png" alt="Android: navigation drawer" width="200">
  <img src="images/Screenshot_Android_Dark_Items.png" alt="Android: items list, dark theme" width="200">
  <img src="images/Screenshot_Android_Dark_Item.png" alt="Android: item with code block and table, dark theme" width="200">
</p>

On screens at least 840 dp wide, such as tablets, the list and the selected item
sit side by side:

![Android: two-pane layout on a tablet](images/Screenshot_Android_Tablet.png)

## Requirements

- CMake `3.21+` and a C++23-compatible compiler
- SQLite 3.38+ with JSON functions
- OpenSSL `libcrypto` for SHA-256 blob identifiers, plus `libssl` for the server's HTTPS support
- Qt 6 `Core` and `Widgets` only for the desktop client
- A current browser for the web client; it needs no build tools at all

### Debian/Ubuntu example

```bash
sudo apt update
sudo apt install -y build-essential cmake libsqlite3-dev libssl-dev qt6-base-dev
```

The server alone needs neither `qt6-base-dev` nor a display.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Lexicon -j
./build/Lexicon
```

The REST server is built by the same tree and needs no Qt:

```bash
cmake --build build --target LexiconServer -j
./build/LexiconServer auth set-user --database ~/lexicon.db
./build/LexiconServer --database ~/lexicon.db --allowed-origin http://127.0.0.1:8080
```

Turn either client off with `-DLEXICON_BUILD_DESKTOP=OFF` or
`-DLEXICON_BUILD_SERVER=OFF`.

### Tests and continuous integration

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
node --test lexicon-web/tests/
```

The Android app has its own Gradle build; see
[lexicon-android/README.md](lexicon-android/README.md#tests).

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs all of this on
every push and pull request, on Ubuntu 24.04: it builds the backend, the
server and the desktop client, runs every CTest suite - the desktop dialogs
offscreen - and the web client's tests, then runs the Android unit, Compose
and lint checks and the Android tests against the `LexiconServer` the first
job built, and builds the debug and release APKs. Started by hand with
**device-tests**, it also runs the instrumented tests on an emulator.

## Architecture

| Target | Responsibility and allowed dependencies |
| --- | --- |
| `lexicon-core` | Records, validation, and string comparison policy; standard C++ only. |
| `lexicon-application` | Repository interface and services; depends on core only. |
| `lexicon-storage-sqlite` | Native SQLite repository, migrations, transactions, and blob files; depends on application, SQLite C API, and OpenSSL Crypto. No Qt. |
| `lexicon-qt-bridge` | Converts UTF-8 standard C++ values to and from Qt values; depends on core and QtCore. |
| `Lexicon` (`lexicon-qt/`) | Qt Widgets frontend and composition root; injects `SqliteRepository` into `LexiconApplication`. No SQL in Widgets. |
| `lexicon-json` | The JSON representation of records, shared by the REST API and the [export format](docs/export-format.md); export and import. Depends on application and vendored nlohmann/json. No Qt. |
| `lexicon-http` | HTTP adapter: authentication, sessions, CORS, TLS, routes. Depends on application, `lexicon-json`, vendored cpp-httplib, and OpenSSL. No Qt. |
| `LexiconServer` (`lexicon-server/`) | Server composition root; injects `SqliteRepository` into `LexiconApplication` and serves `lexicon-http`. No Qt. |
| `lexicon-web/` | Static HTML, CSS and vanilla JavaScript client. No C++, no framework, no build step. Talks only REST. |
| `lexicon-android/` | Native Android client in Kotlin and Jetpack Compose, a separate Gradle project outside the CMake build. No database of its own, no C++. Talks only REST. |

Text in core, application, and storage is UTF-8 `std::string`. Qt converts at the desktop boundary. Public operations return `std::expected<T, lexicon::Error>`. `Repository` is the application boundary; only the SQLite adapter owns `sqlite3` handles, statements, schema migrations, and transactions. RAII finalizes statements and rolls back incomplete savepoints. The application owns the item plus links *unit of work*: `ItemService::saveItemWithLinks` begins it, saves the item and links, then commits or rolls back. `createItem` uses the same path and accepts optional links. The repository also uses nested savepoints for each write. Migration versions and schema are unchanged; old QtSql databases are covered by version 10 and version 20 compatibility fixtures.

Case-insensitive searches, metadata deduplication, suggestions, and schema constraints using `NOCASE` fold ASCII letters only. UTF-8 bytes outside ASCII compare exactly. Exact lookups and constraints without `NOCASE` remain byte-exact. SQLite's `NOCASE`, `LOWER`, and default `LIKE` use the same ASCII case policy. The earlier QtSql adapter used Qt Unicode case folding while deduplicating some metadata; that was incidental to storage, inconsistent with core validation and SQLite indexes/search. After this cleanup, `É` and `é` are distinct everywhere. This is an intentional matching policy, not Unicode case folding. The one exception is the full-text search index (below), which folds case and diacritics in every script.

Item search uses an SQLite FTS5 table, `item_search`, over titles, disambiguations, aliases, tags, flags and content, created when the database is opened by a SQLite that has FTS5. Nothing writes to it while an item is saved: the index keeps the revision of each item it indexed, and a search first re-indexes the items whose revision moved on, so it also follows changes made by another program. A SQLite without FTS5 opens the same database and searches content as a plain substring.

Qt is limited to the frontend and conversion target so another client can use core, application, and native SQLite without Qt. `LexiconServer` is exactly that second client: it composes `SqliteRepository` with `LexiconApplication` and adds an HTTP adapter, without duplicating a single domain rule.

```text
                   Qt Widgets (lexicon-qt)
                             |
                             v
                    LexiconApplication --------------+
                             ^                       |
                             |                       |
                   SQLite Repository            HTTP Adapter
                  (lexicon-storage-sqlite)      (lexicon-http)
                             |                       |
                             v                       v
                        lexicon.db              REST / JSON
                                                     |
                                                     v
                                     static lexicon-web, lexicon-android
```

**`LexiconServer` never serves `lexicon-web`.** The server answers versioned REST/JSON under `/api/v1` and nothing else; the web client is static content deployed separately, possibly on a completely different host.

Qt-free backend build and tests:

```bash
cmake -S . -B build-headless -DLEXICON_BUILD_DESKTOP=OFF -DLEXICON_BUILD_SERVER=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build-headless -j
ctest --test-dir build-headless --output-on-failure
```

This builds and tests the backend and the server, including the REST,
authentication and concurrency suites, with Qt unavailable.

Desktop build and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## REST server and web client

`LexiconServer` exposes the application services over HTTP so a browser can use
the same database as the desktop client:

```bash
LexiconServer auth set-user --database ~/lexicon/lexicon.db   # asks interactively
LexiconServer --database ~/lexicon/lexicon.db \
  --allowed-origin https://lexicon.example.com
```

It binds `127.0.0.1:8628` by default, requires a Bearer session for every
domain endpoint, hashes the password with scrypt, rate limits failed logins,
and refuses to serve password authentication over plaintext HTTP on a public
address unless you override it explicitly.

`lexicon-web/` is the browser client: HTML, CSS and vanilla JavaScript modules
with no bundler, no transpiler and no `npm install`. Copy the directory to any
static host, point it at the API and log in. It reproduces the desktop
workflows - the filtered item table, the six-tab item editor, group and type
management, the overview dialogs, Markdown editing with live preview, and light
and dark themes - and adapts to phones with a responsive layout.

- [`docs/server.md`](docs/server.md) - build, run, TLS, reverse proxies, the security model
- [`docs/rest-api.md`](docs/rest-api.md) - every endpoint, the JSON shapes, the error format
- [`lexicon-web/README.md`](lexicon-web/README.md) - static deployment and browser storage

`lexicon-android/` is the Android client: Kotlin, Jetpack Compose and Material 3,
with the same items, filters, editor, group and type management and overviews
as the other clients, a two-pane layout on tablets, and Share to Lexicon from
other apps. It keeps no Lexicon database; the server is the source of truth.

- [`lexicon-android/README.md`](lexicon-android/README.md) - build, run, security, feature parity

## Extensive user manual

### 1) First launch

1. Start the app.
2. Lexicon creates/opens `lexicon.db` automatically in the executable directory.
3. Database migrations are applied automatically on startup.

### 2) Create and manage groups

Groups are top-level buckets for items (for example: `C++`, `Databases`, `Networking`).
Lexicon creates a `Default` group automatically. With `All groups` selected, new items go there unless the Type filter selects a type scoped to another group.

1. Open `Manage` → `Groups...`.
2. Use:
   - `Add` to create a new group
   - `Edit` to rename a group, change its description, or set its position (lower numbers appear first)
   - `Delete` to remove a group
3. Important: deleting a group also deletes all items in that group (cascade delete).

### 3) Explore items in the main window

Use the row above the table's column names to combine filters:

- `Id`, `Title`, `Disambiguation`, and `Aliases` text filters
- `Group`
- `Type` (`All types` or a specific type; choices follow the selected group)
- `Tags`
- `Flags`
- `Status`
- `Understanding`
- `Pinned`

Selecting a type adds filters above its value columns. The separate `Search` box searches across multiple fields.
Use `Filter Properties...` to add, edit, remove, or clear property filters. All listed filters must match; keys match exactly without case sensitivity, and values match contained text. Leave a value blank to match any value for the key.

Search matches these fields:

- title
- disambiguation
- aliases
- tags
- flags
- content

Titles and the other short fields match any part of the text, ignoring ASCII case. Content is searched word by word: every word you type must occur, as the start of a word, and case and diacritics do not matter (`zlutoucky kun` finds `žluťoučký kůň`). Text without such a word, like `C++`, is matched as a substring of the content.

Results are ranked: the item titled exactly what you typed comes first, then items with it as an alias, titles starting with it, titles containing it, items where it is a disambiguation, alias, tag or flag, and last the items that only mention it in their content. The column you sort by orders the items within each of these groups.

The central table supports:

- row selection to show content/details below
- double click to edit an item
- sorting by clicking column headers

### 4) Pagination and large dictionaries

Lexicon is optimized for larger datasets with paging controls:

- `<< First`
- `< Prev`
- `Next >`
- `Last >>`
- `Page size` selector

Use these controls to browse large lexicons without loading everything into one visible page.

### 5) Create a new item

You have two add options in the main toolbar:

- `Add`: quick add path; title is prefilled from current search text
- `Add ...`: full add dialog path

Both actions use the selected Type filter for the new item. With `All groups` selected, a group-scoped type also determines the new item's group.

Recommended workflow:

1. Select the target group, or leave `All groups` selected to use `Default` when no group-scoped type is selected.
2. Click `Add` or `Add ...`.
3. Fill the General tab:
   - `Title` (required)
   - optional `Type`
   - optional `Disambiguation` (useful for same title in one group)
   - `Status` (`None`, `Draft`, `Completed`)
   - `Understanding` (`Unknown` → `Mastered`)
   - `Pinned`
4. If a type is selected, fill its fields on the `Values` tab. Fill other tabs as needed.
5. Click `Save`.

### 6) Edit item content with Markdown

Open an item and go to the `Content` tab.

- Left pane: raw Markdown text
- Right pane: rendered preview
- Top toolbar shortcuts:
  - bold, italic
  - H2/H3/H4 headings
  - lists and quote
  - horizontal rule
  - inline code and code block
  - link and table insertion
  - `[[ ]]`: a link to another item, chosen from the item titles

The preview updates automatically as you type.

#### Links to other items

Write `[[Title]]` to link to another item by its title. `[[Monoid [algebra]]]` picks the item with that disambiguation, and `[[Monoid|monoids]]` shows its own text. A title is matched exactly first, then ignoring ASCII case, then as an alias, so `[[forwarding reference]]` finds `universal reference` if that is one of its aliases. Code spans and code blocks keep their brackets.

In the rendered content a wiki link opens its item; when there is none, Lexicon offers to create it with that title. On the `Links` tab, **Add links from content** adds a `Related` link to every item the content names, and lists the names no item has. The web client and the Android app do the same.

### 7) Maintain metadata (tags, flags, aliases, properties)

In `Metadata` tab:

- `Tags`: classification labels (topics, versions, domains)
- `Flags`: custom markers (priority/state markers)
- `Aliases`: alternate names and synonyms
- `Properties`: additional key/value pairs for this item

Each list supports `Add`, `Edit`, `Remove`.

Use `Manage` → `Types...` to create types with a name, description, and availability across all groups or within one group. Each type can have ordered fields with integer, float, text, date, time, timestamp, boolean, enum, blob, or other values. Enum fields have an editable list of choices. A blob field stores a file's SHA-256 hash in SQLite and its bytes in the `blobs` directory. Deleting a type clears the Type and its custom field values on affected items; the dialog asks for confirmation.

Tips:

- Keep tags consistent (e.g., `cpp20`, `templates`, `concurrency`)
- Use aliases for alternate spellings and abbreviations
- Use flags for operational workflows (e.g., `review`, `needs-example`)

### 8) Create links and backlinks

Use `Links` and `Backlinks` tabs in the item editor.
`Custom` links require a nonempty Custom Value. Other link types store no Custom Value.

- `Links`: create outgoing relationship from current item to a target item
- `Backlinks`: create incoming relationship from a source item to current item

Available relation types include:

- `Is A`
- `Part Of`
- `Uses`
- `Depends On`
- `Implements`
- `Related`
- `Contrasts`
- `Alternative To`
- `Parent Of`

These relationships help build a concept graph and improve navigation context.

### 9) Read content and relationship context

When you select an item in the main table:

- the lower content pane renders the item’s Markdown as HTML
- links/backlinks summary is shown below the content

This gives quick context while browsing without opening the edit dialog every time.

`View -> Relationship graph...` (`Ctrl+G`) draws the items around the selected one: the selected item in the middle, the items it links to and the items linking to it around it, as arrows labelled with the link type. **Depth** reaches one, two or three links away; at most 150 items are drawn, and the dialog says when more are in reach. Click an item to centre the graph on it, double-click it to open it in the main window; the wheel zooms and dragging moves the view. The web client offers the same from its `View` menu and from the link preview, the Android app from the item page's menu.

### 10) Global overviews and themes

From the menu:

- `View` → `All tags...`
- `View` → `All flags...`
- `View` → `All aliases...`

Use these to inspect normalized values and usage counts.

Appearance:

- `View` → `Light mode`
- `View` → `Dark mode`

Theme preference is persisted between sessions.

### 11) Review what you learned

`View -> Review...` (`Ctrl+R`) goes through the items that are due, one card at a time: the title first, then **Show answer** for the rendered content, then a rating:

| Rating | Understanding | Next review for an `Understood` item |
| --- | --- | --- |
| `Again` (key 1) | one level down | 2 days, and once more in this sitting |
| `Hard` (key 2) | unchanged | 5 days |
| `Good` (key 3) | one level up | 12 days |
| `Easy` (key 4) | two levels up | 30 days |

An item is due again 1, 2, 5, 12 or 30 days after its last review, for `Unknown`, `Recognized`, `Understood`, `Practiced` and `Mastered`. Items never reviewed are due at once and come after the overdue ones. The group box limits the review to one group; **Skip** leaves an item for later. The web client (`View -> Review...`) and the Android app (**Review** in the drawer) show the same queue.

### 12) Editing and deletion safety notes

- Deleting an item removes its aliases/tags/flags and related links due to cascade rules.
- Deleting a group removes all contained items.
- Every item has a revision that moves on with each change to it, its values or its links. If another client (the desktop, the web client or the Android app) saved an item after you opened it, your save is not written. Lexicon lists what differs and lets you **Overwrite** the newer version, **Reload** it and drop your changes, or go back to editing.
- Keep regular backups if your lexicon is mission-critical.

## Database model

Lexicon initializes and migrates schema automatically.

Core tables:

- `item_group`
- `item`
- `item_type`
- `item_field`
- `item_value`
- `property`
- `alias`
- `tag`
- `flag`
- `link`
- `log`
- `configuration`

Design notes:

- Foreign keys enabled
- Cascade delete used for dependent records
- Unique constraints for group names and per-item value deduplication

## Data location and backup

- Default DB file: `lexicon.db`
- Location: next to the executable binary
- Blob files: `blobs/<first two hash characters>/<remaining hash characters>` next to `lexicon.db`

**The SQLite database and Blob directory together form the complete Lexicon data set.**
Backing up only `lexicon.db` is insufficient when Blob Fields are used.

`File -> Export...` writes the whole dictionary as one JSON file, optionally with the files Blob values refer to; `File -> Import...` merges such a file into the open dictionary, matching groups, types and fields by name and leaving items that are already there untouched. The web client and the Android app offer the same, and `LexiconServer export` and `LexiconServer import` do it from the command line. See [docs/export-format.md](docs/export-format.md).

### Blob lifecycle and maintenance

Blob identity is the lowercase hexadecimal SHA-256 digest of the file bytes. Identical
files share one physical Blob, even when multiple Items or Blob Fields reference it.
Import writes a temporary file, checks its digest, then installs the complete file at
the canonical path. Temporary files are created exclusively so an existing path
cannot be opened or truncated by mistake. An existing healthy file with the same
digest is reused. Saving an Item with Blob values checks the canonical files and
their SHA-256 hashes inside the Item's database transaction. If GC removed an
unreferenced import before Item save, the save fails and rolls back; it cannot
commit a new dangling Blob reference.

Clearing a Blob value or deleting an Item, Field, or Type removes database references
but deliberately retains the physical file. An **orphan** is a canonical Blob file
with no current Blob Field value referencing its hash. This permits recovery from
accidental edits and protects shared files. Only **Tools → Blob maintenance… →
Delete unused blobs…**, after an explicit scan and confirmation, removes orphans.
There is no scheduled or automatic Blob garbage collection.

The **Scan** action checks current database references, canonical paths, sizes,
missing files, and unused files without reading every Blob's contents. **Full
integrity check** additionally recalculates SHA-256 for each canonical file.
**Missing** means a Blob Field references bytes absent from the expected path.
**Hash mismatch** means the bytes at a canonical path do not match its hash.
Unexpected files, directories, and symlinks are reported separately. Missing and
corrupt Blobs are never deleted or repaired by GC; investigate them and restore
them from a complete backup. GC refreshes database references and rehashes each
candidate immediately before removal. Failed and skipped deletions are reported.

Backup strategies:

1. Close Lexicon, or take a coordinated snapshot while data is not changing.
2. Copy `lexicon.db` and the adjacent `blobs` directory to safe storage.
3. Optionally version backups (daily/weekly snapshots).

## Troubleshooting

### App does not start

- Verify Qt runtime installation.
- Ensure native SQLite and OpenSSL Crypto libraries are installed.

### Database errors on startup

- Check write permissions for the executable directory.
- Ensure `lexicon.db` is not locked by another process.

### Build fails

- Confirm C++23 compiler support.
- Confirm Qt Core/Widgets, SQLite, and OpenSSL Crypto are discoverable by CMake.

## Recent updates

- Qt-free `LexiconServer` with a versioned REST/JSON API, single-user authentication and TLS
- `lexicon-web`, an independently deployable static web client with desktop feature parity
- `lexicon-android`, a native Android client (Kotlin, Jetpack Compose) for `LexiconServer`
- saves refused with a choice when another client changed the item meanwhile; content search that ignores diacritics; sessions that survive a server restart; export and import; review with spaced repetition; `[[wiki links]]` between items; a relationship graph
- item table supports sorting by clicking column headers
- `New item` now prefills `Title` from current `Search` text

## Roadmap

- export to CSV/JSON
- add new unique indexes
- export to static HTML

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
