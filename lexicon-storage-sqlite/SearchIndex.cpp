// The full-text index behind item search. It is an FTS5 table next to the
// ordinary tables, and it is optional: a SQLite without FTS5 still opens the
// database and searches item content with a plain substring match.
//
// Nothing writes to the index while an item is saved. Every change to an item
// moves its revision on, and the index keeps the revision it last saw, so a
// search first re-indexes whatever changed since - including changes made by
// another program, or by a build that has no FTS5 at all.
#include "SqliteInternal.h"

#include <vector>

namespace storage {
namespace {
bool fts5Available(const Connection &db) {
  // Probed in the temporary schema, so the database file is never touched by
  // a SQLite that turns out not to have the module.
  try {
    db.exec("CREATE VIRTUAL TABLE temp.lexicon_fts5_probe USING fts5(probe);");
    db.exec("DROP TABLE temp.lexicon_fts5_probe;");
    return true;
  } catch (const Failure &) {
    return false;
  }
}

bool wordLike(unsigned char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
         (ch >= 'A' && ch <= 'Z') || ch >= 0x80;
}
} // namespace

bool openSearchIndex(const Connection &db) {
  if (!fts5Available(db)) return false;
  // unicode61 with remove_diacritics 2 lets "prilis" find "Příliš"; the
  // prefix indexes keep "decl" as fast as "decltype".
  db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS item_search USING fts5("
          "title, disambiguation, aliases, tags, flags, content, revision UNINDEXED, "
          "tokenize = 'unicode61 remove_diacritics 2', prefix = '2 3');");
  refreshSearchIndex(db);
  return true;
}

void refreshSearchIndex(const Connection &db) {
  std::vector<int> stale;
  std::vector<int> orphaned;
  {
    Statement changed(db, "SELECT i.id FROM item i LEFT JOIN item_search s ON s.rowid = i.id "
                          "WHERE s.rowid IS NULL OR s.revision IS NOT i.revision;");
    while (changed.step()) stale.push_back(changed.integer(0));
    Statement removed(db, "SELECT rowid FROM item_search WHERE rowid NOT IN (SELECT id FROM item);");
    while (removed.step()) orphaned.push_back(removed.integer(0));
  }
  if (stale.empty() && orphaned.empty()) return;
  Transaction tx(db, "lexicon_search");
  Statement remove(db, "DELETE FROM item_search WHERE rowid = ?;");
  Statement insert(db,
    "INSERT INTO item_search(rowid, title, disambiguation, aliases, tags, flags, content, revision) "
    "SELECT i.id, i.title, COALESCE(i.disambiguation, ''), "
    "COALESCE((SELECT GROUP_CONCAT(alias, ' ') FROM alias WHERE item_id = i.id), ''), "
    "COALESCE((SELECT GROUP_CONCAT(name, ' ') FROM tag WHERE item_id = i.id), ''), "
    "COALESCE((SELECT GROUP_CONCAT(name, ' ') FROM flag WHERE item_id = i.id), ''), "
    "COALESCE(i.content, ''), i.revision FROM item i WHERE i.id = ?;");
  for (int id : orphaned) { remove.bind(id).run(); remove.reset(); }
  for (int id : stale) {
    remove.bind(id).run(); remove.reset();
    insert.bind(id).run(); insert.reset();
  }
  tx.commit();
}

std::string fullTextQuery(const std::string &text) {
  // Every word must occur, each as a prefix, so typing narrows the results.
  // Words are quoted: the user's text is never FTS5 query syntax. A word with
  // fewer than two letters or digits - "C++", "a" - would match nearly every
  // note as a prefix, so only the substring search sees it.
  std::string query;
  std::size_t start = 0;
  while (start < text.size()) {
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' ||
                                   text[start] == '\n' || text[start] == '\r'))
      ++start;
    std::size_t end = start;
    while (end < text.size() && text[end] != ' ' && text[end] != '\t' &&
           text[end] != '\n' && text[end] != '\r')
      ++end;
    const std::string word = text.substr(start, end - start);
    start = end;
    int letters = 0;
    for (unsigned char ch : word)
      if (wordLike(ch)) ++letters;
    if (letters < 2) continue;
    if (!query.empty()) query += ' ';
    query += '"';
    for (char ch : word) query += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    query += "\"*";
  }
  return query;
}
} // namespace storage
