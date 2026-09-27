#pragma once
#include "Repository.h"
#include <optional>

namespace lexicon {
enum class StudyPlanStatus { Upcoming, OnTrack, Behind, AtRisk, Completed, Overdue };
inline constexpr double kStudyPlanRiskPaceFactor = 1.25;

struct StudyPlanOverview {
  StudyPlanRecord plan;
  std::string date;
  StudyPlanStatus status = StudyPlanStatus::Upcoming;
  bool upcoming = false;
  bool active = false;
  bool ended = false;
  bool complete = false;
  bool studyDay = false;
  int totalUnits = 0;
  int completedUnits = 0;
  int remainingUnits = 0;
  int totalStudyDays = 0;
  int elapsedStudyDays = 0;
  int remainingStudyDays = 0;
  double plannedUnitsPerStudyDay = 0;
  std::optional<double> requiredUnitsPerRemainingStudyDay;
  int expectedCompletedUnits = 0;
  int expectedProgress = 0;
  int deficitUnits = 0; // negative means ahead
  std::optional<int> expectedUnitStart;
  std::optional<int> expectedUnitEnd;
  // Legacy range names retained for existing API consumers.
  int todayFirst = 0;
  int todayLast = 0;
  int recommendedFirst = 0;
  int recommendedLast = 0;
};

Result<StudyPlanOverview> calculateStudyPlan(const StudyPlanRecord &plan,
                                            std::string_view date);

class StudyPlanService {
public:
  explicit StudyPlanService(Repository &repository) : repository_(repository) {}
  Result<std::vector<StudyPlanRecord>> loadAll() { return repository_.loadStudyPlans(); }
  Result<StudyPlanRecord> load(int id) { return repository_.loadStudyPlan(id); }
  Result<StudyPlanRecord> save(const StudyPlanRecord &plan);
  Result<void> remove(int id) { return repository_.deleteStudyPlan(id); }
  Result<std::vector<StudyPlanOverview>> overview(std::string_view date);
private:
  Repository &repository_;
};
} // namespace lexicon
