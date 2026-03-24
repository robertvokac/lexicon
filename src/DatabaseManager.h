#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>
#include <QMap>

enum class TermStatus {
    None = 0,
    Draft = 1,
    Completed = 2
};

enum class UnderstandingLevel {
    Unknown = 0,      // Never encountered
    Recognized = 1,   // Seen before, can identify
    Understood = 2,   // Conceptually grasped
    Practiced = 3,    // Can apply in real situations
    Mastered = 4      // Fully internalized, can teach or innovate
};

enum class LinkType {
    None = 0,
    IsA = 1,
    PartOf = 2,
    Uses = 3,
    DependsOn = 4,
    Implements = 5,
    Related = 6,
    Contrasts = 7,
    AlternativeTo = 8,
    ParentOf = 9
};

struct MapRecord {
    int id = -1;
    QString name;
    QString description;
};

struct LinkRecord {
    int id = -1;
    int fromTermId = -1;
    int toTermId = -1;
    LinkType linkType = LinkType::None;
    int position = 0;

    // Optional for UI:
    QString fromTermTitle;
    QString toTermTitle;
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
    TermStatus status = TermStatus::None;
    UnderstandingLevel understanding = UnderstandingLevel::Unknown;
    bool pinned = false;
    QString content;
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

    static QList<TermRecord> loadTerms(int mapId, const QString& searchText, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, int limit = -1, int offset = 0, int sortColumn = 2, Qt::SortOrder sortOrder = Qt::AscendingOrder, QString* errorMessage = nullptr);
    static int countTerms(int mapId, const QString& searchText, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, QString* errorMessage = nullptr);
    static bool loadTerm(int termId, TermRecord& outTerm, QString* errorMessage = nullptr);
    static bool saveTerm(const TermRecord& term, QString* errorMessage = nullptr);
    static bool deleteTerm(int termId, QString* errorMessage = nullptr);

    static QList<LinkRecord> loadLinks(int termId, QString* errorMessage = nullptr);
    static QList<LinkRecord> loadBacklinks(int termId, QString* errorMessage = nullptr);
    static bool saveLink(const LinkRecord& link, QString* errorMessage = nullptr);
    static bool deleteLink(int linkId, QString* errorMessage = nullptr);
    static bool logTermRead(int termId, QString* errorMessage = nullptr);

    static QStringList loadSuggestions(QString* errorMessage = nullptr);
    static QStringList loadTermTitles(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadTagUsage(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadFlagUsage(QString* errorMessage = nullptr);
    static QList<UsageValueRecord> loadAliasUsage(QString* errorMessage = nullptr);

private:
    static bool logOperation(const QString& tableName, int recordId, int logType, QString* errorMessage = nullptr); // 1=created, 2=updated, 3=deleted, 4=read
    static bool applyMigrations(QString* errorMessage = nullptr);
    static bool execStatements(const QStringList& statements, QString* errorMessage);
    static bool replaceStringValues(const QString& tableName, int termId, const QStringList& values, QString* errorMessage);
    static QList<UsageValueRecord> loadUsageTable(const QString& sql, QString* errorMessage);
};
