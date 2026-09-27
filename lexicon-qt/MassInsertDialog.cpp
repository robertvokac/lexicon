#include "MassInsertDialog.h"

#include "ImageValueView.h"

#include <algorithm>
#include <optional>

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr auto kDraftKey = "massInsert/draftV1";
constexpr int kTitle = 0;
constexpr int kDisambiguation = 1;
constexpr int kAliases = 2;
constexpr int kTags = 3;
constexpr int kFlags = 4;
constexpr int kStatus = 5;
constexpr int kUnderstanding = 6;
constexpr int kPinned = 7;
constexpr int kContent = 8;
constexpr int kProperties = 9;
constexpr int kFirstValue = 10;

QString dataTypeName(FieldDataType type) {
    switch (type) {
    case FieldDataType::Integer: return "Integer";
    case FieldDataType::Float: return "Float";
    case FieldDataType::Text: return "Text";
    case FieldDataType::Date: return "Date";
    case FieldDataType::Time: return "Time";
    case FieldDataType::Timestamp: return "Timestamp";
    case FieldDataType::Boolean: return "Boolean";
    case FieldDataType::Enum: return "Enum";
    case FieldDataType::Blob: return "Blob";
    case FieldDataType::Other: return "Other";
    case FieldDataType::Image: return "Image";
    }
    return "Unknown";
}

QJsonObject storedDraft() {
    const QByteArray bytes = QSettings().value(kDraftKey).toByteArray();
    if (bytes.isEmpty()) return {};
    const auto document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? document.object() : QJsonObject{};
}

void removeStoredDraft() {
    QSettings settings;
    settings.remove(kDraftKey);
    settings.sync();
}

QStringList splitValues(const QString& text) {
    QStringList result;
    QSet<QString> seen;
    for (const auto& part : text.split(QRegularExpression("[,\\n]"), Qt::SkipEmptyParts)) {
        const QString value = part.trimmed();
        const QString key = value.toLower();
        if (!value.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            result.append(value);
        }
    }
    return result;
}

bool parseProperties(const QString& text, QList<PropertyRecord>& result, QString& error) {
    QSet<QString> seen;
    for (const auto& part : text.split(QRegularExpression("[;\\n]"), Qt::SkipEmptyParts)) {
        const int equals = part.indexOf('=');
        const QString key = equals >= 0 ? part.left(equals).trimmed() : QString();
        const QString value = equals >= 0 ? part.mid(equals + 1).trimmed() : QString();
        const QString folded = key.toLower();
        if (key.isEmpty()) {
            error = "Properties must use key=value.";
            return false;
        }
        if (seen.contains(folded)) {
            error = QString("Property '%1' occurs twice.").arg(key);
            return false;
        }
        seen.insert(folded);
        result.append({key, value});
    }
    return true;
}

QLineEdit* lineEdit(const QString& value, QWidget* parent = nullptr) {
    auto* edit = new QLineEdit(value, parent);
    edit->setClearButtonEnabled(true);
    return edit;
}

QString textOf(QWidget* widget) {
    const QVariant massInsertValue = widget->property("massInsertValue");
    if (massInsertValue.isValid()) return massInsertValue.toString();
    if (auto* line = qobject_cast<QLineEdit*>(widget)) return line->text();
    if (auto* text = qobject_cast<QPlainTextEdit*>(widget)) return text->toPlainText();
    return {};
}

QString comboValue(QWidget* widget) {
    if (auto* combo = qobject_cast<QComboBox*>(widget)) return combo->currentData().toString();
    return {};
}

struct Scope {
    int groupId = -1;
    int typeId = -1;
};

