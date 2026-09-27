import assert from 'node:assert/strict';
import test from 'node:test';
import { localStudyDate, pace, studyRange, unitLabel } from '../js/studyplans.js';

test('local date uses local calendar components across a timezone boundary', () => {
    const late = new Date('2026-09-27T23:30:00-05:00');
    const expected = `${late.getFullYear()}-${String(late.getMonth() + 1).padStart(2, '0')}-${String(late.getDate()).padStart(2, '0')}`;
    assert.equal(localStudyDate(late), expected);
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
