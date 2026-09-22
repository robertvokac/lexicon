#pragma once
// Spaced repetition: how an item's understanding moves when it is reviewed,
// and how long until it is due again. The better an item is known, the longer
// the gap before the next review.
#include "Records.h"

#include <algorithm>

namespace lexicon {
// How well the item was remembered in a review.
enum class ReviewRating { Again = 0, Hard = 1, Good = 2, Easy = 3 };

// Days from a review to the next one, by the understanding after it.
constexpr int reviewIntervalDays(UnderstandingLevel level) {
  switch (level) {
  case UnderstandingLevel::Unknown: return 1;
  case UnderstandingLevel::Recognized: return 2;
  case UnderstandingLevel::Understood: return 5;
  case UnderstandingLevel::Practiced: return 12;
  case UnderstandingLevel::Mastered: return 30;
  }
  return 1;
}

// Again drops a level, Hard keeps it, Good climbs one and Easy two.
constexpr UnderstandingLevel levelAfterReview(UnderstandingLevel current, ReviewRating rating) {
  const int level = static_cast<int>(current);
  const int step = rating == ReviewRating::Again ? -1
                   : rating == ReviewRating::Hard ? 0
                   : rating == ReviewRating::Good ? 1
                                                  : 2;
  return static_cast<UnderstandingLevel>(
      std::clamp(level + step, static_cast<int>(UnderstandingLevel::Unknown),
                 static_cast<int>(UnderstandingLevel::Mastered)));
}
} // namespace lexicon
