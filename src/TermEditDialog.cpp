#include "TermEditDialog.h"

#include "MarkdownConverter.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
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

TermEditDialog::TermEditDialog(QWidget* parent)
    : QDialog(parent) {
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(1000);
    connect(m_previewTimer, &QTimer::timeout, this, &TermEditDialog::updatePreview);

    setupUi();
    connectSignals();
}

void TermEditDialog::setupUi() {
    setWindowTitle("Edit term");
    resize(800, 600);

    auto* rootLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);

    // --- Tab 1: General ---
    auto* generalTab = new QWidget();
    auto* generalLayout = new QVBoxLayout(generalTab);
    auto* formLayout = new QFormLayout();

    m_mapCombo = new QComboBox(this);
    m_statusCombo = new QComboBox(this);
    m_statusCombo->addItem("None", static_cast<int>(TermStatus::None));
    m_statusCombo->addItem("Draft", static_cast<int>(TermStatus::Draft));
    m_statusCombo->addItem("Completed", static_cast<int>(TermStatus::Completed));

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

    formLayout->addRow("Map:", m_mapCombo);
    formLayout->addRow("Title:", m_titleEdit);
    formLayout->addRow("Disambiguation:", m_disambiguationEdit);
    formLayout->addRow("Status:", m_statusCombo);
    formLayout->addRow("Understanding:", m_understandingCombo);
    formLayout->addRow("Pinned:", m_pinnedCheck);

    generalLayout->addLayout(formLayout);
    generalLayout->addStretch();
    m_tabWidget->addTab(generalTab, "General");

    // --- Tab 2: Content ---
    auto* contentTab = new QWidget();
    auto* contentLayout = new QVBoxLayout(contentTab);

    m_contentToolbar = new QToolBar(this);
    m_contentToolbar->setIconSize(QSize(16, 16));
    m_contentToolbar->addAction("B", this, SLOT(formatBold()))->setToolTip("Bold (**)");
    m_contentToolbar->addAction("I", this, SLOT(formatItalic()))->setToolTip("Italic (*)");
    m_contentToolbar->addAction("H", this, SLOT(formatHeader()))->setToolTip("Header (###)");
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

    // --- Tab 3: Links ---
    auto* linksTab = buildListEditor("Outgoing Links", m_linksList, this,
                                     SLOT(addLink()), SLOT(editLink()), SLOT(removeLink()));
    m_tabWidget->addTab(linksTab, "Links");

    // --- Tab 4: Backlinks ---
    auto* backlinksTab = buildListEditor("Incoming Links", m_backlinksList, this,
                                         SLOT(addBacklink()), SLOT(editBacklink()), SLOT(removeBacklink()));
    m_tabWidget->addTab(backlinksTab, "Backlinks");

    // --- Tab 5: Additional ---
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

    additionalLayout->addLayout(listsLayout);
    m_tabWidget->addTab(additionalTab, "Additional");

    rootLayout->addWidget(m_tabWidget);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = buttonBox->button(QDialogButtonBox::Save);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &TermEditDialog::validateAndAccept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, this, &TermEditDialog::reject);

    rootLayout->addWidget(buttonBox);
}

void TermEditDialog::connectSignals() {
    connect(m_contentEdit, &QTextEdit::textChanged, m_previewTimer, QOverload<>::of(&QTimer::start));
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

    const int mapIdx = m_mapCombo->findData(term.mapId);
    if (mapIdx >= 0) {
        m_mapCombo->setCurrentIndex(mapIdx);
    }

    const int underIdx = m_understandingCombo->findData(static_cast<int>(term.understanding));
    if (underIdx >= 0) {
        m_understandingCombo->setCurrentIndex(underIdx);
    }

    const int statusIdx = m_statusCombo->findData(static_cast<int>(term.status));
    if (statusIdx >= 0) {
        m_statusCombo->setCurrentIndex(statusIdx);
    }

    m_pinnedCheck->setChecked(term.pinned);
    m_contentEdit->setPlainText(term.content); // Use setPlainText to avoid auto-formatting during load
    updatePreview();

    setListValues(m_aliasList, term.aliases);
    setListValues(m_tagList, term.tags);
    setListValues(m_flagList, term.flags);

    if (m_termId != -1) {
        m_currentLinks = DatabaseManager::loadLinks(m_termId);
        m_currentBacklinks = DatabaseManager::loadBacklinks(m_termId);
        updateLinksList();
    }
}

