#include "SqliteInternal.h"
#include <vector>

namespace storage {
void applyMigrations(const Connection &db) {
  db.exec("CREATE TABLE IF NOT EXISTS db_version (version INTEGER PRIMARY KEY);");
  int currentVersion = 0;
  {
    Statement version(db, "SELECT version FROM db_version LIMIT 1;");
    if (version.step()) currentVersion = version.integer(0);
    else db.exec("INSERT INTO db_version (version) VALUES (0);");
  }
  // A migration that rebuilds a table runs with foreign keys off, or dropping
  // the old table would cascade into the rows that refer to it. The pragma has
  // no effect inside a transaction, so it is set around it, and the rebuilt
  // schema must pass foreign_key_check before the migration commits.
  struct Migration { int version; std::vector<std::string> statements; bool rebuildsTables = false; };
    const std::vector<Migration> migrations = {
        {1, {
            "CREATE TABLE IF NOT EXISTS map ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name TEXT NOT NULL,"
            " description TEXT NOT NULL DEFAULT ''"
            ");",
            "CREATE UNIQUE INDEX IF NOT EXISTS map_name_unique ON map(name);",
            "CREATE TABLE IF NOT EXISTS term ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " map_id INTEGER NOT NULL,"
            " title TEXT NOT NULL,"
            " disambiguation TEXT,"
            " FOREIGN KEY(map_id) REFERENCES map(id) ON DELETE CASCADE"
            ");",
            "CREATE UNIQUE INDEX IF NOT EXISTS term_unique "
            "ON term(map_id, title, COALESCE(disambiguation, ''));",
            "CREATE TABLE IF NOT EXISTS alias ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " term_id INTEGER NOT NULL,"
            " alias TEXT NOT NULL,"
            " UNIQUE(term_id, alias),"
            " FOREIGN KEY(term_id) REFERENCES term(id) ON DELETE CASCADE"
            ");",
            "CREATE TABLE IF NOT EXISTS flag ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " term_id INTEGER NOT NULL,"
            " name TEXT NOT NULL,"
            " UNIQUE(term_id, name),"
            " FOREIGN KEY(term_id) REFERENCES term(id) ON DELETE CASCADE"
            ");",
            "CREATE TABLE IF NOT EXISTS tag ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " term_id INTEGER NOT NULL,"
            " name TEXT NOT NULL,"
            " UNIQUE(term_id, name),"
            " FOREIGN KEY(term_id) REFERENCES term(id) ON DELETE CASCADE"
            ");",
            "CREATE INDEX IF NOT EXISTS idx_term_map_id ON term(map_id);",
            "CREATE INDEX IF NOT EXISTS idx_alias_term_id ON alias(term_id);",
            "CREATE INDEX IF NOT EXISTS idx_tag_term_id ON tag(term_id);",
            "CREATE INDEX IF NOT EXISTS idx_flag_term_id ON flag(term_id);",
            "CREATE INDEX IF NOT EXISTS idx_term_title ON term(title);",
            "CREATE INDEX IF NOT EXISTS idx_alias_alias ON alias(alias);",
            "CREATE INDEX IF NOT EXISTS idx_tag_name ON tag(name);",
            "CREATE INDEX IF NOT EXISTS idx_flag_name ON flag(name);"
        }},
        {2, {
            "ALTER TABLE term ADD COLUMN status INTEGER NOT NULL DEFAULT 0;"
        }},
        {3, {
            "ALTER TABLE term ADD COLUMN understanding INTEGER NOT NULL DEFAULT 0;"
        }},
        {4, {
            "ALTER TABLE term ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0;"
        }},
        {5, {
            "CREATE TABLE log ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " table_name TEXT NOT NULL,"
            " record_id INTEGER NOT NULL,"
            " log_type INTEGER NOT NULL," // 1=created, 2=updated, 3=deleted, 4=read, 5=reviewed
            " happened_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
        }},
        {6, {
            "ALTER TABLE term ADD COLUMN content TEXT;"
        }},
        {7, {
            "CREATE TABLE IF NOT EXISTS link ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " from_term_id INTEGER NOT NULL,"
            " to_term_id INTEGER NOT NULL,"
            " link_type INTEGER NOT NULL DEFAULT 0,"
            " FOREIGN KEY(from_term_id) REFERENCES term(id) ON DELETE CASCADE,"
            " FOREIGN KEY(to_term_id) REFERENCES term(id) ON DELETE CASCADE"
            ");",
            "CREATE INDEX IF NOT EXISTS idx_link_from_term_id ON link(from_term_id);",
            "CREATE INDEX IF NOT EXISTS idx_link_to_term_id ON link(to_term_id);"
        }},
        {8, {
            "ALTER TABLE link ADD COLUMN position INTEGER NOT NULL DEFAULT 0;"
        }},
        {9, {
            "ALTER TABLE link ADD COLUMN custom_value TEXT NOT NULL DEFAULT '';"
        }},
        {10, {
            "ALTER TABLE map RENAME TO item_group;",
            "ALTER TABLE term RENAME COLUMN map_id TO group_id;",
            "DROP INDEX map_name_unique;",
            "CREATE UNIQUE INDEX item_group_name_unique ON item_group(name);",
            "DROP INDEX idx_term_map_id;",
            "CREATE INDEX idx_term_group_id ON term(group_id);"
        }},
        {11, {
            "ALTER TABLE term RENAME TO item;",
            "ALTER TABLE alias RENAME COLUMN term_id TO item_id;",
            "ALTER TABLE tag RENAME COLUMN term_id TO item_id;",
            "ALTER TABLE flag RENAME COLUMN term_id TO item_id;",
            "ALTER TABLE link RENAME COLUMN from_term_id TO from_item_id;",
            "ALTER TABLE link RENAME COLUMN to_term_id TO to_item_id;",
            "DROP INDEX term_unique;",
            "CREATE UNIQUE INDEX item_unique ON item(group_id, title, COALESCE(disambiguation, ''));",
            "DROP INDEX idx_term_group_id;",
            "CREATE INDEX idx_item_group_id ON item(group_id);",
            "DROP INDEX idx_term_title;",
            "CREATE INDEX idx_item_title ON item(title);",
            "DROP INDEX idx_alias_term_id;",
            "CREATE INDEX idx_alias_item_id ON alias(item_id);",
            "DROP INDEX idx_tag_term_id;",
            "CREATE INDEX idx_tag_item_id ON tag(item_id);",
            "DROP INDEX idx_flag_term_id;",
            "CREATE INDEX idx_flag_item_id ON flag(item_id);",
            "DROP INDEX idx_link_from_term_id;",
            "CREATE INDEX idx_link_from_item_id ON link(from_item_id);",
            "DROP INDEX idx_link_to_term_id;",
            "CREATE INDEX idx_link_to_item_id ON link(to_item_id);",
            "UPDATE log SET table_name = 'item' WHERE table_name = 'term';"
        }},
        {12, {
            "ALTER TABLE item_group ADD COLUMN position INTEGER NOT NULL DEFAULT 0;",
            "UPDATE item_group SET position = ("
            " SELECT COUNT(*) FROM item_group AS earlier"
            " WHERE earlier.name COLLATE NOCASE < item_group.name COLLATE NOCASE"
            " OR (earlier.name COLLATE NOCASE = item_group.name COLLATE NOCASE"
            " AND earlier.id < item_group.id)"
            ");",
            "CREATE INDEX idx_item_group_position ON item_group(position, name COLLATE NOCASE);"
        }},
        {13, {
            "INSERT INTO item_group(name, description, position) "
            "SELECT 'Default', 'Default group for new items when no group is selected.', "
            "COALESCE((SELECT MAX(position) + 1 FROM item_group), 0) "
            "WHERE NOT EXISTS (SELECT 1 FROM item_group WHERE name = 'Default');",
            "UPDATE item_group SET description = 'Default group for new items when no group is selected.' "
            "WHERE name = 'Default' AND TRIM(description) = '';"
        }},
        {14, {
            "CREATE TABLE item_type ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " group_id INTEGER,"
            " name TEXT NOT NULL CHECK(TRIM(name) <> ''),"
            " FOREIGN KEY(group_id) REFERENCES item_group(id) ON DELETE CASCADE"
            ");",
            "CREATE UNIQUE INDEX item_type_scope_name_unique "
            "ON item_type(COALESCE(group_id, 0), name COLLATE NOCASE);",
            "CREATE INDEX idx_item_type_group_id ON item_type(group_id);",
            "ALTER TABLE item ADD COLUMN item_type_id INTEGER REFERENCES item_type(id) ON DELETE SET NULL;",
            "CREATE INDEX idx_item_item_type_id ON item(item_type_id);",
            "CREATE TRIGGER item_type_scope_insert BEFORE INSERT ON item "
            "WHEN NEW.item_type_id IS NOT NULL AND NOT EXISTS ("
            " SELECT 1 FROM item_type ty WHERE ty.id = NEW.item_type_id"
            " AND (ty.group_id IS NULL OR ty.group_id = NEW.group_id)) "
            "BEGIN SELECT RAISE(ABORT, 'Type is not available for this group'); END;",
            "CREATE TRIGGER item_type_scope_update BEFORE UPDATE OF group_id, item_type_id ON item "
            "WHEN NEW.item_type_id IS NOT NULL AND NOT EXISTS ("
            " SELECT 1 FROM item_type ty WHERE ty.id = NEW.item_type_id"
            " AND (ty.group_id IS NULL OR ty.group_id = NEW.group_id)) "
            "BEGIN SELECT RAISE(ABORT, 'Type is not available for this group'); END;",
            "CREATE TRIGGER item_type_group_update BEFORE UPDATE OF group_id ON item_type "
            "WHEN NEW.group_id IS NOT NULL AND EXISTS ("
            " SELECT 1 FROM item WHERE item_type_id = OLD.id AND group_id <> NEW.group_id) "
            "BEGIN SELECT RAISE(ABORT, 'Type is used by items in another group'); END;"
        }},
        {15, {
            "CREATE TABLE item_field ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " item_type_id INTEGER NOT NULL,"
            " name TEXT NOT NULL CHECK(TRIM(name) <> ''),"
            " data_type INTEGER NOT NULL CHECK(data_type BETWEEN 0 AND 9),"
            " position INTEGER NOT NULL DEFAULT 0,"
            " enum_options TEXT NOT NULL DEFAULT '[]',"
            " FOREIGN KEY(item_type_id) REFERENCES item_type(id) ON DELETE CASCADE"
            ");",
            "CREATE UNIQUE INDEX item_field_type_name_unique ON item_field(item_type_id, name COLLATE NOCASE);",
            "CREATE INDEX idx_item_field_type_position ON item_field(item_type_id, position);",
            "CREATE TABLE item_field_value ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " item_id INTEGER NOT NULL,"
            " item_field_id INTEGER NOT NULL,"
            " value TEXT NOT NULL,"
            " UNIQUE(item_id, item_field_id),"
            " FOREIGN KEY(item_id) REFERENCES item(id) ON DELETE CASCADE,"
            " FOREIGN KEY(item_field_id) REFERENCES item_field(id) ON DELETE CASCADE"
            ");",
            "CREATE INDEX idx_item_field_value_field_id ON item_field_value(item_field_id);",
            "CREATE TRIGGER item_field_value_scope_insert BEFORE INSERT ON item_field_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "CREATE TRIGGER item_field_value_scope_update BEFORE UPDATE ON item_field_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "CREATE TRIGGER item_type_value_cleanup AFTER UPDATE OF item_type_id ON item "
            "WHEN OLD.item_type_id IS NOT NEW.item_type_id "
            "BEGIN DELETE FROM item_field_value WHERE item_id = NEW.id; END;"
        }},
        {16, {
            "CREATE TABLE property ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " item_id INTEGER NOT NULL,"
            " \"key\" TEXT NOT NULL CHECK(TRIM(\"key\") <> ''),"
            " value TEXT NOT NULL DEFAULT '',"
            " FOREIGN KEY(item_id) REFERENCES item(id) ON DELETE CASCADE"
            ");",
            "CREATE UNIQUE INDEX property_item_key_unique ON property(item_id, \"key\" COLLATE NOCASE);",
            "CREATE INDEX idx_property_item_id ON property(item_id);"
        }},
        {17, {
            "ALTER TABLE item_field RENAME TO item_type_field;",
            "ALTER TABLE item_field_value RENAME TO item_value;",
            "DROP INDEX item_field_type_name_unique;",
            "CREATE UNIQUE INDEX item_type_field_type_name_unique "
            "ON item_type_field(item_type_id, name COLLATE NOCASE);",
            "DROP INDEX idx_item_field_type_position;",
            "CREATE INDEX idx_item_type_field_type_position ON item_type_field(item_type_id, position);",
            "DROP INDEX idx_item_field_value_field_id;",
            "CREATE INDEX idx_item_value_field_id ON item_value(item_field_id);",
            "DROP TRIGGER item_field_value_scope_insert;",
            "DROP TRIGGER item_field_value_scope_update;",
            "CREATE TRIGGER item_value_scope_insert BEFORE INSERT ON item_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_type_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "CREATE TRIGGER item_value_scope_update BEFORE UPDATE ON item_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_type_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "DROP TRIGGER item_type_value_cleanup;",
            "CREATE TRIGGER item_type_value_cleanup AFTER UPDATE OF item_type_id ON item "
            "WHEN OLD.item_type_id IS NOT NEW.item_type_id "
            "BEGIN DELETE FROM item_value WHERE item_id = NEW.id; END;",
            "UPDATE log SET table_name = 'item_type_field' WHERE table_name = 'item_field';"
        }},
        {18, {
            "ALTER TABLE item_type ADD COLUMN description TEXT NOT NULL DEFAULT '';"
        }},
        {19, {
            "DROP TRIGGER item_value_scope_insert;",
            "DROP TRIGGER item_value_scope_update;",
            "ALTER TABLE item_type_field RENAME TO item_field;",
            "DROP INDEX item_type_field_type_name_unique;",
            "CREATE UNIQUE INDEX item_field_type_name_unique "
            "ON item_field(item_type_id, name COLLATE NOCASE);",
            "DROP INDEX idx_item_type_field_type_position;",
            "CREATE INDEX idx_item_field_type_position ON item_field(item_type_id, position);",
            "CREATE TRIGGER item_value_scope_insert BEFORE INSERT ON item_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "CREATE TRIGGER item_value_scope_update BEFORE UPDATE ON item_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "UPDATE log SET table_name = 'item_field' WHERE table_name = 'item_type_field';"
        }},
        {20, {
            "CREATE TABLE configuration ("
            " \"key\" TEXT PRIMARY KEY NOT NULL CHECK(TRIM(\"key\") <> ''),"
            " value TEXT NOT NULL"
            ");"
        }},
        {21, {
            // Bumped by every change to an item, its values or its links, so
            // a client saving an item it loaded earlier can be told that
            // someone else changed it in the meantime.
            "ALTER TABLE item ADD COLUMN revision INTEGER NOT NULL DEFAULT 1;"
        }},
        {22, {
            // When the item was last reviewed, as UTC "YYYY-MM-DDTHH:MM:SSZ".
            // NULL for an item never reviewed.
            "ALTER TABLE item ADD COLUMN reviewed_at TEXT;",
            "CREATE INDEX idx_item_reviewed_at ON item(reviewed_at);"
        }},
        {23, {
            "CREATE TABLE alarm ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " title TEXT NOT NULL CHECK(TRIM(title) <> ''),"
            " description TEXT NOT NULL DEFAULT '',"
            // UTC "YYYY-MM-DDTHH:MM:SSZ", so text order is time order.
            " fires_at TEXT NOT NULL"
            ");",
            "CREATE INDEX idx_alarm_fires_at ON alarm(fires_at);"
        }},
        // Admits data type 10, Image. SQLite cannot change a CHECK constraint,
        // so item_field is rebuilt under the same name, with its IDs, its
        // AUTOINCREMENT sequence, indexes and the triggers that name it.
        {24, {
            "CREATE TEMP TABLE migration_item_field AS SELECT * FROM item_field;",
            "CREATE TEMP TABLE migration_item_field_sequence AS "
            "SELECT seq FROM sqlite_sequence WHERE name = 'item_field';",
            "DROP TRIGGER item_value_scope_insert;",
            "DROP TRIGGER item_value_scope_update;",
            "DROP TABLE item_field;",
            "CREATE TABLE item_field ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " item_type_id INTEGER NOT NULL,"
            " name TEXT NOT NULL CHECK(TRIM(name) <> ''),"
            " data_type INTEGER NOT NULL CHECK(data_type BETWEEN 0 AND 10),"
            " position INTEGER NOT NULL DEFAULT 0,"
            " enum_options TEXT NOT NULL DEFAULT '[]',"
            " FOREIGN KEY(item_type_id) REFERENCES item_type(id) ON DELETE CASCADE"
            ");",
            "INSERT INTO item_field(id, item_type_id, name, data_type, position, enum_options) "
            "SELECT id, item_type_id, name, data_type, position, enum_options FROM temp.migration_item_field;",
            "INSERT INTO sqlite_sequence(name, seq) SELECT 'item_field', seq FROM temp.migration_item_field_sequence "
            "WHERE NOT EXISTS (SELECT 1 FROM sqlite_sequence WHERE name = 'item_field');",
            "UPDATE sqlite_sequence SET seq = MAX(seq, COALESCE((SELECT seq FROM temp.migration_item_field_sequence), 0)) "
            "WHERE name = 'item_field';",
            "DROP TABLE temp.migration_item_field;",
            "DROP TABLE temp.migration_item_field_sequence;",
            "CREATE UNIQUE INDEX item_field_type_name_unique "
            "ON item_field(item_type_id, name COLLATE NOCASE);",
            "CREATE INDEX idx_item_field_type_position ON item_field(item_type_id, position);",
            "CREATE TRIGGER item_value_scope_insert BEFORE INSERT ON item_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;",
            "CREATE TRIGGER item_value_scope_update BEFORE UPDATE ON item_value "
            "WHEN NOT EXISTS (SELECT 1 FROM item i JOIN item_field f ON f.item_type_id = i.item_type_id "
            "WHERE i.id = NEW.item_id AND f.id = NEW.item_field_id) "
            "BEGIN SELECT RAISE(ABORT, 'Field does not belong to the item type'); END;"
        }, true},
        {25, {
            // When an alarm that went off was dismissed, as UTC; NULL while it
            // has not gone off or is still ringing. Alarms that went off before
            // clients rang them count as dismissed then.
            "ALTER TABLE alarm ADD COLUMN dismissed_at TEXT;",
            "UPDATE alarm SET dismissed_at = fires_at "
            "WHERE fires_at <= strftime('%Y-%m-%dT%H:%M:%SZ', 'now');"
        }},
        {26, {
            // A question and its answer about one item, for a quiz, with how
            // often the person knew it. The blank check trims the characters
            // lexicon::trim does.
            "CREATE TABLE card ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " item_id INTEGER NOT NULL,"
            " question TEXT NOT NULL CHECK(TRIM(question, ' ' || char(9, 10, 11, 12, 13)) <> ''),"
            " answer TEXT NOT NULL CHECK(TRIM(answer, ' ' || char(9, 10, 11, 12, 13)) <> ''),"
            " success_count INTEGER NOT NULL DEFAULT 0 CHECK(success_count >= 0),"
            " failure_count INTEGER NOT NULL DEFAULT 0 CHECK(failure_count >= 0),"
            // UTC "YYYY-MM-DDTHH:MM:SSZ"; NULL for a card never attempted.
            " last_attempt TEXT,"
            " FOREIGN KEY(item_id) REFERENCES item(id) ON DELETE CASCADE"
            ");",
            "CREATE INDEX idx_card_item_id ON card(item_id, id);"
        }},
        {27, {
            // Snapshots are independent of the live item, so a deleted item
            // can still be restored. The full previous record is JSON; the
            // database backup carries every snapshot with it.
            "CREATE TABLE item_history ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " item_id INTEGER NOT NULL,"
            " operation TEXT NOT NULL CHECK(operation IN ('updated', 'deleted')) ,"
            " snapshot TEXT NOT NULL,"
            " restored_item_id INTEGER,"
            " happened_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))"
            ");",
            "CREATE INDEX idx_item_history_item ON item_history(item_id, id DESC);"
        }},
        {28, {
            "ALTER TABLE alarm ADD COLUMN repeat_days INTEGER NOT NULL DEFAULT 0 "
            "CHECK(repeat_days BETWEEN 0 AND 365);",
            "ALTER TABLE alarm ADD COLUMN item_id INTEGER REFERENCES item(id) ON DELETE SET NULL;",
            "ALTER TABLE alarm ADD COLUMN anchor_at TEXT;",
            "CREATE INDEX idx_alarm_item_id ON alarm(item_id);"
        }}
    };

  if (currentVersion > migrations.back().version)
    throw Failure("Database schema version " + std::to_string(currentVersion) +
                  " is newer than this Lexicon supports (version " +
                  std::to_string(migrations.back().version) + ").");

  for (const auto &migration : migrations) {
    if (migration.version <= currentVersion) continue;
    struct ForeignKeysOff {
      const Connection &db;
      bool active;
      ForeignKeysOff(const Connection &db, bool active) : db(db), active(active) {
        if (active) db.exec("PRAGMA foreign_keys = OFF;");
      }
      ~ForeignKeysOff() {
        if (!active) return;
        try {
          db.exec("PRAGMA foreign_keys = ON;");
        } catch (...) {
          // The connection is unusable anyway; opening it reports that.
        }
      }
    } foreignKeys(db, migration.rebuildsTables);
    Transaction transaction(db, "lexicon_migration");
    for (const auto &sql : migration.statements) db.exec(sql);
    if (migration.rebuildsTables) {
      Statement check(db, "PRAGMA foreign_key_check;");
      if (check.step())
        throw Failure("Migration " + std::to_string(migration.version) + " would break a reference in table " +
                          check.text(0) + ".",
                      lexicon::Error::Code::Storage);
    }
    Statement update(db, "UPDATE db_version SET version = ?;");
    update.bind(migration.version).run();
    transaction.commit();
    currentVersion = migration.version;
  }
}
} // namespace storage
