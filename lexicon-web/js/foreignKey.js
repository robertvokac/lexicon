import { api } from './api.js';
import { button, el, LITERAL_TEXT } from './utils.js';

// Search by title within the field's target type. The visible choice includes
// the ID to distinguish duplicate titles; value always returns the stored ID.
export function foreignKeyControl(fieldRecord, storedValue, onChange = () => {}) {
    const input = el('input', {
        type: 'search', placeholder: 'Search target item by title...',
        autocomplete: 'off', ...LITERAL_TEXT,
    });
    const options = el('div', { class: 'foreign-key-results', role: 'listbox', hidden: true });
    const status = el('small', { class: 'hint' });
    const control = el('div', { class: 'foreign-key-control' }, [input, options, status]);
    let stored = String(storedValue || '');
    let selected = Boolean(stored);
    let generation = 0;
    let timer;
    const choose = (item, label) => {
        stored = String(item.id);
        selected = true;
        input.value = label;
        status.textContent = `Item ID: ${stored}`;
        options.hidden = true;
        options.replaceChildren();
        onChange(stored);
        control.dispatchEvent(new Event('change', { bubbles: true }));
        input.focus();
    };

    Object.defineProperty(control, 'value', {
        get: () => selected ? stored : input.value.trim(),
    });

    if (stored) {
        const initialId = stored;
        status.textContent = `Item ID: ${initialId}`;
        api.getItem(initialId).then(({ item }) => {
            if (!selected || control.value !== initialId) return;
            input.value = item.itemTypeId === fieldRecord.targetItemTypeId
                ? `${item.title}${item.disambiguation ? ` [${item.disambiguation}]` : ''} (#${initialId})`
                : `!missing! ${initialId}`;
        }).catch((error) => {
            if (selected && control.value === initialId)
                input.value = error.status === 404 ? `!missing! ${initialId}` : `Item ID: ${initialId}`;
        });
    }

    input.addEventListener('input', () => {
        clearTimeout(timer);
        generation += 1;
        selected = false;
        stored = '';
        const query = input.value.trim();
        status.textContent = query ? 'Choose an item from the suggestions.' : '';
        onChange(query);
        options.replaceChildren();
        options.hidden = true;
        if (!query) return;
        const request = generation;
        timer = setTimeout(async () => {
            try {
                const page = await api.queryItems({
                    typeId: fieldRecord.targetItemTypeId,
                    columnFilters: { title: query }, limit: 20, sortColumn: 3,
                });
                if (request !== generation) return;
                for (const item of page.items) {
                    const label = `${item.title}${item.disambiguation ? ` [${item.disambiguation}]` : ''} (#${item.id})`;
                    options.appendChild(button(label, {
                        class: 'foreign-key-option', role: 'option',
                        onclick: () => choose(item, label),
                    }));
                }
                options.hidden = !page.items.length;
                if (!page.items.length) status.textContent = 'No matching items.';
            } catch (error) {
                if (request === generation) status.textContent = 'Could not load suggestions.';
            }
        }, 180);
    });
    input.addEventListener('keydown', (event) => {
        if (event.key === 'ArrowDown' && !options.hidden) {
            options.firstElementChild?.focus();
            event.preventDefault();
        }
    });
    options.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') { options.hidden = true; input.focus(); }
        if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
            const next = event.key === 'ArrowDown'
                ? document.activeElement.nextElementSibling : document.activeElement.previousElementSibling;
            next?.focus();
            event.preventDefault();
        }
    });
    return control;
}
