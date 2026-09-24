// The desktop dialogs added after the original client, driven offscreen
// against a real database: each check clicks what a person would click.
#include "AlarmNotifier.h"
#include "AlarmsDialog.h"
#include "ApplicationContext.h"
#include "CardQuizDialog.h"
#include "CardsDialog.h"
#include "GraphDialog.h"
#include "GraphLayout.h"
#include "ImageValueView.h"
#include "InboxDialog.h"
#include "ItemEditDialog.h"
#include "MarkdownConverter.h"
#include "ReviewDialog.h"
#include "SqliteRepository.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>
#include <QDateTimeEdit>
#include <QDialog>
#include <QTimeZone>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QtTest/QTest>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>

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
  // Zoom in and out with the buttons, back to the fit, and the whole screen.
  auto *zoomIn = child<QPushButton>(dialog, "graphZoomIn");
  auto *zoomOut = child<QPushButton>(dialog, "graphZoomOut");
  auto *fitButton = child<QPushButton>(dialog, "graphFit");
  auto *fullScreen = child<QPushButton>(dialog, "graphFullScreen");
  if (zoomIn && zoomOut && fitButton && fullScreen) {
    const double fitted = dialog.zoom();
    zoomIn->click();
    zoomIn->click();
    check(std::abs(dialog.zoom() - fitted * 1.5625) < 1e-9, "the + button zooms in");
    zoomOut->click();
    check(std::abs(dialog.zoom() - fitted * 1.25) < 1e-9, "the - button zooms out");
    fitButton->click();
    check(std::abs(dialog.zoom() - fitted) < 1e-9, "Fit shows the whole graph again");
    fullScreen->click();
    QApplication::processEvents();
    check(dialog.isFullScreen() && fullScreen->text() == "Exit full screen", "Full screen gives the graph the screen");
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QApplication::processEvents();
    check(!dialog.isFullScreen() && dialog.isVisible(), "Escape leaves full screen and keeps the dialog open");
  }
  // Where a node is on screen, for the clicks the hint promises.
  auto *view = dialog.findChild<QGraphicsView *>();
  const auto nodeCentre = [&](int itemId) {
    QPoint where(-1, -1);
    if (!view) return where;
    for (auto *item : view->scene()->items())
      if (item->data(0).toInt() == itemId) where = view->mapFromScene(item->sceneBoundingRect().center());
    return where;
  };
  check(nodeCentre(abelian) != QPoint(-1, -1), "the graph draws a node for Abelian group");
  QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::KeyboardModifiers(), nodeCentre(abelian));
  QApplication::processEvents();
  check(dialog.centreItemId() == abelian, "a click on a node centres the graph on it");

  dialog.centreOn(module);
  check(dialog.nodeCount() == 3 && dialog.centreItemId() == module,
        "centring on Module reloads around it, where Field is three links away");
  QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::KeyboardModifiers(), nodeCentre(abelian));
  QApplication::processEvents();
  check(dialog.openedItemId() == abelian && dialog.result() == QDialog::Accepted,
        "a double click on a node opens that item and closes the dialog");

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

// Answers the modal dialog the next exec() opens, with [what] run on it.
template <class Dialog> void whenOpened(std::function<void(Dialog &)> what) {
  QTimer::singleShot(0, [what] {
    if (auto *dialog = qobject_cast<Dialog *>(QApplication::activeModalWidget())) what(*dialog);
    else check(false, "the expected dialog opened");
  });
}

