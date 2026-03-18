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
    QSqlQuery query(db);
    if (!query.exec("CREATE TABLE IF NOT EXISTS db_version (version INTEGER PRIMARY KEY);")) {
        return setError(errorMessage, "Failed to create version table: " + query.lastError().text());
    }

    // 2. Get current version
    int currentVersion = 0;
    if (query.exec("SELECT version FROM db_version LIMIT 1;") && query.next()) {
        currentVersion = query.value(0).toInt();
    } else {
        // Initial insert if table is empty
        QSqlQuery insertVersion(db);
        insertVersion.exec("INSERT INTO db_version (version) VALUES (0);");
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
            " obsidian INTEGER NOT NULL DEFAULT 0,"
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
    QSqlQuery query(database());
    const QString trimmedName = map.name.trimmed();
    const QString trimmedDescription = map.description.trimmed();

    if (map.id < 0) {
        query.prepare("INSERT INTO map(name, description) VALUES(?, ?);");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
    } else {
        query.prepare("UPDATE map SET name = ?, description = ? WHERE id = ?;");
        query.addBindValue(trimmedName);
        query.addBindValue(trimmedDescription);
        query.addBindValue(map.id);
    }

    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteMap(int mapId, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare("DELETE FROM map WHERE id = ?;");
    query.addBindValue(mapId);
    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
    }
    return true;
}

QList<TermRecord> DatabaseManager::loadTerms(int mapId, const QString& searchText, const QString& tagFilter, const QString& flagFilter, QString* errorMessage) {
    QList<TermRecord> terms;

    QString sql =
        "SELECT t.id, m.name, t.map_id, t.title, t.disambiguation, t.obsidian, "
        "COALESCE((SELECT GROUP_CONCAT(a.alias, ', ') FROM alias a WHERE a.term_id = t.id), '') AS aliases, "
        "COALESCE((SELECT GROUP_CONCAT(g.name, ', ') FROM tag g WHERE g.term_id = t.id), '') AS tags, "
        "COALESCE((SELECT GROUP_CONCAT(f.name, ', ') FROM flag f WHERE f.term_id = t.id), '') AS flags "
        "FROM term t "
        "JOIN map m ON m.id = t.map_id "
        "WHERE 1 = 1 ";

    if (mapId > 0) {
        sql += "AND t.map_id = ? ";
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
    sql += "ORDER BY t.title COLLATE NOCASE, COALESCE(t.disambiguation, '') COLLATE NOCASE;";

    QSqlQuery query(database());
    query.prepare(sql);
    if (mapId > 0) {
        query.addBindValue(mapId);
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
        return terms;
    }

    while (query.next()) {
        TermRecord term;
        term.id = query.value(0).toInt();
        term.mapName = query.value(1).toString();
        term.mapId = query.value(2).toInt();
        term.title = query.value(3).toString();
        term.disambiguation = query.value(4).toString();
        term.obsidian = query.value(5).toInt() != 0;
        term.aliases = query.value(6).toString().split(", ", Qt::SkipEmptyParts);
        term.tags = query.value(7).toString().split(", ", Qt::SkipEmptyParts);
        term.flags = query.value(8).toString().split(", ", Qt::SkipEmptyParts);
        terms.push_back(term);
    }

    return terms;
}

bool DatabaseManager::loadTerm(int termId, TermRecord& outTerm, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare(
        "SELECT t.id, t.map_id, m.name, t.title, COALESCE(t.disambiguation, ''), t.obsidian "
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
    outTerm.obsidian = query.value(5).toInt() != 0;

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
    QSqlQuery query(db);
    if (term.id < 0) {
        query.prepare("INSERT INTO term(map_id, title, disambiguation, obsidian) VALUES(?, ?, NULLIF(?, ''), ?);");
        query.addBindValue(term.mapId);
        query.addBindValue(term.title.trimmed());
        query.addBindValue(normalizeNullable(term.disambiguation));
        query.addBindValue(term.obsidian ? 1 : 0);
        if (!query.exec()) {
            db.rollback();
            return setError(errorMessage, query.lastError().text());
        }
        termId = query.lastInsertId().toInt();
    } else {
        query.prepare("UPDATE term SET map_id = ?, title = ?, disambiguation = NULLIF(?, ''), obsidian = ? WHERE id = ?;");
        query.addBindValue(term.mapId);
        query.addBindValue(term.title.trimmed());
        query.addBindValue(normalizeNullable(term.disambiguation));
        query.addBindValue(term.obsidian ? 1 : 0);
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

    if (!db.commit()) {
        db.rollback();
        return setError(errorMessage, db.lastError().text());
    }
    return true;
}

bool DatabaseManager::deleteTerm(int termId, QString* errorMessage) {
    QSqlQuery query(database());
    query.prepare("DELETE FROM term WHERE id = ?;");
    query.addBindValue(termId);
    if (!query.exec()) {
        return setError(errorMessage, query.lastError().text());
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
