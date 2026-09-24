import assert from 'node:assert/strict';
import { test } from 'node:test';

import { QuizSession, lastAttemptText } from '../js/cardquiz.js';

const cards = [
    { id: 1, itemTitle: 'pointer provenance', question: 'Co znamená řetězec?\nstd::uint64_t', answer: '指针',
        successCount: 0, failureCount: 0, lastAttempt: null },
    { id: 2, itemTitle: 'compiler optimization', question: 'Why?', answer: 'Příliš žluťoučký kůň',
        successCount: 3, failureCount: 1, lastAttempt: '2026-09-24T14:00:00Z' },
];

test('the answer waits to be asked for, and asking records nothing', () => {
    const session = new QuizSession(cards);
    assert.equal(session.current.id, 1);
    assert.equal(session.progress, '1 / 2');
    assert.equal(session.revealed, false);
    assert.equal(session.canAnswer, false);
    assert.equal(session.begin(), false, 'no answer before the answer shows');
    assert.equal(session.reveal(), true);
    assert.equal(session.reveal(), false, 'showing it twice changes nothing');
    assert.equal(session.yes + session.no, 0);
    assert.equal(session.canAnswer, true);
});

test('an answer counts once the server took it, and one at a time', () => {
    const session = new QuizSession(cards);
    session.reveal();
    assert.equal(session.begin(), true);
    assert.equal(session.begin(), false, 'a second Yes while the first is on its way is refused');
    assert.equal(session.canAnswer, false);
    session.accept(true, { id: 1, successCount: 1, lastAttempt: '2026-09-24T15:00:00Z' });
    assert.equal(session.yes, 1);
    assert.equal(session.cards[0].successCount, 1, 'the card keeps what the server says');
    assert.equal(session.cards[0].question, cards[0].question);
    assert.equal(session.current.id, 2);
    assert.equal(session.progress, '2 / 2');
    assert.equal(session.revealed, false, 'the next answer is hidden again');
    session.reveal();
    session.begin();
    session.accept(false, { id: 2, failureCount: 2 });
    assert.equal(session.finished, true);
    assert.equal(session.summary, 'Cards: 2\nYes: 1\nNo: 1');
    assert.equal(session.reveal(), false, 'nothing is left to show');
    assert.equal(session.begin(), false, 'or to answer');
});

test('a refused answer keeps the card; a vanished card is passed over', () => {
    const session = new QuizSession(cards);
    session.reveal();
    session.begin();
    session.fail();
    assert.equal(session.current.id, 1);
    assert.equal(session.revealed, true);
    assert.equal(session.canAnswer, true, 'it can be answered again');
    session.begin();
    session.skip();
    assert.equal(session.current.id, 2);
    assert.equal(session.yes + session.no, 0, 'a skipped card is not counted');
});

test('an empty quiz is over at once', () => {
    const session = new QuizSession([]);
    assert.equal(session.finished, true);
    assert.equal(session.current, null);
    assert.equal(session.reveal(), false);
});

test('the session never changes the cards it was given', () => {
    const given = cards.map((card) => ({ ...card }));
    const session = new QuizSession(given);
    session.reveal();
    session.begin();
    session.accept(true, { successCount: 9 });
    assert.equal(given[0].successCount, 0);
});

test('a last attempt shows in local time, or as Never', () => {
    assert.equal(lastAttemptText(null), 'Never');
    assert.equal(lastAttemptText(''), 'Never');
    const shown = lastAttemptText('2026-09-24T14:00:00Z');
    assert.match(shown, /^2026-09-2\d \d{2}:00$/);
    const date = new Date('2026-09-24T14:00:00Z');
    assert.equal(shown.slice(11, 13), String(date.getHours()).padStart(2, '0'));
});
