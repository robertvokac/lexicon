// Cards: questions and answers about an item, and a quiz over them - the web
// counterparts of CardsDialog and CardQuizDialog. Card text is plain text,
// always shown through textContent, never as HTML or Markdown. A quiz answer
// counts on the card only; it never touches the item's review.
import { api } from './api.js';
import { QuizSession, lastAttemptText } from './cardquiz.js';
import { confirmDialog, errorDialog, field, openDialog } from './dialogs.js';
import { button, clear, el, fillSelect, formatItemTitle, LITERAL_TEXT } from './utils.js';

// How many items a neighbourhood quiz covers at most, as the graph draws.
export const QUIZ_ITEMS = 150;

function statisticsText(card) {
    return `Known ${card.successCount} time(s), not known ${card.failureCount} time(s). `
        + `Last attempt: ${lastAttemptText(card.lastAttempt)}.`;
}

// Adds a card to the item, or edits `card`; resolves with the stored card or null.
function cardDialog(itemId, card) {
    const question = el('textarea', { rows: '4', id: 'card-question', class: 'card-field', ...LITERAL_TEXT });
    const answer = el('textarea', { rows: '6', id: 'card-answer', class: 'card-field', ...LITERAL_TEXT });
    question.value = card ? card.question : '';
    answer.value = card ? card.answer : '';
    return openDialog({
        title: card ? 'Edit card' : 'Add card',
        body: el('div', { class: 'card-edit' }, [
            field('Question:', question),
            field('Answer:', answer),
            // Counted by the quiz; not for a person to change.
            card ? el('p', { class: 'hint card-statistics', text: statisticsText(card) }) : null,
        ]),
        initialFocus: question,
        clearErrorOn: [question, answer],
        // A stray tap beside the editor must not throw away what was typed.
        closeOnBackdrop: false,
        onAccept: async ({ fail }) => {
            if (!question.value.trim()) {
                question.focus();
                fail('Enter a question.');
                return undefined;
            }
            if (!answer.value.trim()) {
                answer.focus();
                fail('Enter an answer.');
                return undefined;
            }
            const values = { question: question.value, answer: answer.value };
            // A refused card keeps the dialog open with the reason.
            return card ? api.updateCard(card.id, values) : api.createCard(itemId, values);
        },
    });
}

