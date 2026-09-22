// Export and import of the whole dictionary as one JSON file. The server
// writes and reads it; the browser only downloads and uploads it.
import { api } from './api.js';
import { field, openDialog } from './dialogs.js';
import { el } from './utils.js';

function download(blob, name) {
    const url = URL.createObjectURL(blob);
    const anchor = el('a', { href: url, download: name });
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
}

export async function exportDictionary() {
    const includeFiles = el('input', { type: 'checkbox' });
    includeFiles.checked = true;
    const choice = await openDialog({
        title: 'Export',
        body: el('div', {}, [
            el('p', {
                class: 'confirm',
                text: 'Downloads the whole dictionary as one file: groups, types, items and links.',
            }),
            field('Include files:', includeFiles,
                'The files that Blob values refer to. The export is larger, but restores them too.'),
        ]),
        acceptLabel: 'Export',
        onAccept: async ({ fail }) => {
            try {
                return await api.exportDictionary(includeFiles.checked);
            } catch (error) {
                fail(error.message);
                return undefined;
            }
        },
    });
    if (!choice) return;
    download(choice, `lexicon-${new Date().toISOString().slice(0, 10)}.json`);
}

// Resolves with true when something was imported, so the caller refreshes.
export async function importDictionary() {
    const picker = el('input', { type: 'file', accept: '.json,application/json' });
    const report = await openDialog({
        title: 'Import',
        body: el('div', {}, [
            el('p', {
                class: 'confirm',
                text: 'Merges a Lexicon export into this dictionary. Groups, types and fields are '
                    + 'matched by name; items that are already here, with the same group, title and '
                    + 'disambiguation, are left as they are.',
            }),
            field('Export file:', picker),
        ]),
        acceptLabel: 'Import',
        onAccept: async ({ fail }) => {
            const file = picker.files && picker.files[0];
            if (!file) {
                fail('Choose an export file.');
                return undefined;
            }
            try {
                return await api.importDictionary(file);
            } catch (error) {
                fail(`Nothing was imported: ${error.message}`);
                return undefined;
            }
        },
    });
    if (!report) return false;
    const summary = `Imported ${report.itemsCreated} item(s), ${report.linksCreated} link(s), `
        + `${report.blobsImported} file(s) and ${report.alarmsCreated ?? 0} alarm(s); `
        + `${report.itemsSkipped} item(s) were already here. `
        + `Created ${report.groupsCreated} group(s), ${report.typesCreated} type(s) and `
        + `${report.fieldsCreated} field(s).`;
    const warnings = el('ul', { class: 'import-warnings' },
        report.warnings.map((warning) => el('li', { text: warning })));
    await openDialog({
        title: 'Import',
        body: el('div', {}, [el('p', { class: 'confirm', text: summary }),
            report.warnings.length ? warnings : null]),
        showAccept: false,
        cancelLabel: 'Close',
    });
    return true;
}