std::optional<Scope> chooseScope(QWidget* parent) {
    QString error;
    const auto groups = services().groups.loadGroups(&error);
    if (!error.isEmpty()) {
        QMessageBox::critical(parent, "Mass Insert", error);
        return std::nullopt;
    }
    if (groups.isEmpty()) {
        QMessageBox::information(parent, "Mass Insert", "Create a Group before inserting items.");
        return std::nullopt;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("Mass Insert");
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* group = new QComboBox(&dialog);
    group->setObjectName("massInsertGroup");
    group->addItem("Choose a Group", -1);
    for (const auto& entry : groups)
        group->addItem(entry.name, entry.id);
    auto* type = new QComboBox(&dialog);
    type->setObjectName("massInsertType");
    type->addItem("No type", -1);
    form->addRow("Group:", group);
    form->addRow("Type:", type);
    root->addLayout(form);
    auto* hint = new QLabel("Group is required. Choosing a Type adds one column "
                            "for each of its fields.",
                            &dialog);
    hint->setWordWrap(true);
    root->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Continue");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    root->addWidget(buttons);

    QObject::connect(group, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                     [group, type, buttons, parent](int) {
                         const int groupId = group->currentData().toInt();
                         buttons->button(QDialogButtonBox::Ok)->setEnabled(groupId > 0);
                         type->clear();
                         type->addItem("No type", -1);
                         if (groupId <= 0) return;
                         QString loadError;
                         const auto types = services().types.loadItemTypes(groupId, &loadError);
                         if (!loadError.isEmpty()) {
                             QMessageBox::critical(parent, "Mass Insert", loadError);
                             return;
                         }
                         for (const auto& entry : types) {
                             const QString scope =
                                 entry.groupId > 0 ? entry.groupName : "All groups";
                             type->addItem(QString("%1 (%2)").arg(entry.name, scope), entry.id);
                         }
                     });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return Scope{group->currentData().toInt(), type->currentData().toInt()};
}

bool scopeExists(int groupId, int typeId) {
    QString error;
    const auto groups = services().groups.loadGroups(&error);
    if (!error.isEmpty() ||
        std::none_of(groups.begin(), groups.end(),
                     [groupId](const auto& group) { return group.id == groupId; }))
        return false;
    if (typeId <= 0) return true;
    const auto types = services().types.loadItemTypes(groupId, &error);
    return error.isEmpty() && std::any_of(types.begin(), types.end(),
                                          [typeId](const auto& type) { return type.id == typeId; });
}
} // namespace

MassInsertDialog::MassInsertDialog(int groupId, int typeId, const QList<ItemFieldRecord>& fields,
                                   const QJsonArray& rows, QWidget* parent)
    : QDialog(parent), m_groupId(groupId), m_typeId(typeId), m_fields(fields) {
    setWindowTitle("Mass Insert");
    resize(1400, 720);
    auto* root = new QVBoxLayout(this);
    auto* scope = new QLabel(this);
    scope->setObjectName("massInsertScope");
    QString groupName = QString("Group #%1").arg(groupId);
    for (const auto& group : services().groups.loadGroups())
        if (group.id == groupId) groupName = group.name;
    QString typeName = "No type";
    if (typeId > 0) {
        for (const auto& type : services().types.loadItemTypes(groupId))
            if (type.id == typeId) typeName = type.name;
    }
    scope->setText(QString("New items: %1 · %2").arg(groupName, typeName));
    root->addWidget(scope);
    auto* help = new QLabel("Lists use commas. Properties use key=value separated by semicolons. "
                            "Every edit is backed up locally and offered again next time.",
                            this);
    help->setWordWrap(true);
    root->addWidget(help);

    m_table = new QTableWidget(this);
    m_table->setObjectName("massInsertTable");
    QStringList headers{"Title",  "Disambiguation", "Aliases", "Tags",    "Flags",
                        "Status", "Understanding",  "Pinned",  "Content", "Properties"};
    for (const auto& field : m_fields)
        headers.append(field.name);
    headers.append("Actions");
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(kTitle, 220);
    m_table->setColumnWidth(kContent, 260);
    m_table->setColumnWidth(kProperties, 220);
    for (int index = 0; index < m_fields.size(); ++index) {
        auto* header = m_table->horizontalHeaderItem(kFirstValue + index);
        header->setToolTip(QString("%1%2")
                               .arg(dataTypeName(m_fields[index].dataType))
                               .arg(m_fields[index].description.isEmpty()
                                        ? QString()
                                        : " — " + m_fields[index].description));
    }
    root->addWidget(m_table, 1);

    auto* tools = new QHBoxLayout();
    auto* add = new QPushButton("Add row", this);
    add->setObjectName("massInsertAddRow");
    auto* discard = new QPushButton("Discard draft", this);
    discard->setObjectName("massInsertDiscard");
    m_status = new QLabel(this);
    m_status->setObjectName("massInsertStatus");
    m_status->setWordWrap(true);
    tools->addWidget(add);
    tools->addWidget(discard);
    tools->addWidget(m_status, 1);
    root->addLayout(tools);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Save)->setText("Insert items");
    buttons->button(QDialogButtonBox::Save)->setObjectName("massInsertSave");
    root->addWidget(buttons);
    connect(add, &QPushButton::clicked, this, &MassInsertDialog::addRow);
    connect(discard, &QPushButton::clicked, this, &MassInsertDialog::discardDraft);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, this,
            &MassInsertDialog::insertItems);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this,
            &QDialog::reject);

    m_draftTimer = new QTimer(this);
    m_draftTimer->setSingleShot(true);
    m_draftTimer->setInterval(250);
    connect(m_draftTimer, &QTimer::timeout, this, &MassInsertDialog::persistDraft);
    setRows(rows.isEmpty() ? QJsonArray{QJsonObject{}} : rows);
}

