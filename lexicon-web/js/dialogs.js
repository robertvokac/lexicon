// Modal dialogs built on the native <dialog> element: Escape cancels, focus is
// trapped by the browser and returns to the opener on close.
import { button, clear, el, fillDatalist } from './utils.js';

let datalistCounter = 0;

function nextId(prefix) {
    datalistCounter += 1;
    return `${prefix}-${datalistCounter}`;
}

// Opens a modal and resolves with the value returned by onAccept, or null when
// the dialog is cancelled. onAccept may return undefined to keep it open, which
// is how validation errors are reported without losing the user's input.
export function openDialog({ title, body, acceptLabel = 'Save', cancelLabel = 'Cancel',
    onAccept, extraActions = [], wide = false, showAccept = true, initialFocus }) {
    return new Promise((resolve) => {
        const dialog = el('dialog', { class: wide ? 'dialog dialog-wide' : 'dialog' });
        const form = el('form', { method: 'dialog', class: 'dialog-form' });
        const errorLine = el('p', { class: 'dialog-error', role: 'alert', hidden: true });
        let settled = null;

        const actions = el('div', { class: 'dialog-actions' });
        for (const action of extraActions) {
            actions.appendChild(button(action.label, {
                class: action.class || 'secondary',
                onclick: () => action.onClick({
                    close: (value) => { settled = value; dialog.close(); },
                    fail: (message) => showDialogError(errorLine, message),
                }),
            }));
        }
        actions.appendChild(el('span', { class: 'spacer' }));
        if (showAccept) {
            actions.appendChild(button(acceptLabel, {
                class: 'primary',
                onclick: async () => {
                    showDialogError(errorLine, '');
                    try {
                        const value = onAccept ? await onAccept({
                            fail: (message) => showDialogError(errorLine, message),
                        }) : true;
                        if (value === undefined) return; // Validation failed.
                        settled = value;
                        dialog.close();
                    } catch (error) {
                        showDialogError(errorLine, error.message || String(error));
                    }
                },
            }));
        }
        actions.appendChild(button(cancelLabel, {
            class: 'secondary',
            onclick: () => { settled = null; dialog.close(); },
        }));

        form.appendChild(el('h2', { class: 'dialog-title', text: title }));
        form.appendChild(el('div', { class: 'dialog-body' }, [body]));
        form.appendChild(errorLine);
        form.appendChild(actions);
        dialog.appendChild(form);
        document.body.appendChild(dialog);

        dialog.addEventListener('close', () => {
            dialog.remove();
            resolve(settled);
        });
        // A click on the backdrop cancels, like clicking outside a Qt dialog.
        dialog.addEventListener('mousedown', (event) => {
            if (event.target === dialog) { settled = null; dialog.close(); }
        });
        dialog.showModal();
        const focusTarget = initialFocus
            || dialog.querySelector('input, select, textarea, button.primary, button');
        if (focusTarget) focusTarget.focus();
        if (focusTarget && focusTarget.select) focusTarget.select();
    });
}

export function showDialogError(node, message) {
    node.textContent = message || '';
    node.hidden = !message;
}

export function field(labelText, control, hint) {
    const id = nextId('field');
    control.id = control.id || id;
    const row = el('div', { class: 'form-row' }, [
        el('label', { for: control.id, text: labelText }),
        control,
    ]);
    if (hint) row.appendChild(el('p', { class: 'hint', text: hint }));
    return row;
}

export function confirmDialog(title, message, { acceptLabel = 'Yes', cancelLabel = 'No',
    danger = false } = {}) {
    return openDialog({
        title,
        body: el('p', { class: danger ? 'confirm danger' : 'confirm', text: message }),
        acceptLabel,
        cancelLabel,
        onAccept: () => true,
    }).then((value) => value === true);
}

export function messageDialog(title, message) {
    return openDialog({
        title,
        body: el('p', { class: 'confirm', text: message }),
        showAccept: false,
        cancelLabel: 'Close',
    });
}

export function errorDialog(message) {
    return messageDialog('Lexicon', message);
}

// The web equivalent of Qt's QInputDialog with a completer.
export function promptDialog(title, labelText, initialValue = '', suggestions = []) {
    const input = el('input', { type: 'text', value: initialValue, autocomplete: 'off' });
    let list = null;
    if (suggestions.length) {
        list = el('datalist', { id: nextId('suggestions') });
        fillDatalist(list, suggestions);
        input.setAttribute('list', list.id);
    }
    const body = el('div', {}, [field(labelText, input), list]);
    return openDialog({
        title,
        body,
        acceptLabel: 'OK',
        initialFocus: input,
        onAccept: ({ fail }) => {
            const value = input.value.trim();
            if (!value) {
                fail('Enter a value.');
                return undefined;
            }
            return value;
        },
    });
}

// A list editor with Add/Edit/Remove buttons, used for tags, flags, aliases,
// links, backlinks and properties.
export function listEditor({ title, onAdd, onEdit, onRemove, renderItem, itemTitle, onSelect }) {
    const list = el('ul', { class: 'value-list', role: 'listbox', tabindex: '0' });
    let values = [];
    let selected = -1;

    const buttons = {
        add: button('Add', { class: 'secondary', onclick: () => onAdd() }),
        edit: button('Edit', { class: 'secondary', onclick: () => trigger(onEdit) }),
        remove: button('Remove', { class: 'secondary', onclick: () => trigger(onRemove) }),
    };

    function trigger(handler) {
        if (selected < 0 || selected >= values.length) return;
        handler(selected);
    }

    function updateButtons() {
        const hasSelection = selected >= 0 && selected < values.length;
        buttons.edit.disabled = !hasSelection;
        buttons.remove.disabled = !hasSelection;
    }

    function select(index, notify = true) {
        render(values, index);
        if (notify && onSelect) onSelect(index);
    }

    function render(newValues, keepSelection = selected) {
        values = newValues;
        selected = keepSelection >= 0 && keepSelection < values.length ? keepSelection : -1;
        clear(list);
        values.forEach((value, index) => {
            const item = el('li', {
                class: index === selected ? 'selected' : '',
                role: 'option',
                'aria-selected': index === selected ? 'true' : 'false',
                title: itemTitle ? itemTitle(value) : null,
                text: renderItem ? renderItem(value) : String(value),
                onclick: () => select(index),
                ondblclick: () => { select(index); trigger(onEdit); },
            });
            list.appendChild(item);
        });
        updateButtons();
    }

    list.addEventListener('keydown', (event) => {
        if (event.key === 'ArrowDown' && selected < values.length - 1) {
            select(selected + 1);
            event.preventDefault();
        } else if (event.key === 'ArrowUp' && selected > 0) {
            select(selected - 1);
            event.preventDefault();
        } else if (event.key === 'Enter') {
            trigger(onEdit);
            event.preventDefault();
        } else if (event.key === 'Delete') {
            trigger(onRemove);
            event.preventDefault();
        }
    });

    const container = el('section', { class: 'list-editor' }, [
        el('h3', { text: title }),
        list,
        el('div', { class: 'list-editor-actions' }, [buttons.add, buttons.edit, buttons.remove]),
    ]);

    return {
        node: container,
        render,
        selectedIndex: () => selected,
        setAddEnabled: (enabled) => { buttons.add.disabled = !enabled; },
    };
}
