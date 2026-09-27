// Spreadsheet-like creation of many items. The worksheet is backed up in
// localStorage after every edit and after each successful insert, so a closed
// tab or a failure halfway through leaves only the rows still to be created.
import { api } from './api.js';
import { confirmDialog, field, messageDialog, openDialog } from './dialogs.js';
import {
    clearMassInsertDraft, readMassInsertDraft, writeMassInsertDraft,
} from './drafts.js';
import {
    button, clear, el, fillSelect, ITEM_STATUSES, LITERAL_TEXT, typeDisplayName,
    UNDERSTANDING_LEVELS,
} from './utils.js';

const BASE_HEADERS = [
    'Title', 'Disambiguation', 'Aliases', 'Tags', 'Flags', 'Status',
    'Understanding', 'Pinned', 'Content', 'Properties',
];

function emptyRow() {
    return {
        title: '', disambiguation: '', aliases: '', tags: '', flags: '',
        status: 'None', understanding: 'Unknown', pinned: false, content: '',
        properties: '', fieldValues: {},
    };
}

function meaningful(row) {
    return Boolean(
        row.title.trim() || row.disambiguation.trim() || row.aliases.trim()
        || row.tags.trim() || row.flags.trim() || row.content
        || row.properties.trim() || row.status !== 'None'
        || row.understanding !== 'Unknown' || row.pinned
        || Object.values(row.fieldValues || {}).some((value) => String(value).trim()),
    );
}

function splitValues(text) {
    const seen = new Set();
    const result = [];
    for (const raw of String(text || '').split(/[\n,]/)) {
        const value = raw.trim();
        const key = value.toLowerCase();
        if (value && !seen.has(key)) {
            seen.add(key);
            result.push(value);
        }
    }
    return result;
}

function parseProperties(text, rowNumber) {
    const result = [];
    const seen = new Set();
    for (const raw of String(text || '').split(/[\n;]/)) {
        if (!raw.trim()) continue;
        const equals = raw.indexOf('=');
        const key = equals >= 0 ? raw.slice(0, equals).trim() : '';
        const value = equals >= 0 ? raw.slice(equals + 1).trim() : '';
        const folded = key.toLowerCase();
        if (!key) throw new Error(`Row ${rowNumber}: properties must use key=value.`);
        if (seen.has(folded)) throw new Error(`Row ${rowNumber}: property '${key}' occurs twice.`);
        seen.add(folded);
        result.push({ key, value });
    }
    return result;
}

function textControl(value, { multiline = false, placeholder = '' } = {}) {
    if (multiline) {
        const control = el('textarea', { rows: '2', placeholder, ...LITERAL_TEXT });
        control.value = value || '';
        return control;
    }
    return el('input', { type: 'text', value: value || '', placeholder, ...LITERAL_TEXT });
}

function fieldControl(fieldRecord, value) {
    if (fieldRecord.dataType === 'Boolean') {
        const control = el('select');
        fillSelect(control, [
            { value: '', label: 'Not set' },
            { value: 'true', label: 'Yes' },
            { value: 'false', label: 'No' },
        ], value || '');
        return control;
    }
    if (fieldRecord.dataType === 'Enum') {
        const control = el('select');
        fillSelect(control, [
            { value: '', label: 'Not set' },
            ...(fieldRecord.enumOptions || []).map((option) => ({ value: option, label: option })),
        ], value || '');
        return control;
    }
    const attributes = { value: value || '', ...LITERAL_TEXT };
    if (fieldRecord.dataType === 'Integer') return el('input', { ...attributes, type: 'number', step: '1' });
    if (fieldRecord.dataType === 'Float') return el('input', { ...attributes, type: 'number', step: 'any' });
    if (fieldRecord.dataType === 'Date') return el('input', { ...attributes, type: 'date' });
    if (fieldRecord.dataType === 'Time') return el('input', { ...attributes, type: 'time', step: '1' });
    if (fieldRecord.dataType === 'Timestamp') {
        return el('input', { ...attributes, type: 'datetime-local', step: '1' });
    }
    if (fieldRecord.dataType === 'Text') return textControl(value, { multiline: true });
    return textControl(value, {
        placeholder: fieldRecord.dataType === 'Blob' ? 'SHA-256'
            : fieldRecord.dataType === 'Image' ? 'image/type:SHA-256' : '',
    });
}

