// Run with a fixed zone: TZ=Europe/Prague node --test tests/
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { formatAlarmTime, hasGoneOff, localInputToUtc, nextFullHour, utcToLocalInput } from '../js/alarmtime.js';

test('a UTC time round-trips through the local datetime-local value', () => {
    for (const utc of ['2030-01-02T09:15:00Z', '2026-07-31T23:45:00Z', '2026-03-29T01:30:00Z']) {
        assert.equal(localInputToUtc(utcToLocalInput(utc)), utc);
    }
});

test('the local value is the browser zone', () => {
    const local = utcToLocalInput('2030-01-02T09:15:00Z');
    const date = new Date('2030-01-02T09:15:00Z');
    const pad = (value) => String(value).padStart(2, '0');
    assert.equal(local.slice(11), `${pad(date.getHours())}:${pad(date.getMinutes())}`);
});

test('an empty or impossible value gives no time', () => {
    assert.equal(localInputToUtc(''), '');
    assert.equal(localInputToUtc('2026-02-30T08:00'), '');
    assert.equal(localInputToUtc('tomorrow'), '');
    assert.equal(utcToLocalInput('never'), '');
});

test('a new alarm starts at the next full hour', () => {
    const next = new Date(nextFullHour(new Date(2026, 8, 22, 10, 37, 12)));
    assert.equal(next.getMinutes(), 0);
    assert.equal(next.getSeconds(), 0);
    assert.equal(next.getHours(), 11);
});

test('the time is shown with its weekday, and past alarms are known', () => {
    assert.match(formatAlarmTime('2030-01-02T09:15:00Z', 'en-US'), /^Wed 2030-01-0[12] \d\d:\d5$/);
    const now = new Date('2026-09-22T12:00:00Z');
    assert.ok(hasGoneOff('2026-09-22T11:59:00Z', now));
    assert.ok(!hasGoneOff('2026-09-22T12:01:00Z', now));
});
