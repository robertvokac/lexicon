// The web counterpart of GroupManagerDialog. Groups stay flat and Default
// remains the system default group.
import { api } from './api.js';
import { confirmDialog, errorDialog, field, listEditor, openDialog } from './dialogs.js';
import { el } from './utils.js';

async function groupDialog(title, group) {
    const name = el('input', { type: 'text', value: group.name || '', required: true });
    const description = el('textarea', { rows: '5' });
    description.value = group.description || '';
    const position = el('input', { type: 'number', step: '1', value: String(group.position ?? 0) });

    return openDialog({
        title,
        body: el('div', {}, [
            field('Name:', name),
            field('Description:', description),
            field('Position:', position,
                'Lower positions appear first. Groups with the same position are sorted by name.'),
        ]),
        initialFocus: name,
        onAccept: ({ fail }) => {
            if (!name.value.trim()) {
                fail('Group name cannot be empty.');
                return undefined;
            }
            return {
                name: name.value.trim(),
                description: description.value.trim(),
                position: Number.parseInt(position.value, 10) || 0,
            };
        },
    });
}

export async function openGroupManager() {
    let groups = await api.groups();
    let changed = false;

    const editor = listEditor({
        title: 'Groups',
        renderItem: (group) => `${group.position}  ${group.name}`,
        itemTitle: (group) => group.description,
        onAdd: async () => {
            const last = groups.length ? groups[groups.length - 1].position : -1;
            const values = await groupDialog('Add group', { position: last + 1 });
            if (!values) return;
            try {
                await api.createGroup(values);
                groups = await api.groups();
                changed = true;
                editor.render(groups);
            } catch (error) {
                await errorDialog(error.message);
            }
        },
        onEdit: async (index) => {
            const values = await groupDialog('Edit group', groups[index]);
            if (!values) return;
            try {
                await api.updateGroup(groups[index].id, values);
                groups = await api.groups();
                changed = true;
                editor.render(groups, index);
            } catch (error) {
                await errorDialog(error.message);
            }
        },
        onRemove: async (index) => {
            const group = groups[index];
            const confirmed = await confirmDialog('Delete group',
                `Delete group '${group.name}'? All items inside it will also be deleted.`,
                { danger: true });
            if (!confirmed) return;
            try {
                await api.deleteGroup(group.id);
                groups = await api.groups();
                changed = true;
                editor.render(groups);
            } catch (error) {
                await errorDialog(error.message);
            }
        },
    });
    editor.render(groups);

    await openDialog({
        title: 'Manage groups',
        body: editor.node,
        showAccept: false,
        cancelLabel: 'Close',
    });
    return changed;
}
