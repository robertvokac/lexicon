// The web counterpart of ItemTypeManagerDialog, including its destructive
// change warnings.
import { api } from './api.js';
import { confirmDialog, errorDialog, field, listEditor, openDialog } from './dialogs.js';
import { el, fillSelect, FIELD_DATA_TYPES, typeScopeLabel } from './utils.js';

async function typeDialog(title, type, groups) {
    const name = el('input', { type: 'text', value: type.name || '', required: true });
    const description = el('textarea', { rows: '4' });
    description.value = type.description || '';
    const group = el('select', {});
    fillSelect(group, [
        { value: '', label: 'All groups' },
        ...groups.map((candidate) => ({ value: candidate.id, label: candidate.name })),
    ], type.groupId ? String(type.groupId) : '');

    return openDialog({
        title,
        body: el('div', {}, [
            field('Name:', name),
            field('Description:', description),
            field('Available in:', group),
        ]),
        initialFocus: name,
        onAccept: ({ fail }) => {
            if (!name.value.trim()) {
                fail('Type name cannot be empty.');
                return undefined;
            }
            return {
                name: name.value.trim(),
                description: description.value.trim(),
                groupId: group.value ? Number.parseInt(group.value, 10) : null,
            };
        },
    });
}

async function fieldDialog(title, fieldRecord) {
    const name = el('input', { type: 'text', value: fieldRecord.name || '', required: true });
    const dataType = el('select', {});
    fillSelect(dataType, FIELD_DATA_TYPES, fieldRecord.dataType || 'Text');
    const position = el('input', { type: 'number', step: '1', value: String(fieldRecord.position ?? 0) });
    const options = el('textarea', { rows: '4', placeholder: 'One enum option per line' });
    options.value = (fieldRecord.enumOptions || []).join('\n');

    const syncOptions = () => { options.disabled = dataType.value !== 'Enum'; };
    dataType.addEventListener('change', syncOptions);
    syncOptions();

    return openDialog({
        title,
        body: el('div', {}, [
            field('Name:', name),
            field('Data type:', dataType),
            field('Position:', position),
            field('Enum options:', options),
        ]),
        initialFocus: name,
        onAccept: ({ fail }) => {
            if (!name.value.trim()) {
                fail('Field name cannot be empty.');
                return undefined;
            }
            const enumOptions = dataType.value === 'Enum'
                ? options.value.split('\n').map((line) => line.trim()).filter(Boolean)
                : [];
            if (dataType.value === 'Enum' && enumOptions.length === 0) {
                fail('Enum fields need at least one option.');
                return undefined;
            }
            return {
                name: name.value.trim(),
                dataType: dataType.value,
                position: Number.parseInt(position.value, 10) || 0,
                enumOptions,
            };
        },
    });
}

function sameOptions(left, right) {
    if (left.length !== right.length) return false;
    return left.every((value, index) => value === right[index]);
}

export async function openTypeManager() {
    const groups = await api.groups();
    let types = await api.types();
    let fields = [];
    let selectedTypeIndex = -1;
    let changed = false;

    let fieldEditor = null;

    async function reloadFields(keepIndex = -1) {
        fields = selectedTypeIndex >= 0 ? await api.fields(types[selectedTypeIndex].id) : [];
        fieldEditor.render(fields, keepIndex);
    }

    const typeEditor = listEditor({
        title: 'Types',
        renderItem: (type) => `${type.name} — ${typeScopeLabel(type)}`,
        itemTitle: (type) => type.description,
        onSelect: async (index) => {
            selectedTypeIndex = index;
            fieldEditor.setAddEnabled(index >= 0);
            await reloadFields();
        },
        onAdd: async () => {
            const values = await typeDialog('Add type', {}, groups);
            if (!values) return;
            try {
                const created = await api.createType(values);
                types = await api.types();
                changed = true;
                selectedTypeIndex = types.findIndex((type) => type.id === created.id);
                typeEditor.render(types, selectedTypeIndex);
                fieldEditor.setAddEnabled(selectedTypeIndex >= 0);
                await reloadFields();
            } catch (error) {
                await errorDialog(error.message);
            }
        },
        onEdit: async (index) => {
            const values = await typeDialog('Edit type', types[index], groups);
            if (!values) return;
            try {
                await api.updateType(types[index].id, values);
                types = await api.types();
                changed = true;
                typeEditor.render(types, index);
            } catch (error) {
                await errorDialog(error.message);
            }
        },
        onRemove: async (index) => {
            const type = types[index];
            let affected = 0;
            try {
                affected = await api.typeItemCount(type.id);
            } catch (error) {
                await errorDialog(error.message);
                return;
            }
            const confirmed = await confirmDialog('Delete type',
                `Delete type '${type.name}'? The Type field will be set to None for all `
                + `${affected} item(s) using it, and their custom field values will be deleted. `
                + 'This data cannot be restored automatically.', { danger: true });
            if (!confirmed) return;
            try {
                await api.deleteType(type.id);
                types = await api.types();
                changed = true;
                selectedTypeIndex = -1;
                typeEditor.render(types);
                fieldEditor.setAddEnabled(false);
                await reloadFields();
            } catch (error) {
                await errorDialog(error.message);
            }
        },
    });

    fieldEditor = listEditor({
        title: 'Fields of the selected type',
        renderItem: (record) => `${record.position}  ${record.name} — ${record.dataType}`,
        onAdd: async () => {
            if (selectedTypeIndex < 0) return;
            const last = fields.length ? fields[fields.length - 1].position : -1;
            const values = await fieldDialog('Add field', { position: last + 1, dataType: 'Text' });
            if (!values) return;
            try {
                await api.createField(types[selectedTypeIndex].id, values);
                changed = true;
                await reloadFields();
            } catch (error) {
                await errorDialog(error.message);
            }
        },
        onEdit: async (index) => {
            const original = fields[index];
            const values = await fieldDialog('Edit field', original);
            if (!values) return;
            if (values.dataType !== original.dataType
                || !sameOptions(values.enumOptions, original.enumOptions)) {
                let affected = 0;
                try {
                    affected = await api.fieldValueCount(original.id);
                } catch (error) {
                    await errorDialog(error.message);
                    return;
                }
                if (affected > 0) {
                    const confirmed = await confirmDialog('Change field data type',
                        `Changing the data type or enum options will clear ${affected} `
                        + 'stored value(s). Continue?', { danger: true });
                    if (!confirmed) return;
                }
            }
            try {
                await api.updateField(original.id, { ...values, itemTypeId: original.itemTypeId });
                changed = true;
                await reloadFields(index);
            } catch (error) {
                await errorDialog(error.message);
            }
        },
        onRemove: async (index) => {
            const record = fields[index];
            let affected = 0;
            try {
                affected = await api.fieldValueCount(record.id);
            } catch (error) {
                await errorDialog(error.message);
                return;
            }
            const confirmed = await confirmDialog('Delete field',
                `Delete field '${record.name}'? This will remove its value from ${affected} `
                + 'item(s). Continue?', { danger: true });
            if (!confirmed) return;
            try {
                await api.deleteField(record.id);
                changed = true;
                await reloadFields();
            } catch (error) {
                await errorDialog(error.message);
            }
        },
    });

    typeEditor.render(types);
    fieldEditor.render([]);
    // Fields belong to a type, so adding one needs a selected type.
    fieldEditor.setAddEnabled(false);

    await openDialog({
        title: 'Manage types',
        body: el('div', { class: 'type-manager' }, [typeEditor.node, fieldEditor.node]),
        showAccept: false,
        cancelLabel: 'Close',
        wide: true,
    });
    return changed;
}
