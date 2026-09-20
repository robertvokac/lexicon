#include "SqliteRepository.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}


}

int SqliteRepository::findItemId(const QString& title, const QString& disambiguation, QString* error) {
    QSqlQuery query(DatabaseManager::database());
    if (disambiguation.isEmpty()) {
        query.prepare("SELECT id FROM item WHERE title = ? AND (disambiguation IS NULL OR disambiguation = '') LIMIT 1");
        query.addBindValue(title);
        if (!query.exec()) { fail(error, query.lastError().text()); return -1; }
        if (query.next()) return query.value(0).toInt();
        query.prepare("SELECT id FROM item WHERE title = ? LIMIT 1");
        query.addBindValue(title);
    } else {
        query.prepare("SELECT id FROM item WHERE title = ? AND disambiguation = ? LIMIT 1");
        query.addBindValue(title);
        query.addBindValue(disambiguation);
    }
    if (!query.exec()) { fail(error, query.lastError().text()); return -1; }
    return query.next() ? query.value(0).toInt() : -1;
}

bool SqliteRepository::beginUnitOfWork(QString* error) {
    QSqlQuery query(DatabaseManager::database());
    return query.exec("SAVEPOINT lexicon_unit") || fail(error, query.lastError().text());
}

bool SqliteRepository::commitUnitOfWork(QString* error) {
    QSqlQuery query(DatabaseManager::database());
    return query.exec("RELEASE SAVEPOINT lexicon_unit") || fail(error, query.lastError().text());
}

void SqliteRepository::rollbackUnitOfWork() {
    QSqlQuery query(DatabaseManager::database());
    query.exec("ROLLBACK TO SAVEPOINT lexicon_unit");
    query.exec("RELEASE SAVEPOINT lexicon_unit");
}
