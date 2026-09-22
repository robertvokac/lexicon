#include "MarkdownConverter.h"
#include "WikiLinks.h"
#include "parser.h"
#include "doc.h"
#include <QTextStream>

QString MarkdownConverter::toHtml(const QString& markdown) {
    if (markdown.isEmpty()) return QString();

    const QByteArray utf8 = markdown.toUtf8();
    QString linked = QString::fromStdString(lexicon::wikiLinksToMarkdown(
        std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())),
        std::string(kItemScheme) + ":"));
    MD::Parser parser;
    QTextStream stream(&linked);
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