void checkCards(lexicon::LexiconApplication &application, int group) {
  const auto create = [&](const std::string &title) {
    lexicon::ItemRecord item;
    item.groupId = group;
    item.title = title;
    return application.items.createItem(item).value_or(-1);
  };
  const int provenance = create("pointer provenance");
  const int compiler = create("compiler optimization");
  const int empty = create("Without cards");
  check(application.links.saveLink({-1, provenance, compiler, lexicon::LinkType::Uses, 0, "", "", ""}).has_value(),
        "link the two items");
  check(application.cards.createCard(compiler, "What may an optimizer assume?", "What provenance allows.").has_value(),
        "a card on the neighbour");
  const auto before = application.items.loadItem(provenance);

  // The manager: add, with a refusal first, then edit.
  CardsDialog cards(provenance);
  cards.show();
  auto *title = child<QLabel>(cards, "cardsItemTitle");
  auto *table = child<QTableWidget>(cards, "cardsTable");
  if (!title || !table) return;
  check(title->text() == "pointer provenance" && cards.cardCount() == 0, "the manager names its item and starts empty");
  bool refused = false;
  whenOpened<CardEditDialog>([&](CardEditDialog &editor) {
    auto *question = child<QPlainTextEdit>(editor, "cardQuestion");
    auto *answer = child<QPlainTextEdit>(editor, "cardAnswer");
    auto *save = child<QPushButton>(editor, "cardSave");
    auto *error = child<QLabel>(editor, "cardError");
    if (!question || !answer || !save || !error) return editor.reject();
    check(!editor.findChild<QLabel *>("cardStatistics"), "a new card has no statistics to show");
    save->click();
    refused = editor.isVisible() && error->text() == "Enter a question.";
    question->setPlainText("Co znamená řetězec?\nstd::uint64_t");
    answer->setPlainText("Příliš žluťoučký kůň\n指针");
    shot(editor, "card-edit");
    save->click();
  });
  cards.addCard();
  check(refused, "a card needs a question");
  check(cards.cardCount() == 1 && table->rowCount() == 1, "the card is added");
  const auto stored = application.cards.loadCards(provenance);
  check(stored && stored->size() == 1 && stored->front().answer == "Příliš žluťoučký kůň\n指针",
        "and stored with its lines and UTF-8");
  check(table->item(0, 2)->text() == "0" && table->item(0, 4)->text() == "Never", "with no answers yet");
  application.cards.createCard(provenance, "What does pointer provenance describe?", "Where a pointer came from.");
  whenOpened<CardEditDialog>([&](CardEditDialog &editor) {
    auto *question = child<QPlainTextEdit>(editor, "cardQuestion");
    check(question && question->toPlainText() == "Co znamená řetězec?\nstd::uint64_t", "the editor shows the card");
    check(editor.findChild<QLabel *>("cardStatistics") != nullptr, "and its statistics, read only");
    if (question) question->setPlainText("Co je řetězec?");
    child<QPushButton>(editor, "cardSave")->click();
  });
  table->selectRow(0);
  cards.editCard();
  check(application.cards.loadCards(provenance)->front().question == "Co je řetězec?", "the card is edited");
  shot(cards, "cards");

  // The quiz over this item: the answer on request, then Yes or No.
  CardQuizDialog quiz(provenance, 0);
  quiz.show();
  quiz.activateWindow();
  const bool active = QTest::qWaitForWindowActive(&quiz);
  auto *progress = child<QLabel>(quiz, "quizProgress");
  auto *source = child<QLabel>(quiz, "quizSource");
  auto *question = child<QLabel>(quiz, "quizQuestion");
  auto *answer = child<QLabel>(quiz, "quizAnswer");
  auto *show = child<QPushButton>(quiz, "quizShow");
  auto *yes = child<QPushButton>(quiz, "quizYes");
  auto *no = child<QPushButton>(quiz, "quizNo");
  auto *message = child<QLabel>(quiz, "quizMessage");
  if (!progress || !source || !question || !answer || !show || !yes || !no || !message) return;
  check(quiz.cardCount() == 2 && progress->text() == "1 / 2", "the quiz counts its cards, got " +
                                                                  progress->text().toStdString());
  check(source->text() == "Item: pointer provenance" && question->text() == "Co je řetězec?",
        "the first card's question and item are shown");
  check(!answer->isVisible() && !yes->isVisible(), "the answer is hidden at first");
  show->click();
  check(answer->isVisible() && answer->text() == "Příliš žluťoučký kůň\n指针" && yes->isEnabled(),
        "Show answer shows it, with Yes and No");
  shot(quiz, "card-quiz");
  check(application.cards.loadCards(provenance)->front().lastAttempt.empty(), "seeing the answer records nothing");
  yes->click();
  const auto first = application.cards.loadCards(provenance)->front();
  check(first.successCount == 1 && first.failureCount == 0 && !first.lastAttempt.empty(), "Yes records a success");
  check(progress->text() == "2 / 2" && !answer->isVisible(), "the next card follows, its answer hidden");
  if (active) {
    // The keys: Space shows the answer, N answers No.
    QTest::keyClick(&quiz, Qt::Key_Space);
    check(answer->isVisible(), "Space shows the answer");
    QTest::keyClick(&quiz, Qt::Key_N);
  } else {
    check(false, "the quiz window became active for the keyboard");
    show->click();
    no->click();
  }
  const auto second = application.cards.loadCards(provenance)->at(1);
  check(second.failureCount == 1 && second.successCount == 0 && !second.lastAttempt.empty(), "No records a failure");
  check(message->isVisible() && message->text() == "Cards: 2\nYes: 1\nNo: 1",
        "the sitting ends with a summary, got " + message->text().toStdString());
  if (active) {
    QTest::keyClick(&quiz, Qt::Key_Y);
    check(application.cards.loadCards(provenance)->front().successCount == 1, "a key after the end answers nothing");
  }
  const auto after = application.items.loadItem(provenance);
  check(before && after && after->understanding == before->understanding && after->reviewedAt.empty() &&
            after->revision == before->revision,
        "the quiz leaves the item's review alone");

  // A neighbourhood, and an item without cards.
  CardQuizDialog around(provenance, 1);
  check(around.cardCount() == 3 && around.depth() == 1, "a neighbourhood quiz adds the neighbour's card");
  CardQuizDialog none(empty, 0);
  none.show();
  check(none.cardCount() == 0 && child<QLabel>(none, "quizMessage")->text() == "No cards are available for this quiz.",
        "an item without cards says so");

  // The graph quizzes around its centre, as deep as it reaches.
  GraphDialog graph(provenance);
  int quizDepth = -1, quizCards = -1;
  whenOpened<CardQuizDialog>([&](CardQuizDialog &opened) {
    quizDepth = opened.depth();
    quizCards = opened.cardCount();
    opened.reject();
  });
  child<QPushButton>(graph, "graphQuizCards")->click();
  check(quizDepth == graph.depth() && quizDepth == 2 && quizCards == 3,
        "Quiz cards uses the graph's centre and depth");

  // Delete, after a confirmation.
  whenOpened<QMessageBox>([](QMessageBox &box) { box.button(QMessageBox::Yes)->click(); });
  table->selectRow(0);
  cards.deleteCard();
  check(cards.cardCount() == 1 && application.cards.loadCards(provenance)->size() == 1, "a card can be deleted");

  // The item editor offers the cards of a stored item only.
  ItemEditDialog newItem;
  newItem.setGroups(services().groups.loadGroups());
  auto *newCards = child<QPushButton>(newItem, "itemCards");
  check(newCards && !newCards->isEnabled() && newCards->toolTip() == "Save the Item before adding Cards.",
        "a new item has no cards yet");
  ItemRecord loaded;
  services().items.loadItem(provenance, loaded);
  ItemEditDialog existing;
  existing.setGroups(services().groups.loadGroups());
  existing.setItem(loaded);
  check(child<QPushButton>(existing, "itemCards")->isEnabled(), "a stored item's cards are one click away");
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
  check(item && item->groupName == "Default" && item->itemTypeName == "Inbox" &&
            item->content == "Try a ring buffer.\nMeasure it first.",
        "in Default, with the type Inbox, with the text as typed");
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
  check(table->item(0, 0)->toolTip() == "Ringing" && table->item(0, 0)->font().bold(),
        "one gone off and not dismissed is marked as ringing");
  check(summary->text() == "2 alarm(s), 1 still to go off", "the summary counts what is still to come");
  shot(list, "alarms");
  list.selectAlarm(editor.alarm().id);
  list.deleteSelected(false);
  check(list.alarmCount() == 1 && table->rowCount() == 1 && table->item(0, 1)->text() == "Old call",
        "a deleted alarm leaves the list");
  check(!application.alarms.loadAlarm(editor.alarm().id).has_value(), "and the database");
}
void checkImages(lexicon::LexiconApplication &application, int group, const fs::path &directory) {
  lexicon::ItemTypeRecord type;
  type.name = "Figure";
  type.groupId = group;
  check(application.types.upsertItemType(type).has_value(), "create the Figure type");
  std::optional<int> typeId;
  if (auto types = application.types.loadItemTypes(group))
    for (const auto &known : *types)
      if (known.name == "Figure") typeId = known.id;
  if (!typeId) return;
  lexicon::ItemFieldRecord field;
  field.itemTypeId = *typeId;
  field.name = "Diagram";
  field.dataType = lexicon::FieldDataType::Image;
  check(application.types.upsertItemField(field).has_value(), "create an Image field");
  const auto fields = application.types.loadItemFields(*typeId);
  if (!fields || fields->empty()) return;
  const int fieldId = fields->front().id;

  // A 400 x 300 picture: blue with a red square.
  QImage drawn(400, 300, QImage::Format_ARGB32);
  drawn.fill(QColor("#1a5fb4"));
  QPainter(&drawn).fillRect(150, 100, 100, 100, QColor("#e01b24"));
  const QString pngPath = QString::fromStdString((directory / "diagram.png").string());
  drawn.save(pngPath, "PNG");
  const QString textPath = QString::fromStdString((directory / "notes.png").string());
  { QFile text(textPath); text.open(QIODevice::WriteOnly); text.write("not an image at all"); }

  QString error;
  check(imagevalues::importFile(textPath, &error).isEmpty() && error == "Choose a PNG, JPEG, GIF, WebP or BMP image.",
        "a file that is no image is refused, whatever its name");
  const QString value = imagevalues::importFile(pngPath, &error);
  check(value.startsWith("image/png:") && value.size() == 10 + 64, "a PNG is stored with its type");
  check(imagevalues::describe(value, imagevalues::load(value)) == "PNG image, 400 × 300", "and is described");
  check(imagevalues::suggestedFileName("Diagram: v2", value) == "Diagram_ v2.png", "Save as suggests a file name");

  lexicon::ItemRecord item;
  item.groupId = group;
  item.itemTypeId = *typeId;
  item.title = "Pipeline";
  item.fieldValues[fieldId] = qtbridge::toCore(value);
  auto itemId = application.items.createItem(item);
  check(itemId.has_value(), "an item keeps the image");
  if (!itemId) return;
  ItemRecord loaded;
  services().items.loadItem(*itemId, loaded);

  ItemEditDialog editor;
  editor.setGroups(services().groups.loadGroups());
  editor.setItem(loaded);
  editor.show();
  auto *thumbnail = child<QLabel>(editor, "imageThumbnail");
  auto *info = child<QLabel>(editor, "imageInfo");
  auto *clear = child<QPushButton>(editor, "imageClear");
  if (!thumbnail || !info || !clear) return;
  check(!thumbnail->pixmap().isNull() && thumbnail->pixmap().width() == 160, "the editor shows a thumbnail");
  check(info->text() == "PNG image, 400 × 300", "and what the image is");
  if (auto *tabs = editor.findChild<QTabWidget *>())
    for (int index = 0; index < tabs->count(); ++index)
      if (tabs->tabText(index) == "Values") tabs->setCurrentIndex(index);
  shot(editor, "image-editor");
  check(editor.item().fieldValues.value(fieldId) == value, "an untouched image is saved as it was");
  clear->click();
  check(thumbnail->pixmap().isNull() && thumbnail->text() == "No image", "Clear removes the picture");
  check(!editor.item().fieldValues.contains(fieldId), "and the value");

  ImageViewDialog viewer(imagevalues::load(value), "Diagram");
  viewer.resize(300, 260);
  viewer.show();
  QApplication::processEvents();
  auto *picture = child<QLabel>(viewer, "imagePicture");
  auto *fit = child<QCheckBox>(viewer, "imageFit");
  if (!picture || !fit) return;
  check(picture->pixmap().width() < 400, "a large image is fitted to the window");
  check(picture->pixmap().width() >= 200, "and fills it rather than showing a thumbnail");
  fit->setChecked(false);
  check(picture->pixmap().size() == QSize(400, 300), "and shown at its own size on request");
  check(!imagevalues::thumbnail(value, 24).isNull() && imagevalues::thumbnail(value, 24).width() == 24,
        "the table gets a small picture");
}
void checkAlarmNotifier(lexicon::LexiconApplication &application) {
  // The earlier checks left an alarm in 2001 ringing.
  if (auto due = application.alarms.loadDueAlarms())
    for (const auto &alarm : *due) application.alarms.dismissAlarm(alarm.id);
  QWidget window;
  AlarmNotifier notifier(&window, 60 * 60 * 1000);
  notifier.check();
  check(!notifier.dialog() || !notifier.dialog()->isVisible(), "nothing rings while no alarm is due");

  auto tea = application.alarms.saveAlarm({-1, "Tea", "Green, two minutes.", "2020-01-01T10:00:00Z", {}});
  auto call = application.alarms.saveAlarm({-1, "Call back", "", "2020-01-01T11:00:00Z", {}});
  auto later = application.alarms.saveAlarm({-1, "Next year", "", "2099-01-01T11:00:00Z", {}});
  check(tea && call && later, "create alarms");
  if (!tea || !call || !later) return;
  notifier.check();
  auto *ring = notifier.dialog();
  check(ring && ring->isVisible() && ring->alarmCount() == 2, "the alarms that have gone off ring");
  check(notifier.announcements() == 1, "announced together");
  if (!ring) return;
  check(ring->findChild<QFrame *>(QString("alarmCard_%1").arg(later->id)) == nullptr, "a future alarm does not");
  shot(*ring, "alarm-ring");
  auto *before = ring->findChild<QPushButton *>(QString("alarmDismiss_%1").arg(tea->id));
  notifier.check();
  check(notifier.announcements() == 1, "an alarm already ringing is not announced again at once");
  QApplication::processEvents();
  check(before && ring->findChild<QPushButton *>(QString("alarmDismiss_%1").arg(tea->id)) == before,
        "and its buttons are not replaced under the cursor");

  auto *snooze = ring->findChild<QPushButton *>(QString("alarmSnooze_%1").arg(tea->id));
  check(snooze != nullptr, "each alarm can be snoozed");
  if (snooze) snooze->click();
  const auto snoozed = application.alarms.loadAlarm(tea->id);
  check(snoozed && snoozed->firesAt > "2026" && snoozed->dismissedAt.empty(), "a snooze moves the alarm on");
  check(ring->alarmCount() == 1, "and it stops ringing for now");

  auto *dismiss = ring->findChild<QPushButton *>(QString("alarmDismiss_%1").arg(call->id));
  check(dismiss != nullptr, "each alarm can be dismissed");
  if (dismiss) dismiss->click();
  const auto dismissed = application.alarms.loadAlarm(call->id);
  check(dismissed && !dismissed->dismissedAt.empty(), "a dismissal is stored, for every client");
  check(!ring->isVisible(), "the window goes when nothing rings");

  // Dismissed elsewhere - in the web client, say - stops ringing here too.
  application.alarms.saveAlarm({snoozed->id, "Tea", "", "2020-01-01T10:00:00Z", {}});
  notifier.check();
  check(ring->isVisible() && notifier.announcements() == 2, "a moved alarm rings again");
  application.alarms.dismissAlarm(tea->id);
  notifier.check();
  check(!ring->isVisible(), "and stops when dismissed elsewhere");
  for (const int id : {tea->id, call->id, later->id}) application.alarms.deleteAlarm(id);
}
// An alarm that goes off while a modal dialog is open - the item editor, the
// alarm list - must still be answerable, or the whole window seems frozen.
void checkAlarmDuringModalDialog(lexicon::LexiconApplication &application) {
  if (auto due = application.alarms.loadDueAlarms())
    for (const auto &alarm : *due) application.alarms.dismissAlarm(alarm.id);
  QWidget window;
  window.resize(800, 600);
  window.show();
  AlarmNotifier notifier(&window, 60 * 60 * 1000);
  auto tea = application.alarms.saveAlarm({-1, "Tea during a dialog", "", "2020-01-01T10:00:00Z", {}});
  check(tea.has_value(), "create an alarm");
  if (!tea) return;
  QDialog modal(&window);
  modal.setWindowTitle("Some modal dialog");
  modal.resize(400, 300);
  bool rang = false;
  bool onTop = false;
  bool modalAgain = false;
  QTimer::singleShot(0, &modal, [&] {
    notifier.check();
    auto *ring = notifier.dialog();
    rang = ring && ring->isVisible();
    QApplication::processEvents();
    // Qt gives input to the most recently shown modal window only; the one
    // that answers must be the alarm, not the dialog beneath it.
    onTop = ring && QGuiApplication::modalWindow() == ring->windowHandle();
    if (ring) {
      if (auto *dismiss = ring->findChild<QPushButton *>(QString("alarmDismiss_%1").arg(tea->id))) dismiss->click();
      QApplication::processEvents();
      modalAgain = !ring->isVisible() && QGuiApplication::modalWindow() == modal.windowHandle();
    }
    modal.accept();
  });
  modal.exec();
  check(rang, "the alarm rings while a modal dialog is open");
  check(onTop, "and takes the input from that dialog, which would otherwise block it");
  check(modalAgain, "dismissed, it hands the input back to the dialog");
  const auto after = application.alarms.loadAlarm(tea->id);
  check(after && !after->dismissedAt.empty(), "and its Dismiss button can be clicked there");
  application.alarms.deleteAlarm(tea->id);
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
  checkCards(application, group);
  checkInbox(application);
  checkAlarms(application);
  checkAlarmNotifier(application);
  checkAlarmDuringModalDialog(application);
  checkImages(application, group, directory);

  if (failures == 0) std::cout << "desktop_gui_smoke: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
