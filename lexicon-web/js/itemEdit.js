// The web counterpart of ItemEditDialog: General, Content, Values, Metadata,
// Links and Backlinks, saved in one atomic item+links request.
import { api } from './api.js';
import { confirmDialog, errorDialog, field, listEditor, openDialog, promptDialog }
    from './dialogs.js';
import { clearDraft, keepDraft, readDraft } from './drafts.js';
import { renderMarkdown } from './markdown.js';
import {
    button, clear, debounce, el, fillDatalist, fillSelect, formatItemTitle, ITEM_STATUSES,
    LINK_TYPES, linkDescription, LITERAL_TEXT, splitItemTitle, typeDisplayName,
    UNDERSTANDING_LEVELS,
} from './utils.js';

const MARKDOWN_ACTIONS = [
    { label: 'B', title: 'Bold (**)', prefix: '**', suffix: '**', sample: 'bold text' },
    { label: 'I', title: 'Italic (*)', prefix: '*', suffix: '*', sample: 'italic text' },
    { separator: true },
    { label: 'H2', title: 'Header 2 (##)', prefix: '\n## ', sample: 'Header 2' },
    { label: 'H3', title: 'Header 3 (###)', prefix: '\n### ', sample: 'Header 3' },
    { label: 'H4', title: 'Header 4 (####)', prefix: '\n#### ', sample: 'Header 4' },
    { separator: true },
    { label: 'List', title: 'Unordered list (-)', prefix: '\n- ', sample: 'list item' },
    { label: '1.', title: 'Ordered list (1.)', prefix: '\n1. ', sample: 'list item' },
    { label: '"', title: 'Quote (>)', prefix: '\n> ', sample: 'quote' },
    { label: '---', title: 'Horizontal line', prefix: '\n---\n', sample: '' },
    { separator: true },
    { label: 'Code', title: 'Inline code (`)', prefix: '`', suffix: '`', sample: 'code' },
    { label: 'Block', title: 'Code block (```)', codeBlock: true },
    { separator: true },
    { label: 'Link', title: 'Insert link ([])', prefix: '[', suffix: '](https://)', sample: 'link text' },
    { label: 'Table', title: 'Insert table (|)', table: true },
];

function tabs(definitions) {
    const list = el('div', { class: 'tab-list', role: 'tablist' });
    const panels = el('div', { class: 'tab-panels' });
    const buttons = new Map();
    const panelNodes = new Map();

    function select(name) {
        for (const [key, tab] of buttons) {
            const active = key === name;
            tab.classList.toggle('active', active);
            tab.setAttribute('aria-selected', active ? 'true' : 'false');
            tab.tabIndex = active ? 0 : -1;
            panelNodes.get(key).hidden = !active;
        }
    }

    definitions.forEach((definition) => {
        const tab = button(definition.label, {
            class: 'tab',
            role: 'tab',
            'aria-controls': `panel-${definition.name}`,
            onclick: () => select(definition.name),
        });
        const panel = el('div', {
            class: 'tab-panel',
            id: `panel-${definition.name}`,
            role: 'tabpanel',
            hidden: true,
        }, [definition.content]);
        buttons.set(definition.name, tab);
        panelNodes.set(definition.name, panel);
        list.appendChild(tab);
        panels.appendChild(panel);
    });
    select(definitions[0].name);

    list.addEventListener('keydown', (event) => {
        const names = [...buttons.keys()].filter((name) => !buttons.get(name).disabled);
        const current = names.findIndex((name) => buttons.get(name).classList.contains('active'));
        if (event.key === 'ArrowRight' && current < names.length - 1) {
            select(names[current + 1]);
            buttons.get(names[current + 1]).focus();
            event.preventDefault();
        } else if (event.key === 'ArrowLeft' && current > 0) {
            select(names[current - 1]);
            buttons.get(names[current - 1]).focus();
            event.preventDefault();
        }
    });

    return {
        node: el('div', { class: 'tabs' }, [list, panels]),
        select,
        setEnabled(name, enabled) {
            const tab = buttons.get(name);
            tab.disabled = !enabled;
            tab.setAttribute('aria-disabled', enabled ? 'false' : 'true');
            if (!enabled && tab.classList.contains('active')) select(definitions[0].name);
        },
    };
}

