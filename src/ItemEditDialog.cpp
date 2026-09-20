#include "ItemEditDialog.h"

#include "BlobStore.h"
#include "MarkdownConverter.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
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

ItemEditDialog::ItemEditDialog(QWidget* parent)
    : QDialog(parent) {
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(1000);
    connect(m_previewTimer, &QTimer::timeout, this, &ItemEditDialog::updatePreview);

    setupUi();
    connectSignals();
}

void ItemEditDialog::setupUi() {
    setWindowTitle("Edit item");
    resize(800, 600);

    auto* rootLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);

    // --- Tab 1: General ---
    auto* generalTab = new QWidget();
    auto* generalLayout = new QVBoxLayout(generalTab);
    auto* formLayout = new QFormLayout();

    m_groupCombo = new QComboBox(this);
    m_typeCombo = new QComboBox(this);
    m_statusCombo = new QComboBox(this);
    m_statusCombo->addItem("None", static_cast<int>(ItemStatus::None));
    m_statusCombo->addItem("Draft", static_cast<int>(ItemStatus::Draft));
    m_statusCombo->addItem("Completed", static_cast<int>(ItemStatus::Completed));

    m_understandingCombo = new QComboBox(this);
    m_understandingCombo->addItem("Unknown", static_cast<int>(UnderstandingLevel::Unknown));
    m_understandingCombo->addItem("Recognized", static_cast<int>(UnderstandingLevel::Recognized));
    m_understandingCombo->addItem("Understood", static_cast<int>(UnderstandingLevel::Understood));
    m_understandingCombo->addItem("Practiced", static_cast<int>(UnderstandingLevel::Practiced));
    m_understandingCombo->addItem("Mastered", static_cast<int>(UnderstandingLevel::Mastered));

    m_understandingCombo->setItemData(0, "Never encountered", Qt::ToolTipRole);
    m_understandingCombo->setItemData(1, "Seen before, can identify", Qt::ToolTipRole);
    m_understandingCombo->setItemData(2, "Conceptually grasped", Qt::ToolTipRole);
    m_understandingCombo->setItemData(3, "Can apply in real situations", Qt::ToolTipRole);
    m_understandingCombo->setItemData(4, "Fully internalized, can teach or innovate", Qt::ToolTipRole);

    m_pinnedCheck = new QCheckBox(this);
    m_titleEdit = new QLineEdit(this);
    m_disambiguationEdit = new QLineEdit(this);

    formLayout->addRow("Group:", m_groupCombo);
    formLayout->addRow("Type:", m_typeCombo);
    formLayout->addRow("Title:", m_titleEdit);
    formLayout->addRow("Disambiguation:", m_disambiguationEdit);
    formLayout->addRow("Status:", m_statusCombo);
    formLayout->addRow("Understanding:", m_understandingCombo);
    formLayout->addRow("Pinned:", m_pinnedCheck);

    generalLayout->addLayout(formLayout);
    generalLayout->addStretch();
    auto* generalScroll = new QScrollArea(this);
    generalScroll->setWidgetResizable(true);
    generalScroll->setWidget(generalTab);
    m_tabWidget->addTab(generalScroll, "General");

    // --- Tab 2: Values ---
    auto* valuesTab = new QWidget();
    auto* valuesLayout = new QVBoxLayout(valuesTab);
    m_noFieldsLabel = new QLabel("This type has no fields yet.", valuesTab);
    valuesLayout->addWidget(m_noFieldsLabel);
    m_fieldsBox = new QGroupBox("Values", valuesTab);
    m_fieldsLayout = new QFormLayout(m_fieldsBox);
    valuesLayout->addWidget(m_fieldsBox);
    valuesLayout->addStretch();
    auto* valuesScroll = new QScrollArea(this);
    valuesScroll->setWidgetResizable(true);
    valuesScroll->setWidget(valuesTab);
    m_valuesTabIndex = m_tabWidget->addTab(valuesScroll, "Values");
    m_tabWidget->setTabEnabled(m_valuesTabIndex, false);

    // --- Tab 3: Content ---
    auto* contentTab = new QWidget();
    auto* contentLayout = new QVBoxLayout(contentTab);

    m_contentToolbar = new QToolBar(this);
    m_contentToolbar->setIconSize(QSize(16, 16));
    m_contentToolbar->addAction("B", this, SLOT(formatBold()))->setToolTip("Bold (**)");
    m_contentToolbar->addAction("I", this, SLOT(formatItalic()))->setToolTip("Italic (*)");
    m_contentToolbar->addSeparator();
    m_contentToolbar->addAction("H2", this, SLOT(formatH2()))->setToolTip("Header 2 (##)");
    m_contentToolbar->addAction("H3", this, SLOT(formatH3()))->setToolTip("Header 3 (###)");
    m_contentToolbar->addAction("H4", this, SLOT(formatH4()))->setToolTip("Header 4 (####)");
    m_contentToolbar->addSeparator();
    m_contentToolbar->addAction("List", this, SLOT(formatList()))->setToolTip("Unordered List (-)");
    m_contentToolbar->addAction("1.", this, SLOT(formatOrderedList()))->setToolTip("Ordered List (1.)");
    m_contentToolbar->addAction("\"", this, SLOT(formatQuote()))->setToolTip("Quote (>)");
    m_contentToolbar->addAction("---", this, SLOT(formatHorizontalLine()))->setToolTip("Horizontal Line");
    m_contentToolbar->addSeparator();
    m_contentToolbar->addAction("Code", this, SLOT(formatCode()))->setToolTip("Inline Code (`) ");
    m_contentToolbar->addAction("Block", this, SLOT(formatCodeBlock()))->setToolTip("Code Block (```)");
    m_contentToolbar->addSeparator();
    m_contentToolbar->addAction("Link", this, SLOT(formatLink()))->setToolTip("Insert Link ([])");
    m_contentToolbar->addAction("Table", this, SLOT(formatTable()))->setToolTip("Insert Table (|)");

    m_contentEdit = new QTextEdit(this);
    m_contentEdit->setAcceptRichText(false);
    m_contentEdit->setPlaceholderText("Markdown content...");

    m_previewEdit = new QTextEdit(this);
    m_previewEdit->setReadOnly(true);
    m_highlighter = new CodeHighlighter(m_previewEdit->document());
    m_previewEdit->setPlaceholderText("Preview...");

    updateMarkdownStyles();

    auto* editorSplitter = new QHBoxLayout();
    editorSplitter->addWidget(m_contentEdit, 1);
    editorSplitter->addWidget(m_previewEdit, 1);

    contentLayout->addWidget(m_contentToolbar);
    contentLayout->addLayout(editorSplitter);
    m_tabWidget->addTab(contentTab, "Content");

    // --- Tab 3: Additional ---
    auto* additionalTab = new QWidget();
    auto* additionalLayout = new QVBoxLayout(additionalTab);
    auto* listsLayout = new QGridLayout();

    listsLayout->addWidget(buildListEditor("Tags", m_tagList, this,
                                           SLOT(addTag()), SLOT(editTag()), SLOT(removeTag())),
                           0, 0);
    listsLayout->addWidget(buildListEditor("Flags", m_flagList, this,
                                           SLOT(addFlag()), SLOT(editFlag()), SLOT(removeFlag())),
                           0, 1);
    listsLayout->addWidget(buildListEditor("Aliases", m_aliasList, this,
                                           SLOT(addAlias()), SLOT(editAlias()), SLOT(removeAlias())),
                           1, 0, 1, 2); // Span aliases across both columns
    listsLayout->addWidget(buildListEditor("Properties", m_propertyList, this,
                                           SLOT(addProperty()), SLOT(editProperty()), SLOT(removeProperty())),
                           2, 0, 1, 2);

    additionalLayout->addLayout(listsLayout);
    m_tabWidget->addTab(additionalTab, "Metadata");

    // --- Tab 4: Links ---
    auto* linksTab = buildListEditor("Outgoing Links", m_linksList, this,
                                     SLOT(addLink()), SLOT(editLink()), SLOT(removeLink()));
    m_tabWidget->addTab(linksTab, "Links");

    // --- Tab 5: Backlinks ---
    auto* backlinksTab = buildListEditor("Incoming Links", m_backlinksList, this,
                                         SLOT(addBacklink()), SLOT(editBacklink()), SLOT(removeBacklink()));
    m_tabWidget->addTab(backlinksTab, "Backlinks");

    rootLayout->addWidget(m_tabWidget);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = buttonBox->button(QDialogButtonBox::Save);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &ItemEditDialog::validateAndAccept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, this, &ItemEditDialog::reject);

    rootLayout->addWidget(buttonBox);
}

