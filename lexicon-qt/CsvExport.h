#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace csvexport {

inline QString quoted(QString value) {
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

inline QByteArray encode(const QStringList &headers, const QList<QStringList> &rows) {
    QString text;
    auto append = [&text](const QStringList &cells) {
        QStringList escaped;
        escaped.reserve(cells.size());
        for (const auto &cell : cells) escaped.push_back(quoted(cell));
        text += escaped.join(',') + "\r\n";
    };
    append(headers);
    for (const auto &row : rows) append(row);
    return QByteArray::fromHex("efbbbf") + text.toUtf8();
}

} // namespace csvexport