function insertMarkdown(textarea, prefix, suffix = '', sample = '') {
    const { selectionStart, selectionEnd, value } = textarea;
    const selected = value.slice(selectionStart, selectionEnd);
    const inserted = selected || sample;
    textarea.value = value.slice(0, selectionStart) + prefix + inserted + suffix
        + value.slice(selectionEnd);
    const start = selectionStart + prefix.length;
    textarea.focus();
    textarea.setSelectionRange(start, start + inserted.length);
    textarea.dispatchEvent(new Event('input', { bubbles: true }));
}

// A Blob field: the browser uploads bytes, the server owns the file system.
function blobEditor(fieldRecord, initialHash, onChange) {
    const value = el('input', {
        type: 'text',
        readonly: true,
        value: initialHash || '',
        placeholder: 'No file selected (SHA-256)',
        class: 'blob-hash',
    });
    const picker = el('input', { type: 'file', class: 'blob-picker', hidden: true });
    const status = el('span', { class: 'hint blob-status' });

    const upload = button('Upload...', {
        class: 'secondary',
        onclick: () => picker.click(),
    });
    picker.addEventListener('change', async () => {
        const file = picker.files && picker.files[0];
        if (!file) return;
        upload.disabled = true;
        status.textContent = `Uploading ${file.name}...`;
        try {
            const hash = await api.uploadBlob(file);
            value.value = hash;
            status.textContent = `${file.name} (${file.size} bytes)`;
            onChange(hash);
        } catch (error) {
            status.textContent = '';
            await errorDialog(error.message);
        } finally {
            upload.disabled = false;
            picker.value = '';
        }
    });

    const download = button('Download', {
        class: 'secondary',
        onclick: async () => {
            if (!value.value) return;
            try {
                const blob = await api.downloadBlob(value.value);
                const url = URL.createObjectURL(blob);
                const anchor = el('a', { href: url, download: `${fieldRecord.name}-${value.value.slice(0, 12)}.bin` });
                document.body.appendChild(anchor);
                anchor.click();
                anchor.remove();
                URL.revokeObjectURL(url);
            } catch (error) {
                await errorDialog(error.message);
            }
        },
    });

    const clearButton = button('Clear', {
        class: 'secondary',
        onclick: () => {
            value.value = '';
            status.textContent = '';
            onChange('');
        },
    });

    return el('div', { class: 'blob-field' }, [
        el('div', { class: 'blob-row' }, [value, download, clearButton, upload]),
        picker,
        status,
    ]);
}

function fieldEditor(fieldRecord, storedValue, onChange) {
    const common = {
        class: 'field-editor', 'data-field-id': String(fieldRecord.id), ...LITERAL_TEXT,
    };
    switch (fieldRecord.dataType) {
    case 'Boolean': {
        const select = el('select', common);
        fillSelect(select, [
            { value: '', label: 'Not set' },
            { value: 'false', label: 'False' },
            { value: 'true', label: 'True' },
        ], storedValue || '');
        select.addEventListener('change', () => onChange(select.value));
        return { node: select, read: () => select.value };
    }
    case 'Enum': {
        const select = el('select', common);
        fillSelect(select, [
            { value: '', label: 'Not set' },
            ...fieldRecord.enumOptions.map((option) => ({ value: option, label: option })),
        ], storedValue || '');
        select.addEventListener('change', () => onChange(select.value));
        return { node: select, read: () => select.value };
    }
    case 'Blob': {
        let current = storedValue || '';
        const node = blobEditor(fieldRecord, current, (hash) => {
            current = hash;
            onChange(hash);
        });
        return { node, read: () => current };
    }
    default: {
        const attributes = { ...common, value: storedValue || '' };
        let input;
        if (fieldRecord.dataType === 'Integer') {
            input = el('input', { ...attributes, type: 'number', step: '1', inputmode: 'numeric' });
        } else if (fieldRecord.dataType === 'Float') {
            input = el('input', { ...attributes, type: 'number', step: 'any' });
        } else if (fieldRecord.dataType === 'Date') {
            input = el('input', { ...attributes, type: 'date', placeholder: 'YYYY-MM-DD' });
        } else if (fieldRecord.dataType === 'Time') {
            input = el('input', { ...attributes, type: 'time', step: '1', placeholder: 'HH:MM:SS' });
        } else if (fieldRecord.dataType === 'Timestamp') {
            // The database stores YYYY-MM-DDTHH:MM(:SS), which is exactly what
            // datetime-local produces.
            input = el('input', { ...attributes, type: 'datetime-local', step: '1' });
        } else if (fieldRecord.dataType === 'Text') {
            input = el('textarea', { ...common, rows: '3' });
            input.value = storedValue || '';
        } else {
            input = el('input', { ...attributes, type: 'text' });
        }
        input.addEventListener('input', () => onChange(input.value));
        return { node: input, read: () => input.value.trim() };
    }
    }
}