void ItemEditDialog::connectSignals() {
    connect(m_contentEdit, &QTextEdit::textChanged, m_previewTimer, QOverload<>::of(&QTimer::start));
    connect(m_groupCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshTypes(); });
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshFields(); });
}

void ItemEditDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (m_titleEdit) {
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
    }
}


void ItemEditDialog::setGroups(const QList<GroupRecord>& groups) {
    {
        const QSignalBlocker blocker(m_groupCombo);
        m_groupCombo->clear();
        for (const GroupRecord& group : groups) {
            m_groupCombo->addItem(group.name, group.id);
        }
    }
    refreshTypes();
}

void ItemEditDialog::refreshTypes() {
    const int previousTypeId = m_typeCombo->currentData().toInt();
    const QSignalBlocker blocker(m_typeCombo);
    m_typeCombo->clear();
    m_typeCombo->addItem("None", -1);
    const int groupId = m_groupCombo->currentData().toInt();
    if (groupId <= 0) {
        refreshFields();
        return;
    }
    QString error;
    const auto types = DatabaseManager::loadItemTypes(groupId, &error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    for (const auto& type : types) {
        const QString scope = type.groupId < 0 ? "All groups" : type.groupName;
        m_typeCombo->addItem(QString("%1 (%2)").arg(type.name, scope), type.id);
    }
    const int index = m_typeCombo->findData(previousTypeId);
    if (index >= 0) {
        m_typeCombo->setCurrentIndex(index);
    }
    refreshFields();
}

QString ItemEditDialog::editorValue(int fieldId) const {
    QWidget* editor = m_fieldEditors.value(fieldId);
    if (auto* line = qobject_cast<QLineEdit*>(editor)) {
        return line->text().trimmed();
    }
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        return combo->currentData().toString();
    }
    return {};
}