TermRecord TermEditDialog::term() const {
    TermRecord result;
    result.id = m_termId;
    result.mapId = m_mapCombo->currentData().toInt();
    result.mapName = m_mapCombo->currentText();
    result.title = m_titleEdit->text().trimmed();
    result.disambiguation = m_disambiguationEdit->text().trimmed();
    result.status = static_cast<TermStatus>(m_statusCombo->currentData().toInt());
    result.understanding = static_cast<UnderstandingLevel>(m_understandingCombo->currentData().toInt());
    result.pinned = m_pinnedCheck->isChecked();
    result.content = m_contentEdit->toPlainText();
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

void TermEditDialog::insertMarkdown(const QString& prefix, const QString& suffix, const QString& defaultText) {
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

void TermEditDialog::formatBold() { insertMarkdown("**", "**", "bold text"); }
void TermEditDialog::formatItalic() { insertMarkdown("*", "*", "italic text"); }
void TermEditDialog::formatLink() { insertMarkdown("[", "](https://)", "link text"); }
void TermEditDialog::formatTable() {
    insertMarkdown("\n| Header 1 | Header 2 |\n| --- | --- |\n| Cell 1 | Cell 2 |\n", "", "");
}

void TermEditDialog::formatList() {
    insertMarkdown("\n- ", "", "list item");
}

void TermEditDialog::formatOrderedList() {
    insertMarkdown("\n1. ", "", "list item");
}

void TermEditDialog::formatHeader() {
    insertMarkdown("\n### ", "", "Header");
}

void TermEditDialog::formatQuote() {
    insertMarkdown("\n> ", "", "quote");
}

void TermEditDialog::formatCode() {
    insertMarkdown("`", "`", "code");
}

void TermEditDialog::formatCodeBlock() {
    bool ok;
    QString language = QInputDialog::getText(this, "Code Block", "Language (e.g. cpp, python, sql):", QLineEdit::Normal, "", &ok);
    if (ok) {
        insertMarkdown("\n```" + language + "\n", "\n```\n", "code block");
    }
}

void TermEditDialog::formatHorizontalLine() {
    insertMarkdown("\n---\n", "", "");
}

void TermEditDialog::updatePreview() {
    m_previewEdit->setHtml(MarkdownConverter::toHtml(m_contentEdit->toPlainText()));
}

void TermEditDialog::updateMarkdownStyles() {
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

void TermEditDialog::updateLinksList() {
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
            default: return "Link";
        }
    };

    for (const auto& link : m_currentLinks) {
        m_linksList->addItem(QString("%1 (%2)").arg(link.toTermTitle, typeToString(link.linkType)));
    }

    m_backlinksList->clear();
    for (const auto& link : m_currentBacklinks) {
        m_backlinksList->addItem(QString("%1 (%2)").arg(link.fromTermTitle, typeToString(link.linkType)));
    }
}

namespace {
    struct LinkData {
        QString term;
        LinkType type;
        bool accepted;
    };

    LinkData getLinkDetails(QWidget* parent, const QString& title, const QString& label, const QString& initialTerm, LinkType initialType) {
        QDialog dialog(parent);
        dialog.setWindowTitle(title);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();

        auto* termEdit = new QLineEdit(&dialog);
        termEdit->setText(initialTerm);
        QStringList titles = DatabaseManager::loadTermTitles();
        auto* completer = new QCompleter(titles, &dialog);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        termEdit->setCompleter(completer);

        auto* typeCombo = new QComboBox(&dialog);
        typeCombo->addItem("Is A", static_cast<int>(LinkType::IsA));
        typeCombo->addItem("Part Of", static_cast<int>(LinkType::PartOf));
        typeCombo->addItem("Uses", static_cast<int>(LinkType::Uses));
        typeCombo->addItem("Depends On", static_cast<int>(LinkType::DependsOn));
        typeCombo->addItem("Implements", static_cast<int>(LinkType::Implements));
        typeCombo->addItem("Related", static_cast<int>(LinkType::Related));
        typeCombo->addItem("Contrasts", static_cast<int>(LinkType::Contrasts));
        typeCombo->addItem("Alternative To", static_cast<int>(LinkType::AlternativeTo));

        for (int i = 0; i < typeCombo->count(); ++i) {
            if (typeCombo->itemData(i).toInt() == static_cast<int>(initialType)) {
                typeCombo->setCurrentIndex(i);
                break;
            }
        }

        form->addRow(label, termEdit);
        form->addRow("Link Type:", typeCombo);
        layout->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);

        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            return {termEdit->text().trimmed(), static_cast<LinkType>(typeCombo->currentData().toInt()), true};
        }
        return {"", LinkType::None, false};
    }

    int findTermId(const QString& text) {
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
            query.prepare("SELECT id FROM term WHERE title = ? AND (disambiguation IS NULL OR disambiguation = '') LIMIT 1;");
            query.addBindValue(title);
            if (query.exec() && query.next()) {
                return query.value(0).toInt();
            }
            // Fallback: try to find by title only even if disambiguation is not specified
            query.prepare("SELECT id FROM term WHERE title = ? LIMIT 1;");
            query.addBindValue(title);
        } else {
            query.prepare("SELECT id FROM term WHERE title = ? AND disambiguation = ? LIMIT 1;");
            query.addBindValue(title);
            query.addBindValue(disambiguation);
        }

        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
        return -1;
    }
}

