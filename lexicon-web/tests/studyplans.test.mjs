import assert from 'node:assert/strict';
import test from 'node:test';
import { canMarkToday, differenceText, expectedRangeText, groupStudyPlans, localStudyDate, pace, requiredPaceText, studyPlanPage, studyRange, unitLabel, validStudyDate } from '../js/studyplans.js';

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

test('203 plans keep every active plan separate from paged future and finished plans', () => {
    const book = (id, flags) => ({ plan: { id, item: `Book ${String(id).padStart(3, '0')}`,
        startDate: '2026-10-01', endDate: '2026-10-31' }, ...flags });
    const values = [book(1, { active: true, status: 'On track' }), book(2, { active: true, status: 'Behind' }),
        book(3, { active: true, status: 'At risk' }),
        ...Array.from({ length: 100 }, (_, i) => book(i + 4, { upcoming: true })),
        ...Array.from({ length: 100 }, (_, i) => book(i + 104, { active: true, complete: true }))];
    const grouped = groupStudyPlans(values);
    assert.deepEqual(grouped.active.map((x) => x.plan.id), [3, 2, 1]);
    assert.equal(grouped.upcoming.length, 100);
    assert.equal(grouped.finished.length, 100);
    const seen = [];
    for (let page = 0; page < 10; page++) {
        const result = studyPlanPage(grouped.upcoming, '', page);
        assert.equal(result.values.length, 10);
        assert.equal(result.pageCount, 10);
        seen.push(...result.values.map((x) => x.plan.id));
    }
    assert.deepEqual(seen, grouped.upcoming.map((x) => x.plan.id));
    assert.equal(groupStudyPlans([book(300, { ended: true, status: 'Overdue' })]).finished.length, 1);
});

test('search covers every page, title, group and note; shrinking lists clamp the page', () => {
    const values = Array.from({ length: 21 }, (_, id) => ({ plan: { id, item: `Book ${id}`,
        group: id === 15 ? 'C++ Library' : '', note: id === 20 ? 'Read templates next' : '' } }));
    assert.deepEqual(studyPlanPage(values, ' book 20 ', 2).values.map((x) => x.plan.id), [20]);
    assert.equal(studyPlanPage(values, 'c++ library', 9).page, 0);
    assert.deepEqual(studyPlanPage(values, 'TEMPLATES').values.map((x) => x.plan.id), [20]);
    assert.equal(studyPlanPage(values, '', 2).values.length, 1);
    const afterDelete = studyPlanPage(values.slice(0, 20), '', 2);
    assert.equal(afterDelete.page, 1);
    assert.equal(afterDelete.values.length, 10);
    const empty = studyPlanPage(values, 'missing', 9);
    assert.equal(empty.page, 0);
    assert.equal(empty.pageCount, 1);
    assert.equal(empty.total, 0);
    assert.deepEqual(empty.values, []);
});
