#pragma once

#include <QString>
#include <QSharedPointer>
#include "html.h"

class MarkdownConverter {
public:
    // Wiki links become links with this scheme; the rest of the URL is the
    // percent-encoded "Title [disambiguation]".
    static constexpr const char* kItemScheme = "lexicon-item";

    static QString toHtml(const QString& markdown);

private:
    class CustomHtmlVisitor : public MD::details::HtmlVisitor {
    public:
        QString toHtml(QSharedPointer<MD::Document> doc,
                      const QString &footnoteBackLinkContent,
                      bool wrappedInArticle = true,
                      const MD::details::IdsMap *idsMap = nullptr) override;

    protected:
        void onCode(MD::Code *code) override;
    };
};