void ItemEditDialog::captureFieldValues() {
    for (const auto& field : m_currentFields) {
        const QString value = editorValue(field.id);
        if (value.isEmpty()) {
            m_pendingFieldValues.remove(field.id);
        } else {
            m_pendingFieldValues.insert(field.id, value);
        }
    }
}

void ItemEditDialog::refreshFields() {
    captureFieldValues();
    for (auto it = m_blobPathEditors.cbegin(); it != m_blobPathEditors.cend(); ++it) {
        m_pendingBlobPaths.insert(it.key(), it.value()->text());
    }
    while (m_fieldsLayout->rowCount() > 0) {
        m_fieldsLayout->removeRow(0);
    }
    m_currentFields.clear();
    m_fieldEditors.clear();
    m_blobPathEditors.clear();
    m_fieldsLoadFailed = false;
    m_fieldsBox->hide();
    const int typeId = m_typeCombo->currentData().toInt();
    m_tabWidget->setTabEnabled(m_valuesTabIndex, typeId > 0);
    if (typeId <= 0) {
        if (m_tabWidget->currentIndex() == m_valuesTabIndex) m_tabWidget->setCurrentIndex(0);
        return;
    }
    QString error;
    m_currentFields = DatabaseManager::loadItemFields(typeId, &error);
    if (!error.isEmpty()) {
        m_fieldsLoadFailed = true;
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    for (const auto& field : m_currentFields) {
        const QString saved = m_pendingFieldValues.value(field.id);
        QWidget* editor = nullptr;
        if (field.dataType == FieldDataType::Boolean || field.dataType == FieldDataType::Enum) {
            auto* combo = new QComboBox(m_fieldsBox);
            combo->addItem("Not set", "");
            if (field.dataType == FieldDataType::Boolean) {
                combo->addItem("False", "false");
                combo->addItem("True", "true");
            } else {
                for (const auto& option : field.enumOptions) {
                    combo->addItem(option, option);
                }
            }
            const int index = combo->findData(saved);
            if (index >= 0) combo->setCurrentIndex(index);
            editor = combo;
            m_fieldsLayout->addRow(field.name + ":", combo);
        } else if (field.dataType == FieldDataType::Blob) {
            auto* wrapper = new QWidget(m_fieldsBox);
            auto* wrapperLayout = new QVBoxLayout(wrapper);
            wrapperLayout->setContentsMargins(0, 0, 0, 0);
            auto* currentRow = new QHBoxLayout();
            auto* valueEdit = new QLineEdit(saved, wrapper);
            valueEdit->setReadOnly(true);
            valueEdit->setPlaceholderText("No file selected (SHA-256)");
            auto* exportButton = new QPushButton("Save as...", wrapper);
            auto* clearButton = new QPushButton("Clear", wrapper);
            currentRow->addWidget(valueEdit, 1);
            currentRow->addWidget(exportButton);
            currentRow->addWidget(clearButton);
            wrapperLayout->addLayout(currentRow);
            auto* pathRow = new QHBoxLayout();
            auto* pathEdit = new QLineEdit(m_pendingBlobPaths.value(field.id), wrapper);
            pathEdit->setPlaceholderText("Path to a file to import or replace...");
            auto* browseButton = new QPushButton("Browse...", wrapper);
            auto* importButton = new QPushButton("Import", wrapper);
            pathRow->addWidget(pathEdit, 1);
            pathRow->addWidget(browseButton);
            pathRow->addWidget(importButton);
            wrapperLayout->addLayout(pathRow);
            connect(browseButton, &QPushButton::clicked, this, [this, pathEdit] {
                const QString path = QFileDialog::getOpenFileName(this, "Choose file");
                if (!path.isEmpty()) pathEdit->setText(path);
            });
            connect(importButton, &QPushButton::clicked, this, [this, pathEdit, valueEdit, fieldId = field.id] {
                const QString path = pathEdit->text().trimmed();
                if (path.isEmpty()) return;
                QString error;
                const QString hash = BlobStore::importFile(path, &error);
                if (hash.isEmpty()) QMessageBox::critical(this, "File error", error);
                else {
                    valueEdit->setText(hash);
                    pathEdit->clear();
                    m_pendingBlobPaths.remove(fieldId);
                }
            });
            connect(exportButton, &QPushButton::clicked, this, [this, valueEdit] {
                if (valueEdit->text().isEmpty()) return;
                const QString path = QFileDialog::getSaveFileName(this, "Save file as");
                if (path.isEmpty()) return;
                QString error;
                if (!BlobStore::exportFile(valueEdit->text(), path, &error)) {
                    QMessageBox::critical(this, "File error", error);
                }
            });
            connect(clearButton, &QPushButton::clicked, this, [this, valueEdit, pathEdit, fieldId = field.id] {
                valueEdit->clear();
                pathEdit->clear();
                m_pendingBlobPaths.remove(fieldId);
            });
            editor = valueEdit;
            m_blobPathEditors.insert(field.id, pathEdit);
            m_fieldsLayout->addRow(field.name + ":", wrapper);
        } else {
            auto* line = new QLineEdit(saved, m_fieldsBox);
            if (field.dataType == FieldDataType::Integer) {
                line->setValidator(new QRegularExpressionValidator(QRegularExpression("-?[0-9]*"), line));
            } else if (field.dataType == FieldDataType::Float) {
                auto* validator = new QDoubleValidator(line);
                validator->setLocale(QLocale::c());
                line->setValidator(validator);
            } else if (field.dataType == FieldDataType::Date) {
                line->setPlaceholderText("YYYY-MM-DD");
            } else if (field.dataType == FieldDataType::Time) {
                line->setPlaceholderText("HH:MM:SS");
            } else if (field.dataType == FieldDataType::Timestamp) {
                line->setPlaceholderText("YYYY-MM-DDTHH:MM:SS");
            }
            editor = line;
            m_fieldsLayout->addRow(field.name + ":", line);
        }
        m_fieldEditors.insert(field.id, editor);
    }
    m_fieldsBox->setVisible(!m_currentFields.isEmpty());
    m_noFieldsLabel->setVisible(m_currentFields.isEmpty());
}

void ItemEditDialog::setItem(const ItemRecord& item) {
    m_itemId = item.id;
    m_originalTypeId = item.itemTypeId;
    m_originalFieldValues = item.fieldValues;
    m_pendingFieldValues = item.fieldValues;
    m_pendingBlobPaths.clear();
    m_titleEdit->setText(item.title);
    m_disambiguationEdit->setText(item.disambiguation);

    {
        const QSignalBlocker blocker(m_groupCombo);
        const int groupIdx = m_groupCombo->findData(item.groupId);
        if (groupIdx >= 0) {
            m_groupCombo->setCurrentIndex(groupIdx);
        }
    }
    refreshTypes();
    {
        const QSignalBlocker blocker(m_typeCombo);
        const int typeIdx = m_typeCombo->findData(item.itemTypeId);
        if (typeIdx >= 0) {
            m_typeCombo->setCurrentIndex(typeIdx);
        }
    }
    refreshFields();

    const int underIdx = m_understandingCombo->findData(static_cast<int>(item.understanding));
    if (underIdx >= 0) {
        m_understandingCombo->setCurrentIndex(underIdx);
    }

    const int statusIdx = m_statusCombo->findData(static_cast<int>(item.status));
    if (statusIdx >= 0) {
        m_statusCombo->setCurrentIndex(statusIdx);
    }

    m_pinnedCheck->setChecked(item.pinned);
    m_contentEdit->setPlainText(item.content); // Use setPlainText to avoid auto-formatting during load
    updatePreview();

    setListValues(m_aliasList, item.aliases);
    setListValues(m_tagList, item.tags);
    setListValues(m_flagList, item.flags);
    m_properties = item.properties;
    updatePropertiesList();

    if (m_itemId != -1) {
        m_currentLinks = DatabaseManager::loadLinks(m_itemId);
        m_currentBacklinks = DatabaseManager::loadBacklinks(m_itemId);
        updateLinksList();
    }
}

ItemRecord ItemEditDialog::item() const {
    ItemRecord result;
    result.id = m_itemId;
    result.groupId = m_groupCombo->currentData().toInt();
    result.groupName = m_groupCombo->currentText();
    result.itemTypeId = m_typeCombo->currentData().toInt();
    for (const auto& field : m_currentFields) {
        const QString value = editorValue(field.id);
        if (!value.isEmpty()) {
            result.fieldValues.insert(field.id, value);
        }
    }
    result.title = m_titleEdit->text().trimmed();
    result.disambiguation = m_disambiguationEdit->text().trimmed();
    result.status = static_cast<ItemStatus>(m_statusCombo->currentData().toInt());
    result.understanding = static_cast<UnderstandingLevel>(m_understandingCombo->currentData().toInt());
    result.pinned = m_pinnedCheck->isChecked();
    result.content = m_contentEdit->toPlainText();
    result.aliases = valuesFromList(m_aliasList);
    result.tags = valuesFromList(m_tagList);
    result.flags = valuesFromList(m_flagList);
    result.properties = m_properties;
    return result;
}

void ItemEditDialog::updatePropertiesList() {
    m_propertyList->clear();
    for (const auto& property : m_properties) {
        m_propertyList->addItem(QString("%1 = %2").arg(property.key, property.value));
    }
}

bool ItemEditDialog::promptForProperty(PropertyRecord& property, int skipIndex) {
    QDialog dialog(this);
    dialog.setWindowTitle(skipIndex >= 0 ? "Edit property" : "Add property");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* keyEdit = new QLineEdit(property.key, &dialog);
    auto* valueEdit = new QLineEdit(property.value, &dialog);
    form->addRow("Key:", keyEdit);
    form->addRow("Value:", valueEdit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    const QString key = keyEdit->text().trimmed();
    if (key.isEmpty()) {
        QMessageBox::warning(this, "Validation", "Property key cannot be empty.");
        return false;
    }
    for (int i = 0; i < m_properties.size(); ++i) {
        if (i != skipIndex && m_properties.at(i).key.compare(key, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, "Validation", "Property key must be unique in this item.");
            return false;
        }
    }
    property.key = key;
    property.value = valueEdit->text();
    return true;
}

void ItemEditDialog::addProperty() {
    PropertyRecord property;
    if (!promptForProperty(property)) return;
    m_properties.push_back(property);
    updatePropertiesList();
}

void ItemEditDialog::editProperty() {
    const int row = m_propertyList->currentRow();
    if (row < 0 || row >= m_properties.size()) return;
    PropertyRecord property = m_properties.at(row);
    if (!promptForProperty(property, row)) return;
    m_properties[row] = property;
    updatePropertiesList();
    m_propertyList->setCurrentRow(row);
}

void ItemEditDialog::removeProperty() {
    const int row = m_propertyList->currentRow();
    if (row < 0 || row >= m_properties.size()) return;
    m_properties.removeAt(row);
    updatePropertiesList();
}

void ItemEditDialog::addValue(QListWidget* list, const QString& title, const QStringList& suggestions) {
    const QString value = getInputValue(title, "Value:", QString(), suggestions);
    if (value.isEmpty()) {
        return;
    }
    list->addItem(value);
    list->sortItems();
}

void ItemEditDialog::editValue(QListWidget* list, const QString& title, const QStringList& suggestions) {
    auto* item = list->currentItem();
    if (!item) {
        QMessageBox::information(this, title, "Select a value first.");
        return;
    }

    const QString value = getInputValue(title, "Value:", item->text(), suggestions);
    if (value.isEmpty()) {
        return;
    }
    item->setText(value);
    list->sortItems();
}

void ItemEditDialog::removeValue(QListWidget* list, const QString& title) {
    auto* item = list->currentItem();
    if (!item) {
        QMessageBox::information(this, title, "Select a value first.");
        return;
    }
    delete item;
}

void ItemEditDialog::insertMarkdown(const QString& prefix, const QString& suffix, const QString& defaultText) {
    auto cursor = m_contentEdit->textCursor();
    if (cursor.hasSelection()) {
        QString text = cursor.selectedText();
        cursor.insertText(prefix + text + suffix);
    } else {
        cursor.insertText(prefix + defaultText + suffix);
        if (!defaultText.isEmpty()) {
            // Select the default text for easy replacement
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, suffix.length() + defaultText.length());
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, defaultText.length());
            m_contentEdit->setTextCursor(cursor);
        }
    }
    m_contentEdit->setFocus();
}