// The cards of one item, to add, edit and delete. Every change is saved at
// once, apart from the item editor's Save and Cancel.
export async function openCards(itemId) {
    const [loaded, initial] = await Promise.all([api.getItem(itemId), api.itemCards(itemId)]);
    let cards = initial;
    let selectedId = null;

    const body = el('tbody');
    const buttons = {
        add: button('Add...', { class: 'secondary', onclick: () => add() }),
        edit: button('Edit...', { class: 'secondary', onclick: () => edit() }),
        remove: button('Delete', { class: 'secondary', onclick: () => remove() }),
        quiz: button('Quiz...', { class: 'secondary', title: "Go through this item's cards", onclick: () => quiz() }),
    };
    const table = el('table', { class: 'value-table card-table' }, [
        el('thead', {}, [el('tr', {}, ['Question', 'Answer', 'Success', 'Failure', 'Last attempt']
            .map((heading) => el('th', { text: heading })))]),
        body,
    ]);

    const selected = () => cards.find((card) => card.id === selectedId);

    function select(id) {
        selectedId = id;
        for (const row of body.querySelectorAll('tr[data-id]')) {
            const chosen = Number(row.dataset.id) === id;
            row.classList.toggle('selected', chosen);
            row.setAttribute('aria-selected', chosen ? 'true' : 'false');
        }
        buttons.edit.disabled = !selected();
        buttons.remove.disabled = !selected();
        buttons.quiz.disabled = !cards.length;
    }

    function render() {
        clear(body);
        for (const card of cards) {
            body.appendChild(el('tr', {
                tabindex: '0',
                dataset: { id: String(card.id) },
                onclick: () => select(card.id),
                onfocus: () => select(card.id),
                ondblclick: () => { select(card.id); edit(); },
                onkeydown: (event) => {
                    if (event.key === 'Enter') { select(card.id); edit(); event.preventDefault(); }
                    if (event.key === 'Delete') { select(card.id); remove(); event.preventDefault(); }
                },
            }, [
                el('td', { class: 'card-text', text: card.question }),
                el('td', { class: 'card-text', text: card.answer }),
                el('td', { class: 'card-count', dataset: { label: 'Success' }, text: String(card.successCount) }),
                el('td', { class: 'card-count', dataset: { label: 'Failure' }, text: String(card.failureCount) }),
                el('td', { class: 'card-when', dataset: { label: 'Last attempt' },
                    text: lastAttemptText(card.lastAttempt) }),
            ]));
        }
        if (!cards.length) {
            body.appendChild(el('tr', {}, [el('td', { colspan: '5', class: 'empty', text: 'No cards yet.' })]));
        }
        select(selectedId);
    }

    async function reload(selectId = selectedId) {
        cards = await api.itemCards(itemId);
        selectedId = selectId;
        render();
    }

    async function add() {
        const saved = await cardDialog(itemId, null);
        if (saved) await reload(saved.id);
    }

    async function edit() {
        const card = selected();
        if (!card) return;
        const saved = await cardDialog(itemId, card);
        if (saved) await reload(saved.id);
    }

    async function remove() {
        const card = selected();
        if (!card) return;
        const question = card.question.replace(/\s+/g, ' ').trim();
        const shown = question.length > 80 ? `${question.slice(0, 79)}…` : question;
        if (!await confirmDialog('Delete card', `Delete the card '${shown}'?`, { danger: true })) return;
        try {
            await api.deleteCard(card.id);
            await reload(null);
        } catch (error) {
            if (!error.isUnauthorized) await errorDialog(error.message);
        }
    }

    async function quiz() {
        await openCardQuiz({ itemId, depth: 0 });
        // The quiz moved the counts.
        await reload();
    }

    render();
    await openDialog({
        title: 'Cards',
        body: el('div', { class: 'cards' }, [
            el('h3', { class: 'cards-item-title', text: formatItemTitle(loaded.item.title, loaded.item.disambiguation) }),
            el('p', { class: 'hint', text: 'Questions to ask yourself about this item. Each change is saved at once; '
                + 'the counts are kept by the quiz.' }),
            el('div', { class: 'scroll-area' }, [table]),
            el('div', { class: 'list-editor-actions' }, [buttons.add, buttons.edit, buttons.remove, buttons.quiz]),
        ]),
        wide: true,
        showAccept: false,
        cancelLabel: 'Close',
        initialFocus: buttons.add,
    });
}

