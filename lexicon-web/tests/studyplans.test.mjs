import assert from 'node:assert/strict';
import test from 'node:test';
import { canMarkToday, differenceText, expectedRangeText, localStudyDate, pace, requiredPaceText, studyRange, unitLabel, validStudyDate } from '../js/studyplans.js';

test('local date uses local calendar components across a timezone boundary', () => {
    const late = new Date('2026-09-27T23:30:00-05:00');
    const expected = `${late.getFullYear()}-${String(late.getMonth() + 1).padStart(2, '0')}-${String(late.getDate()).padStart(2, '0')}`;
    assert.equal(localStudyDate(late), expected);
});
test('Study Plan dates use the ISO calendar format', () => {
    assert.equal(validStudyDate('2024-02-29'), true);
    assert.equal(validStudyDate('2026-09-27'), true);
    for (const value of ['27.09.2026', '2026-9-27', '2026-02-29', '2026-13-01', '2026-04-31', '']) {
        assert.equal(validStudyDate(value), false, value);
    }
});
test('overview presents absolute progress, daily range and unavailable pace', () => {
    const plan = { unitType: 'Page', firstUnit: 101, lastUnit: 300, currentProgress: 187 };
    const value = { plan, active: true, ended: false, complete: false, studyDay: true,
        expectedProgress: 220, expectedUnitStart: 211, expectedUnitEnd: 220,
        recommendedLast: 230, deficitUnits: 33, requiredUnitsPerRemainingStudyDay: 14.25 };
    assert.equal(value.expectedProgress, 220);
    assert.equal(expectedRangeText(value), 'pages 211–220');
    assert.equal(differenceText(value), 'Behind by: 33 pages');
    assert.equal(requiredPaceText(value), '14.25 pages/day');
    assert.equal(canMarkToday(value), true);
    assert.equal(differenceText({ ...value, deficitUnits: -10 }), 'Ahead by: 10 pages');
    assert.equal(differenceText({ ...value, deficitUnits: 0 }), 'On expected progress');
    const noDay = { ...value, studyDay: false, expectedUnitStart: null, expectedUnitEnd: null,
        requiredUnitsPerRemainingStudyDay: null };
    assert.equal(expectedRangeText(noDay), 'No study scheduled');
    assert.equal(requiredPaceText(noDay), 'N/A — no study days remaining');
    assert.equal(canMarkToday(noDay), false);
    assert.equal(expectedRangeText({ ...noDay, studyDay: true }), 'No units scheduled today');
    assert.equal(canMarkToday({ ...noDay, studyDay: true }), false);
    assert.equal(expectedRangeText({ ...noDay, active: false }), '—');
    assert.equal(requiredPaceText({ ...noDay, active: false, ended: true }), 'N/A — deadline passed');
    assert.equal(canMarkToday({ ...value, complete: true }), false);
    assert.equal(canMarkToday({ ...value, recommendedLast: 187, expectedUnitEnd: 187 }), false);
});
test('pace is displayed to exactly two decimals', () => {
    assert.equal(pace(500 / 73), '6.85');
    assert.equal(pace(1), '1.00');
});
test('ranges use unit labels and omit empty targets', () => {
    assert.equal(studyRange({ unitType: 'Lesson' }, 17, 18), 'lessons 17–18');
    assert.equal(studyRange({ unitType: 'Chapter' }, 7, 7), 'chapter 7');
    assert.equal(studyRange({ unitType: 'Other', customUnit: 'kata' }, 3, 4), 'katas 3–4');
    assert.equal(studyRange({ unitType: 'Page' }, 0, 0), 'No units scheduled today');
    assert.equal(unitLabel({ unitType: 'Minute' }, 2), 'minutes');
});