bool MassInsertDialog::open(QWidget* parent) {
    QJsonObject draft = storedDraft();
    int groupId = draft.value("groupId").toInt(-1);
    int typeId = draft.value("typeId").toInt(-1);
    QJsonArray rows;
    if (!draft.isEmpty()) {
        if (!scopeExists(groupId, typeId)) {
            QMessageBox::information(parent, "Mass Insert",
                                     "The saved draft refers to a Group or Type that "
                                     "no longer exists and cannot be resumed.");
            removeStoredDraft();
            draft = {};
        } else {
            QMessageBox choice(parent);
            choice.setWindowTitle("Resume Mass Insert");
            choice.setText(QString("Resume %1 locally backed-up row(s)?")
                               .arg(draft.value("rows").toArray().size()));
            auto* resume = choice.addButton("Resume", QMessageBox::AcceptRole);
            auto* startOver = choice.addButton("Start over", QMessageBox::DestructiveRole);
            choice.addButton(QMessageBox::Cancel);
            choice.setDefaultButton(qobject_cast<QPushButton*>(resume));
            choice.exec();
            if (choice.clickedButton() == nullptr ||
                choice.buttonRole(choice.clickedButton()) == QMessageBox::RejectRole)
                return false;
            if (choice.clickedButton() == startOver) {
                removeStoredDraft();
                draft = {};
            } else {
                rows = draft.value("rows").toArray();
            }
        }
    }
    if (draft.isEmpty()) {
        const auto scope = chooseScope(parent);
        if (!scope) return false;
        groupId = scope->groupId;
        typeId = scope->typeId;
    }
    QString error;
    const auto fields =
        typeId > 0 ? services().types.loadItemFields(typeId, &error) : QList<ItemFieldRecord>{};
    if (!error.isEmpty()) {
        QMessageBox::critical(parent, "Mass Insert", error);
        return false;
    }
    MassInsertDialog dialog(groupId, typeId, fields, rows, parent);
    dialog.exec();
    return dialog.insertedCount() > 0;
}

void MassInsertDialog::setRows(const QJsonArray& rows) {
    m_populating = true;
    m_table->setRowCount(0);
    for (const auto& row : rows)
        appendRow(row.isObject() ? row.toObject() : QJsonObject{});
    if (m_table->rowCount() == 0) appendRow();
    m_populating = false;
}

