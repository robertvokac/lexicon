// The web counterpart of AlarmsDialog: every alarm, the soonest first, to add,
// change and delete. Times are edited in local time and stored in UTC.
import { api } from './api.js';
import { confirmDialog, errorDialog, field, openDialog } from './dialogs.js';
import { notificationPermission, requestNotifications } from './alarmbell.js';
import { formatAlarmTime, hasGoneOff, localInputToUtc, nextFullHour, utcToLocalInput } from './alarmtime.js';
import { button, clear, el } from './utils.js';

function alarmDialog(alarm) {
    const title = el('input', { type: 'text', value: alarm.title || '', required: true, id: 'alarm-title' });
    const firesAt = el('input', { type: 'datetime-local', id: 'alarm-fires-at',
        value: utcToLocalInput(alarm.firesAt || nextFullHour()) });
    const description = el('textarea', { rows: '5', id: 'alarm-description' });
    description.value = alarm.description || '';

    return openDialog({
        title: alarm.id ? 'Edit alarm' : 'Add alarm',
        body: el('div', {}, [
            field('Title:', title),
            field('Goes off:', firesAt, 'In your local time.'),
            field('Description:', description),
        ]),
        initialFocus: title,
        onAccept: async ({ fail }) => {
            if (!title.value.trim()) {
                fail('Enter a title.');
                return undefined;
            }
            const utc = localInputToUtc(firesAt.value);
            if (!utc) {
                fail('Choose when the alarm goes off.');
                return undefined;
            }
            const values = { title: title.value.trim(), description: description.value, firesAt: utc };
            // A refused save keeps the dialog open with the reason.
            return alarm.id ? api.updateAlarm(alarm.id, values) : api.createAlarm(values);
        },
    });
}

// The line under the table about how alarms ring in this browser.
function notificationLine() {
    const line = el('p', { class: 'hint alarm-notifications' });
    const render = () => {
        clear(line);
        const permission = notificationPermission();
        if (permission === 'granted') {
            line.textContent = 'Alarms ring on this page and as system notifications while Lexicon is open.';
        } else if (permission === 'default') {
            line.append('Alarms ring on this page while Lexicon is open. ', button('Allow notifications', {
                class: 'secondary',
                onclick: async () => { await requestNotifications(); render(); },
            }));
        } else {
            line.textContent = permission === 'denied'
                ? 'Alarms ring on this page while Lexicon is open; this browser blocks notifications from it.'
                : 'Alarms ring on this page while Lexicon is open.';
        }
    };
    render();
    return line;
}

export async function openAlarms({ onChange } = {}) {
    let alarms = await api.alarms();
    let selectedId = null;

    const body = el('tbody');
    const summary = el('p', { class: 'hint alarm-summary' });
    const buttons = {
        add: button('Add...', { class: 'secondary', onclick: () => add() }),
        edit: button('Edit...', { class: 'secondary', onclick: () => edit() }),
        remove: button('Delete', { class: 'secondary', onclick: () => remove() }),
    };
    const table = el('table', { class: 'value-table alarm-table' }, [
        el('thead', {}, [el('tr', {}, [
            el('th', { text: 'Goes off' }),
            el('th', { text: 'Title' }),
            el('th', { text: 'Description' }),
        ])]),
        body,
    ]);

    const selected = () => alarms.find((alarm) => alarm.id === selectedId);

    // Marks the row without rebuilding the table, so a double click still
    // lands on the row it started on.
    function select(id) {
        selectedId = id;
        for (const row of body.querySelectorAll('tr[data-id]')) {
            const chosen = Number(row.dataset.id) === id;
            row.classList.toggle('selected', chosen);
            row.setAttribute('aria-selected', chosen ? 'true' : 'false');
        }
        buttons.edit.disabled = !selected();
        buttons.remove.disabled = !selected();
    }

    function render() {
        clear(body);
        const now = new Date();
        for (const alarm of alarms) {
            const gone = hasGoneOff(alarm.firesAt, now);
            const ringing = gone && !alarm.dismissedAt;
            const row = el('tr', {
                class: ringing ? 'ringing' : gone ? 'past' : '',
                tabindex: '0',
                title: ringing ? 'Ringing' : gone ? 'Already gone off' : null,
                dataset: { id: String(alarm.id) },
                onclick: () => select(alarm.id),
                onfocus: () => select(alarm.id),
                ondblclick: () => { select(alarm.id); edit(); },
                onkeydown: (event) => {
                    if (event.key === 'Enter') { select(alarm.id); edit(); event.preventDefault(); }
                    if (event.key === 'Delete') { select(alarm.id); remove(); event.preventDefault(); }
                },
            }, [
                el('td', { class: 'alarm-when', text: formatAlarmTime(alarm.firesAt) }),
                el('td', { text: alarm.title }),
                el('td', { class: 'alarm-description', text: alarm.description.split('\n')[0],
                    title: alarm.description || null }),
            ]);
            body.appendChild(row);
        }
        if (!alarms.length) {
            body.appendChild(el('tr', {}, [el('td', { colspan: '3', class: 'empty', text: 'No alarms yet.' })]));
        }
        const upcoming = alarms.filter((alarm) => !hasGoneOff(alarm.firesAt, now)).length;
        summary.textContent = alarms.length ? `${alarms.length} alarm(s), ${upcoming} still to go off` : '';
        select(selectedId);
    }

    async function reload(selectId = selectedId) {
        alarms = await api.alarms();
        selectedId = selectId;
        render();
        if (onChange) onChange();
    }

    async function add() {
        const saved = await alarmDialog({});
        if (saved) await reload(saved.id);
    }

    async function edit() {
        const alarm = selected();
        if (!alarm) return;
        const saved = await alarmDialog(alarm);
        if (saved) await reload(saved.id);
    }

    async function remove() {
        const alarm = selected();
        if (!alarm) return;
        if (!await confirmDialog('Delete alarm', `Delete the alarm '${alarm.title}'?`, { danger: true })) return;
        try {
            await api.deleteAlarm(alarm.id);
            await reload(null);
        } catch (error) {
            await errorDialog(error.message);
        }
    }

    render();
    await openDialog({
        title: 'Alarms',
        body: el('div', { class: 'alarms' }, [
            el('div', { class: 'scroll-area' }, [table]),
            summary,
            el('div', { class: 'list-editor-actions' }, [buttons.add, buttons.edit, buttons.remove]),
            notificationLine(),
        ]),
        wide: true,
        showAccept: false,
        cancelLabel: 'Close',
    });
}