// A quiz over the cards of an item (depth 0) or of its neighbourhood (1 to
// 3 links away): the question, Show answer (Space), then Yes (Y) or No (N).
export function openCardQuiz({ itemId, depth = 0 }) {
    let session = new QuizSession([]);
    let loading = 0;
    const scopeName = `quiz-scope-${itemId}-${Date.now()}`;
    const thisItem = el('input', { type: 'radio', name: scopeName, value: 'item', id: `${scopeName}-item` });
    const neighbourhood = el('input', { type: 'radio', name: scopeName, value: 'around', id: `${scopeName}-around` });
    const depthSelect = el('select', { 'aria-label': 'Depth', class: 'quiz-depth' });
    fillSelect(depthSelect, [
        { value: 1, label: '1 link away' },
        { value: 2, label: '2 links away' },
        { value: 3, label: '3 links away' },
    ], depth > 0 ? Math.min(depth, 3) : 2); // The graph's default depth.
    thisItem.checked = !(depth > 0);
    neighbourhood.checked = depth > 0;
    const scopeSummary = el('span', { class: 'hint quiz-scope-summary' });
    const truncated = el('p', { class: 'hint quiz-truncated', hidden: true,
        text: `The neighborhood has more items than a quiz covers; the ${QUIZ_ITEMS} nearest are included.` });

    const source = el('p', { class: 'quiz-source' });
    const progress = el('span', { class: 'quiz-progress' });
    const question = el('p', { class: 'quiz-text quiz-question' });
    const showButton = button('Show answer', { class: 'primary quiz-show', title: 'Space' });
    const answer = el('p', { class: 'quiz-text quiz-answer', tabindex: '-1' });
    const yesButton = button('Yes', { class: 'primary quiz-yes', title: 'Y' });
    const noButton = button('No', { class: 'secondary quiz-no', title: 'N' });
    const answerBox = el('div', { class: 'quiz-answer-box', hidden: true }, [
        el('h4', { class: 'quiz-heading', text: 'Answer' }),
        answer,
        el('div', { class: 'quiz-verdict' }, [el('span', { text: 'Do you know?' }), yesButton, noButton]),
    ]);
    const card = el('section', { class: 'quiz-card', hidden: true }, [
        el('div', { class: 'quiz-top' }, [source, progress]),
        el('h4', { class: 'quiz-heading', text: 'Question' }),
        question,
        el('div', {}, [showButton]),
        answerBox,
    ]);
    const message = el('p', { class: 'quiz-done', role: 'status' });
    const againButton = button('Start again', { class: 'secondary', onclick: () => load() });
    const done = el('section', { class: 'quiz-end', hidden: true }, [message, againButton]);
    const status = el('p', { class: 'dialog-error', role: 'alert', hidden: true });

    function fail(text) {
        status.textContent = text || '';
        status.hidden = !text;
    }

    const depthNow = () => (neighbourhood.checked ? Number.parseInt(depthSelect.value, 10) || 2 : 0);

    function render() {
        depthSelect.disabled = !neighbourhood.checked;
        if (session.finished) {
            card.hidden = true;
            done.hidden = false;
            message.textContent = session.cards.length ? session.summary : 'No cards are available for this quiz.';
            againButton.hidden = !session.cards.length;
            return;
        }
        const current = session.current;
        done.hidden = true;
        card.hidden = false;
        source.textContent = `Item: ${current.itemTitle}`;
        progress.textContent = session.progress;
        question.textContent = current.question;
        answerBox.hidden = !session.revealed;
        showButton.hidden = session.revealed;
        answer.textContent = session.revealed ? current.answer : '';
        yesButton.disabled = !session.canAnswer;
        noButton.disabled = !session.canAnswer;
    }

    async function load() {
        const ticket = ++loading;
        fail('');
        try {
            const quiz = await api.quizCards(itemId, depthNow(), QUIZ_ITEMS);
            if (ticket !== loading) return; // A later scope was chosen meanwhile.
            session = new QuizSession(quiz.cards);
            scopeSummary.textContent = `${quiz.cards.length} card(s) from ${quiz.itemCount} item(s)`;
            truncated.hidden = !quiz.truncated;
        } catch (error) {
            if (ticket !== loading) return;
            session = new QuizSession([]);
            scopeSummary.textContent = '';
            if (!error.isUnauthorized) fail(error.message);
        }
        render();
        if (!session.finished) showButton.focus();
    }

    function reveal() {
        // Seeing the answer records nothing; only Yes or No does.
        if (!session.reveal()) return;
        render();
        answer.focus();
    }

    async function respond(knew) {
        if (!session.begin()) return;
        render();
        fail('');
        const current = session.current;
        try {
            const stored = await api.attemptCard(current.id, knew);
            session.accept(knew, stored);
        } catch (error) {
            // A card deleted meanwhile cannot be answered; the quiz goes on.
            if (error.status === 404) session.skip();
            else session.fail();
            if (!error.isUnauthorized) fail(error.message);
        }
        render();
        if (!session.finished) showButton.focus();
    }

    showButton.addEventListener('click', reveal);
    yesButton.addEventListener('click', () => respond(true));
    noButton.addEventListener('click', () => respond(false));
    for (const control of [thisItem, neighbourhood, depthSelect]) control.addEventListener('change', load);

    const body = el('div', { class: 'quiz' }, [
        el('div', { class: 'quiz-scope' }, [
            el('span', { text: 'Cards of:' }),
            el('label', { for: thisItem.id }, [thisItem, 'This item']),
            el('label', { for: neighbourhood.id }, [neighbourhood, 'Neighborhood']),
            depthSelect,
            el('span', { class: 'spacer' }),
            scopeSummary,
        ]),
        truncated,
        card,
        done,
        status,
        el('p', { class: 'hint', text: 'Space shows the answer; Y and N answer. '
            + "Only Yes and No count, on the card - never on the item's review." }),
    ]);
    // A key held down answers once, and a key in the scope controls is theirs.
    body.addEventListener('keydown', (event) => {
        if (event.repeat || event.altKey || event.ctrlKey || event.metaKey) return;
        if (event.target.matches('input, select, textarea')) return;
        const key = event.key.toLowerCase();
        if (key === ' ') {
            event.preventDefault();
            reveal();
        } else if (key === 'y' || key === 'n') {
            event.preventDefault();
            respond(key === 'y');
        }
    });
    load();
    return openDialog({
        title: 'Card quiz',
        body,
        wide: true,
        showAccept: false,
        cancelLabel: 'Close',
        initialFocus: showButton,
    });
}