void MassInsertDialog::appendRow(QJsonObject row) {
    const int index = m_table->rowCount();
    m_table->insertRow(index);
    const auto addLine = [&](int column, const QString& key,
                             const QString& placeholder = QString()) {
        auto* edit = lineEdit(row.value(key).toString(), m_table);
        edit->setPlaceholderText(placeholder);
        m_table->setCellWidget(index, column, edit);
        connect(edit, &QLineEdit::textChanged, this, &MassInsertDialog::scheduleDraft);
    };
    addLine(kTitle, "title");
    addLine(kDisambiguation, "disambiguation");
    addLine(kAliases, "aliases", "Comma separated");
    addLine(kTags, "tags", "Comma separated");
    addLine(kFlags, "flags", "Comma separated");

    auto* status = new QComboBox(m_table);
    status->addItem("None", static_cast<int>(ItemStatus::None));
    status->addItem("Draft", static_cast<int>(ItemStatus::Draft));
    status->addItem("Completed", static_cast<int>(ItemStatus::Completed));
    status->setCurrentIndex(std::clamp(row.value("status").toInt(0), 0, 2));
    m_table->setCellWidget(index, kStatus, status);
    connect(status, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MassInsertDialog::scheduleDraft);

    auto* understanding = new QComboBox(m_table);
    for (const auto& name : {"Unknown", "Recognized", "Understood", "Practiced", "Mastered"})
        understanding->addItem(name, understanding->count());
    understanding->setCurrentIndex(std::clamp(row.value("understanding").toInt(0), 0, 4));
    m_table->setCellWidget(index, kUnderstanding, understanding);
    connect(understanding, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MassInsertDialog::scheduleDraft);

    auto* pinned = new QComboBox(m_table);
    pinned->addItem("No", false);
    pinned->addItem("Yes", true);
    pinned->setCurrentIndex(row.value("pinned").toBool(false) ? 1 : 0);
    m_table->setCellWidget(index, kPinned, pinned);
    connect(pinned, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MassInsertDialog::scheduleDraft);

    auto* content = new QPlainTextEdit(row.value("content").toString(), m_table);
    content->setPlaceholderText("Markdown");
    content->setTabChangesFocus(true);
    m_table->setCellWidget(index, kContent, content);
    connect(content, &QPlainTextEdit::textChanged, this, &MassInsertDialog::scheduleDraft);
    auto* properties = new QPlainTextEdit(row.value("properties").toString(), m_table);
    properties->setPlaceholderText("key=value; key=value");
    properties->setTabChangesFocus(true);
    m_table->setCellWidget(index, kProperties, properties);
    connect(properties, &QPlainTextEdit::textChanged, this, &MassInsertDialog::scheduleDraft);

    const auto values = row.value("fieldValues").toObject();
    for (int fieldIndex = 0; fieldIndex < m_fields.size(); ++fieldIndex) {
        const auto& field = m_fields[fieldIndex];
        const int column = kFirstValue + fieldIndex;
        const QString value = values.value(QString::number(field.id)).toString();
        if (field.dataType == FieldDataType::Boolean || field.dataType == FieldDataType::Enum) {
            auto* combo = new QComboBox(m_table);
            combo->addItem("Not set", QString());
            if (field.dataType == FieldDataType::Boolean) {
                combo->addItem("Yes", "true");
                combo->addItem("No", "false");
            } else {
                for (const auto& option : field.enumOptions)
                    combo->addItem(option, option);
            }
            const int selected = combo->findData(value);
            combo->setCurrentIndex(selected >= 0 ? selected : 0);
            m_table->setCellWidget(index, column, combo);
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    &MassInsertDialog::scheduleDraft);
        } else if (field.dataType == FieldDataType::Text) {
            auto* edit = new QPlainTextEdit(value, m_table);
            edit->setTabChangesFocus(true);
            m_table->setCellWidget(index, column, edit);
            connect(edit, &QPlainTextEdit::textChanged, this, &MassInsertDialog::scheduleDraft);
        } else if (field.dataType == FieldDataType::Image) {
            auto* editor = new QWidget(m_table);
            editor->setProperty("massInsertValue", value);
            auto* layout = new QVBoxLayout(editor);
            layout->setContentsMargins(2, 2, 2, 2);
            layout->setSpacing(3);
            auto* actions = new QHBoxLayout();
            actions->setContentsMargins(0, 0, 0, 0);
            auto* choose = new QPushButton(editor);
            choose->setObjectName(QString("massInsertImageChoose_%1_%2").arg(index).arg(field.id));
            auto* clear = new QPushButton("Clear", editor);
            clear->setObjectName(QString("massInsertImageClear_%1_%2").arg(index).arg(field.id));
            auto* status = new QLabel(editor);
            status->setObjectName(QString("massInsertImageStatus_%1_%2").arg(index).arg(field.id));
            status->setWordWrap(true);
            actions->addWidget(choose);
            actions->addWidget(clear);
            layout->addLayout(actions);
            layout->addWidget(status);
            const auto refresh = [editor, choose, clear, status] {
                const QString imageValue = editor->property("massInsertValue").toString();
                const bool present = !imagevalues::describe(imageValue).isEmpty();
                choose->setText(present ? "Replace image..." : "Choose image...");
                clear->setEnabled(!imageValue.isEmpty());
                status->setText(present ? imagevalues::describe(imageValue)
                                        : imageValue.isEmpty() ? "No image"
                                                               : "Invalid saved image value");
            };
            refresh();
            connect(choose, &QPushButton::clicked, this,
                    [this, editor, refresh] {
                        const QString path = QFileDialog::getOpenFileName(
                            this, "Choose image", {}, imagevalues::fileFilter());
                        if (path.isEmpty()) return;
                        QString error;
                        const QString imageValue = imagevalues::importFile(path, &error);
                        if (imageValue.isEmpty()) {
                            QMessageBox::critical(this, "Image", error);
                            return;
                        }
                        editor->setProperty("massInsertValue", imageValue);
                        refresh();
                        scheduleDraft();
                    });
            connect(clear, &QPushButton::clicked, this,
                    [this, editor, refresh] {
                        editor->setProperty("massInsertValue", QString());
                        refresh();
                        scheduleDraft();
                    });
            m_table->setCellWidget(index, column, editor);
        } else {
            auto* edit = lineEdit(value, m_table);
            if (field.dataType == FieldDataType::Blob) edit->setPlaceholderText("SHA-256");
            m_table->setCellWidget(index, column, edit);
            connect(edit, &QLineEdit::textChanged, this, &MassInsertDialog::scheduleDraft);
        }
    }

    auto* remove = new QPushButton("Remove", m_table);
    remove->setObjectName(QString("massInsertRemove_%1").arg(index));
    m_table->setCellWidget(index, kFirstValue + m_fields.size(), remove);
    connect(remove, &QPushButton::clicked, this, [this, remove] {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->cellWidget(row, kFirstValue + m_fields.size()) == remove) {
                removeRow(row);
                return;
            }
        }
    });
}

