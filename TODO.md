# Lexicon TODO List

This file tracks features, improvements, and fixes for the Lexicon application.

---

## 1. UI / UX Improvements

- [ ] **Table layout:**
    - Make columns resizable and prioritize key columns (Title, Status, Understanding).
    - Wrap long text in Tags / Aliases / Flags.
    - Possibly hide less important columns by default.

- [ ] **Markdown preview:**
    - Separate preview into a clear panel with border or header.
    - Add syntax highlighting for bold, italic, code, tables, etc.
    - Ensure tables display visible borders (convert Markdown table to HTML if needed).

- [ ] **Links / Backlinks panel:**
    - Move to a dedicated panel or bottom splitter.
    - Highlight as separate section with background color or border.

- [ ] **Toolbar / Buttons:**
    - Move Add / Edit / Delete to a consistent toolbar or sidebar.
    - Add icons for key actions.

- [ ] **General layout:**
    - Use `QSplitter` to allow resizing between table, preview, and links.
    - Improve spacing and alignment for readability.
  
- [ ] Bug: Adding new term, if no map is selected, uses the first map

---

## 2. Features / Enhancements

- [ ] **Relation types:**
    - Ensure all relation types (`IsA`, `PartOf`, `Uses`, etc.) are visible and selectable when adding links.
    - Consider bidirectional links for `Related` and `Contrasts`.

- [ ] **Filtering / Searching:**
    - Improve search box to handle multiple fields (title, alias, tag, flag).
    - Add quick filters for pinned / unpinned terms.

- [ ] **Markdown editor toolbar:**
    - Ensure all formatting buttons work correctly.
    - Add tooltips describing Markdown syntax.
    - Possibly support undo/redo for Markdown inserts.

- [ ] **Validation:**
    - Ensure a map exists before adding a term.
    - Check title is not empty before saving.

---

## 3. Database / Backend

- [ ] **Term relations table:**
    - Ensure correct mapping of relation types to SQLite.
    - Add optional `note` or `bidirectional` columns if needed.

- [ ] **Aliases / Tags / Flags tables:**
    - Verify proper linking to `dictionary_term`.
    - Ensure sorting and uniqueness of items.

- [ ] **Markdown content:**
    - Decide whether to store raw Markdown or HTML.
    - Ensure safe rendering in `QTextEdit`.

---

## 4. Future / Nice-to-Have

- [ ] Dark mode / theme support.
- [ ] Export / import terms (Markdown or JSON).
- [ ] Keyboard shortcuts for common actions (Add, Edit, Delete, Format).
- [ ] Undo/redo for term edits.
- [ ] Graph visualization of term relationships.

---

*Last updated: 2026-03-21*