void ItemEditDialog::formatBold() { insertMarkdown("**", "**", "bold text"); }
void ItemEditDialog::formatItalic() { insertMarkdown("*", "*", "italic text"); }
void ItemEditDialog::formatLink() { insertMarkdown("[", "](https://)", "link text"); }
void ItemEditDialog::formatTable() {
    insertMarkdown("\n| Header 1 | Header 2 |\n| --- | --- |\n| Cell 1 | Cell 2 |\n", "", "");
}

void ItemEditDialog::formatList() {
    insertMarkdown("\n- ", "", "list item");
}

void ItemEditDialog::formatOrderedList() {
    insertMarkdown("\n1. ", "", "list item");
}

void ItemEditDialog::formatH2() {
    insertMarkdown("\n## ", "", "Header 2");
}

void ItemEditDialog::formatH3() {
    insertMarkdown("\n### ", "", "Header 3");
}

void ItemEditDialog::formatH4() {
    insertMarkdown("\n#### ", "", "Header 4");
}

void ItemEditDialog::formatQuote() {
    insertMarkdown("\n> ", "", "quote");
}

void ItemEditDialog::formatCode() {
    insertMarkdown("`", "`", "code");
}

void ItemEditDialog::formatCodeBlock() {
    bool ok;
    QString language = QInputDialog::getText(this, "Code Block", "Language (e.g. cpp, python, sql):", QLineEdit::Normal, "", &ok);
    if (ok) {
        insertMarkdown("\n```" + language + "\n", "\n```\n", "code block");
    }
}