QJsonObject MassInsertDialog::rowData(int row) const {
    QJsonObject result{{"title", textOf(m_table->cellWidget(row, kTitle))},
                       {"disambiguation", textOf(m_table->cellWidget(row, kDisambiguation))},
                       {"aliases", textOf(m_table->cellWidget(row, kAliases))},
                       {"tags", textOf(m_table->cellWidget(row, kTags))},
                       {"flags", textOf(m_table->cellWidget(row, kFlags))},
                       {"content", textOf(m_table->cellWidget(row, kContent))},
                       {"properties", textOf(m_table->cellWidget(row, kProperties))}};
    result["status"] =
        qobject_cast<QComboBox*>(m_table->cellWidget(row, kStatus))->currentData().toInt();
    result["understanding"] =
        qobject_cast<QComboBox*>(m_table->cellWidget(row, kUnderstanding))->currentData().toInt();
    result["pinned"] =
        qobject_cast<QComboBox*>(m_table->cellWidget(row, kPinned))->currentData().toBool();
    QJsonObject values;
    for (int index = 0; index < m_fields.size(); ++index) {
        QWidget* editor = m_table->cellWidget(row, kFirstValue + index);
        const QString value =
            qobject_cast<QComboBox*>(editor) ? comboValue(editor) : textOf(editor);
        values[QString::number(m_fields[index].id)] = value;
    }
    result["fieldValues"] = values;
    return result;
}

QJsonArray MassInsertDialog::rowsData() const {
    QJsonArray rows;
    for (int row = 0; row < m_table->rowCount(); ++row)
        rows.append(rowData(row));
    return rows;
}

bool MassInsertDialog::rowMeaningful(const QJsonObject& row) const {
    for (const auto& key :
         {"title", "disambiguation", "aliases", "tags", "flags", "content", "properties"})
        if (!row.value(key).toString().trimmed().isEmpty()) return true;
    if (row.value("status").toInt() != 0 || row.value("understanding").toInt() != 0 ||
        row.value("pinned").toBool())
        return true;
    for (const auto value : row.value("fieldValues").toObject())
        if (!value.toString().trimmed().isEmpty()) return true;
    return false;
}

void MassInsertDialog::scheduleDraft() {
    if (!m_populating) m_draftTimer->start();
}

