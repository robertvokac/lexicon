#include "DatabaseManager.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("OpenAI");
    QCoreApplication::setApplicationName("Lexicon");

    const QString dbPath = QDir(QCoreApplication::applicationDirPath()).filePath("lexicon.db");
    QString error;
    if (!DatabaseManager::initialize(dbPath, &error)) {
        QMessageBox::critical(nullptr, "Lexicon", error);
        return 1;
    }

    MainWindow window;
    window.show();
    return app.exec();
}