async function linkDialog({ title, label, link, itemTitles }) {
    const target = el('input', {
        type: 'text',
        value: link.targetTitle || '',
        list: 'item-title-suggestions',
        autocomplete: 'off',
        ...LITERAL_TEXT,
    });
    const suggestions = el('datalist', { id: 'item-title-suggestions' });
    fillDatalist(suggestions, itemTitles);
    const typeSelect = el('select', {});
    fillSelect(typeSelect, LINK_TYPES, link.linkType || 'Related');
    const customValue = el('input', {
        type: 'text', value: link.customValue || '', ...LITERAL_TEXT,
    });
    const position = el('input', { type: 'number', step: '1', value: String(link.position || 0) });

    const syncCustom = () => { customValue.disabled = typeSelect.value !== 'Custom'; };
    typeSelect.addEventListener('change', syncCustom);
    syncCustom();

    const body = el('div', {}, [
        field(label, target),
        suggestions,
        field('Link type:', typeSelect),
        field('Custom value:', customValue),
        field('Position:', position),
    ]);

    return openDialog({
        title,
        body,
        acceptLabel: 'OK',
        initialFocus: target,
        onAccept: async ({ fail }) => {
            const text = target.value.trim();
            if (!text) {
                fail('Choose a target item.');
                return undefined;
            }
            if (typeSelect.value === 'Custom' && !customValue.value.trim()) {
                fail('Custom links need a value.');
                return undefined;
            }
            const parsed = splitItemTitle(text);
            let itemId;
            try {
                itemId = await api.resolveItem(parsed.title, parsed.disambiguation);
            } catch (error) {
                fail(error.status === 404 ? 'Item not found.' : error.message);
                return undefined;
            }
            return {
                itemId,
                targetTitle: text,
                linkType: typeSelect.value,
                customValue: typeSelect.value === 'Custom' ? customValue.value.trim() : '',
                position: Number.parseInt(position.value, 10) || 0,
            };
        },
    });
}

async function propertyDialog(title, property, existing, skipIndex) {
    const key = el('input', { type: 'text', value: property.key || '', ...LITERAL_TEXT });
    const value = el('input', { type: 'text', value: property.value || '', ...LITERAL_TEXT });
    return openDialog({
        title,
        body: el('div', {}, [field('Key:', key), field('Value:', value)]),
        initialFocus: key,
        onAccept: ({ fail }) => {
            const trimmed = key.value.trim();
            if (!trimmed) {
                fail('Property key cannot be empty.');
                return undefined;
            }
            const duplicate = existing.some((candidate, index) => index !== skipIndex
                && candidate.key.toLowerCase() === trimmed.toLowerCase());
            if (duplicate) {
                fail('Property key must be unique in this item.');
                return undefined;
            }
            return { key: trimmed, value: value.value };
        },
    });
}

