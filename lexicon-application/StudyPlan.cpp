#include "StudyPlan.h"
#include "Validation.h"

#include <algorithm>
#include <bit>
#include <chrono>

namespace lexicon {
namespace {
using namespace std::chrono;
sys_days parseDate(std::string_view value) {
  const auto number = [&](std::size_t offset, std::size_t length) {
    int result = 0;
    for (std::size_t index = offset; index < offset + length; ++index)
      result = result * 10 + value[index] - '0';
    return result;
  };
  return sys_days{year{number(0, 4)} / month{static_cast<unsigned>(number(5, 2))} /
                  day{static_cast<unsigned>(number(8, 2))}};
}
bool selected(sys_days date, int mask) {
  const unsigned mondayZero = (weekday{date}.c_encoding() + 6) % 7;
  return (mask & (1 << mondayZero)) != 0;
}
int countDays(sys_days first, sys_days last, int mask) {
  if (last < first) return 0;
  const auto days = (last - first).count() + 1;
  const int weeks = static_cast<int>(days / 7);
  int result = weeks * std::popcount(static_cast<unsigned>(mask));
  for (int remainder = 0; remainder < days % 7; ++remainder)
    if (selected(first + std::chrono::days{remainder}, mask)) ++result;
  return result;
}
} // namespace

Result<StudyPlanOverview> calculateStudyPlan(const StudyPlanRecord &plan,
                                            std::string_view date) {
  if (auto valid = validateStudyPlan(plan); !valid) return std::unexpected(valid.error());
  if (!validCalendarDate(date))
    return std::unexpected(Error{Error::Code::Validation, "Enter a valid YYYY-MM-DD calendar date."});
  const auto start = parseDate(plan.startDate);
  const auto end = parseDate(plan.endDate);
  const auto today = parseDate(date);
  StudyPlanOverview result;
  result.plan = plan;
  result.date = date;
  result.upcoming = today < start;
  result.ended = today > end;
  result.active = !result.upcoming && !result.ended;
  result.complete = plan.currentProgress == plan.lastUnit;
  result.studyDay = result.active && selected(today, plan.studyDaysMask);
  result.totalUnits = plan.lastUnit - plan.firstUnit + 1;
  result.completedUnits = plan.currentProgress == 0 ? 0 : plan.currentProgress - plan.firstUnit + 1;
  result.completedUnits = std::clamp(result.completedUnits, 0, result.totalUnits);
  result.remainingUnits = result.totalUnits - result.completedUnits;
  result.totalStudyDays = countDays(start, end, plan.studyDaysMask);
  // A date range may contain no chosen weekdays; there is no valid schedule in that case.
  if (result.totalStudyDays == 0)
    return std::unexpected(Error{Error::Code::Validation, "The date range contains no selected study day."});
  result.elapsedStudyDays = countDays(start, std::min(today, end), plan.studyDaysMask);
  result.remainingStudyDays = countDays(std::max(today, start), end, plan.studyDaysMask);
  result.plannedUnitsPerStudyDay = static_cast<double>(result.totalUnits) / result.totalStudyDays;
  if (result.remainingStudyDays > 0)
    result.requiredUnitsPerRemainingStudyDay = static_cast<double>(result.remainingUnits) / result.remainingStudyDays;
  result.expectedUnits = static_cast<int>(static_cast<long long>(result.totalUnits) * result.elapsedStudyDays / result.totalStudyDays);
  result.deficitUnits = result.expectedUnits - result.completedUnits;
  if (result.studyDay) {
    const int before = result.elapsedStudyDays - 1;
    const int firstOffset = static_cast<int>(static_cast<long long>(result.totalUnits) * before / result.totalStudyDays);
    const int throughOffset = result.expectedUnits;
    if (throughOffset > firstOffset) {
      result.todayFirst = plan.firstUnit + firstOffset;
      result.todayLast = plan.firstUnit + throughOffset - 1;
    }
    if (result.deficitUnits > 0 && result.remainingUnits > 0 && result.remainingStudyDays > 0) {
      result.recommendedFirst = plan.currentProgress == 0 ? plan.firstUnit : plan.currentProgress + 1;
      const int target = (result.remainingUnits + result.remainingStudyDays - 1) / result.remainingStudyDays;
      result.recommendedLast = std::min(plan.lastUnit, result.recommendedFirst + target - 1);
    }
  }
  if (result.complete) result.status = StudyPlanStatus::Completed;
  else if (result.ended) result.status = StudyPlanStatus::Overdue;
  else if (result.upcoming) result.status = StudyPlanStatus::Upcoming;
  else if (result.deficitUnits <= 0) result.status = StudyPlanStatus::OnTrack;
  else if (result.remainingStudyDays == 0 ||
           result.requiredUnitsPerRemainingStudyDay > result.plannedUnitsPerStudyDay * kStudyPlanRiskPaceFactor)
    result.status = StudyPlanStatus::AtRisk;
  else result.status = StudyPlanStatus::Behind;
  return result;
}

Result<StudyPlanRecord> StudyPlanService::save(const StudyPlanRecord &plan) {
  if (auto valid = validateStudyPlan(plan); !valid) return std::unexpected(valid.error());
  auto id = repository_.saveStudyPlan(plan);
  if (!id) return std::unexpected(id.error());
  return repository_.loadStudyPlan(*id);
}

Result<std::vector<StudyPlanOverview>> StudyPlanService::overview(std::string_view date) {
  if (!validCalendarDate(date))
    return std::unexpected(Error{Error::Code::Validation, "Enter a valid YYYY-MM-DD calendar date."});
  auto plans = repository_.loadStudyPlans();
  if (!plans) return std::unexpected(plans.error());
  std::vector<StudyPlanOverview> result;
  for (const auto &plan : *plans) {
    auto calculated = calculateStudyPlan(plan, date);
    if (!calculated) return std::unexpected(calculated.error());
    result.push_back(std::move(*calculated));
  }
  return result;
}
} // namespace lexicon
