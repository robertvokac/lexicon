#include "Exchange.h"
#include "SqliteRepository.h"
#include "StudyPlan.h"
#include "Validation.h"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>

namespace {
int failures = 0;
void check(bool value, const char* what) {
  if (!value) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
lexicon::StudyPlanRecord plan(std::string start, std::string end, int first, int last, int mask = 127) {
  lexicon::StudyPlanRecord result;
  result.item = "Effective Modern C++";
  result.startDate = std::move(start);
  result.endDate = std::move(end);
  result.firstUnit = first;
  result.lastUnit = last;
  result.studyDaysMask = mask;
  return result;
}
lexicon::StudyPlanOverview overview(const lexicon::StudyPlanRecord& p, const char* date) {
  auto result = lexicon::calculateStudyPlan(p, date);
  check(result.has_value(), "calculate overview");
  return result.value_or(lexicon::StudyPlanOverview{});
}
void schedules() {
  using lexicon::StudyPlanStatus;
  auto one = plan("2026-09-27", "2026-09-27", 101, 110);
  auto day = overview(one, "2026-09-27");
  check(day.active && day.studyDay && day.totalStudyDays == 1 && day.todayFirst == 101 && day.todayLast == 110,
        "one-day inclusive plan and absolute range");
  check(day.completedUnits == 0 && day.requiredUnitsPerRemainingStudyDay == 10 && day.status == StudyPlanStatus::Behind,
        "one-day unstarted plan is behind");
  one.currentProgress = 105;
  day = overview(one, "2026-09-27");
  check(day.completedUnits == 5 && day.remainingUnits == 5 && day.recommendedFirst == 106 && day.recommendedLast == 110,
        "non-one first unit and recommendation");
  one.currentProgress = 110;
  check(overview(one, "2026-09-28").status == StudyPlanStatus::Completed, "completed overrides overdue");
  one.currentProgress = 0;
  check(overview(one, "2026-09-28").status == StudyPlanStatus::Overdue, "incomplete after end is overdue");
  check(overview(one, "2026-09-26").status == StudyPlanStatus::Upcoming, "before start is upcoming");

  auto weekday = plan("2026-09-28", "2026-10-04", 1, 10, 31); // Mon–Sun
  check(overview(weekday, "2026-09-28").totalStudyDays == 5, "Monday–Friday has five days");
  check(!overview(weekday, "2026-10-03").studyDay && overview(weekday, "2026-10-03").todayFirst == 0,
        "Saturday has no target");
  check(overview(weekday, "2026-10-02").todayLast == 10, "Friday finishes every unit");
  auto weekend = plan("2026-09-28", "2026-10-04", 1, 4, 96);
  check(overview(weekend, "2026-10-03").todayFirst == 1 && overview(weekend, "2026-10-04").todayLast == 4,
        "weekend-only schedule");
  auto sparse = plan("2026-09-28", "2026-10-04", 1, 2);
  std::set<int> seen;
  for (const char* date : {"2026-09-28", "2026-09-29", "2026-09-30", "2026-10-01", "2026-10-02", "2026-10-03", "2026-10-04"}) {
    auto value = overview(sparse, date);
    for (int unit = value.todayFirst; unit > 0 && unit <= value.todayLast; ++unit)
      check(seen.insert(unit).second, "no overlap between daily ranges");
  }
  check(seen == std::set<int>({1, 2}), "daily ranges collectively cover all units");
  check(overview(sparse, "2026-09-28").todayFirst == 0, "some study days have no unit");

  auto risk = plan("2026-09-28", "2026-10-02", 1, 50, 31);
  risk.currentProgress = 20;
  check(overview(risk, "2026-09-29").status == StudyPlanStatus::OnTrack, "exactly on schedule");
  risk.currentProgress = 25;
  check(overview(risk, "2026-09-29").deficitUnits == -5, "ahead is negative deficit");
  risk.currentProgress = 18;
  check(overview(risk, "2026-09-29").status == StudyPlanStatus::Behind, "small deficit is behind");
  risk.currentProgress = 0;
  check(overview(risk, "2026-09-29").status == StudyPlanStatus::Behind,
        "exactly 125 percent of original pace is still behind");
  check(overview(risk, "2026-09-30").status == StudyPlanStatus::AtRisk, "required pace over 125 percent is at risk");
  check(overview(risk, "2026-10-02").remainingStudyDays == 1, "final day is included among remaining days");
  check(overview(risk, "2026-10-03").remainingStudyDays == 0, "zero days after end");
  check(overview(risk, "2026-10-03").requiredUnitsPerRemainingStudyDay == 0, "no divide by zero");
  auto leap = plan("2028-02-28", "2028-03-01", 1, 3);
  check(overview(leap, "2028-02-29").elapsedStudyDays == 2, "leap day");
  auto boundary = plan("2026-12-31", "2027-01-01", 1, 2);
  check(overview(boundary, "2027-01-01").todayFirst == 2, "year boundary");
  check(!lexicon::validCalendarDate("2026-02-29") && lexicon::validCalendarDate("2028-02-29"), "real calendar dates");
}
void storageAndExport() {
  namespace fs = std::filesystem;
  const auto directory = fs::temp_directory_path() /
      ("lexicon-study-plan-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(directory);
  struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{directory};
  const auto path = (directory / "lexicon.db").string();
  SqliteRepository repository;
  check(repository.open(path).has_value(), "open new database");
  lexicon::LexiconApplication app(repository);
  sqlite3* raw = nullptr;
  sqlite3_open(path.c_str(), &raw);
  sqlite3_stmt* query = nullptr;
  sqlite3_prepare_v2(raw, "SELECT version FROM db_version;", -1, &query, nullptr);
  check(sqlite3_step(query) == SQLITE_ROW && sqlite3_column_int(query, 0) == 34, "fresh schema 34");
  sqlite3_finalize(query);
  auto p = plan("2026-09-27", "2026-11-30", 1, 334);
  p.item = "Žluťoučký kůň";
  auto saved = app.studyPlans.save(p);
  check(saved.has_value() && saved->id > 0, "create UTF-8 plan");
  if (!saved) { sqlite3_close(raw); return; }
  check(app.studyPlans.loadAll()->size() == 1 && app.studyPlans.load(saved->id)->item == p.item, "list and get");
  saved->currentProgress = 50;
  auto updated = app.studyPlans.save(*saved);
  check(updated.has_value() && updated->currentProgress == 50, "update progress");
  auto exported = lexicon::exchange::exportDocument(app, false);
  check(exported && exported->find("studyPlans") != std::string::npos, "export carries Study Plans");
  const auto otherPath = (directory / "other.db").string();
  SqliteRepository other;
  check(other.open(otherPath).has_value(), "open import database");
  lexicon::LexiconApplication target(other);
  auto imported = lexicon::exchange::importDocument(target, *exported);
  check(imported && imported->studyPlansCreated == 1, "import creates Study Plan");
  check(target.studyPlans.loadAll()->at(0).currentProgress == 50, "import retains progress");
  auto oldExport = nlohmann::json::parse(*exported);
  oldExport["version"] = 8;
  oldExport.erase("studyPlans");
  check(lexicon::exchange::importDocument(target, oldExport.dump()).has_value(),
        "version 8 export without plans remains readable");
  auto again = lexicon::exchange::importDocument(target, *exported);
  check(again && again->studyPlansCreated == 0, "reimport does not duplicate");
  auto changedExport = nlohmann::json::parse(*exported);
  changedExport["studyPlans"][0]["currentProgress"] = 60;
  auto changedImport = lexicon::exchange::importDocument(target, changedExport.dump());
  check(changedImport && changedImport->studyPlansCreated == 1 && target.studyPlans.loadAll()->size() == 2,
        "a different imported progress is not silently discarded");
  auto invalid = *saved;
  invalid.id = -1;
  invalid.item = "  ";
  check(!app.studyPlans.save(invalid), "blank item rejected");
  invalid = p; invalid.type = static_cast<lexicon::StudyPlanType>(999);
  check(!app.studyPlans.save(invalid), "invalid plan enum rejected");
  invalid = p; invalid.unitType = static_cast<lexicon::StudyUnitType>(999);
  check(!app.studyPlans.save(invalid), "invalid unit enum rejected");
  invalid = p; invalid.firstUnit = 40; invalid.lastUnit = 20;
  check(!app.studyPlans.save(invalid), "invalid range rejected");
  invalid = p; invalid.currentProgress = 335;
  check(!app.studyPlans.save(invalid), "progress after last rejected");
  invalid = p; invalid.studyDaysMask = 0;
  check(!app.studyPlans.save(invalid), "zero mask rejected");
  invalid = p; invalid.startDate = "2026-02-30";
  check(!app.studyPlans.save(invalid), "invalid calendar date rejected");
  invalid = p; invalid.endDate = "2026-01-01";
  check(!app.studyPlans.save(invalid), "end before start rejected");
  invalid = p; invalid.unitType = lexicon::StudyUnitType::Other; invalid.customUnit = " ";
  check(!app.studyPlans.save(invalid), "custom unit required");
  check(sqlite3_exec(raw, "INSERT INTO study_plan(item,type,unit_type,start_date,end_date,last_unit) "
                          "VALUES('Invalid',999,0,'2026-09-27','2026-09-28',3);", nullptr, nullptr, nullptr) == SQLITE_CONSTRAINT,
        "SQLite rejects invalid enum even outside the service");
  check(!app.studyPlans.load(999999) && app.studyPlans.load(999999).error().code == lexicon::Error::Code::NotFound,
        "get missing returns NotFound");
  check(!app.studyPlans.remove(999999) && app.studyPlans.remove(999999).error().code == lexicon::Error::Code::NotFound,
        "delete missing returns NotFound");
  check(app.studyPlans.remove(saved->id).has_value() && app.studyPlans.loadAll()->empty(), "delete plan");
  sqlite3_close(raw);
  // Recreate an actual version-33 database by removing only migration-34
  // structures. Existing data and its IDs must survive the next open.
  const auto oldPath = (directory / "version33.db").string();
  {
    SqliteRepository before;
    check(before.open(oldPath).has_value(), "create pre-feature fixture");
    lexicon::LexiconApplication old(before);
    lexicon::ItemRecord item;
    item.groupId = old.groups.defaultGroupId().value_or(-1);
    item.title = "Preserved item";
    check(old.items.createItem(item).has_value(), "write pre-feature item");
  }
  sqlite3* oldRaw = nullptr;
  sqlite3_open(oldPath.c_str(), &oldRaw);
  check(sqlite3_exec(oldRaw, "DROP TABLE study_plan; UPDATE db_version SET version = 33;", nullptr, nullptr, nullptr) == SQLITE_OK,
        "downgrade fixture to real version 33 schema");
  sqlite3_close(oldRaw);
  {
    SqliteRepository after;
    check(after.open(oldPath).has_value(), "migrate version 33 database");
    lexicon::LexiconApplication upgraded(after);
    check(upgraded.search.findItemId("Preserved item").has_value(), "version 33 item survives migration");
    check(upgraded.studyPlans.loadAll()->empty(), "migration 34 creates empty plan table");
  }
}
} // namespace
int main() { schedules(); storageAndExport(); return failures ? 1 : 0; }
