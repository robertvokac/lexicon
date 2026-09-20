#pragma once

#include "DatabaseManager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;
class QToolBar;
class QAction;
class QTabWidget;
class QLabel;
class QTimer;

#include "MainWindow.h" // Reuse CodeHighlighter

class ItemEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit ItemEditDialog(QWidget* parent = nullptr);

    void setGroups(const QList<GroupRecord>& groups);
    void setItem(const ItemRecord& item);
    ItemRecord item() const;

private slots:
    void addAlias();
    void editAlias();
    void removeAlias();

    void addTag();
    void editTag();
    void removeTag();

    void addFlag();
    void editFlag();
    void removeFlag();

    void formatBold();
    void formatItalic();
    void formatLink();
    void formatTable();
    void formatList();
    void formatOrderedList();
    void formatH2();
    void formatH3();
    void formatH4();
    void formatQuote();
    void formatCode();
    void formatCodeBlock();
    void formatHorizontalLine();
    void updatePreview();

    void addLink();
    void editLink();
    void removeLink();
    void addBacklink();
    void editBacklink();
    void removeBacklink();

    void validateAndAccept();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void updateMarkdownStyles();
    void updateLinksList();
    void setupUi();
    void connectSignals();
    QString getInputValue(const QString& title, const QString& label, const QString& initialValue, const QStringList& suggestions);
    void addValue(QListWidget* list, const QString& title, const QStringList& suggestions = QStringList());
    void editValue(QListWidget* list, const QString& title, const QStringList& suggestions = QStringList());
    void removeValue(QListWidget* list, const QString& title);
    void insertMarkdown(const QString& prefix, const QString& suffix = QString(), const QString& defaultText = QString());
    static QStringList valuesFromList(QListWidget* list);
    static void setListValues(QListWidget* list, const QStringList& values);

    int m_itemId = -1;

    QComboBox* m_groupCombo = nullptr;
    QComboBox* m_statusCombo = nullptr;
    QComboBox* m_understandingCombo = nullptr;
    QCheckBox* m_pinnedCheck = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QLineEdit* m_disambiguationEdit = nullptr;
    QTextEdit* m_contentEdit = nullptr;
    QTextEdit* m_previewEdit = nullptr;
    QTimer* m_previewTimer = nullptr;
    CodeHighlighter* m_highlighter = nullptr;
    QToolBar* m_contentToolbar = nullptr;
    QTabWidget* m_tabWidget = nullptr;

    QListWidget* m_aliasList = nullptr;
    QListWidget* m_tagList = nullptr;
    QListWidget* m_flagList = nullptr;

    QListWidget* m_linksList = nullptr;
    QListWidget* m_backlinksList = nullptr;
    QList<LinkRecord> m_currentLinks;
    QList<LinkRecord> m_currentBacklinks;

    QPushButton* m_saveButton = nullptr;
};
