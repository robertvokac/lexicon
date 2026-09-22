// The desktop dialogs added after the original client, driven offscreen
// against a real database: each check clicks what a person would click.
#include "AlarmsDialog.h"
#include "ApplicationContext.h"
#include "GraphDialog.h"
#include "GraphLayout.h"
#include "InboxDialog.h"
#include "ItemEditDialog.h"
#include "MarkdownConverter.h"
#include "ReviewDialog.h"
#include "SqliteRepository.h"

#include <QApplication>
#include <QDateTimeEdit>
#include <QTimeZone>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>

#include <chrono>
#include <cmath>
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
// With LEXICON_SMOKE_SHOTS=<directory>, each dialog is also saved as a PNG
// there, for a person to look at.
void shot(QWidget &widget, const char *name) {
  const QByteArray directory = qgetenv("LEXICON_SMOKE_SHOTS");
  if (directory.isEmpty()) return;
  widget.resize(widget.size().expandedTo(QSize(900, 650)));
  QApplication::processEvents();
  widget.grab().save(QString::fromUtf8(directory) + "/" + name + ".png");
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
  shot(dialog, "review");
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

void checkGraph(lexicon::LexiconApplication &application, int group) {
  const auto create = [&](const std::string &title) {
    lexicon::ItemRecord item;
    item.groupId = group;
    item.title = title;
    return application.items.createItem(item).value_or(-1);
  };
  const int ring = create("Ring"), field = create("Field"), abelian = create("Abelian group");
  const int module = create("Module");
  const auto link = [&](int from, int to) {
    check(application.links.saveLink({-1, from, to, lexicon::LinkType::IsA, 0, "", "", ""}).has_value(), "link");
  };
  link(field, ring);
  link(ring, abelian);
  link(module, abelian);
  GraphDialog dialog(ring);
  auto *summary = child<QLabel>(dialog, "graphSummary");
  check(dialog.nodeCount() == 4, "depth 2 around Ring reaches Module through Abelian group, got " +
                                     std::to_string(dialog.nodeCount()));
  if (summary) check(summary->text().startsWith("4 item(s), 3 link(s)"), "the summary counts, got " + summary->text().toStdString());
  dialog.show();
  shot(dialog, "graph");
  dialog.centreOn(module);
  check(dialog.nodeCount() == 3 && dialog.centreItemId() == module,
        "centring on Module reloads around it, where Field is three links away");
  dialog.openItem(module);
  check(dialog.openedItemId() == module && dialog.result() == QDialog::Accepted, "opening an item closes the dialog with it");

  // The layout keeps every item apart and the centre in the middle.
  const std::vector<int> depths{0, 1, 1, 1, 2, 2, 2, 2, 2};
  const std::vector<std::pair<int, int>> edges{{0, 1}, {0, 2}, {0, 3}, {1, 4}, {1, 5}, {2, 6}, {3, 7}, {3, 8}};
  const auto points = graphlayout::layout(depths, edges);
  check(points[0].x == 0 && points[0].y == 0, "the centre stays at the origin");
  double closest = 1e9;
  for (std::size_t i = 0; i < points.size(); ++i)
    for (std::size_t j = i + 1; j < points.size(); ++j)
      closest = std::min(closest, std::hypot(points[i].x - points[j].x, points[i].y - points[j].y));
  check(closest > 60, "no two items overlap, closest " + std::to_string(closest));
  check(graphlayout::layout(depths, edges)[5].x == points[5].x, "the layout is the same every time");
}

void checkInbox(lexicon::LexiconApplication &application) {
  InboxDialog dialog;
  dialog.show();
  auto *title = child<QLineEdit>(dialog, "inboxTitle");
  auto *content = child<QPlainTextEdit>(dialog, "inboxContent");
  auto *save = child<QPushButton>(dialog, "inboxSave");
  auto *error = child<QLabel>(dialog, "inboxError");
  if (!title || !content || !save || !error) return;
  save->click();
  check(dialog.isVisible() && error->text() == "Enter a title.", "a title is required");
  title->setText("Lock-free queue");
  content->setPlainText("Try a ring buffer.\nMeasure it first.");
  shot(dialog, "inbox");
  save->click();
  check(dialog.result() == QDialog::Accepted && dialog.savedItemId() > 0, "the idea is saved");
  const auto item = application.items.loadItem(dialog.savedItemId());
  check(item && item->groupName == "Default" && item->itemTypeId <= 0 &&
            item->content == "Try a ring buffer.\nMeasure it first.",
        "in Default, without a type, with the text as typed");
  InboxDialog twin;
  child<QLineEdit>(twin, "inboxTitle")->setText("Lock-free queue");
  child<QPushButton>(twin, "inboxSave")->click();
  check(twin.result() != QDialog::Accepted &&
            child<QLabel>(twin, "inboxError")->text().contains("already exists"),
        "a title already in Default is refused with the reason");
}
void checkAlarms(lexicon::LexiconApplication &application) {
  AlarmEditDialog editor(lexicon::AlarmRecord{});
  editor.show();
  auto *title = child<QLineEdit>(editor, "alarmTitle");
  auto *when = child<QDateTimeEdit>(editor, "alarmFiresAt");
  auto *description = child<QPlainTextEdit>(editor, "alarmDescription");
  auto *save = child<QPushButton>(editor, "alarmSave");
  auto *error = child<QLabel>(editor, "alarmError");
  if (!title || !when || !description || !save || !error) return;
  check(when->dateTime() > QDateTime::currentDateTime(), "a new alarm starts in the future");
  save->click();
  check(editor.isVisible() && error->text() == "Enter a title.", "an alarm needs a title");
  title->setText("Renew the passport");
  description->setPlainText("Photos first.");
  // Shown in local time, stored in UTC.
  when->setDateTime(QDateTime(QDate(2030, 1, 2), QTime(9, 15), QTimeZone::UTC).toLocalTime());
  shot(editor, "alarm-edit");
  save->click();
  check(editor.result() == QDialog::Accepted && editor.alarm().id > 0, "the alarm is saved");
  check(editor.alarm().firesAt == "2030-01-02T09:15:00Z", "at the chosen moment, in UTC");
  check(alarmtime::fromUtcText(editor.alarm().firesAt) == when->dateTime(), "which reads back as shown");

  lexicon::AlarmRecord past{-1, "Old call", "", "2001-05-06T07:08:00Z"};
  check(application.alarms.saveAlarm(past).has_value(), "an alarm in the past can be kept");
  AlarmsDialog list;
  list.show();
  auto *table = child<QTableWidget>(list, "alarmTable");
  auto *summary = child<QLabel>(list, "alarmSummary");
  if (!table || !summary) return;
  check(list.alarmCount() == 2 && table->rowCount() == 2, "the list shows every alarm");
  check(table->item(0, 1)->text() == "Old call" && table->item(1, 1)->text() == "Renew the passport",
        "the soonest first");
  check(table->item(1, 2)->text() == "Photos first.", "with its description");
  check(table->item(0, 0)->toolTip() == "Already gone off", "one already gone off is marked");
  check(summary->text() == "2 alarm(s), 1 still to go off", "the summary counts what is still to come");
  shot(list, "alarms");
  list.selectAlarm(editor.alarm().id);
  list.deleteSelected(false);
  check(list.alarmCount() == 1 && table->rowCount() == 1 && table->item(0, 1)->text() == "Old call",
        "a deleted alarm leaves the list");
  check(!application.alarms.loadAlarm(editor.alarm().id).has_value(), "and the database");
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
  checkGraph(application, group);
  checkInbox(application);
  checkAlarms(application);

  if (failures == 0) std::cout << "desktop_gui_smoke: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
