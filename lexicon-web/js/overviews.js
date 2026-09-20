// ValueListDialog (all tags/flags/aliases), PropertyFilterDialog and the
// column visibility dialog.
import { api } from './api.js';
import { field, listEditor, openDialog } from './dialogs.js';
import { el } from './utils.js';

function usageTable(valueHeader, values) {
    const table = el('table', { class: 'value-table' }, [
        el('thead', {}, [el('tr', {}, [
            el('th', { text: valueHeader }),
            el('th', { text: 'Usage count' }),
        ])]),
    ]);
    const body = el('tbody');
    for (const entry of values) {
        body.appendChild(el('tr', {}, [
            el('td', { text: entry.value }),
            el('td', { class: 'numeric', text: String(entry.usageCount) }),
        ]));
    }
    if (!values.length) {
        body.appendChild(el('tr', {}, [el('td', { colspan: '2', class: 'empty', text: 'Nothing yet.' })]));
    }
    table.appendChild(body);
    return table;
}

export async function showValueOverview(kind) {
    const loaders = {
        tags: { title: 'All tags', header: 'Tag', load: () => api.tagUsage() },
        flags: { title: 'All flags', header: 'Flag', load: () => api.flagUsage() },
        aliases: { title: 'All aliases', header: 'Alias', load: () => api.aliasUsage() },
    };
    const definition = loaders[kind];
    const values = await definition.load();
    await openDialog({
        title: definition.title,
        body: el('div', { class: 'scroll-area' }, [usageTable(definition.header, values)]),
        showAccept: false,
        cancelLabel: 'Close',
    });
}

async function propertyFilterRow(title, filter) {
    const key = el('input', { type: 'text', value: filter.key || '' });
    const value = el('input', { type: 'text', value: filter.value || '' });
    return openDialog({
        title,
        body: el('div', {}, [field('Key:', key), field('Value contains:', value)]),
        initialFocus: key,
        onAccept: ({ fail }) => {
            if (!key.value.trim()) {
                fail('Key cannot be empty.');
                return undefined;
            }
            return { key: key.value.trim(), value: value.value.trim() };
        },
    });
}

// All filters must match. Keys are exact, values contain the entered text, and
// an empty value matches any value for that key.
export async function openPropertyFilterDialog(currentFilters) {
    let filters = currentFilters.map((filter) => ({ ...filter }));
    let editor = null;
    editor = listEditor({
        title: 'Property filters',
        renderItem: (filter) => (filter.value ? `${filter.key} = ${filter.value}` : `${filter.key} = (any)`),
        onAdd: async () => {
            const filter = await propertyFilterRow('Add property filter', {});
            if (filter) { filters.push(filter); editor.render(filters, filters.length - 1); }
        },
        onEdit: async (index) => {
            const filter = await propertyFilterRow('Edit property filter', filters[index]);
            if (filter) { filters[index] = filter; editor.render(filters, index); }
        },
        onRemove: (index) => { filters.splice(index, 1); editor.render(filters); },
    });
    editor.render(filters);

    return openDialog({
        title: 'Filter properties',
        body: el('div', {}, [
            el('p', {
                class: 'hint',
                text: 'All filters must match. Keys are exact; values contain the entered text. '
                    + 'Leave a value empty to match any value for that key.',
            }),
            editor.node,
        ]),
        acceptLabel: 'Apply',
        extraActions: [{
            label: 'Clear',
            onClick: () => { filters = []; editor.render(filters); },
        }],
        onAccept: () => filters,
    });
}

export async function openColumnDialog(configurableColumns, visibility) {
    const checkboxes = new Map();
    const body = el('div', { class: 'column-list' });
    for (const name of configurableColumns) {
        const checkbox = el('input', { type: 'checkbox' });
        checkbox.checked = visibility[name] !== false;
        checkboxes.set(name, checkbox);
        body.appendChild(el('label', { class: 'checkbox-row' }, [checkbox, el('span', { text: name })]));
    }
    return openDialog({
        title: 'Visible columns',
        body,
        acceptLabel: 'Save',
        onAccept: () => {
            const result = {};
            for (const [name, checkbox] of checkboxes) result[name] = checkbox.checked;
            return result;
        },
    });
}
