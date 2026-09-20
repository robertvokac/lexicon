#include "DatabaseManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace {
constexpr const char* kConnectionName = "lexicon_connection";

QString normalizeNullable(const QString& value) {
    const QString trimmed = value.trimmed();
    return trimmed;
}

QStringList cleanedUniqueValues(const QStringList& values) {
    QSet<QString> seen;
    QStringList result;
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QString key = trimmed.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.push_back(trimmed);
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    return result;
}

bool setError(QString* errorMessage, const QString& message) {
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}
}

bool DatabaseManager::initialize(const QString& dbPath, QString* errorMessage) {
    QSqlDatabase db;
    if (QSqlDatabase::contains(kConnectionName)) {
        db = QSqlDatabase::database(kConnectionName);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", kConnectionName);
    }

    db.setDatabaseName(dbPath);
    if (!db.open()) {
        return setError(errorMessage, QString("Cannot open database: %1").arg(db.lastError().text()));
    }

    QSqlQuery pragmaQuery(db);
    if (!pragmaQuery.exec("PRAGMA foreign_keys = ON;")) {
        return setError(errorMessage, QString("Cannot enable foreign keys: %1").arg(pragmaQuery.lastError().text()));
    }

    return applyMigrations(errorMessage);
}

QSqlDatabase DatabaseManager::database() {
    return QSqlDatabase::database(kConnectionName);
}

bool DatabaseManager::applyMigrations(QString* errorMessage) {
    QSqlDatabase db = database();

    // 1. Create version table if not exists
    {
        QSqlQuery query(db);
        if (!query.exec("CREATE TABLE IF NOT EXISTS db_version (version INTEGER PRIMARY KEY);")) {
            return setError(errorMessage, "Failed to create version table: " + query.lastError().text());
        }
    }

    // 2. Get current version
    int currentVersion = 0;
    {
        QSqlQuery query(db);
        if (query.exec("SELECT version FROM db_version LIMIT 1;") && query.next()) {
            currentVersion = query.value(0).toInt();
        } else {
            // Initial insert if table is empty
            QSqlQuery insertVersion(db);
            insertVersion.exec("INSERT INTO db_version (version) VALUES (0);");
        }
    }

    // 3. Define migrations
    struct Migration {
        int version;
        QStringList statements;
    };

    QList<Migration> migrations = {
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
            " log_type INTEGER NOT NULL," // 1=created, 2=updated, 3=deleted, 4=read
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
        }}
    };

    // 4. Apply migrations
    for (const auto& migration : migrations) {
        if (migration.version > currentVersion) {
            if (!db.transaction()) {
                return setError(errorMessage, "Failed to start migration transaction: " + db.lastError().text());
            }

            if (!execStatements(migration.statements, errorMessage)) {
                db.rollback();
                return false;
            }

            QSqlQuery updateVersion(db);
            updateVersion.prepare("UPDATE db_version SET version = ?;");
            updateVersion.addBindValue(migration.version);
            if (!updateVersion.exec()) {
                db.rollback();
                return setError(errorMessage, "Failed to update database version: " + updateVersion.lastError().text());
            }

            if (!db.commit()) {
                db.rollback();
                return setError(errorMessage, "Failed to commit migration: " + db.lastError().text());
            }
            currentVersion = migration.version;
        }
    }

    return true;
}

bool DatabaseManager::execStatements(const QStringList& statements, QString* errorMessage) {
    QSqlDatabase db = database();
    for (const QString& statement : statements) {
        QSqlQuery query(db);
        if (!query.exec(statement)) {
            return setError(errorMessage, QString("Schema error: %1\nSQL: %2")
                .arg(query.lastError().text(), statement));
        }
    }
    return true;
}

QList<GroupRecord> DatabaseManager::loadGroups(QString* errorMessage) {
    QList<GroupRecord> groups;
    QSqlQuery query(database());
    if (!query.exec("SELECT id, name, description FROM item_group ORDER BY name COLLATE NOCASE;")) {
        setError(errorMessage, query.lastError().text());
        return groups;
    }

    while (query.next()) {
        GroupRecord group;
        group.id = query.value(0).toInt();
        group.name = query.value(1).toString();
        group.description = query.value(2).toString();
        groups.push_back(group);
    }
    return groups;
}