async function chooseScope(groups) {
    const group = el('select');
    const type = el('select');
    fillSelect(group, [
        { value: '', label: 'Choose a group' },
        ...groups.map((entry) => ({ value: entry.id, label: entry.name })),
    ], '');
    fillSelect(type, [{ value: '', label: 'No type' }], '');
    let availableTypes = [];
    let loadingTypes = false;

    async function refreshTypes() {
        const selected = group.value;
        fillSelect(type, [{ value: '', label: selected ? 'Loading...' : 'No type' }], '');
        if (!selected) return;
        loadingTypes = true;
        try {
            availableTypes = await api.types(Number(selected));
            fillSelect(type, [
                { value: '', label: 'No type' },
                ...availableTypes.map((entry) => ({ value: entry.id, label: typeDisplayName(entry) })),
            ], '');
        } finally {
            loadingTypes = false;
        }
    }
    group.addEventListener('change', () => refreshTypes());

    return openDialog({
        title: 'Mass Insert',
        body: el('div', {}, [
            field('Group:', group, 'Required for every new item.'),
            field('Type:', type, 'Optional. Choosing one adds a column for each field.'),
        ]),
        acceptLabel: 'Continue',
        initialFocus: group,
        onAccept: ({ fail }) => {
            if (!group.value) {
                fail('Choose a Group.');
                return undefined;
            }
            if (loadingTypes) {
                fail('Wait until Types are loaded.');
                return undefined;
            }
            const typeId = type.value ? Number(type.value) : null;
            return {
                groupId: Number(group.value),
                typeId,
                typeRecord: availableTypes.find((entry) => entry.id === typeId) || null,
            };
        },
    });
}

async function worksheet({ groupId, typeId, groupName, typeName, fields, initialRows }) {
    const table = el('table', { class: 'value-table mass-insert-table' });
    const body = el('tbody');
    const backup = el('span', { class: 'hint mass-insert-backup', role: 'status' });
    let controls = [];

    function readRows() {
        return controls.map((row) => row.read());
    }

    function persist() {
        const rows = readRows();
        if (!rows.some(meaningful)) {
            clearMassInsertDraft();
            backup.textContent = 'No unsaved rows.';
            return;
        }
        const saved = writeMassInsertDraft({ version: 1, groupId, typeId, rows });
        backup.textContent = saved
            ? `Draft backed up locally at ${new Date().toLocaleTimeString()}.`
            : 'Draft backup is unavailable in this browser.';
    }

    function watched(control) {
        control.addEventListener('input', persist);
        control.addEventListener('change', persist);
        return control;
    }

    function renderRows(rows) {
        clear(body);
        controls = [];
        rows.forEach((source, index) => {
            const row = { ...emptyRow(), ...source, fieldValues: { ...(source.fieldValues || {}) } };
            const title = watched(textControl(row.title));
            const disambiguation = watched(textControl(row.disambiguation));
            const aliases = watched(textControl(row.aliases, { placeholder: 'Comma separated' }));
            const tags = watched(textControl(row.tags, { placeholder: 'Comma separated' }));
            const flags = watched(textControl(row.flags, { placeholder: 'Comma separated' }));
            const status = watched(el('select'));
            fillSelect(status, ITEM_STATUSES, row.status);
            const understanding = watched(el('select'));
            fillSelect(understanding, UNDERSTANDING_LEVELS, row.understanding);
            const pinned = watched(el('input', { type: 'checkbox', checked: Boolean(row.pinned) }));
            const content = watched(textControl(row.content, { multiline: true, placeholder: 'Markdown' }));
            const properties = watched(textControl(row.properties, {
                multiline: true, placeholder: 'key=value; key=value',
            }));
            const valueControls = fields.map((fieldRecord) => watched(
                fieldControl(fieldRecord, row.fieldValues[String(fieldRecord.id)] || ''),
            ));
            const remove = button('Remove', {
                class: 'secondary',
                title: `Remove row ${index + 1}`,
                onclick: () => {
                    const current = readRows();
                    current.splice(index, 1);
                    renderRows(current.length ? current : [emptyRow()]);
                    persist();
                },
            });
            controls.push({
                read: () => ({
                    title: title.value,
                    disambiguation: disambiguation.value,
                    aliases: aliases.value,
                    tags: tags.value,
                    flags: flags.value,
                    status: status.value,
                    understanding: understanding.value,
                    pinned: pinned.checked,
                    content: content.value,
                    properties: properties.value,
                    fieldValues: Object.fromEntries(fields.map((entry, fieldIndex) => [
                        String(entry.id), valueControls[fieldIndex].value,
                    ])),
                }),
            });
            body.appendChild(el('tr', {}, [
                el('td', {}, [title]),
                el('td', {}, [disambiguation]),
                el('td', {}, [aliases]),
                el('td', {}, [tags]),
                el('td', {}, [flags]),
                el('td', {}, [status]),
                el('td', {}, [understanding]),
                el('td', { class: 'mass-insert-check' }, [pinned]),
                el('td', {}, [content]),
                el('td', {}, [properties]),
                ...valueControls.map((control) => el('td', {}, [control])),
                el('td', {}, [remove]),
            ]));
        });
    }

    const header = el('tr', {}, [
        ...BASE_HEADERS.map((name) => el('th', { text: name })),
        ...fields.map((entry) => el('th', {
            text: entry.name,
            title: `${entry.dataType}${entry.description ? ` — ${entry.description}` : ''}`,
        })),
        el('th', { text: 'Actions' }),
    ]);
    table.append(el('thead', {}, [header]), body);
    renderRows(initialRows && initialRows.length ? initialRows : [emptyRow()]);

    const add = button('Add row', {
        class: 'secondary',
        onclick: () => {
            const rows = readRows();
            rows.push(emptyRow());
            renderRows(rows);
            persist();
            const lastTitle = body.lastElementChild?.querySelector('input');
            if (lastTitle) lastTitle.focus();
        },
    });
    const scope = `${groupName}${typeName ? ` · ${typeName}` : ' · No type'}`;
    const result = await openDialog({
        title: 'Mass Insert',
        body: el('div', { class: 'mass-insert' }, [
            el('p', { class: 'hint', text: `New items: ${scope}` }),
            el('p', { class: 'hint', text: 'Lists use commas. Properties use key=value separated by semicolons or new lines.' }),
            el('div', { class: 'mass-insert-scroll' }, [table]),
            el('div', { class: 'mass-insert-tools' }, [add, backup]),
        ]),
        acceptLabel: 'Insert items',
        cancelLabel: 'Close',
        wide: true,
        className: 'dialog-mass-insert',
        closeOnBackdrop: false,
        extraActions: [{
            label: 'Discard draft',
            onClick: async ({ close }) => {
                const discard = await confirmDialog(
                    'Discard Mass Insert draft',
                    'Discard every unfinished row in this worksheet?',
                    { acceptLabel: 'Discard', danger: true },
                );
                if (discard) {
                    clearMassInsertDraft();
                    close(0);
                }
            },
        }],
        onAccept: async ({ fail }) => {
            let remaining = readRows().filter(meaningful);
            if (!remaining.length) {
                fail('Add at least one item.');
                return undefined;
            }
            for (let index = 0; index < remaining.length; ++index) {
                if (!remaining[index].title.trim()) {
                    fail(`Row ${index + 1}: Title is required.`);
                    return undefined;
                }
                // Validate property syntax before inserting anything.
                parseProperties(remaining[index].properties, index + 1);
            }
            renderRows(remaining);
            persist();
            let inserted = 0;
            while (remaining.length) {
                const row = remaining[0];
                const fieldValues = {};
                for (const entry of fields) {
                    const value = String(row.fieldValues[String(entry.id)] || '').trim();
                    if (value) fieldValues[String(entry.id)] = value;
                }
                const payload = {
                    item: {
                        groupId,
                        itemTypeId: typeId,
                        title: row.title.trim(),
                        disambiguation: row.disambiguation.trim(),
                        aliases: splitValues(row.aliases),
                        tags: splitValues(row.tags),
                        flags: splitValues(row.flags),
                        status: row.status,
                        understanding: row.understanding,
                        pinned: row.pinned,
                        content: row.content,
                        properties: parseProperties(row.properties, inserted + 1),
                        fieldValues,
                    },
                    links: [],
                    backlinks: [],
                };
                await api.createItem(payload);
                inserted += 1;
                remaining = remaining.slice(1);
                renderRows(remaining.length ? remaining : [emptyRow()]);
                if (remaining.length) persist();
                else clearMassInsertDraft();
            }
            return inserted;
        },
    });
    // Every input event already persisted. This final snapshot catches an
    // edit immediately followed by Escape or the Close button.
    if (result === null) persist();
    return result || 0;
}

