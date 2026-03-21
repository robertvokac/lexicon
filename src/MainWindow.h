#pragma once

#include "DatabaseManager.h"

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

class QComboBox;
class QCompleter;
class QLineEdit;
class QStandardItemModel;
class QTableView;
class QTextEdit;
class QTextBrowser;

#include <QSyntaxHighlighter>
#include <QRegularExpression>

class CodeHighlighter : public QSyntaxHighlighter {
public:
    explicit CodeHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
        HighlightingRule rule;
        
        bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;

        // C++ Highlighting
        keywordFormat.setForeground(isDark ? QColor(86, 156, 214) : Qt::darkBlue);
        keywordFormat.setFontWeight(QFont::Bold);
        QStringList keywordPatterns = {
            "\\bchar\\b", "\\bclass\\b", "\\bconst\\b", "\\bdouble\\b", "\\benum\\b", "\\bexplicit\\b",
            "\\bfriend\\b", "\\binline\\b", "\\bint\\b", "\\blong\\b", "\\bnamespace\\b", "\\boperator\\b",
            "\\bprivate\\b", "\\bprotected\\b", "\\bpublic\\b", "\\bshort\\b", "\\bsignals\\b", "\\bsigned\\b",
            "\\bslots\\b", "\\bstatic\\b", "\\bstruct\\b", "\\btemplate\\b", "\\btypedef\\b", "\\btypename\\b",
            "\\bunion\\b", "\\bunsigned\\b", "\\bvirtual\\b", "\\bvoid\\b", "\\bvolatile\\b", "\\bbool\\b",
            "\\bif\\b", "\\belse\\b", "\\bfor\\b", "\\bwhile\\b", "\\breturn\\b", "\\bswitch\\b", "\\bcase\\b",
            "\\bdefault\\b", "\\bbreak\\b", "\\bcontinue\\b", "\\bgoto\\b", "\\btrue\\b", "\\bfalse\\b",
            "\\bnullptr\\b", "\\busing\\b", "\\bnamespace\\b", "\\btry\\b", "\\bcatch\\b", "\\bthrow\\b"
        };
        for (const QString& pattern : keywordPatterns) {
            rule.pattern = QRegularExpression(pattern);
            rule.format = keywordFormat;
            cppRules.append(rule);
        }

        stringFormat.setForeground(isDark ? QColor(206, 145, 120) : QColor(165, 42, 42));
        rule.pattern = QRegularExpression("\".*\"");
        rule.format = stringFormat;
        cppRules.append(rule);

        singleLineCommentFormat.setForeground(isDark ? QColor(106, 153, 85) : Qt::darkGreen);
        rule.pattern = QRegularExpression("//[^\n]*");
        rule.format = singleLineCommentFormat;
        cppRules.append(rule);

        multiLineCommentFormat.setForeground(isDark ? QColor(106, 153, 85) : Qt::darkGreen);
        commentStartExpression = QRegularExpression("/\\*");
        commentEndExpression = QRegularExpression("\\*/");
    }

protected:
    void highlightBlock(const QString& text) override {
        QTextBlock block = currentBlock();
        QTextCharFormat blockFormat = block.charFormat();
        
        // Only highlight blocks that are marked as 'cpp' syntax via our custom HTML attribute
        // or blocks that have fixed pitch font (which we set in CSS for all pre)
        bool isCode = blockFormat.fontFixedPitch();
        if (!isCode) return;

        // Check if it's specifically C++
        bool isCpp = blockFormat.property(QTextFormat::UserProperty + 1).toString() == "cpp";
        
        // Force monospace
        QTextCharFormat format = blockFormat;
        format.setFontFamilies({ "Courier New", "monospace" });
        setFormat(0, text.length(), format);

        if (!isCpp || text.trimmed().isEmpty()) return;

        for (const HighlightingRule& rule : cppRules) {
            QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                QRegularExpressionMatch match = matchIterator.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        setCurrentBlockState(0);
        int startIndex = 0;
        if (previousBlockState() != 1)
            startIndex = text.indexOf(commentStartExpression);

        while (startIndex >= 0) {
            QRegularExpressionMatch match = commentEndExpression.match(text, startIndex);
            int endIndex = match.capturedStart();
            int commentLength;
            if (endIndex == -1) {
                setCurrentBlockState(1);
                commentLength = text.length() - startIndex;
            } else {
                commentLength = endIndex - startIndex + match.capturedLength();
            }
            setFormat(startIndex, commentLength, multiLineCommentFormat);
            startIndex = text.indexOf(commentStartExpression, startIndex + commentLength);
        }
    }

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> cppRules;

    QRegularExpression commentStartExpression;
    QRegularExpression commentEndExpression;

    QTextCharFormat keywordFormat;
    QTextCharFormat singleLineCommentFormat;
    QTextCharFormat multiLineCommentFormat;
    QTextCharFormat stringFormat;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void setLightTheme();
    void setDarkTheme();

    void refreshAll();
    void refreshMaps();
    void refreshTags();
    void refreshFlags();
    void refreshTerms();
    void resetPaginationAndRefresh();
    void firstPage();
    void prevPage();
    void nextPage();
    void lastPage();
    void refreshSuggestions();

    void addTerm();
    void quickAdd();
    void editSelectedTerm();
    void deleteSelectedTerm();

    void openMapManager();
    void showTagsOverview();
    void showFlagsOverview();
    void showAliasesOverview();

    void updateActions();
    void showTermContent(const QModelIndex& index);
    void onLinkActivated(const QUrl& link);

private:
    void applySavedTheme();
    void applyTheme(const QString& themeName);
    void saveSettings();
    void loadSettings();

    void updateMarkdownStyles();
    void updateLinksDisplay(int termId);
    void setupUi();
    void setupMenus();
    int selectedTermId() const;
    QList<MapRecord> maps() const;
    void showError(const QString& message);

    QComboBox* m_mapFilter = nullptr;
    QComboBox* m_tagFilter = nullptr;
    QComboBox* m_flagFilter = nullptr;
    QComboBox* m_statusFilter = nullptr;
    QComboBox* m_understandingFilter = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTableView* m_tableView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QCompleter* m_completer = nullptr;

    QPushButton* m_firstButton = nullptr;
    QPushButton* m_prevButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QPushButton* m_lastButton = nullptr;
    QLabel* m_pageLabel = nullptr;
    QComboBox* m_pageSizeCombo = nullptr;
    QTextEdit* m_termContentView = nullptr;
    QTextBrowser* m_linksView = nullptr;
    CodeHighlighter* m_highlighter = nullptr;
    CodeHighlighter* m_linksHighlighter = nullptr;

    int m_currentPage = 0;
    int m_pageSize = 20;

    QList<MapRecord> m_maps;
};