void ItemEditDialog::formatHorizontalLine() {
    insertMarkdown("\n---\n", "", "");
}

void ItemEditDialog::updatePreview() {
    m_previewEdit->setHtml(MarkdownConverter::toHtml(m_contentEdit->toPlainText()));
}

void ItemEditDialog::updateMarkdownStyles() {
    if (!m_previewEdit) return;

    QPalette pal = palette();
    QString bgColor = pal.color(QPalette::Base).name();
    QString textColor = pal.color(QPalette::Text).name();
    QString borderColor = pal.color(QPalette::Text).name(); // Use Text color for borders for maximum visibility
    QString headerBgColor = pal.color(QPalette::AlternateBase).name();
    QString codeBgColor = pal.color(QPalette::AlternateBase).name();
    if (pal.color(QPalette::Window).lightness() < 128) {
        // In dark theme, use pure black for code blocks
        codeBgColor = "#000000";
    }

    m_previewEdit->setStyleSheet(QString("QTextEdit[readOnly=\"true\"] { background-color: %1; color: %2; }").arg(bgColor, textColor));

    QString style = QString(
        "table { border: 1px solid %2; margin-top: 10px; margin-bottom: 10px; border-collapse: collapse; } "
        "th { background-color: %3; color: %1; font-weight: bold; border: 1px solid %2; padding: 4px; text-align: left; }"
        "td { color: %1; border: 1px solid %2; padding: 4px; text-align: left; }"
        "pre { background-color: %4; border: 1px solid %2; padding: 8px; border-radius: 4px; font-family: 'Courier New', monospace; white-space: pre; } "
        "pre[syntax=\"cpp\"] { -qt-user-property-1: \"cpp\"; } "
        "code { background-color: %4; font-family: 'Courier New', monospace; }"
        "body { color: %1; }"
    ).arg(textColor, borderColor, headerBgColor, codeBgColor);

    m_previewEdit->document()->setDefaultStyleSheet(style);
    updatePreview();
}