// Asks what to do with unsaved changes found in the browser. Resolves with
// 'use', 'discard', or null when the question is put off.
export function askAboutDraft(stored, laterLabel = 'Cancel') {
    const title = (stored.state.item.title || '').trim();
    const name = title ? `"${title}"` : 'a new item';
    const when = new Date(stored.savedAt).toLocaleString();
    return openDialog({
        title: 'Unsaved changes',
        body: el('p', {
            class: 'confirm',
            text: `Your changes to ${name} were never saved (last edited ${when}).`,
        }),
        acceptLabel: 'Continue editing',
        cancelLabel: laterLabel,
        extraActions: [{ label: 'Discard', onClick: ({ close }) => close('discard') }],
        onAccept: () => 'use',
    });
}

// restore: an editor state kept by drafts.js, laid over what the server holds.
// Without one, a draft stored for this item is offered first.
export async function openItemEditor({ itemId, draft, groups, restore }) {
    if (restore === undefined) {
        const stored = readDraft(itemId);
        if (stored) {
            const choice = await askAboutDraft(stored);
            if (choice === null) return null;
            if (choice === 'use') restore = stored.state;
            else clearDraft(itemId);
        }
    }
    let record = draft || {
        id: null,
        groupId: groups.length ? groups[0].id : null,
        itemTypeId: null,
        title: '',
        disambiguation: '',
        status: 'None',
        understanding: 'Unknown',
        pinned: false,
        content: '',
        tags: [],
        flags: [],
        aliases: [],
        properties: [],
        fieldValues: {},
    };
    let links = [];
    let backlinks = [];
    if (itemId) {
        const loaded = await api.getItem(itemId, ['links', 'backlinks']);
        record = loaded.item;
        links = loaded.links.map((link) => ({
            id: link.id,
            itemId: link.toItemId,
            targetTitle: link.toItemTitle,
            linkType: link.linkType,
            customValue: link.customValue,
            position: link.position,
        }));
        backlinks = loaded.backlinks.map((link) => ({
            id: link.id,
            itemId: link.fromItemId,
            targetTitle: link.fromItemTitle,
            linkType: link.linkType,
            customValue: link.customValue,
            position: link.position,
        }));
    }

    const originalTypeId = record.itemTypeId;
    const originalFieldValues = { ...(record.fieldValues || {}) };
    if (restore) {
        // What the server holds decides the type change warnings; the draft
        // decides what the editor shows.
        record = { ...record, ...restore.item };
        links = restore.links || [];
        backlinks = restore.backlinks || [];
    }
    let typeChangeConfirmed = false;
    // Pending values survive switching types back and forth, as in Qt.
    const pendingValues = { ...(record.fieldValues || {}) };
    let currentFields = [];
    let fieldReaders = new Map();
    let displayedTypeId = record.itemTypeId;
    let availableTypes = [];
    let itemTitles = [];
    let tags = [...(record.tags || [])];
    let flags = [...(record.flags || [])];
    let aliases = [...(record.aliases || [])];
    let properties = [...(record.properties || [])];

    // --- General ---------------------------------------------------------
    const groupSelect = el('select', {});
    fillSelect(groupSelect, groups.map((group) => ({ value: group.id, label: group.name })),
        record.groupId);
    const typeSelect = el('select', {});
    const titleInput = el('input', {
        type: 'text', value: record.title, required: true, maxlength: '2000', ...LITERAL_TEXT,
    });
    const disambiguationInput = el('input', {
        type: 'text', value: record.disambiguation || '', ...LITERAL_TEXT,
    });
    const statusSelect = el('select', {});
    fillSelect(statusSelect, ITEM_STATUSES, record.status);
    const understandingSelect = el('select', {});
    fillSelect(understandingSelect, UNDERSTANDING_LEVELS, record.understanding);
    const pinnedCheck = el('input', { type: 'checkbox' });
    pinnedCheck.checked = Boolean(record.pinned);

    const generalPanel = el('div', { class: 'form-grid' }, [
        field('Group:', groupSelect),
        field('Type:', typeSelect),
        field('Title:', titleInput),
        field('Disambiguation:', disambiguationInput),
        field('Status:', statusSelect),
        field('Understanding:', understandingSelect,
            'Unknown, Recognized, Understood, Practiced and Mastered describe how well you know this item.'),
        field('Pinned:', pinnedCheck),
    ]);

    // --- Content ---------------------------------------------------------
    const contentArea = el('textarea', {
        class: 'markdown-source', rows: '18', placeholder: 'Markdown content...', ...LITERAL_TEXT,
    });
    contentArea.value = record.content || '';
    const preview = el('div', { class: 'markdown-preview', 'aria-live': 'polite' });
    const refreshPreview = debounce(() => renderMarkdown(preview, contentArea.value), 300);
    contentArea.addEventListener('input', refreshPreview);
    renderMarkdown(preview, contentArea.value);

    const toolbar = el('div', { class: 'markdown-toolbar', role: 'toolbar' });
    for (const action of MARKDOWN_ACTIONS) {
        if (action.separator) {
            toolbar.appendChild(el('span', { class: 'toolbar-separator' }));
            continue;
        }
        toolbar.appendChild(button(action.label, {
            class: 'toolbar-button',
            title: action.title,
            onclick: async () => {
                if (action.codeBlock) {
                    const language = await promptDialog('Code block',
                        'Language (e.g. cpp, python, sql):', '');
                    if (language === null) return;
                    insertMarkdown(contentArea, `\n\`\`\`${language}\n`, '\n```\n', 'code block');
                    return;
                }
                if (action.table) {
                    insertMarkdown(contentArea,
                        '\n| Header 1 | Header 2 |\n| --- | --- |\n| Cell 1 | Cell 2 |\n', '', '');
                    return;
                }
                insertMarkdown(contentArea, action.prefix, action.suffix || '', action.sample || '');
            },
        }));
    }
    // On a narrow screen source and preview take turns instead of sharing a
    // height the keyboard already halves. A wide screen shows both.
    const contentPanel = el('div', { class: 'content-panel' });
    const modeButtons = {};
    function showContent(mode) {
        const previewing = mode === 'preview';
        if (previewing) {
            refreshPreview.cancel();
            renderMarkdown(preview, contentArea.value);
        }
        contentPanel.classList.toggle('previewing', previewing);
        for (const [name, node] of Object.entries(modeButtons)) {
            node.classList.toggle('active', name === mode);
            node.setAttribute('aria-pressed', name === mode ? 'true' : 'false');
        }
    }
    modeButtons.source = button('Source', {
        class: 'content-mode-button', onclick: () => showContent('source'),
    });
    modeButtons.preview = button('Preview', {
        class: 'content-mode-button', onclick: () => showContent('preview'),
    });
    contentPanel.append(
        el('div', { class: 'content-mode', role: 'group', 'aria-label': 'Content view' },
            [modeButtons.source, modeButtons.preview]),
        toolbar,
        el('div', { class: 'content-split' }, [contentArea, preview]),
    );
    showContent('source');

    // --- Values ----------------------------------------------------------
    const valuesPanel = el('div', { class: 'values-panel' });

    function captureFieldValues() {
        for (const [fieldId, read] of fieldReaders) {
            const value = read();
            if (value === '' || value === undefined || value === null) delete pendingValues[fieldId];
            else pendingValues[fieldId] = value;
        }
    }

    function renderFields() {
        clear(valuesPanel);
        fieldReaders = new Map();
        if (!currentFields.length) {
            valuesPanel.appendChild(el('p', { class: 'hint', text: 'This type has no fields yet.' }));
            return;
        }
        const grid = el('div', { class: 'form-grid' });
        for (const fieldRecord of currentFields) {
            const stored = pendingValues[String(fieldRecord.id)] ?? pendingValues[fieldRecord.id] ?? '';
            const editor = fieldEditor(fieldRecord, stored, (value) => {
                if (value === '') delete pendingValues[String(fieldRecord.id)];
                else pendingValues[String(fieldRecord.id)] = value;
            });
            grid.appendChild(field(`${fieldRecord.name}:`, editor.node,
                fieldRecord.dataType === 'Enum'
                    ? `Enum: ${fieldRecord.enumOptions.join(', ')}`
                    : fieldRecord.dataType));
            fieldReaders.set(String(fieldRecord.id), editor.read);
        }
        valuesPanel.appendChild(grid);
    }

    // --- Metadata --------------------------------------------------------
    const tagEditor = listEditor({
        title: 'Tags',
        onAdd: async () => {
            const value = await promptDialog('Add tag', 'Value:', '',
                (await api.tagUsage()).map((usage) => usage.value));
            if (value && !tags.includes(value)) { tags = [...tags, value].sort(); tagEditor.render(tags); }
        },
        onEdit: async (index) => {
            const value = await promptDialog('Edit tag', 'Value:', tags[index],
                (await api.tagUsage()).map((usage) => usage.value));
            if (value) { tags[index] = value; tags = [...tags].sort(); tagEditor.render(tags); }
        },
        onRemove: (index) => { tags.splice(index, 1); tagEditor.render(tags); },
    });
    const flagEditor = listEditor({
        title: 'Flags',
        onAdd: async () => {
            const value = await promptDialog('Add flag', 'Value:', '',
                (await api.flagUsage()).map((usage) => usage.value));
            if (value && !flags.includes(value)) { flags = [...flags, value].sort(); flagEditor.render(flags); }
        },
        onEdit: async (index) => {
            const value = await promptDialog('Edit flag', 'Value:', flags[index],
                (await api.flagUsage()).map((usage) => usage.value));
            if (value) { flags[index] = value; flags = [...flags].sort(); flagEditor.render(flags); }
        },
        onRemove: (index) => { flags.splice(index, 1); flagEditor.render(flags); },
    });
    const aliasEditor = listEditor({
        title: 'Aliases',
        onAdd: async () => {
            const value = await promptDialog('Add alias', 'Value:', '',
                (await api.aliasUsage()).map((usage) => usage.value));
            if (value && !aliases.includes(value)) { aliases = [...aliases, value].sort(); aliasEditor.render(aliases); }
        },
        onEdit: async (index) => {
            const value = await promptDialog('Edit alias', 'Value:', aliases[index],
                (await api.aliasUsage()).map((usage) => usage.value));
            if (value) { aliases[index] = value; aliases = [...aliases].sort(); aliasEditor.render(aliases); }
        },
        onRemove: (index) => { aliases.splice(index, 1); aliasEditor.render(aliases); },
    });
    const propertyEditor = listEditor({
        title: 'Properties',
        renderItem: (property) => `${property.key} = ${property.value}`,
        onAdd: async () => {
            const property = await propertyDialog('Add property', {}, properties, -1);
            if (property) { properties.push(property); propertyEditor.render(properties); }
        },
        onEdit: async (index) => {
            const property = await propertyDialog('Edit property', properties[index], properties, index);
            if (property) { properties[index] = property; propertyEditor.render(properties, index); }
        },
        onRemove: (index) => { properties.splice(index, 1); propertyEditor.render(properties); },
    });
    tagEditor.render(tags);
    flagEditor.render(flags);
    aliasEditor.render(aliases);
    propertyEditor.render(properties);
    const metadataPanel = el('div', { class: 'metadata-grid' },
        [tagEditor.node, flagEditor.node, aliasEditor.node, propertyEditor.node]);

    // --- Links and backlinks ---------------------------------------------
    function sortLinks(entries) {
        return entries.sort((left, right) => (left.position - right.position)
            || String(left.targetTitle).localeCompare(String(right.targetTitle),
                undefined, { sensitivity: 'accent' }));
    }
    const renderLinkItem = (link) => `[${link.position}] ${link.targetTitle} (${linkDescription(link)})`;

    const linkEditor = listEditor({
        title: 'Outgoing links',
        renderItem: renderLinkItem,
        onAdd: async () => {
            const result = await linkDialog({
                title: 'Add link',
                label: 'Target item:',
                link: { linkType: 'Related', position: 0 },
                itemTitles,
            });
            if (result) { links.push(result); linkEditor.render(sortLinks(links)); }
        },
        onEdit: async (index) => {
            const result = await linkDialog({
                title: 'Edit link', label: 'Target item:', link: links[index], itemTitles,
            });
            if (result) {
                links[index] = { ...links[index], ...result };
                linkEditor.render(sortLinks(links), index);
            }
        },
        onRemove: (index) => { links.splice(index, 1); linkEditor.render(sortLinks(links)); },
    });
    const backlinkEditor = listEditor({
        title: 'Incoming links',
        renderItem: renderLinkItem,
        onAdd: async () => {
            const result = await linkDialog({
                title: 'Add backlink',
                label: 'Source item:',
                link: { linkType: 'Related', position: 0 },
                itemTitles,
            });
            if (result) { backlinks.push(result); backlinkEditor.render(sortLinks(backlinks)); }
        },
        onEdit: async (index) => {
            const result = await linkDialog({
                title: 'Edit backlink', label: 'Source item:', link: backlinks[index], itemTitles,
            });
            if (result) {
                backlinks[index] = { ...backlinks[index], ...result };
                backlinkEditor.render(sortLinks(backlinks), index);
            }
        },
        onRemove: (index) => { backlinks.splice(index, 1); backlinkEditor.render(sortLinks(backlinks)); },
    });
    linkEditor.render(sortLinks(links));
    backlinkEditor.render(sortLinks(backlinks));

    const tabStrip = tabs([
        { name: 'general', label: 'General', content: generalPanel },
        { name: 'content', label: 'Content', content: contentPanel },
        { name: 'values', label: 'Values', content: valuesPanel },
        { name: 'metadata', label: 'Metadata', content: metadataPanel },
        { name: 'links', label: 'Links', content: linkEditor.node },
        { name: 'backlinks', label: 'Backlinks', content: backlinkEditor.node },
    ]);

    async function loadFields(typeId) {
        currentFields = typeId ? await api.fields(typeId) : [];
        displayedTypeId = typeId;
        tabStrip.setEnabled('values', Boolean(typeId));
        renderFields();
    }

    async function refreshTypes(selectedTypeId) {
        const groupId = Number.parseInt(groupSelect.value, 10);
        availableTypes = groupId > 0 ? await api.types(groupId) : [];
        fillSelect(typeSelect, [
            { value: '', label: 'None' },
            ...availableTypes.map((type) => ({ value: type.id, label: typeDisplayName(type) })),
        ], selectedTypeId ? String(selectedTypeId) : '');
        if (typeSelect.selectedIndex < 0) typeSelect.value = '';
    }

    groupSelect.addEventListener('change', async () => {
        const previous = typeSelect.value;
        await refreshTypes(previous ? Number.parseInt(previous, 10) : null);
        await handleTypeChange();
    });

    async function handleTypeChange() {
        const newTypeId = typeSelect.value ? Number.parseInt(typeSelect.value, 10) : null;
        if (newTypeId === displayedTypeId) return;
        captureFieldValues();
        const oldTypeId = displayedTypeId;
        const oldFieldIds = currentFields.map((fieldRecord) => String(fieldRecord.id));
        const affected = oldFieldIds.filter((fieldId) => {
            if (pendingValues[fieldId]) return true;
            return oldTypeId === originalTypeId && !typeChangeConfirmed
                && Boolean(originalFieldValues[fieldId]);
        }).length;
        if (oldTypeId && affected > 0) {
            const confirmed = await confirmDialog('Change type',
                `Changing the type will remove ${affected} field value(s) from this item. Continue?`,
                { danger: true });
            if (!confirmed) {
                typeSelect.value = oldTypeId ? String(oldTypeId) : '';
                return;
            }
            if (oldTypeId === originalTypeId) typeChangeConfirmed = true;
        }
        await loadFields(newTypeId);
        for (const fieldId of oldFieldIds) delete pendingValues[fieldId];
        renderFields();
    }

    typeSelect.addEventListener('change', handleTypeChange);

    await refreshTypes(record.itemTypeId);
    await loadFields(record.itemTypeId);
    try {
        itemTitles = await api.itemTitles();
    } catch (error) {
        itemTitles = [];
    }

    // Everything the editor would save, in the shape `restore` accepts.
    function snapshot() {
        captureFieldValues();
        return {
            item: {
                groupId: Number.parseInt(groupSelect.value, 10) || null,
                itemTypeId: typeSelect.value ? Number.parseInt(typeSelect.value, 10) : null,
                title: titleInput.value,
                disambiguation: disambiguationInput.value,
                status: statusSelect.value,
                understanding: understandingSelect.value,
                pinned: pinnedCheck.checked,
                content: contentArea.value,
                tags: [...tags],
                flags: [...flags],
                aliases: [...aliases],
                properties: properties.map((property) => ({ ...property })),
                fieldValues: { ...pendingValues },
            },
            links: links.map((link) => ({ ...link })),
            backlinks: backlinks.map((link) => ({ ...link })),
        };
    }
    const keeper = keepDraft({ itemId, snapshot, restored: Boolean(restore) });

    const saved = await openDialog({
        title: itemId ? 'Edit item' : 'Add item',
        body: tabStrip.node,
        acceptLabel: 'Save',
        wide: true,
        // A fixed frame, like the desktop dialog: switching tabs must not move
        // the title, the tab strip or the buttons.
        className: 'dialog-item-editor',
        // A stray tap beside the editor must not throw away what was typed.
        closeOnBackdrop: false,
        initialFocus: titleInput,
        onAccept: async ({ fail }) => {
            if (!titleInput.value.trim()) {
                tabStrip.select('general');
                titleInput.focus();
                fail('Title cannot be empty.');
                return undefined;
            }
            if (!groupSelect.value) {
                fail('Create at least one group first.');
                return undefined;
            }
            const typeId = typeSelect.value ? Number.parseInt(typeSelect.value, 10) : null;
            if (itemId && originalTypeId !== typeId
                && Object.keys(originalFieldValues).length > 0 && !typeChangeConfirmed) {
                const confirmed = await confirmDialog('Change type',
                    `Changing the type will remove ${Object.keys(originalFieldValues).length} `
                    + 'saved field value(s) from this item. Continue?', { danger: true });
                if (!confirmed) return undefined;
                typeChangeConfirmed = true;
            }
            captureFieldValues();
            const fieldValues = {};
            if (typeId) {
                for (const fieldRecord of currentFields) {
                    const value = pendingValues[String(fieldRecord.id)];
                    if (value !== undefined && value !== '') fieldValues[String(fieldRecord.id)] = value;
                }
            }
            const payload = {
                item: {
                    groupId: Number.parseInt(groupSelect.value, 10),
                    itemTypeId: typeId,
                    title: titleInput.value.trim(),
                    disambiguation: disambiguationInput.value.trim(),
                    status: statusSelect.value,
                    understanding: understandingSelect.value,
                    pinned: pinnedCheck.checked,
                    content: contentArea.value,
                    tags,
                    flags,
                    aliases,
                    properties,
                    fieldValues,
                },
                links: links.map((link) => ({
                    id: link.id ?? null,
                    toItemId: link.itemId,
                    linkType: link.linkType,
                    customValue: link.customValue || '',
                    position: link.position,
                })),
                backlinks: backlinks.map((link) => ({
                    id: link.id ?? null,
                    fromItemId: link.itemId,
                    linkType: link.linkType,
                    customValue: link.customValue || '',
                    position: link.position,
                })),
            };
            try {
                // One request, one unit of work: the item and both link
                // directions are committed together or not at all.
                const result = itemId
                    ? await api.updateItem(itemId, payload)
                    : await api.createItem(payload);
                return result.id;
            } catch (error) {
                fail(error.message);
                return undefined;
            }
        },
    });

    refreshPreview.cancel();
    // A cancel is a decision, unless the session ended under the editor: then
    // the draft waits for the next sign-in.
    keeper.stop(saved === null && !api.authenticated);
    return saved;
}

export { formatItemTitle };
