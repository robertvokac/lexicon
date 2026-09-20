#include "LexiconApplication.h"

#include <cassert>
#include <QSet>

namespace { LexiconApplication* activeApplication = nullptr; }

namespace {
bool validateForSave(Repository& repository, const ItemRecord& item, QString* error) {
    QString fieldError;
    const auto fields = item.itemTypeId > 0 ? repository.loadItemFields(item.itemTypeId, &fieldError)
                                            : QList<ItemFieldRecord>();
    if (!fieldError.isEmpty()) {
        if (error) *error = fieldError;
        return false;
    }
    return lexicon::validateItem(item, fields, error);
}

bool syncLinks(Repository& repository, int itemId, QList<LinkRecord> desired, bool outgoing, QString* error) {
    QString queryError;
    const auto existing = outgoing ? repository.loadLinks(itemId, &queryError)
                                   : repository.loadBacklinks(itemId, &queryError);
    if (!queryError.isEmpty()) {
        if (error) *error = queryError;
        return false;
    }
    QSet<int> ownedIds;
    QSet<int> seenIds;
    for (const auto& old : existing) ownedIds.insert(old.id);
    for (const auto& link : desired) {
        if (link.id < 0) continue;
        if (!ownedIds.contains(link.id) || seenIds.contains(link.id)) {
            if (error) *error = "Link does not belong to this item or occurs twice.";
            return false;
        }
        seenIds.insert(link.id);
    }
    for (const auto& old : existing) {
        bool kept = false;
        for (const auto& current : desired) {
            if (current.id == old.id) { kept = true; break; }
        }
        if (!kept && !repository.deleteLink(old.id, error)) return false;
    }
    for (auto& link : desired) {
        if (outgoing) link.fromItemId = itemId;
        else link.toItemId = itemId;
        if (!lexicon::validateLink(link, error) || !repository.saveLink(link, error)) return false;
    }
    return true;
}
}

bool ItemService::saveItem(const ItemRecord& item, QString* error) {
    return validateForSave(repository_, item, error) && repository_.saveItem(item, error);
}

int ItemService::createItem(const ItemRecord& item, QString* error) {
    if (item.id >= 0) {
        if (error) *error = "A new item must not already have an ID.";
        return -1;
    }
    if (!validateForSave(repository_, item, error)) return -1;
    int id = -1;
    return repository_.saveItemReturningId(item, &id, error) ? id : -1;
}

bool ItemService::saveItemWithLinks(const ItemRecord& item, const QList<LinkRecord>& links,
                                    const QList<LinkRecord>& backlinks, int* savedId, QString* error) {
    if (!validateForSave(repository_, item, error)) return false;
    if (!repository_.beginUnitOfWork(error)) return false;
    const auto rollback = [&] { repository_.rollbackUnitOfWork(); return false; };
    int id = -1;
    if (!repository_.saveItemReturningId(item, &id, error)) return rollback();
    if (!syncLinks(repository_, id, links, true, error)
        || !syncLinks(repository_, id, backlinks, false, error)) return rollback();
    if (!repository_.commitUnitOfWork(error)) return rollback();
    if (savedId) *savedId = id;
    return true;
}

void installApplication(LexiconApplication& application) { activeApplication = &application; }

LexiconApplication& services() {
    assert(activeApplication != nullptr);
    return *activeApplication;
}
