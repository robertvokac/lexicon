#pragma once

#include <QString>
#include <QSharedPointer>
#include "html.h"

class MarkdownConverter {
public:
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