export async function openMassInsert() {
    const groups = await api.groups();
    if (!groups.length) {
        await messageDialog('Mass Insert', 'Create a Group before inserting items.');
        return false;
    }

    let draft = readMassInsertDraft();
    if (draft) {
        const validGroup = groups.some((entry) => entry.id === draft.groupId);
        let validType = !draft.typeId;
        if (validGroup && draft.typeId) {
            validType = (await api.types(draft.groupId)).some((entry) => entry.id === draft.typeId);
        }
        if (!validGroup || !validType) {
            await messageDialog('Mass Insert', 'The saved draft refers to a Group or Type that no longer exists and cannot be resumed.');
            clearMassInsertDraft();
            draft = null;
        } else {
            const choice = await openDialog({
                title: 'Resume Mass Insert',
                body: el('p', {
                    text: `Resume ${draft.rows?.length || 0} locally backed-up row(s)?`,
                }),
                acceptLabel: 'Resume',
                cancelLabel: 'Cancel',
                extraActions: [{
                    label: 'Start over',
                    class: 'danger',
                    onClick: ({ close }) => close('start-over'),
                }],
                onAccept: () => 'resume',
            });
            if (choice === null) return false;
            if (choice === 'start-over') {
                clearMassInsertDraft();
                draft = null;
            }
        }
    }

    let scope;
    if (draft) {
        const availableTypes = draft.typeId ? await api.types(draft.groupId) : [];
        scope = {
            groupId: draft.groupId,
            typeId: draft.typeId || null,
            typeRecord: availableTypes.find((entry) => entry.id === draft.typeId) || null,
        };
    } else {
        scope = await chooseScope(groups);
        if (!scope) return false;
    }
    const fields = scope.typeId ? await api.fields(scope.typeId) : [];
    const inserted = await worksheet({
        ...scope,
        fields,
        groupName: groups.find((entry) => entry.id === scope.groupId)?.name || `Group #${scope.groupId}`,
        typeName: scope.typeRecord?.name || '',
        initialRows: draft?.rows || [emptyRow()],
    });
    return inserted > 0;
}