QStringList ItemEditDialog::valuesFromList(QListWidget* list) {
    QStringList values;
    for (int i = 0; i < list->count(); ++i) {
        values.push_back(list->item(i)->text().trimmed());
    }
    return values;
}

void ItemEditDialog::setListValues(QListWidget* list, const QStringList& values) {
    list->clear();
    list->addItems(values);
    list->sortItems();
}

void ItemEditDialog::addAlias() {
    QStringList suggestions;
    for (const auto& item : DatabaseManager::loadAliasUsage()) suggestions << item.value;
    addValue(m_aliasList, "Add alias", suggestions);
}
void ItemEditDialog::editAlias() {
    QStringList suggestions;
    for (const auto& item : DatabaseManager::loadAliasUsage()) suggestions << item.value;
    editValue(m_aliasList, "Edit alias", suggestions);
}
void ItemEditDialog::removeAlias() { removeValue(m_aliasList, "Remove alias"); }

void ItemEditDialog::addTag() {
    QStringList suggestions;
    for (const auto& item : DatabaseManager::loadTagUsage()) suggestions << item.value;
    addValue(m_tagList, "Add tag", suggestions);
}
void ItemEditDialog::editTag() {
    QStringList suggestions;
    for (const auto& item : DatabaseManager::loadTagUsage()) suggestions << item.value;
    editValue(m_tagList, "Edit tag", suggestions);
}
void ItemEditDialog::removeTag() { removeValue(m_tagList, "Remove tag"); }

void ItemEditDialog::addFlag() {
    QStringList suggestions;
    for (const auto& item : DatabaseManager::loadFlagUsage()) suggestions << item.value;
    addValue(m_flagList, "Add flag", suggestions);
}
void ItemEditDialog::editFlag() {
    QStringList suggestions;
    for (const auto& item : DatabaseManager::loadFlagUsage()) suggestions << item.value;
    editValue(m_flagList, "Edit flag", suggestions);
}
void ItemEditDialog::removeFlag() { removeValue(m_flagList, "Remove flag"); }

void ItemEditDialog::updateLinksList() {
    m_linksList->clear();
    auto typeToString = [](LinkType type) -> QString {
        switch (type) {
            case LinkType::IsA: return "Is A";
            case LinkType::PartOf: return "Part Of";
            case LinkType::Uses: return "Uses";
            case LinkType::DependsOn: return "Depends On";
            case LinkType::Implements: return "Implements";
            case LinkType::Related: return "Related";
            case LinkType::Contrasts: return "Contrasts";
            case LinkType::AlternativeTo: return "Alternative To";
            case LinkType::ParentOf: return "Parent Of";
            case LinkType::Custom: return "Custom";
            default: return "Link";
        }
    };

    // Sort links by position, then title
    std::sort(m_currentLinks.begin(), m_currentLinks.end(), [](const LinkRecord& a, const LinkRecord& b) {
        if (a.position != b.position) return a.position < b.position;
        return a.toItemTitle.compare(b.toItemTitle, Qt::CaseInsensitive) < 0;
    });

    for (const auto& link : m_currentLinks) {
        const QString type = link.linkType == LinkType::Custom && !link.customValue.isEmpty()
            ? QString("Custom: %1").arg(link.customValue)
            : typeToString(link.linkType);
        m_linksList->addItem(QString("[%1] %2 (%3)").arg(link.position).arg(link.toItemTitle, type));
    }

    m_backlinksList->clear();

    // Sort backlinks by position, then title
    std::sort(m_currentBacklinks.begin(), m_currentBacklinks.end(), [](const LinkRecord& a, const LinkRecord& b) {
        if (a.position != b.position) return a.position < b.position;
        return a.fromItemTitle.compare(b.fromItemTitle, Qt::CaseInsensitive) < 0;
    });

    for (const auto& link : m_currentBacklinks) {
        const QString type = link.linkType == LinkType::Custom && !link.customValue.isEmpty()
            ? QString("Custom: %1").arg(link.customValue)
            : typeToString(link.linkType);
        m_backlinksList->addItem(QString("[%1] %2 (%3)").arg(link.position).arg(link.fromItemTitle, type));
    }
}

namespace {
    struct LinkData {
        QString item;
        LinkType type;
        int position;
        QString customValue;
        bool accepted;
    };

