# Lexicon

Lexicon is a Qt Widgets + SQLite desktop application for managing maps, terms, aliases, tags, and flags.

## Features

- SQLite-backed knowledge dictionary
- CRUD for maps and terms
- Per-term aliases, tags, and flags stored in normalized tables
- Search by title, disambiguation, alias, tag, and flag
- Autocomplete from existing term titles and aliases
- Read-only global overviews for tags, flags, and aliases
- CMake-based build

## Database schema

The app initializes this schema automatically:

- `map`
- `term`
- `alias`
- `tag`
- `flag`

Foreign keys are enabled and cascade deletes are used for term child rows.

## Build

### Qt 6

```bash
# Debian
sudo apt update
sudo apt install qt6-base-dev
```

```bash
cmake -S . -B build
cmake --build build
```

### Run

```bash
./build/Lexicon
```

## Notes

- The application stores data in `lexicon.db` next to the executable by default.
- The UI is intentionally code-only, without `.ui` files, to keep the project portable and easy to review.


## Recent update

- term table now supports sorting by clicking column headers
- `New term` now prefills `Title` from the current `Search` text

## Todo

- export to CSV/JSON
- add new unique indexes
- export to static html
- http server