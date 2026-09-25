import { api } from './api.js';
import { errorDialog, field, openDialog } from './dialogs.js';
import { button, clear, el } from './utils.js';

export function changePasswordDialog(onChanged) {
    const current = el('input', { type: 'password', autocomplete: 'current-password', required: true });
    const next = el('input', { type: 'password', autocomplete: 'new-password', required: true });
    const confirm = el('input', { type: 'password', autocomplete: 'new-password', required: true });
    return openDialog({
        title: 'Change password',
        acceptLabel: 'Change password',
        initialFocus: current,
        body: el('div', {}, [
            field('Current password:', current),
            field('New password:', next, 'At least 12 characters.'),
            field('Repeat new password:', confirm),
        ]),
        onAccept: async ({ fail }) => {
            if (!current.value || next.value.length < 12 || next.value !== confirm.value) {
                fail(next.value !== confirm.value ? 'The new passwords do not match.'
                    : 'Enter the current password and a new password of at least 12 characters.');
                return undefined;
            }
            await api.changePassword(current.value, next.value);
            current.value = next.value = confirm.value = '';
            await onChanged();
            return true;
        },
    });
}

export async function openSessions() {
    const sessions = await api.sessions();
    const list = el('div', { class: 'session-list' });
    const render = () => {
        clear(list);
        for (const session of sessions) {
            const created = new Date(session.createdAtSeconds * 1000).toLocaleString();
            const lastSeen = new Date(session.lastSeenSeconds * 1000).toLocaleString();
            const line = el('div', { class: 'session-row' }, [
                el('span', { text: `${session.current ? 'This session' : 'Session'} · opened ${created} · last used ${lastSeen}` }),
            ]);
            if (!session.current) line.append(button('Revoke', {
                class: 'secondary',
                onclick: async () => {
                    try {
                        await api.revokeSession(session.id);
                        sessions.splice(sessions.indexOf(session), 1);
                        render();
                    } catch (error) {
                        await errorDialog(error.message || String(error));
                    }
                },
            }));
            list.append(line);
        }
    };
    render();
    return openDialog({ title: 'Signed-in sessions', body: list,
        showAccept: false, cancelLabel: 'Close' });
}
