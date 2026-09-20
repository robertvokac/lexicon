#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool check(bool ok, const char* message, const QString& detail = {}) {
    if (ok) return true;
    std::cerr << message << ": " << detail.toStdString() << '\n';
    return false;
}
}

int main(int argc, char** argv) {
    QCoreApplication runtime(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "Temporary directory")) return 1;

    SqliteRepository repository;
    QString error;
    if (!check(repository.open(directory.filePath("lexicon.db"), &error), "Database migration", error)) return 1;
    LexiconApplication lexicon(repository);

    const int groupId = lexicon.groups.defaultGroupId(&error);
    if (!check(groupId > 0, "Default group", error)) return 1;

    ItemRecord first;
    first.groupId = groupId;
    first.title = "Pointer provenance";
    int firstId = lexicon.items.createItem(first, &error);
    if (!check(firstId > 0,
               "Create first item", error)) return 1;

    ItemRecord second;
    second.groupId = groupId;
    second.title = "C++";
    int secondId = -1;
    if (!check(lexicon.items.saveItemWithLinks(second, {}, {}, &secondId, &error) && secondId > firstId,
               "Create second item", error)) return 1;

    first.id = firstId;
    LinkRecord relation;
    relation.toItemId = secondId;
    relation.linkType = LinkType::PartOf;
    if (!check(lexicon.items.saveItemWithLinks(first, {relation}, {}, &firstId, &error),
               "Save item with link", error)) return 1;
    const auto links = lexicon.links.loadLinks(firstId, &error);
    if (!check(links.size() == 1 && links.first().toItemId == secondId,
               "Link persisted", error)) return 1;

    first.title = "Should roll back";
    LinkRecord invalid;
    invalid.toItemId = -1;
    error.clear();
    if (!check(!lexicon.items.saveItemWithLinks(first, {invalid}, {}, nullptr, &error),
               "Invalid link rejected", error)) return 1;
    ItemRecord persisted;
    error.clear();
    if (!check(lexicon.items.loadItem(firstId, persisted, &error)
               && persisted.title == "Pointer provenance"
               && lexicon.links.loadLinks(firstId, &error).size() == 1,
               "Transaction rollback", error)) return 1;
    return 0;
}