bool DatabaseManager::upsertGroup(const GroupRecord& group, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    const QString trimmedName = group.name.trimmed();
    const QString trimmedDescription = group.description.trimmed();

    int groupId = group.id;
    int logType = 2; // updated

    if (group.id < 0) {
        query.prepare("INSERT INTO item_group(name, description) VALUES(?, ?);");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        logType = 1; // created
    } else {
        query.prepare("UPDATE item_group SET name = ?, description = ? WHERE id = ?;");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        query.addBindValue(group.id);
    }

    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (group.id < 0) {
        groupId = query.lastInsertId().toInt();
    }

    if (!logOperation("item_group", groupId, logType, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteGroup(int groupId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM item_group WHERE id = ?;");
    query.addBindValue(groupId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (!logOperation("item_group", groupId, 3, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

QList<ItemRecord> DatabaseManager::loadItems(int groupId, const QString& searchText, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, int limit, int offset, int sortColumn, Qt::SortOrder sortOrder, QString* errorMessage) {
    QList<ItemRecord> items;

    QString sql =
        "SELECT t.id, m.name, t.group_id, t.title, t.disambiguation, "
        "COALESCE((SELECT GROUP_CONCAT(a.alias, ', ') FROM alias a WHERE a.item_id = t.id), '') AS aliases, "
        "COALESCE((SELECT GROUP_CONCAT(g.name, ', ') FROM tag g WHERE g.item_id = t.id), '') AS tags, "
        "COALESCE((SELECT GROUP_CONCAT(f.name, ', ') FROM flag f WHERE f.item_id = t.id), '') AS flags, "
        "t.understanding, t.status, t.pinned "
        "FROM item t "
        "JOIN item_group m ON m.id = t.group_id "
        "WHERE 1 = 1 ";

    QString filters;
    if (groupId > 0) {
        filters += "AND t.group_id = ? ";
    }
    if (understandingFilter >= 0) {
        filters += "AND t.understanding = ? ";
    }
    if (statusFilter >= 0) {
        filters += "AND t.status = ? ";
    }
    if (pinnedFilter >= 0) {
        filters += "AND t.pinned = ? ";
    }
    if (!tagFilter.trimmed().isEmpty()) {
        filters += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND tg.name = ?) ";
    }
    if (!flagFilter.trimmed().isEmpty()) {
        filters += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND fg.name = ?) ";
    }
    if (!searchText.trimmed().isEmpty()) {
        filters +=
            "AND (LOWER(t.title) LIKE ? "
            " OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
            " OR EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id AND LOWER(a.alias) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND LOWER(tg.name) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND LOWER(fg.name) LIKE ?)) ";
    }

    sql += filters;

    // Mapping columns: 0:Id, 1:Group, 2:Title, 3:Disambiguation, 4:Tags, 5:Flags, 6:Aliases, 7:Status, 8:Understanding
    QString orderClause;
    switch (sortColumn) {
        case 0: orderClause = "t.id"; break;
        case 1: orderClause = "m.name COLLATE NOCASE"; break;
        case 2: orderClause = "t.title COLLATE NOCASE"; break;
        case 3: orderClause = "COALESCE(t.disambiguation, '') COLLATE NOCASE"; break;
        case 4: orderClause = "tags COLLATE NOCASE"; break;
        case 5: orderClause = "flags COLLATE NOCASE"; break;
        case 6: orderClause = "aliases COLLATE NOCASE"; break;
        case 7: orderClause = "t.status"; break;
        case 8: orderClause = "t.understanding"; break;
        default: orderClause = "t.title COLLATE NOCASE"; break;
    }

    sql += "ORDER BY " + orderClause + (sortOrder == Qt::AscendingOrder ? " ASC " : " DESC ");
    
    // Secondary sort for stability
    if (sortColumn != 2) {
        sql += ", t.title COLLATE NOCASE";
    }
    if (sortColumn != 3) {
        sql += ", COALESCE(t.disambiguation, '') COLLATE NOCASE";
    }
    sql += ", t.id ASC ";

    if (limit > 0) {
        sql += "LIMIT ? OFFSET ? ";
    }
    sql += ";";

    QSqlQuery query(database());
    query.prepare(sql);
    if (groupId > 0) {
        query.addBindValue(groupId);
    }
    if (understandingFilter >= 0) {
        query.addBindValue(understandingFilter);
    }
    if (statusFilter >= 0) {
        query.addBindValue(statusFilter);
    }
    if (pinnedFilter >= 0) {
        query.addBindValue(pinnedFilter);
    }
    if (!tagFilter.trimmed().isEmpty()) {
        query.addBindValue(tagFilter.trimmed());
    }
    if (!flagFilter.trimmed().isEmpty()) {
        query.addBindValue(flagFilter.trimmed());
    }
    if (!searchText.trimmed().isEmpty()) {
        const QString trimmedSearch = searchText.trimmed();
        const QString like = QString("%%%1%").arg(trimmedSearch.toLower());
        for (int i = 0; i < 5; ++i) {
            query.addBindValue(like);
        }
    }
    if (limit > 0) {
        query.addBindValue(limit);
        query.addBindValue(offset);
    }

    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return items;
    }

    while (query.next()) {
        ItemRecord item;
        item.id = query.value(0).toInt();
        item.groupName = query.value(1).toString();
        item.groupId = query.value(2).toInt();
        item.title = query.value(3).toString();
        item.disambiguation = query.value(4).toString();
        item.aliases = query.value(5).toString().split(", ", Qt::SkipEmptyParts);
        item.tags = query.value(6).toString().split(", ", Qt::SkipEmptyParts);
        item.flags = query.value(7).toString().split(", ", Qt::SkipEmptyParts);
        item.understanding = static_cast<UnderstandingLevel>(query.value(8).toInt());
        item.status = static_cast<ItemStatus>(query.value(9).toInt());
        item.pinned = query.value(10).toInt() != 0;
        items.push_back(item);
    }

    return items;
}

int DatabaseManager::countItems(int groupId, const QString& searchText, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, QString* errorMessage) {
    QString sql = "SELECT COUNT(*) FROM item t WHERE 1 = 1 ";

    if (groupId > 0) {
        sql += "AND t.group_id = ? ";
    }
    if (understandingFilter >= 0) {
        sql += "AND t.understanding = ? ";
    }
    if (statusFilter >= 0) {
        sql += "AND t.status = ? ";
    }
    if (pinnedFilter >= 0) {
        sql += "AND t.pinned = ? ";
    }
    if (!tagFilter.trimmed().isEmpty()) {
        sql += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND tg.name = ?) ";
    }
    if (!flagFilter.trimmed().isEmpty()) {
        sql += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND fg.name = ?) ";
    }
    if (!searchText.trimmed().isEmpty()) {
        sql +=
            "AND (LOWER(t.title) LIKE ? "
            " OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
            " OR EXISTS (SELECT 1 FROM alias a WHERE a.item_id = t.id AND LOWER(a.alias) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM tag tg WHERE tg.item_id = t.id AND LOWER(tg.name) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM flag fg WHERE fg.item_id = t.id AND LOWER(fg.name) LIKE ?)) ";
    }

    QSqlQuery query(database());
    query.prepare(sql);
    if (groupId > 0) {
        query.addBindValue(groupId);
    }
    if (understandingFilter >= 0) {
        query.addBindValue(understandingFilter);
    }
    if (statusFilter >= 0) {
        query.addBindValue(statusFilter);
    }
    if (pinnedFilter >= 0) {
        query.addBindValue(pinnedFilter);
    }
    if (!tagFilter.trimmed().isEmpty()) {
        query.addBindValue(tagFilter.trimmed());
    }
    if (!flagFilter.trimmed().isEmpty()) {
        query.addBindValue(flagFilter.trimmed());
    }
    if (!searchText.trimmed().isEmpty()) {
        const QString trimmedSearch = searchText.trimmed();
        const QString like = QString("%%%1%").arg(trimmedSearch.toLower());
        for (int i = 0; i < 5; ++i) {
            query.addBindValue(like);
        }
    }

    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool DatabaseManager::loadItem(int itemId, ItemRecord& outItem, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare(
        "SELECT t.id, t.group_id, m.name, t.title, COALESCE(t.disambiguation, ''), t.understanding, t.status, t.pinned, COALESCE(t.content, '') "
        "FROM item t JOIN item_group m ON m.id = t.group_id WHERE t.id = ?;");
    query.addBindValue(itemId);

    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    if (!query.next()) {
        return setError(errorMessage, "Item not found.");
    }

    outItem.id = query.value(0).toInt();
    outItem.groupId = query.value(1).toInt();
    outItem.groupName = query.value(2).toString();
    outItem.title = query.value(3).toString();
    outItem.disambiguation = query.value(4).toString();
    outItem.understanding = static_cast<UnderstandingLevel>(query.value(5).toInt());
    outItem.status = static_cast<ItemStatus>(query.value(6).toInt());
    outItem.pinned = query.value(7).toInt() != 0;
    outItem.content = query.value(8).toString();

    auto loadValues = [&](const QString& sql, QStringList& target) -> bool {
        QSqlQuery childQuery(database());
        childQuery.prepare(sql);
        childQuery.addBindValue(itemId);
        if (!childQuery.exec()) {
            return setError(errorMessage, childQuery.lastError().text());
        }
        target.clear();
        while (childQuery.next()) {
            target.push_back(childQuery.value(0).toString());
        }
        return true;
    };

    return loadValues("SELECT alias FROM alias WHERE item_id = ? ORDER BY alias COLLATE NOCASE;", outItem.aliases)
        && loadValues("SELECT name FROM tag WHERE item_id = ? ORDER BY name COLLATE NOCASE;", outItem.tags)
        && loadValues("SELECT name FROM flag WHERE item_id = ? ORDER BY name COLLATE NOCASE;", outItem.flags);
}

bool DatabaseManager::replaceStringValues(const QString& tableName, int itemId, const QStringList& values, QString* errorMessage) {
    QSqlQuery deleteQuery(database());
    deleteQuery.prepare(QString("DELETE FROM %1 WHERE item_id = ?;").arg(tableName));
    deleteQuery.addBindValue(itemId);
    if (!deleteQuery.exec()) {
        return setError(errorMessage, deleteQuery.lastError().text());
    }

    const QString columnName = tableName == "alias" ? "alias" : "name";
    QSqlQuery insertQuery(database());
    insertQuery.prepare(QString("INSERT INTO %1(item_id, %2) VALUES(?, ?);").arg(tableName, columnName));

    const QStringList cleaned = cleanedUniqueValues(values);
    for (const QString& value : cleaned) {
        insertQuery.addBindValue(itemId);
        insertQuery.addBindValue(value);
        if (!insertQuery.exec()) {
            return setError(errorMessage, insertQuery.lastError().text());
        }
        insertQuery.finish();
    }
    return true;
}

bool DatabaseManager::saveItem(const ItemRecord& item, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    int itemId = item.id;
    int logType = 2; // updated
    QSqlQuery query(db);
    if (item.id < 0) {
        query.prepare("INSERT INTO item(group_id, title, disambiguation, understanding, status, pinned, content) VALUES(?, ?, NULLIF(?, ''), ?, ?, ?, ?);");
        query.addBindValue(item.groupId);
        query.addBindValue(item.title.trimmed());
        query.addBindValue(normalizeNullable(item.disambiguation));
        query.addBindValue(static_cast<int>(item.understanding));
        query.addBindValue(static_cast<int>(item.status));
        query.addBindValue(item.pinned ? 1 : 0);
        query.addBindValue(item.content);
        if (!query.exec()) {
            db.rollback();
            return setError(errorMessage, query.lastError().text());
        }
        itemId = query.lastInsertId().toInt();
        logType = 1; // created
    } else {
        query.prepare("UPDATE item SET group_id = ?, title = ?, disambiguation = NULLIF(?, ''), understanding = ?, status = ?, pinned = ?, content = ? WHERE id = ?;");
        query.addBindValue(item.groupId);
        query.addBindValue(item.title.trimmed());
        query.addBindValue(normalizeNullable(item.disambiguation));
        query.addBindValue(static_cast<int>(item.understanding));
        query.addBindValue(static_cast<int>(item.status));
        query.addBindValue(item.pinned ? 1 : 0);
        query.addBindValue(item.content);
        query.addBindValue(item.id);
        if (!query.exec()) {
            db.rollback();
            return setError(errorMessage, query.lastError().text());
        }
    }

    if (!replaceStringValues("alias", itemId, item.aliases, errorMessage)
        || !replaceStringValues("tag", itemId, item.tags, errorMessage)
        || !replaceStringValues("flag", itemId, item.flags, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!logOperation("item", itemId, logType, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteItem(int itemId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM item WHERE id = ?;");
    query.addBindValue(itemId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (!logOperation("item", itemId, 3, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::logOperation(const QString& tableName, int recordId, int logType, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare("INSERT INTO log(table_name, record_id, log_type) VALUES(?, ?, ?);");
    query.addBindValue(tableName);
    query.addBindValue(recordId);
    query.addBindValue(logType);
    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    return true;
}

bool DatabaseManager::logItemRead(int itemId, QString* errorMessage) {
    return logOperation("item", itemId, 4, errorMessage);
}

QList<LinkRecord> DatabaseManager::loadLinks(int itemId, QString* errorMessage) {
    QList<LinkRecord> result;
    QSqlDatabase db = database();
    QSqlQuery query(db);
    query.prepare("SELECT l.id, l.from_item_id, l.to_item_id, l.link_type, l.position, l.custom_value, t.title "
                  "FROM link l "
                  "JOIN item t ON l.to_item_id = t.id "
                  "WHERE l.from_item_id = ? "
                  "ORDER BY l.position, t.title COLLATE NOCASE;");
    query.addBindValue(itemId);

    if (!query.exec()) {
        setError(errorMessage, "Failed to load links: " + query.lastError().text());
        return result;
    }

    while (query.next()) {
        LinkRecord link;
        link.id = query.value(0).toInt();
        link.fromItemId = query.value(1).toInt();
        link.toItemId = query.value(2).toInt();
        link.linkType = static_cast<LinkType>(query.value(3).toInt());
        link.position = query.value(4).toInt();
        link.customValue = query.value(5).toString();
        link.toItemTitle = query.value(6).toString();
        result.push_back(link);
    }
    return result;
}

QList<LinkRecord> DatabaseManager::loadBacklinks(int itemId, QString* errorMessage) {
    QList<LinkRecord> result;
    QSqlDatabase db = database();
    QSqlQuery query(db);
    query.prepare("SELECT l.id, l.from_item_id, l.to_item_id, l.link_type, l.position, l.custom_value, t.title "
                  "FROM link l "
                  "JOIN item t ON l.from_item_id = t.id "
                  "WHERE l.to_item_id = ? "
                  "ORDER BY l.position, t.title COLLATE NOCASE;");
    query.addBindValue(itemId);

    if (!query.exec()) {
        setError(errorMessage, "Failed to load backlinks: " + query.lastError().text());
        return result;
    }

    while (query.next()) {
        LinkRecord link;
        link.id = query.value(0).toInt();
        link.fromItemId = query.value(1).toInt();
        link.toItemId = query.value(2).toInt();
        link.linkType = static_cast<LinkType>(query.value(3).toInt());
        link.position = query.value(4).toInt();
        link.customValue = query.value(5).toString();
        link.fromItemTitle = query.value(6).toString();
        result.push_back(link);
    }
    return result;
}

bool DatabaseManager::saveLink(const LinkRecord& link, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, "Failed to start transaction: " + db.lastError().text());
    }

    QSqlQuery query(db);
    int logType = 2; // updated
    if (link.id == -1) {
        query.prepare("INSERT INTO link (from_item_id, to_item_id, link_type, position, custom_value) VALUES (?, ?, ?, ?, ?);");
        query.addBindValue(link.fromItemId);
        query.addBindValue(link.toItemId);
        query.addBindValue(static_cast<int>(link.linkType));
        query.addBindValue(link.position);
        query.addBindValue(link.customValue.trimmed());
        logType = 1; // created
    } else {
        query.prepare("UPDATE link SET from_item_id = ?, to_item_id = ?, link_type = ?, position = ?, custom_value = ? WHERE id = ?;");
        query.addBindValue(link.fromItemId);
        query.addBindValue(link.toItemId);
        query.addBindValue(static_cast<int>(link.linkType));
        query.addBindValue(link.position);
        query.addBindValue(link.customValue.trimmed());
        query.addBindValue(link.id);
    }

    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, "Failed to save link: " + query.lastError().text());
    }

    int recordId = link.id;
    if (recordId == -1) {
        recordId = query.lastInsertId().toInt();
    }

    if (!logOperation("link", recordId, logType, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, "Failed to commit transaction: " + db.lastError().text());
    }

    return true;
}

bool DatabaseManager::deleteLink(int linkId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, "Failed to start transaction: " + db.lastError().text());
    }

    if (!logOperation("link", linkId, 3, errorMessage)) { // 3 = deleted
        db.rollback();
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM link WHERE id = ?;");
    query.addBindValue(linkId);

    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, "Failed to delete link: " + query.lastError().text());
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, "Failed to commit transaction: " + db.lastError().text());
    }

    return true;
}

QStringList DatabaseManager::loadSuggestions(QString* errorMessage) {
    QStringList values;
    QSet<QString> seen;

    QSqlQuery query(database());
    if (!query.exec("SELECT title AS value FROM item UNION SELECT alias AS value FROM alias ORDER BY value COLLATE NOCASE;")) {
        setError(errorMessage, query.lastError().text());
        return values;
    }

    while (query.next()) {
        const QString value = query.value(0).toString().trimmed();
        if (value.isEmpty()) {
            continue;
        }
        const QString key = value.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        values.push_back(value);
    }
    return values;
}

QStringList DatabaseManager::loadItemTitles(QString* errorMessage) {
    QStringList values;
    QSqlQuery query(database());
    if (!query.exec("SELECT title, disambiguation FROM item ORDER BY title COLLATE NOCASE;")) {
        setError(errorMessage, query.lastError().text());
        return values;
    }

    while (query.next()) {
        QString title = query.value(0).toString();
        QString disambiguation = query.value(1).toString();
        if (!disambiguation.isEmpty()) {
            values.push_back(QString("%1 [%2]").arg(title, disambiguation));
        } else {
            values.push_back(title);
        }
    }
    return values;
}

QList<UsageValueRecord> DatabaseManager::loadUsageTable(const QString& sql, QString* errorMessage) {
    QList<UsageValueRecord> values;
    QSqlQuery query(database());
    if (!query.exec(sql)) {
        setError(errorMessage, query.lastError().text());
        return values;
    }

    while (query.next()) {
        UsageValueRecord record;
        record.value = query.value(0).toString();
        record.usageCount = query.value(1).toInt();
        values.push_back(record);
    }
    return values;
}

QList<UsageValueRecord> DatabaseManager::loadTagUsage(QString* errorMessage) {
    return loadUsageTable(
        "SELECT name, COUNT(*) AS usage_count FROM tag GROUP BY name ORDER BY name COLLATE NOCASE;",
        errorMessage);
}

QList<UsageValueRecord> DatabaseManager::loadFlagUsage(QString* errorMessage) {
    return loadUsageTable(
        "SELECT name, COUNT(*) AS usage_count FROM flag GROUP BY name ORDER BY name COLLATE NOCASE;",
        errorMessage);
}

QList<UsageValueRecord> DatabaseManager::loadAliasUsage(QString* errorMessage) {
    return loadUsageTable(
        "SELECT alias, COUNT(*) AS usage_count FROM alias GROUP BY alias ORDER BY alias COLLATE NOCASE;",
        errorMessage);
}
