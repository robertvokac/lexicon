// One sitting of a card quiz, without the page: which card is up, whether its
// answer shows, and how many were known. Showing an answer records nothing;
// an answer counts only once the server has taken it, and while one is on
// its way no other can be sent. Pure, so Node can test it.
import { utcToLocalInput } from './alarmtime.js';

export class QuizSession {
    constructor(cards) {
        this.cards = [...cards];
        this.index = 0;
        this.revealed = false;
        this.busy = false;
        this.yes = 0;
        this.no = 0;
    }

    get current() {
        return this.index < this.cards.length ? this.cards[this.index] : null;
    }

    get finished() {
        return this.index >= this.cards.length;
    }

    // "3 / 17" while a card is up.
    get progress() {
        return `${Math.min(this.index + 1, this.cards.length)} / ${this.cards.length}`;
    }

    // Shows the answer of the current card; false when there is nothing to show.
    reveal() {
        if (!this.current || this.revealed) return false;
        this.revealed = true;
        return true;
    }

    get canAnswer() {
        return Boolean(this.current) && this.revealed && !this.busy;
    }

    // Claims the answer about to be sent, or says it may not be sent now.
    begin() {
        if (!this.canAnswer) return false;
        this.busy = true;
        return true;
    }

    // The server took the answer: count it, keep the card as it came back
    // and move on to the next one.
    accept(knew, card) {
        if (!this.busy) return;
        this.cards[this.index] = { ...this.cards[this.index], ...card };
        if (knew) this.yes += 1;
        else this.no += 1;
        this.next();
    }

    // The answer was refused: the same card, its answer still showing.
    fail() {
        this.busy = false;
    }

    // Past a card that can no longer be answered, without counting it.
    skip() {
        if (this.current) this.next();
    }

    next() {
        this.index += 1;
        this.revealed = false;
        this.busy = false;
    }

    get summary() {
        return `Cards: ${this.cards.length}\nYes: ${this.yes}\nNo: ${this.no}`;
    }
}

// When a card was last answered, in local time, or "Never".
export function lastAttemptText(utc) {
    if (!utc) return 'Never';
    return utcToLocalInput(utc).replace('T', ' ') || utc;
}
