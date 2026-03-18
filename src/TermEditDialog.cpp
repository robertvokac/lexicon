#include "TermEditDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
QWidget* buildListEditor(const QString& title,
                         QListWidget*& outList,
                         QObject* receiver,
                         const char* addSlot,
                         const char* editSlot,
                         const char* removeSlot) {
    auto* box = new QGroupBox(title);
    auto* layout = new QVBoxLayout(box);

    outList = new QListWidget(box);
    outList->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* buttonsLayout = new QHBoxLayout();
    auto* addButton = new QPushButton("Add", box);
    auto* editButton = new QPushButton("Edit", box);
    auto* removeButton = new QPushButton("Remove", box);

    QObject::connect(addButton, SIGNAL(clicked()), receiver, addSlot);
    QObject::connect(editButton, SIGNAL(clicked()), receiver, editSlot);
    QObject::connect(removeButton, SIGNAL(clicked()), receiver, removeSlot);

    buttonsLayout->addWidget(addButton);
    buttonsLayout->addWidget(editButton);
    buttonsLayout->addWidget(removeButton);
    buttonsLayout->addStretch();

    layout->addWidget(outList);
    layout->addLayout(buttonsLayout);
    return box;
}
}

TermEditDialog::TermEditDialog(QWidget* parent)
    : QDialog(parent) {
    setupUi();
    connectSignals();
}

void TermEditDialog::setupUi() {
    setWindowTitle("Edit term");
    resize(720, 520);

    auto* rootLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    m_mapCombo = new QComboBox(this);
    m_titleEdit = new QLineEdit(this);
    m_disambiguationEdit = new QLineEdit(this);
    m_obsidianCheck = new QCheckBox("Obsidian", this);

    formLayout->addRow("Map:", m_mapCombo);
    formLayout->addRow("Title:", m_titleEdit);
    formLayout->addRow("Disambiguation:", m_disambiguationEdit);
    formLayout->addRow(QString(), m_obsidianCheck);

    rootLayout->addLayout(formLayout);

    auto* listsLayout = new QGridLayout();
    listsLayout->addWidget(buildListEditor("Aliases", m_aliasList, this,
                                           SLOT(addAlias()), SLOT(editAlias()), SLOT(removeAlias())),
                           0, 0);
    listsLayout->addWidget(buildListEditor("Tags", m_tagList, this,
                                           SLOT(addTag()), SLOT(editTag()), SLOT(removeTag())),
                           0, 1);
    listsLayout->addWidget(buildListEditor("Flags", m_flagList, this,
                                           SLOT(addFlag()), SLOT(editFlag()), SLOT(removeFlag())),
                           0, 2);

    rootLayout->addLayout(listsLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = buttonBox->button(QDialogButtonBox::Save);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &TermEditDialog::validateAndAccept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, this, &TermEditDialog::reject);

    rootLayout->addWidget(buttonBox);
}

void TermEditDialog::connectSignals() {
}

void TermEditDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (m_titleEdit) {
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
    }
}


void TermEditDialog::setMaps(const QList<MapRecord>& maps) {
    m_mapCombo->clear();
    for (const MapRecord& map : maps) {
        m_mapCombo->addItem(map.name, map.id);
    }
}

void TermEditDialog::setTerm(const TermRecord& term) {
    m_termId = term.id;
    m_titleEdit->setText(term.title);
    m_disambiguationEdit->setText(term.disambiguation);
    m_obsidianCheck->setChecked(term.obsidian);

    const int index = m_mapCombo->findData(term.mapId);
    if (index >= 0) {
        m_mapCombo->setCurrentIndex(index);
    }

    setListValues(m_aliasList, term.aliases);
    setListValues(m_tagList, term.tags);
    setListValues(m_flagList, term.flags);
}

TermRecord TermEditDialog::term() const {
    TermRecord result;
    result.id = m_termId;
    result.mapId = m_mapCombo->currentData().toInt();
    result.mapName = m_mapCombo->currentText();
    result.title = m_titleEdit->text().trimmed();
    result.disambiguation = m_disambiguationEdit->text().trimmed();
    result.obsidian = m_obsidianCheck->isChecked();
    result.aliases = valuesFromList(m_aliasList);
    result.tags = valuesFromList(m_tagList);
    result.flags = valuesFromList(m_flagList);
    return result;
}

void TermEditDialog::addValue(QListWidget* list, const QString& title) {
    bool ok = false;
    const QString value = QInputDialog::getText(this, title, "Value:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || value.isEmpty()) {
        return;
    }
    list->addItem(value);
    list->sortItems();
}

void TermEditDialog::editValue(QListWidget* list, const QString& title) {
    auto* item = list->currentItem();
    if (!item) {
        QMessageBox::information(this, title, "Select a value first.");
        return;
    }

    bool ok = false;
    const QString value = QInputDialog::getText(this, title, "Value:", QLineEdit::Normal, item->text(), &ok).trimmed();
    if (!ok || value.isEmpty()) {
        return;
    }
    item->setText(value);
    list->sortItems();
}

void TermEditDialog::removeValue(QListWidget* list, const QString& title) {
    auto* item = list->currentItem();
    if (!item) {
        QMessageBox::information(this, title, "Select a value first.");
        return;
    }
    delete item;
}

QStringList TermEditDialog::valuesFromList(QListWidget* list) {
    QStringList values;
    for (int i = 0; i < list->count(); ++i) {
        values.push_back(list->item(i)->text().trimmed());
    }
    return values;
}

void TermEditDialog::setListValues(QListWidget* list, const QStringList& values) {
    list->clear();
    list->addItems(values);
    list->sortItems();
}

void TermEditDialog::addAlias() { addValue(m_aliasList, "Add alias"); }
void TermEditDialog::editAlias() { editValue(m_aliasList, "Edit alias"); }
void TermEditDialog::removeAlias() { removeValue(m_aliasList, "Remove alias"); }

void TermEditDialog::addTag() { addValue(m_tagList, "Add tag"); }
void TermEditDialog::editTag() { editValue(m_tagList, "Edit tag"); }
void TermEditDialog::removeTag() { removeValue(m_tagList, "Remove tag"); }

void TermEditDialog::addFlag() { addValue(m_flagList, "Add flag"); }
void TermEditDialog::editFlag() { editValue(m_flagList, "Edit flag"); }
void TermEditDialog::removeFlag() { removeValue(m_flagList, "Remove flag"); }

void TermEditDialog::validateAndAccept() {
    if (m_mapCombo->currentIndex() < 0) {
        QMessageBox::warning(this, "Validation", "Create at least one map first.");
        return;
    }
    if (m_titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation", "Title cannot be empty.");
        m_titleEdit->setFocus();
        return;
    }
    accept();
}
