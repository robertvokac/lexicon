// Named shared Markdown Boards: choose and manage one, then read or edit it
// with the same source/preview experience as item content.
import { api } from './api.js';
import { confirmDialog, openDialog, promptDialog } from './dialogs.js';
import { renderMarkdown } from './markdown.js';
import { button, debounce, el, LITERAL_TEXT, readLocal, writeLocal } from './utils.js';

const CODE_LANGUAGE_KEY = 'lexicon.web.codeLanguage';
const ACTIONS = [
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
    { label: '---', title: 'Horizontal line', prefix: '\n---\n' },
    { separator: true },
    { label: 'Code', title: 'Inline code (`)', prefix: '`', suffix: '`', sample: 'code' },
    { label: 'Block', title: 'Code block (```)', codeBlock: true },
    { separator: true },
    { label: 'Link', title: 'Insert link ([])', prefix: '[', suffix: '](https://)', sample: 'link text' },
    { label: '[[ ]]', title: 'Link to an item ([[Title]])', itemLink: true },
    { label: 'Table', title: 'Insert table (|)', table: true },
];

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

async function editor(board) {
    const source = el('textarea', {
        class: 'markdown-source', rows: '18', placeholder: 'Markdown content...', ...LITERAL_TEXT,
    });
    source.value = board.content || '';
    const preview = el('div', { class: 'markdown-preview', 'aria-live': 'polite' });
    const refresh = debounce(() => renderMarkdown(preview, source.value), 300);
    source.addEventListener('input', refresh);
    renderMarkdown(preview, source.value);

    const toolbar = el('div', { class: 'markdown-toolbar', role: 'toolbar' });
    for (const action of ACTIONS) {
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
                        'Language (e.g. cpp, python, sql):', readLocal(CODE_LANGUAGE_KEY, ''));
                    if (language === null) return;
                    writeLocal(CODE_LANGUAGE_KEY, language);
                    insertMarkdown(source, `\n\`\`\`${language}\n`, '\n```\n', 'code block');
                } else if (action.itemLink) {
                    const selected = source.value.slice(source.selectionStart, source.selectionEnd).trim();
                    const target = await promptDialog('Link to an item', 'Item:', selected,
                        await api.itemTitles());
                    if (!target) return;
                    const label = selected && selected !== target ? `|${selected}` : '';
                    insertMarkdown(source, `[[${target}${label}]]`, '', '');
                } else if (action.table) {
                    insertMarkdown(source,
                        '\n| Header 1 | Header 2 |\n| --- | --- |\n| Cell 1 | Cell 2 |\n');
                } else {
                    insertMarkdown(source, action.prefix, action.suffix || '', action.sample || '');
                }
            },
        }));
    }

    const panel = el('div', { class: 'content-panel' });
    const modes = {};
    const show = (mode) => {
        const previewing = mode === 'preview';
        if (previewing) renderMarkdown(preview, source.value);
        panel.classList.toggle('previewing', previewing);
        for (const [name, node] of Object.entries(modes)) {
            node.classList.toggle('active', name === mode);
            node.setAttribute('aria-pressed', name === mode ? 'true' : 'false');
        }
    };
    modes.source = button('Source', { class: 'content-mode-button', onclick: () => show('source') });
    modes.preview = button('Preview', { class: 'content-mode-button', onclick: () => show('preview') });
    panel.append(
        el('div', { class: 'content-mode', role: 'group', 'aria-label': 'Board view' },
            [modes.source, modes.preview]),
        toolbar,
        el('div', { class: 'content-split' }, [source, preview]),
    );
    show('source');

    return openDialog({
        title: `Edit Board — ${board.name}`,
        body: panel,
        wide: true,
        acceptLabel: 'Save',
        clearErrorOn: [source],
        initialFocus: source,
        onAccept: async () => api.saveBoard({ ...board, content: source.value }),
    });
}

async function boardPicker(boards) {
    const select = el('select', {
        size: String(Math.min(Math.max(boards.length, 3), 10)),
        class: 'board-list',
        'aria-label': 'Boards',
    }, boards.map((board) => el('option', { value: String(board.id), text: board.name })));
    if (boards.length) select.value = String(boards[0].id);
    const selected = () => boards.find((board) => String(board.id) === select.value);
    return openDialog({
        title: 'Boards',
        body: el('div', {}, [
            select,
            el('p', { class: 'hint', text: 'Choose a Board to read or edit.' }),
        ]),
        acceptLabel: 'Open',
        cancelLabel: 'Close',
        initialFocus: select,
        extraActions: [
            { label: 'New', onClick: ({ close }) => close({ action: 'new' }) },
            { label: 'Rename', onClick: ({ close, fail }) => {
                const board = selected();
                if (board) close({ action: 'rename', board });
                else fail('Choose a Board first.');
            } },
            { label: 'Delete', onClick: ({ close, fail }) => {
                const board = selected();
                if (!board) fail('Choose a Board first.');
                else if (boards.length <= 1) fail('At least one Board must remain.');
                else close({ action: 'delete', board });
            } },
        ],
        onAccept: ({ fail }) => {
            const board = selected();
            if (!board) {
                fail('Choose a Board first.');
                return undefined;
            }
            return { action: 'open', board };
        },
    });
}

async function viewBoard(initial) {
    let board = initial;
    while (true) {
        const rendered = el('div', { class: 'markdown-preview board-view' });
        if (board.content) renderMarkdown(rendered, board.content);
        else rendered.appendChild(el('p', { class: 'hint', text: 'The Board is empty.' }));
        const choice = await openDialog({
            title: `Board — ${board.name}`,
            body: rendered,
            showAccept: false,
            cancelLabel: 'Boards',
            wide: true,
            extraActions: [{ label: 'Edit', class: 'primary', onClick: ({ close }) => close('edit') }],
        });
        if (choice !== 'edit') return board;
        const saved = await editor(board);
        if (!saved) continue;
        board = saved;
    }
}

export async function openBoard() {
    let boards = await api.boards();
    if (!boards.length) boards = [await api.createBoard('Main')];
    while (true) {
        const choice = await boardPicker(boards);
        if (!choice) return;
        if (choice.action === 'new') {
            const name = await promptDialog('New Board', 'Name:', boards.length ? 'New Board' : 'Main');
            if (!name) continue;
            const created = await api.createBoard(name);
            boards = await api.boards();
            await viewBoard(created);
            boards = await api.boards();
        } else if (choice.action === 'rename') {
            const name = await promptDialog('Rename Board', 'Name:', choice.board.name);
            if (!name) continue;
            await api.saveBoard({ ...choice.board, name });
            boards = await api.boards();
        } else if (choice.action === 'delete') {
            const confirmed = await confirmDialog(
                'Delete Board',
                `Delete Board '${choice.board.name}'? Its Markdown content cannot be restored.`,
                { acceptLabel: 'Delete', danger: true },
            );
            if (!confirmed) continue;
            await api.deleteBoard(choice.board.id);
            boards = await api.boards();
        } else {
            await viewBoard(choice.board);
            boards = await api.boards();
        }
    }
}
