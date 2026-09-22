// Review: the items due now, one card at a time. The title comes first, the
// content on request, then a rating that moves the understanding and decides
// when the item is due again - the same rules as the desktop and the server.
import { api } from './api.js';
import { openDialog } from './dialogs.js';
import { bindItemLinks, renderMarkdown } from './markdown.js';
import { button, clear, el, fillSelect, formatItemTitle, understandingLabel } from './utils.js';

const LEVELS = ['Unknown', 'Recognized', 'Understood', 'Practiced', 'Mastered'];
const INTERVAL_DAYS = [1, 2, 5, 12, 30];
const RATINGS = [
    { name: 'Again', step: -1 },
    { name: 'Hard', step: 0 },
    { name: 'Good', step: 1 },
    { name: 'Easy', step: 2 },
];
const BATCH = 20;

function levelAfter(current, rating) {
    const index = Math.max(0, LEVELS.indexOf(current));
    return LEVELS[Math.min(LEVELS.length - 1, Math.max(0, index + rating.step))];
}

function days(count) {
    return count === 1 ? '1 day' : `${count} days`;
}

// Resolves with true when a review was recorded, so the list is refreshed.
export async function openReview(groups) {
    let queue = [];
    let reviewed = 0;
    let revealed = false;

    const groupSelect = el('select', { 'aria-label': 'Group' });
    fillSelect(groupSelect, [{ value: '', label: 'All groups' },
        ...groups.map((group) => ({ value: group.id, label: group.name }))], '');
    const dueLabel = el('span', { class: 'hint review-due' });
    const title = el('h3', { class: 'review-title' });
    const details = el('p', { class: 'hint review-details' });
    const content = el('div', { class: 'markdown-preview review-content', hidden: true });
    // Items are opened from the main view; here a link to one does nothing.
    bindItemLinks(content, () => {});
    const showButton = button('Show answer', { class: 'primary' });
    const ratingButtons = RATINGS.map((rating, index) => button(rating.name, {
        class: 'secondary review-rating',
        title: `Key ${index + 1}`,
        onclick: () => rate(rating),
    }));
    const skipButton = button('Skip', { class: 'secondary' });
    const card = el('section', { class: 'review-card' }, [
        title, details, content,
        el('div', { class: 'review-actions' }, [showButton, ...ratingButtons,
            el('span', { class: 'spacer' }), skipButton]),
    ]);
    const doneText = el('p', { class: 'confirm review-done' });
    const continueButton = button('Continue', { class: 'primary', onclick: () => load() });
    const done = el('section', { class: 'review-card', hidden: true }, [doneText, continueButton]);
    const status = el('p', { class: 'dialog-error', role: 'alert', hidden: true });

    function fail(message) {
        status.textContent = message;
        status.hidden = !message;
    }

    function groupId() {
        return Number.parseInt(groupSelect.value, 10) || 0;
    }

    async function load() {
        fail('');
        try {
            const result = await api.reviewQueue(groupId(), BATCH);
            queue = result.items;
            dueLabel.textContent = `${result.dueCount} due`;
            show();
        } catch (error) {
            if (!error.isUnauthorized) fail(error.message);
        }
    }

    async function show() {
        revealed = false;
        if (!queue.length) {
            let due = 0;
            try {
                due = (await api.reviewQueue(groupId(), 1)).dueCount;
            } catch (error) {
                // The count is a courtesy; the summary still stands.
            }
            dueLabel.textContent = `${due} due`;
            doneText.textContent = due > 0
                ? `${reviewed} reviewed. ${due} more item(s) are due.`
                : `${reviewed} reviewed. Nothing else is due now.`;
            continueButton.hidden = due === 0;
            card.hidden = true;
            done.hidden = false;
            return;
        }
        card.hidden = false;
        done.hidden = true;
        const item = queue[0];
        title.textContent = formatItemTitle(item.title, item.disambiguation);
        details.textContent = [item.groupName, item.itemTypeName, (item.tags || []).join(', '),
            understandingLabel(item.understanding),
            item.reviewedAt ? `last reviewed ${item.reviewedAt.slice(0, 10)}` : 'never reviewed',
        ].filter(Boolean).join(' · ');
        clear(content);
        content.hidden = true;
        showButton.hidden = false;
        RATINGS.forEach((rating, index) => {
            const next = levelAfter(item.understanding, rating);
            ratingButtons[index].textContent = `${rating.name} (${days(INTERVAL_DAYS[LEVELS.indexOf(next)])})`;
            ratingButtons[index].title = `Understanding becomes ${next}. Key ${index + 1}.`;
            ratingButtons[index].disabled = true;
        });
        showButton.focus();
    }

    async function reveal() {
        if (!queue.length || revealed) return;
        try {
            const loaded = await api.getItem(queue[0].id);
            if (loaded.item.content) renderMarkdown(content, loaded.item.content);
            else content.appendChild(el('p', { class: 'hint', text: 'This item has no content.' }));
            api.logItemRead(queue[0].id).catch(() => {});
        } catch (error) {
            if (!error.isUnauthorized) fail(error.message);
            return;
        }
        revealed = true;
        content.hidden = false;
        showButton.hidden = true;
        for (const node of ratingButtons) node.disabled = false;
        ratingButtons[2].focus();
    }

    async function rate(rating) {
        if (!queue.length || !revealed) return;
        try {
            const item = await api.reviewItem(queue[0].id, rating.name);
            reviewed += 1;
            queue.shift();
            // Forgotten items come back at the end of this sitting.
            if (rating.name === 'Again') queue.push(item);
            show();
        } catch (error) {
            if (!error.isUnauthorized) fail(error.message);
        }
    }

    showButton.addEventListener('click', reveal);
    skipButton.addEventListener('click', () => { queue.shift(); show(); });
    groupSelect.addEventListener('change', load);
    const body = el('div', { class: 'review' }, [
        el('div', { class: 'review-header' }, [el('label', { text: 'Group:' }), groupSelect,
            el('span', { class: 'spacer' }), dueLabel]),
        card, done, status,
    ]);
    body.addEventListener('keydown', (event) => {
        if (event.target instanceof HTMLSelectElement) return;
        if (event.key === ' ' && !revealed) {
            event.preventDefault();
            reveal();
        } else if (['1', '2', '3', '4'].includes(event.key) && revealed) {
            event.preventDefault();
            rate(RATINGS[Number(event.key) - 1]);
        }
    });
    load();
    await openDialog({
        title: 'Review',
        body,
        wide: true,
        showAccept: false,
        cancelLabel: 'Close',
        initialFocus: showButton,
    });
    return reviewed > 0;
}
