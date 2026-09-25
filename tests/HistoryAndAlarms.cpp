#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
  const auto root = fs::temp_directory_path() /
      ("lexicon-history-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  const auto cleanup = [&] { std::error_code ignored; fs::remove_all(root, ignored); };
  const auto fail = [&](const char *message) {
    std::cerr << message << '\n'; cleanup(); return 1;
  };
  SqliteRepository repository;
  if (!repository.open((root / "lexicon.db").string())) return fail("database did not open");
  lexicon::LexiconApplication app(repository);
  auto group = app.groups.defaultGroupId();
  if (!group) return fail("default group missing");

  lexicon::ItemRecord first;
  first.groupId = *group; first.title = "First"; first.content = "Before";
  auto firstId = app.items.createItem(first);
  lexicon::ItemRecord peer;
  peer.groupId = *group; peer.title = "Peer";
  auto peerId = app.items.createItem(peer);
  if (!firstId || !peerId) return fail("items were not created");
  lexicon::LinkRecord link;
  link.fromItemId = *firstId; link.toItemId = *peerId;
  link.linkType = lexicon::LinkType::Related;
  if (!app.links.saveLink(link)) return fail("link was not created");
  auto card = app.cards.createCard(*firstId, "Question", "Answer");
  if (!card || !app.cards.recordAttempt(card->id, true)) return fail("card was not created");
  auto changed = app.items.loadItem(*firstId);
  if (!changed) return fail("item did not load");
  changed->content = "After";
  if (!app.items.saveItem(*changed)) return fail("item did not save");
  auto history = app.items.loadItemHistory(*firstId);
  if (!history || history->empty() || history->front().item.content != "Before")
    return fail("previous content was not recorded");
  auto restoredVersion = app.items.restoreItemHistory(history->front().id);
  auto previous = app.items.loadItem(*firstId);
  if (!restoredVersion || *restoredVersion != *firstId || !previous || previous->content != "Before")
    return fail("previous version did not restore");

  lexicon::AlarmRecord repeating{-1, "Repeat", "", "2020-01-01T10:00:00Z", ""};
  repeating.repeatDays = 1; repeating.itemId = *firstId;
  auto alarm = app.alarms.saveAlarm(repeating);
  if (!alarm || alarm->itemId != *firstId) return fail("linked alarm was not saved");
  auto next = app.alarms.dismissAlarm(alarm->id);
  if (!next || !next->dismissedAt.empty() || next->firesAt <= "2026-01-01T00:00:00Z")
    return fail("repeating alarm did not move to a future day");
  auto again = app.alarms.dismissAlarm(alarm->id);
  if (!again || again->firesAt != next->firesAt)
    return fail("a second dismissal changed the next occurrence");
  auto due = app.alarms.loadDueAlarms();
  if (!due || !due->empty()) return fail("repeating alarm remained due");

  if (!app.items.deleteItem(*firstId)) return fail("item did not delete");
  if (app.items.loadItem(*firstId)) return fail("deleted item still loads");
  auto trash = app.items.loadTrash();
  if (!trash || trash->empty() || trash->front().item.title != "First")
    return fail("deleted item is missing from trash");
  auto detached = app.alarms.loadAlarm(alarm->id);
  if (!detached || detached->itemId != -1) return fail("alarm was not detached on deletion");
  auto restored = app.items.restoreItemHistory(trash->front().id);
  if (!restored || *restored == *firstId) return fail("deleted item was not recreated");
  auto recreated = app.items.loadItem(*restored);
  auto cards = app.cards.loadCards(*restored);
  auto links = app.links.loadLinks(*restored);
  if (!recreated || recreated->content != "Before" || !cards || cards->size() != 1 ||
      cards->front().successCount != 1 || !links || links->size() != 1 ||
      links->front().toItemId != *peerId)
    return fail("restored item lost its content, cards or links");
  auto secondRestore = app.items.restoreItemHistory(trash->front().id);
  if (secondRestore) return fail("the same trash entry restored twice");
  cleanup();
  return 0;
}
