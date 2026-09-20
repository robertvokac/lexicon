#include "BlobStore.h"

#include "DatabaseManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>

namespace {
bool fail(QString* errorMessage, const QString& message) {
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

QString blobsDirectory() {
    return QFileInfo(DatabaseManager::database().databaseName()).dir().filePath("blobs");
}
}

QString BlobStore::blobPath(const QString& hash) {
    if (!QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch()) {
        return {};
    }
    return QDir(blobsDirectory()).filePath(hash.left(2) + "/" + hash.mid(2));
}

QString BlobStore::importFile(const QString& sourcePath, QString* errorMessage) {
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        fail(errorMessage, "Cannot read file: " + source.errorString());
        return {};
    }
    QDir root(blobsDirectory());
    if (!root.mkpath(".")) {
        fail(errorMessage, "Cannot create blobs directory.");
        return {};
    }
    QTemporaryFile temporary(root.filePath(".import-XXXXXX"));
    if (!temporary.open()) {
        fail(errorMessage, "Cannot create temporary blob file.");
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            fail(errorMessage, "Cannot read file: " + source.errorString());
            return {};
        }
        digest.addData(chunk);
        if (temporary.write(chunk) != chunk.size()) {
            fail(errorMessage, "Cannot write blob: " + temporary.errorString());
            return {};
        }
    }
    if (!temporary.flush()) {
        fail(errorMessage, "Cannot finish writing blob: " + temporary.errorString());
        return {};
    }
    const QString hash = QString::fromLatin1(digest.result().toHex());
    const QString destination = blobPath(hash);
    if (!root.mkpath(hash.left(2))) {
        fail(errorMessage, "Cannot create blob subdirectory.");
        return {};
    }
    if (QFile::exists(destination)) {
        return hash;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.setAutoRemove(false);
    temporary.close();
    if (!QFile::rename(temporaryPath, destination)) {
        QFile::remove(temporaryPath);
        if (!QFile::exists(destination)) {
            fail(errorMessage, "Cannot move blob into place.");
            return {};
        }
    }
    return hash;
}

bool BlobStore::exportFile(const QString& hash, const QString& destinationPath, QString* errorMessage) {
    const QString sourcePath = blobPath(hash);
    if (sourcePath.isEmpty()) {
        return fail(errorMessage, "Invalid blob identifier.");
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return fail(errorMessage, "Cannot read blob: " + source.errorString());
    }
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        return fail(errorMessage, "Cannot create output file: " + destination.errorString());
    }
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if ((chunk.isEmpty() && source.error() != QFile::NoError) || destination.write(chunk) != chunk.size()) {
            return fail(errorMessage, "Cannot copy blob to output file.");
        }
    }
    if (!destination.commit()) {
        return fail(errorMessage, "Cannot finish output file: " + destination.errorString());
    }
    return true;
}
