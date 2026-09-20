#include "ApplicationContext.h"
#include "SqliteRepository.h"
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
    SqliteRepository repository;
    if (auto opened = repository.open(qtbridge::toCore(dbPath)); !opened) {
        QMessageBox::critical(nullptr, "Lexicon", qtbridge::toQt(opened.error().message));
        return 1;
    }

    lexicon::LexiconApplication application(repository);
    QtApplicationFacade desktop(application);
    installApplication(desktop);

    MainWindow window;
    window.show();
    return app.exec();
}
