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

QList<MapRecord> DatabaseManager::loadMaps(QString* errorMessage) {
    QList<MapRecord> maps;
    QSqlQuery query(database());
    if (!query.exec("SELECT id, name, description FROM map ORDER BY name COLLATE NOCASE;")) {
        setError(errorMessage, query.lastError().text());
        return maps;
    }

    while (query.next()) {
        MapRecord map;
        map.id = query.value(0).toInt();
        map.name = query.value(1).toString();
        map.description = query.value(2).toString();
        maps.push_back(map);
    }
    return maps;
}

bool DatabaseManager::upsertMap(const MapRecord& map, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    const QString trimmedName = map.name.trimmed();
    const QString trimmedDescription = map.description.trimmed();

    int mapId = map.id;
    int logType = 2; // updated

    if (map.id < 0) {
        query.prepare("INSERT INTO map(name, description) VALUES(?, ?);");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        logType = 1; // created
    } else {
        query.prepare("UPDATE map SET name = ?, description = ? WHERE id = ?;");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        query.addBindValue(map.id);
    }

    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (map.id < 0) {
        mapId = query.lastInsertId().toInt();
    }

    if (!logOperation("map", mapId, logType, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteMap(int mapId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM map WHERE id = ?;");
    query.addBindValue(mapId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (!logOperation("map", mapId, 3, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

QList<TermRecord> DatabaseManager::loadTerms(int mapId, const QString& searchText, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, int limit, int offset, int sortColumn, Qt::SortOrder sortOrder, QString* errorMessage) {
    QList<TermRecord> terms;

    QString sql =
        "SELECT t.id, m.name, t.map_id, t.title, t.disambiguation, "
        "COALESCE((SELECT GROUP_CONCAT(a.alias, ', ') FROM alias a WHERE a.term_id = t.id), '') AS aliases, "
        "COALESCE((SELECT GROUP_CONCAT(g.name, ', ') FROM tag g WHERE g.term_id = t.id), '') AS tags, "
        "COALESCE((SELECT GROUP_CONCAT(f.name, ', ') FROM flag f WHERE f.term_id = t.id), '') AS flags, "
        "t.understanding, t.status, t.pinned "
        "FROM term t "
        "JOIN map m ON m.id = t.map_id "
        "WHERE 1 = 1 ";

    QString filters;
    if (mapId > 0) {
        filters += "AND t.map_id = ? ";
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
        filters += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.term_id = t.id AND tg.name = ?) ";
    }
    if (!flagFilter.trimmed().isEmpty()) {
        filters += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.term_id = t.id AND fg.name = ?) ";
    }
    if (!searchText.trimmed().isEmpty()) {
        filters +=
            "AND (LOWER(t.title) LIKE ? "
            " OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
            " OR EXISTS (SELECT 1 FROM alias a WHERE a.term_id = t.id AND LOWER(a.alias) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM tag tg WHERE tg.term_id = t.id AND LOWER(tg.name) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM flag fg WHERE fg.term_id = t.id AND LOWER(fg.name) LIKE ?)) ";
    }

    sql += filters;

    // Mapping columns: 0:Id, 1:Map, 2:Title, 3:Disambiguation, 4:Tags, 5:Flags, 6:Aliases, 7:Status, 8:Understanding
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
    if (mapId > 0) {
        query.addBindValue(mapId);
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
        return terms;
    }

    while (query.next()) {
        TermRecord term;
        term.id = query.value(0).toInt();
        term.mapName = query.value(1).toString();
        term.mapId = query.value(2).toInt();
        term.title = query.value(3).toString();
        term.disambiguation = query.value(4).toString();
        term.aliases = query.value(5).toString().split(", ", Qt::SkipEmptyParts);
        term.tags = query.value(6).toString().split(", ", Qt::SkipEmptyParts);
        term.flags = query.value(7).toString().split(", ", Qt::SkipEmptyParts);
        term.understanding = static_cast<UnderstandingLevel>(query.value(8).toInt());
        term.status = static_cast<TermStatus>(query.value(9).toInt());
        term.pinned = query.value(10).toInt() != 0;
        terms.push_back(term);
    }

    return terms;
}

int DatabaseManager::countTerms(int mapId, const QString& searchText, const QString& tagFilter, const QString& flagFilter, int understandingFilter, int statusFilter, int pinnedFilter, QString* errorMessage) {
    QString sql = "SELECT COUNT(*) FROM term t WHERE 1 = 1 ";

    if (mapId > 0) {
        sql += "AND t.map_id = ? ";
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
        sql += "AND EXISTS (SELECT 1 FROM tag tg WHERE tg.term_id = t.id AND tg.name = ?) ";
    }
    if (!flagFilter.trimmed().isEmpty()) {
        sql += "AND EXISTS (SELECT 1 FROM flag fg WHERE fg.term_id = t.id AND fg.name = ?) ";
    }
    if (!searchText.trimmed().isEmpty()) {
        sql +=
            "AND (LOWER(t.title) LIKE ? "
            " OR LOWER(COALESCE(t.disambiguation, '')) LIKE ? "
            " OR EXISTS (SELECT 1 FROM alias a WHERE a.term_id = t.id AND LOWER(a.alias) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM tag tg WHERE tg.term_id = t.id AND LOWER(tg.name) LIKE ?) "
            " OR EXISTS (SELECT 1 FROM flag fg WHERE fg.term_id = t.id AND LOWER(fg.name) LIKE ?)) ";
    }

    QSqlQuery query(database());
    query.prepare(sql);
    if (mapId > 0) {
        query.addBindValue(mapId);
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

bool DatabaseManager::loadTerm(int termId, TermRecord& outTerm, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare(
        "SELECT t.id, t.map_id, m.name, t.title, COALESCE(t.disambiguation, ''), t.understanding, t.status, t.pinned, COALESCE(t.content, '') "
        "FROM term t JOIN map m ON m.id = t.map_id WHERE t.id = ?;");
    query.addBindValue(termId);

    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    if (!query.next()) {
        return setError(errorMessage, "Term not found.");
    }

    outTerm.id = query.value(0).toInt();
    outTerm.mapId = query.value(1).toInt();
    outTerm.mapName = query.value(2).toString();
    outTerm.title = query.value(3).toString();
    outTerm.disambiguation = query.value(4).toString();
    outTerm.understanding = static_cast<UnderstandingLevel>(query.value(5).toInt());
    outTerm.status = static_cast<TermStatus>(query.value(6).toInt());
    outTerm.pinned = query.value(7).toInt() != 0;
    outTerm.content = query.value(8).toString();

    auto loadValues = [&](const QString& sql, QStringList& target) -> bool {
        QSqlQuery childQuery(database());
        childQuery.prepare(sql);
        childQuery.addBindValue(termId);
        if (!childQuery.exec()) {
            return setError(errorMessage, childQuery.lastError().text());
        }
        target.clear();
        while (childQuery.next()) {
            target.push_back(childQuery.value(0).toString());
        }
        return true;
    };

    return loadValues("SELECT alias FROM alias WHERE term_id = ? ORDER BY alias COLLATE NOCASE;", outTerm.aliases)
        && loadValues("SELECT name FROM tag WHERE term_id = ? ORDER BY name COLLATE NOCASE;", outTerm.tags)
        && loadValues("SELECT name FROM flag WHERE term_id = ? ORDER BY name COLLATE NOCASE;", outTerm.flags);
}

bool DatabaseManager::replaceStringValues(const QString& tableName, int termId, const QStringList& values, QString* errorMessage) {
    QSqlQuery deleteQuery(database());
    deleteQuery.prepare(QString("DELETE FROM %1 WHERE term_id = ?;").arg(tableName));
    deleteQuery.addBindValue(termId);
    if (!deleteQuery.exec()) {
        return setError(errorMessage, deleteQuery.lastError().text());
    }

    const QString columnName = tableName == "alias" ? "alias" : "name";
    QSqlQuery insertQuery(database());
    insertQuery.prepare(QString("INSERT INTO %1(term_id, %2) VALUES(?, ?);").arg(tableName, columnName));

    const QStringList cleaned = cleanedUniqueValues(values);
    for (const QString& value : cleaned) {
        insertQuery.addBindValue(termId);
        insertQuery.addBindValue(value);
        if (!insertQuery.exec()) {
            return setError(errorMessage, insertQuery.lastError().text());
        }
        insertQuery.finish();
    }
    return true;
}

bool DatabaseManager::saveTerm(const TermRecord& term, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    int termId = term.id;
    int logType = 2; // updated
    QSqlQuery query(db);
    if (term.id < 0) {
        query.prepare("INSERT INTO term(map_id, title, disambiguation, understanding, status, pinned, content) VALUES(?, ?, NULLIF(?, ''), ?, ?, ?, ?);");
        query.addBindValue(term.mapId);
        query.addBindValue(term.title.trimmed());
        query.addBindValue(normalizeNullable(term.disambiguation));
        query.addBindValue(static_cast<int>(term.understanding));
        query.addBindValue(static_cast<int>(term.status));
        query.addBindValue(term.pinned ? 1 : 0);
        query.addBindValue(term.content);
        if (!query.exec()) {
            db.rollback();
            return setError(errorMessage, query.lastError().text());
        }
        termId = query.lastInsertId().toInt();
        logType = 1; // created
    } else {
        query.prepare("UPDATE term SET map_id = ?, title = ?, disambiguation = NULLIF(?, ''), understanding = ?, status = ?, pinned = ?, content = ? WHERE id = ?;");
        query.addBindValue(term.mapId);
        query.addBindValue(term.title.trimmed());
        query.addBindValue(normalizeNullable(term.disambiguation));
        query.addBindValue(static_cast<int>(term.understanding));
        query.addBindValue(static_cast<int>(term.status));
        query.addBindValue(term.pinned ? 1 : 0);
        query.addBindValue(term.content);
        query.addBindValue(term.id);
        if (!query.exec()) {
            db.rollback();
            return setError(errorMessage, query.lastError().text());
        }
    }

    if (!replaceStringValues("alias", termId, term.aliases, errorMessage)
        || !replaceStringValues("tag", termId, term.tags, errorMessage)
        || !replaceStringValues("flag", termId, term.flags, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!logOperation("term", termId, logType, errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteTerm(int termId, QString* errorMessage) {
    QSqlDatabase db = database();
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM term WHERE id = ?;");
    query.addBindValue(termId);
    if (!query.exec()) {
        db.rollback();
        return setError(errorMessage, query.lastError().text());
    }

    if (!logOperation("term", termId, 3, errorMessage)) {
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

bool DatabaseManager::logTermRead(int termId, QString* errorMessage) {
    return logOperation("term", termId, 4, errorMessage);
}

QList<LinkRecord> DatabaseManager::loadLinks(int termId, QString* errorMessage) {
    QList<LinkRecord> result;
    QSqlDatabase db = database();
    QSqlQuery query(db);
    query.prepare("SELECT l.id, l.from_term_id, l.to_term_id, l.link_type, t.title "
                  "FROM link l "
                  "JOIN term t ON l.to_term_id = t.id "
                  "WHERE l.from_term_id = ?;");
    query.addBindValue(termId);

    if (!query.exec()) {
        setError(errorMessage, "Failed to load links: " + query.lastError().text());
        return result;
    }

    while (query.next()) {
        LinkRecord link;
        link.id = query.value(0).toInt();
        link.fromTermId = query.value(1).toInt();
        link.toTermId = query.value(2).toInt();
        link.linkType = static_cast<LinkType>(query.value(3).toInt());
        link.toTermTitle = query.value(4).toString();
        result.push_back(link);
    }
    return result;
}

QList<LinkRecord> DatabaseManager::loadBacklinks(int termId, QString* errorMessage) {
    QList<LinkRecord> result;
    QSqlDatabase db = database();
    QSqlQuery query(db);
    query.prepare("SELECT l.id, l.from_term_id, l.to_term_id, l.link_type, t.title "
                  "FROM link l "
                  "JOIN term t ON l.from_term_id = t.id "
                  "WHERE l.to_term_id = ?;");
    query.addBindValue(termId);

    if (!query.exec()) {
        setError(errorMessage, "Failed to load backlinks: " + query.lastError().text());
        return result;
    }

    while (query.next()) {
        LinkRecord link;
        link.id = query.value(0).toInt();
        link.fromTermId = query.value(1).toInt();
        link.toTermId = query.value(2).toInt();
        link.linkType = static_cast<LinkType>(query.value(3).toInt());
        link.fromTermTitle = query.value(4).toString();
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
        query.prepare("INSERT INTO link (from_term_id, to_term_id, link_type) VALUES (?, ?, ?);");
        query.addBindValue(link.fromTermId);
        query.addBindValue(link.toTermId);
        query.addBindValue(static_cast<int>(link.linkType));
        logType = 1; // created
    } else {
        query.prepare("UPDATE link SET from_term_id = ?, to_term_id = ?, link_type = ? WHERE id = ?;");
        query.addBindValue(link.fromTermId);
        query.addBindValue(link.toTermId);
        query.addBindValue(static_cast<int>(link.linkType));
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
    if (!query.exec("SELECT title AS value FROM term UNION SELECT alias AS value FROM alias ORDER BY value COLLATE NOCASE;")) {
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

QStringList DatabaseManager::loadTermTitles(QString* errorMessage) {
    QStringList values;
    QSqlQuery query(database());
    if (!query.exec("SELECT title, disambiguation FROM term ORDER BY title COLLATE NOCASE;")) {
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
