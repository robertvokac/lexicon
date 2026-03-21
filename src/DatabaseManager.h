#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>
#include <QMap>

enum class UnderstandingLevel {
    Unknown = 0,      // Never encountered
    Recognized = 1,   // Seen before, can identify
    Understood = 2,   // Conceptually grasped
    Practiced = 3,    // Can apply in real situations
    Mastered = 4      // Fully internalized, can teach or innovate
};

struct MapRecord {
    int id = -1;
    QString name;
    QString description;
};

struct TermRecord {
    int id = -1;
    int mapId = -1;
    QString mapName;
    QString title;
    QString disambiguation;
    QStringList aliases;
    QStringList tags;
    QStringList flags;
    UnderstandingLevel understanding = UnderstandingLevel::Unknown;
};

struct UsageValueRecord {
    QString value;
    int usageCount = 0;
};

class DatabaseManager {
public:
    static bool initialize(const QString& dbPath, QString* errorMessage = nullptr);
    static QSqlDatabase database();

    static QList<MapRecord> loadMaps(QString* errorMessage = nullptr);
    static bool upsertMap(const MapRecord& map, QString* errorMessage = nullptr);
    static bool deleteMap(int mapId, QString* errorMessage = nullptr);

    static QList<TermRecord> loadTerms(int mapId, const QString& searchText, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int limit = -1, int offset = 0, int sortColumn = 2, Qt::SortOrder sortOrder = Qt::AscendingOrder, QString* errorMessage = nullptr);
    static int countTerms(int mapId, const QString& searchText, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, QString* errorMessage = nullptr);
    static bool loadTerm(int termId, TermRecord& outTerm, QString* errorMessage = nullptr);
    static bool saveTerm(const TermRecord& term, QString* errorMessage = nullptr);
    static bool deleteTerm(int termId, QString* errorMessage = nullptr);

    static QStringList loadSuggestions(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadTagUsage(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadFlagUsage(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadAliasUsage(QString* errorMessage = nullptr);

private:
    static bool applyMigrations(QString* errorMessage = nullptr);
    static bool execStatements(const QStringList& statements, QString* errorMessage);
    static bool replaceStringValues(const QString& tableName, int termId, const QStringList& values, QString* errorMessage);
    static QList<UsageValueRecord> loadUsageTable(const QString& sql, QString* errorMessage);
};
