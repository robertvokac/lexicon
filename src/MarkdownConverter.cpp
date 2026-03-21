#include "MarkdownConverter.h"
#include "parser.h"
#include "doc.h"
#include <QTextStream>

QString MarkdownConverter::toHtml(const QString& markdown) {
    if (markdown.isEmpty()) return QString();

    MD::Parser parser;
    QTextStream stream(const_cast<QString*>(&markdown));
    auto doc = parser.parse(stream, QString(), QString());

    CustomHtmlVisitor visitor;
    return visitor.toHtml(doc, QString(), true);
}

QString MarkdownConverter::CustomHtmlVisitor::toHtml(QSharedPointer<MD::Document> doc,
                                                  const QString &footnoteBackLinkContent,
                                                  bool wrappedInArticle,
                                                  const MD::details::IdsMap *idsMap) {
    return MD::details::HtmlVisitor::toHtml(doc, footnoteBackLinkContent, wrappedInArticle, idsMap);
}

void MarkdownConverter::CustomHtmlVisitor::onCode(MD::Code *code) {
    if (code->isInline()) {
        m_html += QStringLiteral("<code>");
        m_html += prepareTextForHtml(code->text());
        m_html += QStringLiteral("</code>");
    } else {
        // We use pre[syntax="..."] because Qt's QTextDocument 
        // has limited support for nested CSS classes like .language-cpp
        QString syntax = code->syntax().toLower();
        m_html += QStringLiteral("<pre");
        if (!syntax.isEmpty()) {
             m_html += QStringLiteral(" syntax=\"") + prepareTextForHtml(syntax) + QStringLiteral("\"");
        }
        m_html += QStringLiteral(">");
        m_html += prepareTextForHtml(code->text());
        m_html += QStringLiteral("</pre>\n");
    }
}
