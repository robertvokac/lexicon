#pragma once

#include "ApplicationContext.h"

#include <QCompleter>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

// A visible title search and a separate stored value. The latter is what the
// item editor and Mass Insert read, so a title can never be saved as an ID.
inline QWidget *foreignKeyValueEditor(QWidget *parent, int targetTypeId,
                                      const QString &savedId, QLineEdit **storedValue = nullptr) {
    auto *wrapper = new QWidget(parent);
    auto *layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto *search = new QLineEdit(wrapper);
    search->setPlaceholderText("Search target item by title...");
    auto *status = new QLabel(wrapper);
    auto *id = new QLineEdit(savedId, wrapper);
    id->hide();
    wrapper->setProperty("massInsertValue", savedId);
    layout->addWidget(search);
    layout->addWidget(status);
    if (storedValue) *storedValue = id;

    const auto showStored = [search, status, targetTypeId](const QString &value) {
        if (value.isEmpty()) { search->clear(); status->clear(); return; }
        bool numeric = false;
        const int number = value.toInt(&numeric);
        ItemRecord target;
        if (numeric && number > 0 && services().items.loadItem(number, target)
                && target.itemTypeId == targetTypeId) {
            search->setText(target.title + (target.disambiguation.isEmpty()
                ? QString() : " [" + target.disambiguation + "]") + " (#" + value + ")");
            status->setText("Item ID: " + value);
        } else {
            search->setText("!missing! " + value);
            status->setText("Item ID: " + value);
        }
    };
    showStored(savedId);

    auto *model = new QStandardItemModel(wrapper);
    auto *completer = new QCompleter(model, wrapper);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    search->setCompleter(completer);

    QObject::connect(search, &QLineEdit::textEdited, wrapper,
                     [wrapper, search, status, id, model, completer, targetTypeId](const QString &query) {
        id->setText(query); // Unselected text fails value validation on Save.
        wrapper->setProperty("massInsertValue", query);
        status->setText(query.isEmpty() ? QString() : "Choose an item from the suggestions.");
        model->clear();
        if (query.trimmed().isEmpty()) return;
        QTimer::singleShot(180, wrapper, [wrapper, search, model, completer, targetTypeId, query] {
            if (search->text() != query) return;
            const auto items = services().items.loadItems(-1, targetTypeId, {}, {},
                {{}, query.trimmed(), {}, {}}, {}, {}, {}, -1, -1, -1, 20);
            if (search->text() != query) return;
            model->clear();
            for (const auto &item : items) {
                const QString label = item.title + (item.disambiguation.isEmpty()
                    ? QString() : " [" + item.disambiguation + "]")
                    + " (#" + QString::number(item.id) + ")";
                model->appendRow(new QStandardItem(label));
            }
            if (model->rowCount()) {
                completer->setCompletionPrefix(query);
                completer->complete();
            }
        });
    });
    QObject::connect(completer, qOverload<const QModelIndex &>(&QCompleter::activated), wrapper,
                     [wrapper, search, status, id](const QModelIndex &index) {
        const QString label = index.data().toString();
        const int marker = label.lastIndexOf(" (#");
        if (marker < 0 || !label.endsWith(')')) return;
        const QString selectedId = label.mid(marker + 3, label.size() - marker - 4);
        id->setText(selectedId);
        wrapper->setProperty("massInsertValue", selectedId);
        search->setText(label);
        status->setText("Item ID: " + selectedId);
    });
    return wrapper;
}
