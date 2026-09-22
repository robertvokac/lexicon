package com.robertvokac.lexicon.model

import org.junit.Assert.assertEquals
import org.junit.Test

/** The rating buttons preview what the server does; lexicon-core/Review.h has the same rules. */
class ReviewRulesTest {
    @Test
    fun ratingsMoveTheUnderstandingWithinItsRange() {
        assertEquals(UnderstandingLevel.Recognized, ReviewRating.Again.levelAfter(UnderstandingLevel.Understood))
        assertEquals(UnderstandingLevel.Unknown, ReviewRating.Again.levelAfter(UnderstandingLevel.Unknown))
        assertEquals(UnderstandingLevel.Understood, ReviewRating.Hard.levelAfter(UnderstandingLevel.Understood))
        assertEquals(UnderstandingLevel.Practiced, ReviewRating.Good.levelAfter(UnderstandingLevel.Understood))
        assertEquals(UnderstandingLevel.Mastered, ReviewRating.Easy.levelAfter(UnderstandingLevel.Practiced))
        assertEquals(UnderstandingLevel.Mastered, ReviewRating.Easy.levelAfter(UnderstandingLevel.Mastered))
    }

    @Test
    fun betterKnownItemsWaitLonger() {
        val days = UnderstandingLevel.entries.map { ReviewRating.intervalDays(it) }
        assertEquals(listOf(1, 2, 5, 12, 30), days)
    }
}
