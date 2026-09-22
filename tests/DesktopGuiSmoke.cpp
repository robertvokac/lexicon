// The desktop dialogs added after the original client, driven offscreen
// against a real database: each check clicks what a person would click.
#include "ApplicationContext.h"
#include "ItemEditDialog.h"
#include "MarkdownConverter.h"
#include "ReviewDialog.h"
#include "SqliteRepository.h"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <iostream>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
template <class T> T *child(QWidget &parent, const char *name) {
  auto *found = parent.findChild<T *>(name);
  check(found != nullptr, std::string("the dialog has ") + name);
  return found;
}

void checkReview(lexicon::LexiconApplication &application, int group) {
  for (const char *title : {"Functor", "Monad"}) {
    lexicon::ItemRecord item;
    item.groupId = group;
    item.title = title;
    item.content = std::string("# ") + title + "\n\nA structure.";
    check(application.items.createItem(item).has_value(), std::string("create ") + title);
  }
  ReviewDialog dialog;
  dialog.show();
  auto *title = child<QLabel>(dialog, "reviewTitle");
  auto *show = child<QPushButton>(dialog, "reviewShow");
  auto *good = child<QPushButton>(dialog, "reviewGood");
  auto *again = child<QPushButton>(dialog, "reviewAgain");
  auto *content = child<QTextBrowser>(dialog, "reviewContent");
  auto *done = child<QLabel>(dialog, "reviewDone");
  if (!title || !show || !good || !again || !content || !done) return;
  check(title->text() == "Functor", "the first due item is shown, got " + title->text().toStdString());
  check(!good->isEnabled() && content->isHidden(), "the answer waits to be asked for");
  check(good->text().contains("2 days"), "Good says when the item comes back, got " + good->text().toStdString());
  show->click();
  check(content->toPlainText().contains("A structure."), "the content is shown on request");
  good->click();
  check(title->text() == "Monad", "the next item follows");
  show->click();
  again->click();
  check(title->text() == "Monad", "a forgotten item comes back in the same sitting");
  show->click();
  good->click();
  check(done->text().contains("3 reviewed"), "the sitting ends with a summary, got " + done->text().toStdString());
  check(dialog.changedItems(), "the item list is told to refresh");
  auto functor = application.items.loadItem(*application.search.findItemId("Functor"));
  check(functor && functor->understanding == lexicon::UnderstandingLevel::Recognized && !functor->reviewedAt.empty(),
        "the rating is stored");
}

void checkWikiLinks(lexicon::LexiconApplication &application, int group) {
  const auto html = MarkdownConverter::toHtml("See [[Semigroup]] and `[[code]]`.");
  check(html.contains("href=\"lexicon-item:Semigroup\""), "a wiki link renders as an item link");
  check(html.contains("[[code]]"), "code keeps its brackets");

  lexicon::ItemRecord semigroup;
  semigroup.groupId = group;
  semigroup.title = "Semigroup";
  check(application.items.createItem(semigroup).has_value(), "create Semigroup");
  lexicon::ItemRecord monoid;
  monoid.groupId = group;
  monoid.title = "Monoid";
  monoid.content = "A [[semigroup]] with a unit; not a [[Ghost]].";
  const auto id = application.items.createItem(monoid);
  check(id.has_value(), "create Monoid");
  ItemRecord loaded;
  services().items.loadItem(*id, loaded);

  ItemEditDialog dialog;
  dialog.setGroups(services().groups.loadGroups());
  dialog.setItem(loaded);
  auto *button = child<QPushButton>(dialog, "linksFromContent");
  auto *links = child<QListWidget>(dialog, "outgoingLinks");
  if (!button || !links) return;
  QString shown;
  QTimer::singleShot(0, [&] {
    if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      shown = message->text();
      message->accept();
    }
  });
  button->click();
  check(links->count() == 1 && links->item(0)->text().contains("Semigroup"),
        "a Related link to the named item is added");
  check(shown.contains("1 link(s) added") && shown.contains("Ghost"),
        "the missing item is named, got " + shown.toStdString());
}
} // namespace

int main(int argc, char **argv) {
  QApplication qt(argc, argv);
  const auto directory = fs::temp_directory_path() /
      ("lexicon-desktop-gui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(directory);
  struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{directory};
  SqliteRepository repository;
  if (!repository.open((directory / "lexicon.db").string())) return 1;
  lexicon::LexiconApplication application(repository);
  QtApplicationFacade facade(application);
  installApplication(facade);
  const int group = application.groups.defaultGroupId().value_or(-1);

  checkReview(application, group);
  checkWikiLinks(application, group);

  if (failures == 0) std::cout << "desktop_gui_smoke: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