void MassInsertDialog::persistDraft() {
    if (m_populating) return;
    const QJsonArray rows = rowsData();
    bool hasWork = false;
    for (const auto& row : rows)
        if (rowMeaningful(row.toObject())) hasWork = true;
    if (!hasWork) {
        clearStoredDraft();
        m_status->setText("No unsaved rows.");
        return;
    }
    const QJsonObject draft{{"version", 1},
                            {"groupId", m_groupId},
                            {"typeId", m_typeId},
                            {"savedAt", QDateTime::currentMSecsSinceEpoch()},
                            {"rows", rows}};
    QSettings settings;
    settings.setValue(kDraftKey, QJsonDocument(draft).toJson(QJsonDocument::Compact));
    settings.sync();
    m_status->setText(settings.status() == QSettings::NoError
                          ? QString("Draft backed up locally at %1.")
                                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                          : QString("Draft backup could not be written."));
}

void MassInsertDialog::clearStoredDraft() { removeStoredDraft(); }

void MassInsertDialog::reject() {
    m_draftTimer->stop();
    persistDraft();
    QDialog::reject();
}

void MassInsertDialog::addRow() {
    appendRow();
    m_table->scrollToBottom();
    if (auto* title =
            qobject_cast<QLineEdit*>(m_table->cellWidget(m_table->rowCount() - 1, kTitle)))
        title->setFocus();
    scheduleDraft();
}

void MassInsertDialog::removeRow(int row) {
    m_table->removeRow(row);
    if (m_table->rowCount() == 0) appendRow();
    scheduleDraft();
}

void MassInsertDialog::discardDraft() {
    if (QMessageBox::question(this, "Discard Mass Insert draft",
                              "Discard every unfinished row in this worksheet?",
                              QMessageBox::Discard | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Discard)
        return;
    clearStoredDraft();
    QDialog::reject();
}

void MassInsertDialog::insertItems() {
    m_draftTimer->stop();
    QJsonArray remaining;
    for (const auto& value : rowsData())
        if (rowMeaningful(value.toObject())) remaining.append(value);
    if (remaining.isEmpty()) {
        m_status->setText("Add at least one item.");
        return;
    }
    for (int index = 0; index < remaining.size(); ++index) {
        const auto row = remaining[index].toObject();
        if (row.value("title").toString().trimmed().isEmpty()) {
            m_status->setText(QString("Row %1: Title is required.").arg(index + 1));
            return;
        }
        QList<PropertyRecord> ignored;
        QString error;
        if (!parseProperties(row.value("properties").toString(), ignored, error)) {
            m_status->setText(QString("Row %1: %2").arg(index + 1).arg(error));
            return;
        }
    }
    setRows(remaining);
    persistDraft();
    while (!remaining.isEmpty()) {
        const auto row = remaining.first().toObject();
        ItemRecord item;
        item.groupId = m_groupId;
        item.itemTypeId = m_typeId;
        item.title = row.value("title").toString().trimmed();
        item.disambiguation = row.value("disambiguation").toString().trimmed();
        item.aliases = splitValues(row.value("aliases").toString());
        item.tags = splitValues(row.value("tags").toString());
        item.flags = splitValues(row.value("flags").toString());
        item.status = static_cast<ItemStatus>(row.value("status").toInt());
        item.understanding = static_cast<UnderstandingLevel>(row.value("understanding").toInt());
        item.pinned = row.value("pinned").toBool();
        item.content = row.value("content").toString();
        QString propertyError;
        parseProperties(row.value("properties").toString(), item.properties, propertyError);
        const auto values = row.value("fieldValues").toObject();
        for (const auto& field : m_fields) {
            const QString value = values.value(QString::number(field.id)).toString().trimmed();
            if (!value.isEmpty()) item.fieldValues.insert(field.id, value);
        }
        QString error;
        if (services().items.createItem(item, &error) <= 0) {
            setRows(remaining);
            persistDraft();
            m_status->setText(QString("Not inserted: %1").arg(error));
            return;
        }
        ++m_insertedCount;
        remaining.removeFirst();
        setRows(remaining.isEmpty() ? QJsonArray{QJsonObject{}} : remaining);
        if (remaining.isEmpty())
            clearStoredDraft();
        else
            persistDraft();
    }
    m_status->setText(QString("Inserted %1 item(s).").arg(m_insertedCount));
    accept();
}
