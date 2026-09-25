import { api } from './api.js';
import { confirmDialog, openDialog } from './dialogs.js';
import { el } from './utils.js';

async function chooseVersion(entries, title, trash) {
    const select = el('select', { size: '8', class: 'history-entries' });
    const preview = el('pre', { class: 'history-preview' });
    for (const entry of entries) {
        select.append(el('option', { value: String(entry.id),
            text: `${entry.happenedAt} · ${entry.item.title} · revision ${entry.item.revision}` }));
    }
    const show = () => {
        const entry = entries.find((candidate) => candidate.id === Number(select.value));
        preview.textContent = entry?.item.content || '';
    };
    select.addEventListener('change', show);
    if (entries.length) { select.selectedIndex = 0; show(); }
    return openDialog({
        title,
        body: el('div', { class: 'item-history' }, [
            entries.length ? select : el('p', { text: trash ? 'Trash is empty.' : 'No older versions yet.' }),
            preview,
        ]),
        showAccept: entries.length > 0,
        acceptLabel: trash ? 'Restore item' : 'Restore version',
        cancelLabel: 'Close',
        onAccept: async ({ fail }) => {
            const entry = entries.find((candidate) => candidate.id === Number(select.value));
            if (!entry) { fail('Select a version.'); return undefined; }
            if (!trash && !await confirmDialog('Restore version',
                'Replace the current item and its links with this version?')) return undefined;
            const restored = await api.restoreItemHistory(entry.id);
            return { id: restored.itemId, title: entry.item.title };
        },
    });
}

export async function openItemHistory(itemId) {
    return chooseVersion(await api.itemHistory(itemId), 'Item history', false);
}

export async function openTrash() {
    return chooseVersion(await api.trash(), 'Trash', true);
}