void TermEditDialog::addLink() {
    LinkData data = getLinkDetails(this, "Add Link", "Target term:", "", LinkType::Related);
    if (!data.accepted || data.term.isEmpty()) return;

    int toId = findTermId(data.term);
    if (toId != -1) {
        LinkRecord link;
        link.fromTermId = m_termId;
        link.toTermId = toId;
        link.toTermTitle = data.term;
        link.linkType = data.type;
        m_currentLinks.append(link);
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Add Link", "Target term not found.");
    }
}

void TermEditDialog::editLink() {
    int row = m_linksList->currentRow();
    if (row < 0 || row >= m_currentLinks.size()) return;

    LinkRecord& link = m_currentLinks[row];
    LinkData data = getLinkDetails(this, "Edit Link", "Target term:", link.toTermTitle, link.linkType);
    if (!data.accepted || data.term.isEmpty()) return;

    int toId = findTermId(data.term);
    if (toId != -1) {
        link.toTermId = toId;
        link.toTermTitle = data.term;
        link.linkType = data.type;
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Edit Link", "Target term not found.");
    }
}

void TermEditDialog::removeLink() {
    int row = m_linksList->currentRow();
    if (row >= 0) {
        m_currentLinks.removeAt(row);
        updateLinksList();
    }
}

void TermEditDialog::addBacklink() {
    LinkData data = getLinkDetails(this, "Add Backlink", "Source term:", "", LinkType::Related);
    if (!data.accepted || data.term.isEmpty()) return;

    int fromId = findTermId(data.term);
    if (fromId != -1) {
        LinkRecord link;
        link.fromTermId = fromId;
        link.toTermId = m_termId;
        link.fromTermTitle = data.term;
        link.linkType = data.type;
        m_currentBacklinks.append(link);
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Add Backlink", "Source term not found.");
    }
}

void TermEditDialog::editBacklink() {
    int row = m_backlinksList->currentRow();
    if (row < 0 || row >= m_currentBacklinks.size()) return;

    LinkRecord& link = m_currentBacklinks[row];
    LinkData data = getLinkDetails(this, "Edit Backlink", "Source term:", link.fromTermTitle, link.linkType);
    if (!data.accepted || data.term.isEmpty()) return;

    int fromId = findTermId(data.term);
    if (fromId != -1) {
        link.fromTermId = fromId;
        link.fromTermTitle = data.term;
        link.linkType = data.type;
        updateLinksList();
    } else {
        QMessageBox::warning(this, "Edit Backlink", "Source term not found.");
    }
}

void TermEditDialog::removeBacklink() {
    int row = m_backlinksList->currentRow();
    if (row >= 0) {
        m_currentBacklinks.removeAt(row);
        updateLinksList();
    }
}

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

    // Save term first to get an ID if it's new
    TermRecord t = term();
    QString error;
    if (!DatabaseManager::saveTerm(t, &error)) {
        QMessageBox::critical(this, "Error", "Failed to save term: " + error);
        return;
    }

    // If it was a new term, we need to load its ID (it might have been -1)
    if (m_termId == -1) {
        // Find the term by map and title
        QSqlDatabase db = DatabaseManager::database();
        QSqlQuery query(db);
        query.prepare("SELECT id FROM term WHERE map_id = ? AND title = ?;");
        query.addBindValue(t.mapId);
        query.addBindValue(t.title);
        if (query.exec() && query.next()) {
            m_termId = query.value(0).toInt();
        }
    }

    if (m_termId != -1) {
        // Sync links
        QList<LinkRecord> oldLinks = DatabaseManager::loadLinks(m_termId);
        for (const auto& old : oldLinks) {
            bool found = false;
            for (const auto& cur : m_currentLinks) {
                if (cur.id == old.id) { found = true; break; }
            }
            if (!found) DatabaseManager::deleteLink(old.id);
        }
        for (auto& cur : m_currentLinks) {
            cur.fromTermId = m_termId;
            DatabaseManager::saveLink(cur);
        }

        // Sync backlinks
        QList<LinkRecord> oldBacklinks = DatabaseManager::loadBacklinks(m_termId);
        for (const auto& old : oldBacklinks) {
            bool found = false;
            for (const auto& cur : m_currentBacklinks) {
                if (cur.id == old.id) { found = true; break; }
            }
            if (!found) DatabaseManager::deleteLink(old.id);
        }
        for (auto& cur : m_currentBacklinks) {
            cur.toTermId = m_termId;
            DatabaseManager::saveLink(cur);
        }
    }

    accept();
}
