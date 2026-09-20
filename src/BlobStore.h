#pragma once

#include <QString>

class BlobStore {
public:
    static QString importFile(const QString& sourcePath, QString* errorMessage = nullptr);
    static bool exportFile(const QString& hash, const QString& destinationPath, QString* errorMessage = nullptr);

private:
    static QString blobPath(const QString& hash);
};
