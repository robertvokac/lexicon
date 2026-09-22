package com.robertvokac.lexicon.alarms

import com.robertvokac.lexicon.model.Alarm
import com.robertvokac.lexicon.ui.alarms.AlarmTimes
import java.time.Instant

/** What the phone does with the server's alarms at a moment. */
data class AlarmPlan(
    /** Still to go off: scheduled with the system. */
    val upcoming: List<Alarm>,
    /** Gone off and nobody has dismissed them: shown as notifications. */
    val ringing: List<Alarm>,
) {
    companion object {
        fun of(alarms: List<Alarm>, now: Instant, dismissedHere: Set<Int> = emptySet()): AlarmPlan {
            val upcoming = mutableListOf<Alarm>()
            val ringing = mutableListOf<Alarm>()
            for (alarm in alarms) {
                val id = alarm.id ?: continue
                val at = AlarmTimes.parse(alarm.firesAt) ?: continue
                when {
                    at.isAfter(now) -> upcoming += alarm
                    alarm.dismissedAt == null && id !in dismissedHere -> ringing += alarm
                }
            }
            return AlarmPlan(upcoming, ringing)
        }
    }
}
