// Unsaved item edits outlive the tab. A phone browser discards background tabs
// freely - switching to a PDF reader is enough - and everything typed since the
// last save would go with it. A draft stays in localStorage until the edit is
// saved or deliberately cancelled; logging out removes every draft.
import { readLocal, removeLocal, writeLocal } from './utils.js';

const KEY = 'lexicon.web.drafts';

// Drafts belong to one user on one server, so a shared browser never offers
// one account's notes to another.
let owner = '';

export function setDraftOwner(baseUrl, username) {
    owner = baseUrl && username ? `${username} @ ${baseUrl}` : '';
}

function readAll() {
    try {
        const parsed = JSON.parse(readLocal(KEY, '{}'));
        return parsed && typeof parsed === 'object' ? parsed : {};
    } catch (error) {
        return {};
    }
}

function writeAll(drafts) {
    if (Object.keys(drafts).length) writeLocal(KEY, JSON.stringify(drafts));
    else removeLocal(KEY);
}

// One slot per item, and one for the item being added.
function slot(itemId) {
    return `${owner}#${itemId || 'new'}`;
}

export function readDraft(itemId) {
    return owner ? readAll()[slot(itemId)] || null : null;
}

export function latestDraft() {
    if (!owner) return null;
    const mine = Object.entries(readAll())
        .filter(([key]) => key.startsWith(`${owner}#`))
        .map(([, draft]) => draft)
        .sort((left, right) => right.savedAt - left.savedAt);
    return mine[0] || null;
}

function writeDraft(itemId, state) {
    if (!owner) return;
    const drafts = readAll();
    drafts[slot(itemId)] = { itemId: itemId || null, savedAt: Date.now(), state };
    writeAll(drafts);
}

export function clearDraft(itemId) {
    if (!owner) return;
    const drafts = readAll();
    delete drafts[slot(itemId)];
    writeAll(drafts);
}

export function clearAllDrafts() {
    removeLocal(KEY);
}

// Writes the editor's state whenever it changes, checked every second and
// once more when the page is hidden, which on a phone is the last moment the
// page is sure to run. An untouched editor leaves no draft behind.
export function keepDraft({ itemId, snapshot, restored }) {
    const initial = JSON.stringify(snapshot());
    // A restored draft differs from the server even before any change.
    let written = restored ? null : initial;
    const persist = () => {
        const state = snapshot();
        const text = JSON.stringify(state);
        if (text === written) return;
        written = text;
        if (text === initial && !restored) clearDraft(itemId);
        else writeDraft(itemId, state);
    };
    const timer = window.setInterval(persist, 1000);
    const onVisibility = () => {
        if (document.visibilityState === 'hidden') persist();
    };
    document.addEventListener('visibilitychange', onVisibility);
    window.addEventListener('pagehide', persist);
    return {
        // keep: the editor closed without its changes reaching the server.
        stop(keep) {
            window.clearInterval(timer);
            document.removeEventListener('visibilitychange', onVisibility);
            window.removeEventListener('pagehide', persist);
            if (keep) persist();
            else clearDraft(itemId);
        },
    };
}
