# Lexicon

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)

Lexicon is a desktop knowledge dictionary built with Qt Widgets and SQLite.
It is designed for structured learning and technical note-taking with groups, items, metadata, and typed links between concepts.

## Table of contents

- [Highlights](#highlights)
- [Screenshots](#screenshots)
- [Requirements](#requirements)
- [Build and run](#build-and-run)
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
  - link types like `Is A`, `Part Of`, `Depends On`, `Related`, etc.
- Markdown item content editor with formatting toolbar and live preview
- Global read-only overviews for all tags, flags, and aliases
- Fast filtering and search:
  - search in title, disambiguation, alias, tag, and flag
  - per-column filters above the table headers, including type fields
- Pagination for large datasets
- Column sorting in the item table
- Theme switch: light mode and dark mode

## Screenshots

### Main window

![Main window](images/Screenshot.png)

The main screen combines filters, searchable item table, pagination, rendered Markdown content, and link/backlink preview.

### Item editor — General tab

![General tab](images/Screenshot_General.png)

Basic identity and state fields for an item: group, optional type, title, disambiguation, status, understanding, and pinned flag.

### Item editor — Values tab

Items with a type have a Values tab containing the fields defined by that type. Blob fields accept a file path (or Browse) and import the file when you click Import or save the item. The field stores its SHA-256 hash, and the file is kept in `blobs` next to the database.

### Item editor — Content tab

![Content tab](images/Screenshot_Content.png)

Markdown editor on the left, live rendered preview on the right, plus a formatting toolbar.

### Item editor — Metadata tab

![Metadata tab](images/Screenshot_Metadata.png)

Manage tags, flags, aliases, and additional key/value properties with dedicated add/edit/remove controls.

### Item editor — Links tab

![Links tab](images/Screenshot_Links.png)

Create and maintain outgoing relationships to other items.

### Item editor — Backlinks tab

![Backlinks tab](images/Screenshot_Backlinks.png)

Create and maintain incoming relationships (who references this item).

## Requirements

- CMake `3.21+`
- C++23-compatible compiler (GCC/Clang/MSVC)
- Qt 6 with:
  - `Widgets`
  - `Sql`
- SQLite Qt driver (usually included with Qt packages)

### Debian/Ubuntu example

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev libqt6sql6-sqlite
```

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Lexicon -j
./build/Lexicon
```

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

Search matches these fields:

- title
- disambiguation
- aliases
- tags
- flags

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

The preview updates automatically as you type.

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

### 11) Editing and deletion safety notes

- Deleting an item removes its aliases/tags/flags and related links due to cascade rules.
- Deleting a group removes all contained items.
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

Backup strategies:

1. Close Lexicon.
2. Copy `lexicon.db` and the adjacent `blobs` directory to safe storage.
3. Optionally version backups (daily/weekly snapshots).

## Troubleshooting

### App does not start (Qt plugin/driver issue)

- Verify Qt runtime installation.
- Ensure SQLite Qt SQL driver is installed.

### Database errors on startup

- Check write permissions for the executable directory.
- Ensure `lexicon.db` is not locked by another process.

### Build fails

- Confirm C++23 compiler support.
- Confirm `Qt6::Widgets` and `Qt6::Sql` are discoverable by CMake.

## Recent updates

- item table supports sorting by clicking column headers
- `New item` now prefills `Title` from current `Search` text

## Roadmap

- export to CSV/JSON
- add new unique indexes
- export to static HTML
- HTTP server
- improve search ranking so exact match appears first

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