    LinkData getLinkDetails(QWidget* parent, const QString& title, const QString& label, const QString& initialItem, LinkType initialType, int initialPosition = 0, const QString& initialCustomValue = QString()) {
        QDialog dialog(parent);
        dialog.setWindowTitle(title);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();

        auto* itemEdit = new QLineEdit(&dialog);
        itemEdit->setText(initialItem);
        QStringList titles = DatabaseManager::loadItemTitles();
        auto* completer = new QCompleter(titles, &dialog);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        itemEdit->setCompleter(completer);

        auto* typeCombo = new QComboBox(&dialog);
        typeCombo->addItem("Is A", static_cast<int>(LinkType::IsA));
        typeCombo->addItem("Part Of", static_cast<int>(LinkType::PartOf));
        typeCombo->addItem("Uses", static_cast<int>(LinkType::Uses));
        typeCombo->addItem("Depends On", static_cast<int>(LinkType::DependsOn));
        typeCombo->addItem("Implements", static_cast<int>(LinkType::Implements));
        typeCombo->addItem("Related", static_cast<int>(LinkType::Related));
        typeCombo->addItem("Contrasts", static_cast<int>(LinkType::Contrasts));
        typeCombo->addItem("Alternative To", static_cast<int>(LinkType::AlternativeTo));
        typeCombo->addItem("Parent Of", static_cast<int>(LinkType::ParentOf));
        typeCombo->addItem("Custom", static_cast<int>(LinkType::Custom));

        for (int i = 0; i < typeCombo->count(); ++i) {
            if (typeCombo->itemData(i).toInt() == static_cast<int>(initialType)) {
                typeCombo->setCurrentIndex(i);
                break;
            }
        }

        auto* positionSpin = new QSpinBox(&dialog);
        positionSpin->setRange(-10000, 10000);
        positionSpin->setValue(initialPosition);

        auto* customValueEdit = new QLineEdit(&dialog);
        customValueEdit->setText(initialCustomValue);
        customValueEdit->setEnabled(initialType == LinkType::Custom);
        QObject::connect(typeCombo, &QComboBox::currentIndexChanged, &dialog, [typeCombo, customValueEdit]() {
            customValueEdit->setEnabled(static_cast<LinkType>(typeCombo->currentData().toInt()) == LinkType::Custom);
        });

        form->addRow(label, itemEdit);
        form->addRow("Link Type:", typeCombo);
        form->addRow("Custom Value:", customValueEdit);
        form->addRow("Position:", positionSpin);
        layout->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);

        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            return {itemEdit->text().trimmed(), static_cast<LinkType>(typeCombo->currentData().toInt()), positionSpin->value(), customValueEdit->text().trimmed(), true};
        }
        return {"", LinkType::None, 0, "", false};
    }

    int findItemId(const QString& text) {
        QString title = text;
        QString disambiguation;
        if (text.contains(" [") && text.endsWith("]")) {
            int idx = text.lastIndexOf(" [");
            title = text.left(idx).trimmed();
            disambiguation = text.mid(idx + 2, text.length() - idx - 3).trimmed();
        }

        QSqlDatabase db = DatabaseManager::database();
        QSqlQuery query(db);
        if (disambiguation.isEmpty()) {
            query.prepare("SELECT id FROM item WHERE title = ? AND (disambiguation IS NULL OR disambiguation = '') LIMIT 1;");
            query.addBindValue(title);
            if (query.exec() && query.next()) {
                return query.value(0).toInt();
            }
            // Fallback: try to find by title only even if disambiguation is not specified
            query.prepare("SELECT id FROM item WHERE title = ? LIMIT 1;");
            query.addBindValue(title);
        } else {
            query.prepare("SELECT id FROM item WHERE title = ? AND disambiguation = ? LIMIT 1;");
            query.addBindValue(title);
            query.addBindValue(disambiguation);
        }

        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
        return -1;
    }
}

QString ItemEditDialog::getInputValue(const QString& title, const QString& label, const QString& initialValue, const QStringList& suggestions) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* lineEdit = new QLineEdit(&dialog);
    lineEdit->setText(initialValue);
    if (!suggestions.isEmpty()) {
        auto* completer = new QCompleter(suggestions, &dialog);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        lineEdit->setCompleter(completer);
    }

    form->addRow(label, lineEdit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        return lineEdit->text().trimmed();
    }
    return QString();
}

void ItemEditDialog::addLink() {
    LinkData data = getLinkDetails(this, "Add Link", "Target item:", "", LinkType::Related, 0);
    if (!data.accepted || data.item.isEmpty()) return;

    int toId = findItemId(data.item);
    if (toId != -1) {
        LinkRecord link;
        link.fromItemId = m_itemId;
        link.toItemId = toId;
        link.toItemTitle = data.item;
        link.linkType = data.type;
        link.position = data.position;
        link.customValue = data.customValue;
        m_currentLinks.append(link);
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Add Link", "Target item not found.");
    }
}

void ItemEditDialog::editLink() {
    int row = m_linksList->currentRow();
    if (row < 0 || row >= m_currentLinks.size()) return;

    LinkRecord& link = m_currentLinks[row];
    LinkData data = getLinkDetails(this, "Edit Link", "Target item:", link.toItemTitle, link.linkType, link.position, link.customValue);
    if (!data.accepted || data.item.isEmpty()) return;

    int toId = findItemId(data.item);
    if (toId != -1) {
        link.toItemId = toId;
        link.toItemTitle = data.item;
        link.linkType = data.type;
        link.position = data.position;
        link.customValue = data.customValue;
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Edit Link", "Target item not found.");
    }
}

void ItemEditDialog::removeLink() {
    int row = m_linksList->currentRow();
    if (row >= 0) {
        m_currentLinks.removeAt(row);
        updateLinksList();
    }
}

void ItemEditDialog::addBacklink() {
    LinkData data = getLinkDetails(this, "Add Backlink", "Source item:", "", LinkType::Related, 0);
    if (!data.accepted || data.item.isEmpty()) return;

    int fromId = findItemId(data.item);
    if (fromId != -1) {
        LinkRecord link;
        link.fromItemId = fromId;
        link.toItemId = m_itemId;
        link.fromItemTitle = data.item;
        link.linkType = data.type;
        link.position = data.position;
        link.customValue = data.customValue;
        m_currentBacklinks.append(link);
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Add Backlink", "Source item not found.");
    }
}

void ItemEditDialog::editBacklink() {
    int row = m_backlinksList->currentRow();
    if (row < 0 || row >= m_currentBacklinks.size()) return;

    LinkRecord& link = m_currentBacklinks[row];
    LinkData data = getLinkDetails(this, "Edit Backlink", "Source item:", link.fromItemTitle, link.linkType, link.position, link.customValue);
    if (!data.accepted || data.item.isEmpty()) return;

    int fromId = findItemId(data.item);
    if (fromId != -1) {
        link.fromItemId = fromId;
        link.fromItemTitle = data.item;
        link.linkType = data.type;
        link.position = data.position;
        link.customValue = data.customValue;
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Edit Backlink", "Source item not found.");
    }
}

void ItemEditDialog::removeBacklink() {
    int row = m_backlinksList->currentRow();
    if (row >= 0) {
        m_currentBacklinks.removeAt(row);
        updateLinksList();
    }
}

void ItemEditDialog::validateAndAccept() {
    if (m_fieldsLoadFailed) {
        QMessageBox::warning(this, "Validation", "Cannot save while type fields failed to load.");
        return;
    }
    if (m_groupCombo->currentIndex() < 0) {
        QMessageBox::warning(this, "Validation", "Create at least one group first.");
        return;
    }
    if (m_titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation", "Title cannot be empty.");
        m_titleEdit->setFocus();
        return;
    }

    if (m_itemId >= 0 && m_originalTypeId != m_typeCombo->currentData().toInt()
        && !m_originalFieldValues.isEmpty()) {
        const auto answer = QMessageBox::question(this, "Change type",
            QString("Changing the type will remove %1 saved field value(s) from this item. Continue?")
                .arg(m_originalFieldValues.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    for (auto it = m_blobPathEditors.cbegin(); it != m_blobPathEditors.cend(); ++it) {
        const QString path = it.value()->text().trimmed();
        if (path.isEmpty()) continue;
        QString error;
        const QString hash = BlobStore::importFile(path, &error);
        if (hash.isEmpty()) {
            QMessageBox::critical(this, "File error", error);
            return;
        }
        if (auto* valueEdit = qobject_cast<QLineEdit*>(m_fieldEditors.value(it.key()))) {
            valueEdit->setText(hash);
        }
        it.value()->clear();
        m_pendingBlobPaths.remove(it.key());
    }

    // Save item first to get an ID if it's new
    ItemRecord t = item();
    QString error;
    if (!DatabaseManager::saveItem(t, &error)) {
        QMessageBox::critical(this, "Error", "Failed to save item: " + error);
        return;
    }

    // If it was a new item, we need to load its ID (it might have been -1)
    if (m_itemId == -1) {
        // Find the item by group and title
        QSqlDatabase db = DatabaseManager::database();
        QSqlQuery query(db);
        query.prepare("SELECT id FROM item WHERE group_id = ? AND title = ? AND COALESCE(disambiguation, '') = ?;");
        query.addBindValue(t.groupId);
        query.addBindValue(t.title);
        query.addBindValue(t.disambiguation);
        if (query.exec() && query.next()) {
            m_itemId = query.value(0).toInt();
        }
    }

    if (m_itemId != -1) {
        // Sync links
        QList<LinkRecord> oldLinks = DatabaseManager::loadLinks(m_itemId);
        for (const auto& old : oldLinks) {
            bool found = false;
            for (const auto& cur : m_currentLinks) {
                if (cur.id == old.id) { found = true; break; }
            }
            if (!found) DatabaseManager::deleteLink(old.id);
        }
        for (auto& cur : m_currentLinks) {
            cur.fromItemId = m_itemId;
            DatabaseManager::saveLink(cur);
        }

        // Sync backlinks
        QList<LinkRecord> oldBacklinks = DatabaseManager::loadBacklinks(m_itemId);
        for (const auto& old : oldBacklinks) {
            bool found = false;
            for (const auto& cur : m_currentBacklinks) {
                if (cur.id == old.id) { found = true; break; }
            }
            if (!found) DatabaseManager::deleteLink(old.id);
        }
        for (auto& cur : m_currentBacklinks) {
            cur.toItemId = m_itemId;
            DatabaseManager::saveLink(cur);
        }
    }

    accept();
}
