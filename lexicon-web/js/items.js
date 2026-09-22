// The main window: search and actions, the filtered item table with its filter
// row, pagination, the Markdown preview and the link/backlink preview.
import { api } from './api.js';
import { confirmDialog, errorDialog, field, messageDialog, openDialog } from './dialogs.js';
import { clearDraft, latestDraft } from './drafts.js';
import { openGraph } from './graph.js';
import { imageSection } from './images.js';
import { describeImage } from './imagevalue.js';
import { askAboutDraft, openItemEditor } from './itemEdit.js';
import { bindItemLinks, renderMarkdown } from './markdown.js';
import { openColumnDialog, openPropertyFilterDialog } from './overviews.js';
import {
    button, clear, debounce, el, fillDatalist, fillSelect, formatItemTitle, ITEM_STATUSES,
    joinValues, linkDescription, LITERAL_TEXT, readLocal, statusLabel, typeDisplayName,
    UNDERSTANDING_LEVELS, understandingLabel, writeLocal,
} from './utils.js';

// Whether the text is the item's title, its title with the disambiguation,
// or one of its aliases, ignoring case.
function namedExactly(item, text) {
    const wanted = text.toLocaleLowerCase();
    return item.title.toLocaleLowerCase() === wanted
        || formatItemTitle(item.title, item.disambiguation).toLocaleLowerCase() === wanted
        || (item.aliases || []).some((alias) => alias.toLocaleLowerCase() === wanted);
}

// filterKey names the widget in filterControls; filter picks how it is built.
const BASE_COLUMNS = [
    { key: 'id', label: 'Id', filter: 'id', filterKey: 'id' },
    { key: 'group', label: 'Group', filter: 'group', filterKey: 'groupId' },
    { key: 'type', label: 'Type', filter: 'type', filterKey: 'typeId' },
    { key: 'title', label: 'Title', filter: 'title', filterKey: 'title' },
    { key: 'disambiguation', label: 'Disambiguation', filter: 'disambiguation',
      filterKey: 'disambiguation', configurable: true },
    { key: 'tags', label: 'Tags', filter: 'tag', filterKey: 'tag', configurable: true },
    { key: 'flags', label: 'Flags', filter: 'flag', filterKey: 'flag', configurable: true },
    { key: 'aliases', label: 'Aliases', filter: 'alias', filterKey: 'alias', configurable: true },
    { key: 'status', label: 'Status', filter: 'status', filterKey: 'status', configurable: true },
    { key: 'understanding', label: 'Understanding', filter: 'understanding',
      filterKey: 'understanding', configurable: true },
    { key: 'pinned', label: 'Pinned', filter: 'pinned', filterKey: 'pinned', configurable: true },
];
const CONFIGURABLE_COLUMNS = BASE_COLUMNS.filter((column) => column.configurable)
    .map((column) => column.label);
const PAGE_SIZES = [10, 20, 50, 100];
const STORAGE = {
    pageSize: 'lexicon.web.pageSize',
    columns: 'lexicon.web.columns',
    lastItem: 'lexicon.web.lastItemId',
    viewMode: 'lexicon.web.viewMode',
    tableHeight: 'lexicon.web.tableHeight',
};
// Below this width the desktop table stops being the better way to read a
// list, so the layout switches to cards unless the user insists otherwise.
const COMPACT_QUERY = '(max-width: 720px)';

export class MainView {
    constructor(root) {
        this.root = root;
        this.groups = [];
        this.types = [];
        this.typeFields = [];
        this.tagValues = [];
        this.flagValues = [];
        this.items = [];
        this.propertyFilters = [];
        // Filter state lives here, not in the DOM, so rebuilding the dynamic
        // type columns never loses a filter.
        this.filters = {
            groupId: 0,
            typeId: 0,
            id: '',
            title: '',
            disambiguation: '',
            alias: '',
            tag: '',
            flag: '',
            status: '',
            understanding: '',
            pinned: '',
            values: {},
        };
        this.filterControls = new Map();
        this.valueFilterControls = new Map();
        this.selectedItemId = null;
        this.totalCount = 0;
        this.page = 0;
        this.pageSize = Number.parseInt(readLocal(STORAGE.pageSize, '20'), 10) || 20;
        this.sortColumn = 0;
        this.sortOrder = 'Ascending';
        // 'auto' follows the screen width; 'table' and 'list' are explicit.
        this.viewPreference = readLocal(STORAGE.viewMode, 'auto');
        this.compactQuery = window.matchMedia(COMPACT_QUERY);
        this.columnVisibility = this.loadColumnVisibility();
        this.pendingRestoreItemId = Number.parseInt(readLocal(STORAGE.lastItem, '-1'), 10);
        this.build();
    }

    loadColumnVisibility() {
        const stored = readLocal(STORAGE.columns, '');
        const visibility = {};
        for (const name of CONFIGURABLE_COLUMNS) visibility[name] = true;
        if (stored) {
            try {
                Object.assign(visibility, JSON.parse(stored));
            } catch (error) {
                // A corrupted preference simply falls back to everything visible.
            }
        }
        return visibility;
    }

    // --- Layout ----------------------------------------------------------
    build() {
        clear(this.root);
        this.searchInput = el('input', {
            type: 'search',
            id: 'search-input',
            class: 'search-input',
            placeholder: 'Search titles, aliases, tags, flags and content...',
            list: 'search-suggestions',
            autocomplete: 'off',
            ...LITERAL_TEXT,
        });
        this.searchSuggestions = el('datalist', { id: 'search-suggestions' });
        this.quickAddButton = button('Add', {
            class: 'primary',
            title: 'Quick add the search text as a new item',
        });
        this.inboxButton = button('Inbox', {
            class: 'secondary',
            title: 'Save an idea quickly: a title and plain text, in Default without a type',
        });
        this.addButton = button('Add ...', {
            class: 'secondary',
            title: 'Open the full item editor',
        });
        this.editButton = button('Edit', {
            class: 'secondary',
            title: 'Edit the selected item',
            disabled: true,
        });
        this.deleteButton = button('Delete', {
            class: 'secondary',
            title: 'Delete the selected item',
            disabled: true,
        });
        this.columnsButton = button('Columns...', { class: 'secondary' });
        this.propertyFilterButton = button('Filter Properties...', { class: 'secondary' });

        // On a phone the secondary actions move into this menu, so the row
        // keeps room for the search field and quick add.
        this.overflowPopup = el('div', { class: 'menu-popup', role: 'menu' });
        this.overflowTrigger = button('\u22ee', {
            class: 'overflow-trigger',
            title: 'More actions',
            'aria-haspopup': 'true',
            'aria-expanded': 'false',
            'aria-label': 'More actions',
        });
        this.overflowMenu = el('div', { class: 'menu overflow-menu' },
            [this.overflowTrigger, this.overflowPopup]);
        this.inlineActions = el('div', { class: 'action-buttons' }, [
            this.quickAddButton, this.addButton, this.inboxButton, this.editButton,
            this.deleteButton, this.columnsButton, this.propertyFilterButton,
        ]);

        const actionBar = el('section', { class: 'action-bar' }, [
            el('label', { class: 'search-label', for: 'search-input', text: 'Search:' }),
            this.searchInput,
            this.searchSuggestions,
            this.inlineActions,
            this.overflowMenu,
        ]);

        // The same filter widgets, shown either in the table header or, when
        // there is no table to put them in, stacked in this panel.
        this.filterToggle = button('Filters', {
            class: 'secondary filter-toggle',
            'aria-expanded': 'false',
        });
        this.sortSelect = el('select', { class: 'sort-select', 'aria-label': 'Sort by' });
        this.sortDirection = button('\u25b2', {
            class: 'secondary sort-direction',
            title: 'Sort ascending or descending',
            'aria-label': 'Sort direction',
        });
        this.filterPanel = el('div', { class: 'filter-panel', hidden: true });
        this.listBar = el('section', { class: 'list-bar', hidden: true }, [
            this.filterToggle,
            el('span', { class: 'spacer' }),
            el('label', { class: 'sort-label', text: 'Sort:' }),
            this.sortSelect,
            this.sortDirection,
        ]);
        this.itemList = el('ul', { class: 'item-list', role: 'listbox', hidden: true });

        this.headerRow = el('tr', { class: 'header-row' });
        this.filterRow = el('tr', { class: 'filter-row' });
        this.tableBody = el('tbody');
        this.table = el('table', { class: 'item-table' }, [
            el('thead', {}, [this.headerRow, this.filterRow]),
            this.tableBody,
        ]);

        this.pageLabel = el('span', { class: 'page-label', text: 'Page 1' });
        this.firstButton = button('<< First', { class: 'secondary', onclick: () => this.goToPage(0) });
        this.prevButton = button('< Prev', {
            class: 'secondary',
            onclick: () => this.goToPage(this.page - 1),
        });
        this.nextButton = button('Next >', {
            class: 'secondary',
            onclick: () => this.goToPage(this.page + 1),
        });
        this.lastButton = button('Last >>', {
            class: 'secondary',
            onclick: () => this.goToPage(this.totalPages() - 1),
        });
        this.pageSizeSelect = el('select', { class: 'page-size', 'aria-label': 'Page size' });
        fillSelect(this.pageSizeSelect,
            PAGE_SIZES.map((size) => ({ value: size, label: String(size) })), this.pageSize);

        const pagination = el('section', { class: 'pagination' }, [
            this.firstButton, this.prevButton, this.pageLabel, this.nextButton, this.lastButton,
            el('span', { class: 'spacer' }),
            el('label', { class: 'page-size-label', text: 'Page size:' }),
            this.pageSizeSelect,
        ]);

        this.contentPreview = el('div', {
            class: 'markdown-preview content-preview',
            'aria-live': 'polite',
        });
        this.contentPreview.appendChild(
            el('p', { class: 'hint', text: 'Select an item to view content...' }));
        bindItemLinks(this.contentPreview, (target) => this.openWikiLink(target));
        this.linksPreview = el('div', { class: 'links-preview' });

        this.tableWrapper = el('div', { class: 'table-wrapper' }, [this.table]);
        this.splitter = el('div', {
            class: 'splitter',
            role: 'separator',
            tabindex: '0',
            'aria-orientation': 'horizontal',
            'aria-label': 'Resize the item list',
            title: 'Drag to resize. Double click to reset.',
        });
        this.previewArea = el('section', { class: 'preview-area' }, [
            this.contentPreview,
            this.linksPreview,
        ]);

        this.root.appendChild(actionBar);
        this.root.appendChild(this.listBar);
        this.root.appendChild(this.filterPanel);
        this.root.appendChild(this.tableWrapper);
        this.root.appendChild(this.itemList);
        this.root.appendChild(pagination);
        this.root.appendChild(this.splitter);
        this.root.appendChild(this.previewArea);

        this.buildHeader();
        this.connect();
        this.applyLayout();
        this.restoreTableHeight();
    }

    // The base columns and their filter widgets are created once and keep
    // their identity, so refreshing data never steals focus from a filter.
    buildHeader() {
        clear(this.headerRow);
        clear(this.filterRow);
        this.columns = [...BASE_COLUMNS];
        this.columns.forEach((column, index) => {
            this.headerRow.appendChild(this.buildHeaderCell(column, index));
            this.filterRow.appendChild(
                el('td', { class: 'filter-cell' }, [this.buildFilterWidget(column)]));
        });
        this.updateSortIndicators();
        this.applyColumnVisibility();
    }

    buildHeaderCell(column, index) {
        const indicator = el('span', { class: 'sort-indicator' });
        const cell = el('th', {
            class: 'sortable',
            scope: 'col',
            tabindex: '0',
            role: 'columnheader',
            onclick: () => this.toggleSort(index),
            onkeydown: (event) => {
                if (event.key === 'Enter' || event.key === ' ') {
                    this.toggleSort(index);
                    event.preventDefault();
                }
            },
        }, [el('span', { text: column.label }), indicator]);
        cell.dataset.columnIndex = String(index);
        return cell;
    }

    // Replaces only the type field columns; the base columns stay in place.
    updateDynamicColumns() {
        while (this.headerRow.children.length > BASE_COLUMNS.length) {
            this.headerRow.removeChild(this.headerRow.lastChild);
        }
        while (this.filterRow.children.length > BASE_COLUMNS.length) {
            this.filterRow.removeChild(this.filterRow.lastChild);
        }
        this.valueFilterControls = new Map();
        this.columns = [...BASE_COLUMNS];
        if (this.filters.typeId > 0) {
            for (const fieldRecord of this.typeFields) {
                const column = {
                    key: `field-${fieldRecord.id}`,
                    label: fieldRecord.name,
                    field: fieldRecord,
                };
                const index = this.columns.length;
                this.columns.push(column);
                this.headerRow.appendChild(this.buildHeaderCell(column, index));
                this.filterRow.appendChild(el('td', { class: 'filter-cell' },
                    [this.buildValueFilter(fieldRecord)]));
            }
        }
        this.updateSortIndicators();
        this.applyColumnVisibility();
        if (this.filterPanel) {
            this.placeFilterWidgets();
            this.updateSortControl();
        }
    }

    updateSortIndicators() {
        [...this.headerRow.children].forEach((cell, index) => {
            const active = index === this.sortColumn;
            cell.setAttribute('aria-sort', active
                ? (this.sortOrder === 'Ascending' ? 'ascending' : 'descending') : 'none');
            const indicator = cell.querySelector('.sort-indicator');
            if (indicator) {
                indicator.textContent = active
                    ? (this.sortOrder === 'Ascending' ? ' ▲' : ' ▼') : '';
            }
        });
    }

    applyColumnVisibility() {
        this.columns.forEach((column, index) => {
            const hidden = this.isHidden(column);
            if (this.headerRow.children[index]) this.headerRow.children[index].hidden = hidden;
            if (this.filterRow.children[index]) this.filterRow.children[index].hidden = hidden;
        });
    }

    buildFilterWidget(column) {
        const textFilter = (placeholder, key, options = {}) => {
            const input = el('input', {
                type: 'text',
                class: 'column-filter',
                placeholder,
                'aria-label': `${placeholder} filter`,
                value: this.filters[key],
                ...LITERAL_TEXT,
                ...options,
            });
            const update = debounce(() => {
                this.filters[key] = input.value.trim();
                this.resetPaginationAndRefresh();
            }, 250);
            input.addEventListener('input', () => {
                if (options.digitsOnly) input.value = input.value.replace(/\D+/g, '');
                update();
            });
            this.filterControls.set(key, input);
            return input;
        };
        const choiceFilter = (key, label, options, onChange) => {
            const select = el('select', { class: 'column-filter', 'aria-label': label });
            // groupId and typeId are numbers where 0 means "all", which is the
            // empty option.
            fillSelect(select, options, this.filters[key] || '');
            select.addEventListener('change', () => {
                this.filters[key] = select.value;
                if (onChange) onChange();
                else this.resetPaginationAndRefresh();
            });
            this.filterControls.set(key, select);
            return select;
        };

        switch (column.filter) {
        case 'id':
            return textFilter('ID...', 'id', { digitsOnly: true, inputmode: 'numeric' });
        case 'title':
            return textFilter('Title...', 'title');
        case 'disambiguation':
            return textFilter('Disambiguation...', 'disambiguation');
        case 'alias':
            return textFilter('Alias...', 'alias');
        case 'group':
            return choiceFilter('groupId', 'Group filter',
                [{ value: '', label: 'All groups' }], async () => {
                    this.filters.groupId = Number.parseInt(this.filters.groupId, 10) || 0;
                    await this.refreshTypes();
                    await this.resetPaginationAndRefresh();
                });
        case 'type':
            return choiceFilter('typeId', 'Type filter',
                [{ value: '', label: 'All types' }], async () => {
                    this.filters.typeId = Number.parseInt(this.filters.typeId, 10) || 0;
                    await this.refreshTypeFields();
                    await this.resetPaginationAndRefresh();
                });
        case 'tag':
            return choiceFilter('tag', 'Tag filter', [{ value: '', label: 'All tags' }]);
        case 'flag':
            return choiceFilter('flag', 'Flag filter', [{ value: '', label: 'All flags' }]);
        case 'status':
            return choiceFilter('status', 'Status filter',
                [{ value: '', label: 'All Statuses' }, ...ITEM_STATUSES]);
        case 'understanding':
            return choiceFilter('understanding', 'Understanding filter',
                [{ value: '', label: 'All Levels' }, ...UNDERSTANDING_LEVELS]);
        case 'pinned':
            return choiceFilter('pinned', 'Pinned filter', [
                { value: '', label: 'All Pinned' },
                { value: 'true', label: 'Pinned' },
                { value: 'false', label: 'Not Pinned' },
            ]);
        default:
            return el('span');
        }
    }

    // Dynamic type field filters mirror the desktop behaviour: enum and boolean
    // fields become a choice, everything else a contains filter.
    buildValueFilter(fieldRecord) {
        const previous = this.filters.values[fieldRecord.id] || '';
        if (fieldRecord.dataType === 'Boolean' || fieldRecord.dataType === 'Enum') {
            const select = el('select', {
                class: 'column-filter',
                'aria-label': `${fieldRecord.name} filter`,
            });
            const options = fieldRecord.dataType === 'Boolean'
                ? [{ value: 'false', label: 'False' }, { value: 'true', label: 'True' }]
                : fieldRecord.enumOptions.map((option) => ({ value: option, label: option }));
            fillSelect(select, [{ value: '', label: 'Any' }, ...options], previous);
            select.addEventListener('change', () => {
                this.filters.values[fieldRecord.id] = select.value;
                this.resetPaginationAndRefresh();
            });
            this.valueFilterControls.set(fieldRecord.id, { node: select, field: fieldRecord });
            return select;
        }
        const input = el('input', {
            type: 'text',
            class: 'column-filter',
            placeholder: `Filter ${fieldRecord.name}...`,
            'aria-label': `${fieldRecord.name} filter`,
            value: previous,
        });
        const update = debounce(() => {
            this.filters.values[fieldRecord.id] = input.value.trim();
            this.resetPaginationAndRefresh();
        }, 250);
        input.addEventListener('input', update);
        this.valueFilterControls.set(fieldRecord.id, { node: input, field: fieldRecord });
        return input;
    }

    // --- Layout ----------------------------------------------------------
    get compact() {
        return this.compactQuery.matches;
    }

    get viewMode() {
        if (this.viewPreference === 'table' || this.viewPreference === 'list') {
            return this.viewPreference;
        }
        return this.compact ? 'list' : 'table';
    }

    setViewPreference(preference) {
        this.viewPreference = preference;
        writeLocal(STORAGE.viewMode, preference);
        this.applyLayout();
        this.renderRows();
    }

    // Moves the shared widgets between the table header and the stacked panel
    // instead of building a second set of them.
    placeFilterWidgets() {
        const list = this.viewMode === 'list';
        clear(this.filterPanel);
        this.columns.forEach((column, index) => {
            const widget = column.field
                ? (this.valueFilterControls.get(column.field.id) || {}).node
                : this.filterControls.get(column.filterKey);
            if (!widget) return;
            if (list) {
                if (this.isHidden(column)) return;
                this.filterPanel.appendChild(el('div', { class: 'filter-field' }, [
                    el('label', { text: column.label }),
                    widget,
                ]));
            } else {
                const cell = this.filterRow.children[index];
                if (cell && widget.parentElement !== cell) {
                    clear(cell);
                    cell.appendChild(widget);
                }
            }
        });
        if (list && !this.filterPanel.childElementCount) {
            this.filterPanel.appendChild(
                el('p', { class: 'hint', text: 'No filters are available.' }));
        }
    }

    updateSortControl() {
        fillSelect(this.sortSelect, this.columns.map((column, index) => ({
            value: index,
            label: column.label,
        })), this.sortColumn);
        this.sortDirection.textContent = this.sortOrder === 'Ascending' ? '\u25b2' : '\u25bc';
    }

    applyLayout() {
        const list = this.viewMode === 'list';
        this.root.classList.toggle('list-view', list);
        this.root.classList.toggle('compact', this.compact);
        this.tableWrapper.hidden = list;
        this.itemList.hidden = !list;
        this.listBar.hidden = !list;
        if (!list) {
            this.filterPanel.hidden = true;
            this.filterToggle.setAttribute('aria-expanded', 'false');
        }
        // On a phone the secondary actions live behind the overflow menu.
        const host = this.compact ? this.overflowPopup : this.inlineActions;
        for (const action of [this.addButton, this.inboxButton, this.editButton, this.deleteButton,
            this.columnsButton, this.propertyFilterButton]) {
            action.classList.toggle('menu-item', this.compact);
            if (action.parentElement !== host) host.appendChild(action);
        }
        this.overflowMenu.hidden = !this.compact;
        this.placeFilterWidgets();
        this.updateSortControl();
        this.updatePreviewVisibility();
    }

    updatePreviewVisibility() {
        // A large empty preview is wasted space on a phone.
        const empty = this.selectedItemId === null;
        // A distinct name: `empty` is the table's empty-cell style.
        this.previewArea.classList.toggle('no-selection', empty);
        this.splitter.hidden = this.compact || (empty && this.compact);
    }

    restoreTableHeight() {
        const stored = Number.parseInt(readLocal(STORAGE.tableHeight, ''), 10);
        if (Number.isFinite(stored) && stored > 80) this.setTableHeight(stored);
    }

    setTableHeight(pixels) {
        const target = this.viewMode === 'list' ? this.itemList : this.tableWrapper;
        target.style.flex = `0 0 ${pixels}px`;
        this.tableHeight = pixels;
    }

    connectSplitter() {
        const surface = () => (this.viewMode === 'list' ? this.itemList : this.tableWrapper);
        const drag = (event) => {
            const top = surface().getBoundingClientRect().top;
            const height = Math.max(96, Math.round(event.clientY - top));
            this.setTableHeight(height);
        };
        const stop = () => {
            window.removeEventListener('pointermove', drag);
            window.removeEventListener('pointerup', stop);
            document.body.classList.remove('resizing');
            if (this.tableHeight) writeLocal(STORAGE.tableHeight, String(this.tableHeight));
        };
        this.splitter.addEventListener('pointerdown', (event) => {
            event.preventDefault();
            document.body.classList.add('resizing');
            window.addEventListener('pointermove', drag);
            window.addEventListener('pointerup', stop);
        });
        this.splitter.addEventListener('dblclick', () => {
            surface().style.flex = '';
            this.tableHeight = 0;
            writeLocal(STORAGE.tableHeight, '');
        });
        this.splitter.addEventListener('keydown', (event) => {
            const step = event.key === 'ArrowUp' ? -24 : event.key === 'ArrowDown' ? 24 : 0;
            if (!step) return;
            event.preventDefault();
            const current = surface().getBoundingClientRect().height;
            this.setTableHeight(Math.max(96, Math.round(current + step)));
            writeLocal(STORAGE.tableHeight, String(this.tableHeight));
        });
    }

    connect() {
        const search = debounce(() => this.resetPaginationAndRefresh(), 250);
        this.searchInput.addEventListener('input', search);
        this.searchInput.addEventListener('keydown', (event) => {
            if (event.key === 'Enter' && !event.isComposing) {
                // A phone keyboard's search key sends Enter, so Enter must
                // never create an item that already exists.
                event.preventDefault();
                search.cancel();
                this.searchOrAdd();
            }
        });
        this.quickAddButton.addEventListener('click', () => this.quickAdd());
        this.inboxButton.addEventListener('click', () => this.openInbox());
        this.addButton.addEventListener('click', () => this.addItem());
        this.editButton.addEventListener('click', () => this.editSelectedItem());
        this.deleteButton.addEventListener('click', () => this.deleteSelectedItem());
        this.columnsButton.addEventListener('click', () => this.chooseColumns());
        this.propertyFilterButton.addEventListener('click', () => this.choosePropertyFilters());
        this.pageSizeSelect.addEventListener('change', () => {
            this.pageSize = Number.parseInt(this.pageSizeSelect.value, 10) || 20;
            writeLocal(STORAGE.pageSize, String(this.pageSize));
            this.resetPaginationAndRefresh();
        });

        this.filterToggle.addEventListener('click', () => {
            const open = this.filterPanel.hidden;
            this.filterPanel.hidden = !open;
            this.filterToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
            this.filterToggle.classList.toggle('active', open);
        });
        this.sortSelect.addEventListener('change', () => {
            this.sortColumn = Number.parseInt(this.sortSelect.value, 10) || 0;
            this.updateSortIndicators();
            this.updateSortControl();
            this.resetPaginationAndRefresh();
        });
        this.sortDirection.addEventListener('click', () => {
            this.sortOrder = this.sortOrder === 'Ascending' ? 'Descending' : 'Ascending';
            this.updateSortIndicators();
            this.updateSortControl();
            this.resetPaginationAndRefresh();
        });
        this.overflowTrigger.addEventListener('click', (event) => {
            event.stopPropagation();
            const open = !this.overflowMenu.classList.contains('open');
            this.overflowMenu.classList.toggle('open', open);
            this.overflowTrigger.setAttribute('aria-expanded', open ? 'true' : 'false');
        });
        // Any command closes the menu, and so does a tap outside it.
        this.overflowPopup.addEventListener('click', () => this.closeOverflow());
        document.addEventListener('click', () => this.closeOverflow());
        this.compactQuery.addEventListener('change', () => {
            this.applyLayout();
            this.renderRows();
        });
        this.connectSplitter();
    }

    closeOverflow() {
        this.overflowMenu.classList.remove('open');
        this.overflowTrigger.setAttribute('aria-expanded', 'false');
    }

    // --- Data ------------------------------------------------------------
    selectedTypeGroupId() {
        const type = this.types.find((candidate) => candidate.id === this.filters.typeId);
        return type && type.groupId ? type.groupId : 0;
    }

    async refreshAll() {
        this.fieldCache = null; // Types and their fields may have changed.
        await this.refreshGroups();
        await this.refreshTypes();
        await this.refreshTagsAndFlags();
        await this.refreshSuggestions();
        await this.refreshItems();
        // Something may have changed the selected item, or selected a new one,
        // so the preview is reloaded rather than left showing what it held.
        this.updatePreviewVisibility();
        if (this.selectedItemId !== null) await this.loadPreview(this.selectedItemId);
    }

    setChoices(key, options) {
        const select = this.filterControls.get(key);
        if (!select) return;
        fillSelect(select, options, this.filters[key] || '');
        if (select.selectedIndex < 0) {
            // The previously selected value disappeared; fall back to "all".
            select.value = '';
            this.filters[key] = key === 'groupId' || key === 'typeId' ? 0 : '';
        }
    }

    async refreshGroups() {
        this.groups = await api.groups();
        this.setChoices('groupId', [
            { value: '', label: 'All groups' },
            ...this.groups.map((group) => ({ value: group.id, label: group.name })),
        ]);
    }

    async refreshTypes() {
        const groupId = this.filters.groupId;
        this.types = await api.types(groupId > 0 ? groupId : undefined);
        this.setChoices('typeId', [
            { value: '', label: 'All types' },
            ...this.types.map((type) => ({
                value: type.id,
                label: typeDisplayName(type),
                title: type.description,
            })),
        ]);
        await this.refreshTypeFields();
    }

    async refreshTypeFields() {
        this.typeFields = this.filters.typeId > 0 ? await api.fields(this.filters.typeId) : [];
        this.updateDynamicColumns();
    }

    async refreshTagsAndFlags() {
        const [tags, flags] = await Promise.all([api.tagUsage(), api.flagUsage()]);
        this.tagValues = tags.map((usage) => usage.value);
        this.flagValues = flags.map((usage) => usage.value);
        this.setChoices('tag', [
            { value: '', label: 'All tags' },
            ...this.tagValues.map((value) => ({ value, label: value })),
        ]);
        this.setChoices('flag', [
            { value: '', label: 'All flags' },
            ...this.flagValues.map((value) => ({ value, label: value })),
        ]);
    }

    async refreshSuggestions() {
        try {
            fillDatalist(this.searchSuggestions, await api.suggestions());
        } catch (error) {
            // Suggestions are a convenience; the table still works without them.
        }
    }

    buildQuery() {
        const valueFilters = [];
        for (const [fieldId, entry] of this.valueFilterControls) {
            const value = entry.node.value.trim();
            if (!value) continue;
            const exact = entry.field.dataType !== 'Text' && entry.field.dataType !== 'Other';
            valueFilters.push({ fieldId, value, exact });
        }
        return {
            groupId: this.filters.groupId || null,
            typeId: this.filters.typeId || null,
            searchText: this.searchInput.value.trim(),
            columnFilters: {
                id: this.filters.id,
                title: this.filters.title,
                disambiguation: this.filters.disambiguation,
                alias: this.filters.alias,
            },
            propertyFilters: this.propertyFilters,
            valueFilters,
            tagFilter: this.filters.tag,
            flagFilter: this.filters.flag,
            understandingFilter: this.filters.understanding || null,
            statusFilter: this.filters.status || null,
            pinnedFilter: this.filters.pinned === '' ? null : this.filters.pinned === 'true',
            limit: this.pageSize,
            offset: this.page * this.pageSize,
            sortColumn: this.sortColumn,
            sortOrder: this.sortOrder,
        };
    }

    totalPages() {
        return Math.max(1, Math.ceil(this.totalCount / this.pageSize));
    }

    async refreshItems() {
        await this.prepareLastItemSearch();
        let result;
        try {
            result = await api.queryItems(this.buildQuery());
        } catch (error) {
            if (error.isUnauthorized) return;
            await errorDialog(error.message);
            return;
        }
        this.totalCount = result.totalCount;
        if (this.page >= this.totalPages()) {
            this.page = this.totalPages() - 1;
            result = await api.queryItems(this.buildQuery());
        }
        this.items = result.items;
        this.renderRows();
        this.renderPagination();
        await this.restoreLastItem();
    }

    // At startup the last item you looked at is searched for and reselected,
    // the same session restore the desktop client performs.
    async prepareLastItemSearch() {
        if (!this.pendingRestoreItemId || this.pendingRestoreItemId < 0) return;
        if (this.searchInput.value.trim()) return;
        try {
            const loaded = await api.getItem(this.pendingRestoreItemId);
            this.searchInput.value = loaded.item.title;
        } catch (error) {
            // The item is gone; fall through to the unfiltered list.
            this.pendingRestoreItemId = -1;
        }
    }

    async restoreLastItem() {
        if (!this.pendingRestoreItemId || this.pendingRestoreItemId < 0) return;
        const wanted = this.pendingRestoreItemId;
        this.pendingRestoreItemId = -1;
        if (this.items.some((item) => item.id === wanted)) await this.selectItem(wanted);
    }

    isHidden(column) {
        return Boolean(column.configurable) && this.columnVisibility[column.label] === false;
    }

    cellText(item, column) {
        switch (column.key) {
        case 'id': return String(item.id);
        case 'group': return item.groupName;
        case 'type': return item.itemTypeName;
        case 'title': return item.title;
        case 'disambiguation': return item.disambiguation;
        case 'tags': return joinValues(item.tags);
        case 'flags': return joinValues(item.flags);
        case 'aliases': return joinValues(item.aliases);
        case 'status': return statusLabel(item.status);
        case 'understanding': return understandingLabel(item.understanding);
        case 'pinned': return item.pinned ? 'Yes' : 'No';
        default:
            break;
        }
        if (column.field) {
            const value = (item.fieldValues || {})[String(column.field.id)] || '';
            // An image reads as its kind, not as its hash.
            return column.field.dataType === 'Image' ? describeImage(value) || value : value;
        }
        return '';
    }

    renderRows() {
        if (this.viewMode === 'list') this.renderCards();
        else this.renderTableRows();
        this.updateActions();
    }

    // The same rows, read as cards: a phone should not need a sideways scroll
    // to answer "what is this item".
    renderCards() {
        clear(this.itemList);
        if (!this.items.length) {
            this.itemList.appendChild(el('li', {
                class: 'empty',
                text: 'No items match the current filters.',
            }));
            return;
        }
        const visible = (key) => {
            const column = this.columns.find((candidate) => candidate.key === key);
            return column && !this.isHidden(column);
        };
        for (const item of this.items) {
            const selected = item.id === this.selectedItemId;
            const title = visible('disambiguation') && item.disambiguation
                ? `${item.title} [${item.disambiguation}]`
                : item.title;
            const meta = [item.groupName, item.itemTypeName].filter(Boolean).join(' \u00b7 ');
            const badges = el('div', { class: 'card-badges' });
            if (visible('status') && item.status !== 'None') {
                badges.appendChild(el('span', { class: 'badge', text: statusLabel(item.status) }));
            }
            if (visible('understanding') && item.understanding !== 'Unknown') {
                badges.appendChild(el('span', {
                    class: 'badge',
                    text: understandingLabel(item.understanding),
                }));
            }
            if (visible('pinned') && item.pinned) {
                badges.appendChild(el('span', { class: 'badge pinned', text: 'Pinned' }));
            }
            for (const [key, values] of [['tags', item.tags], ['flags', item.flags],
                ['aliases', item.aliases]]) {
                if (!visible(key) || !values || !values.length) continue;
                badges.appendChild(el('span', {
                    class: 'badge muted',
                    text: joinValues(values),
                }));
            }
            for (const column of this.columns) {
                if (!column.field) continue;
                const value = this.cellText(item, column);
                if (value) {
                    badges.appendChild(el('span', {
                        class: 'badge muted',
                        text: `${column.label}: ${value}`,
                    }));
                }
            }

            const card = el('li', {
                class: selected ? 'item-card selected' : 'item-card',
                tabindex: '0',
                role: 'option',
                'aria-selected': selected ? 'true' : 'false',
                onclick: () => this.selectItem(item.id),
                onkeydown: (event) => {
                    if (event.key === 'Enter') {
                        this.selectItem(item.id).then(() => this.editSelectedItem());
                        event.preventDefault();
                    }
                },
            }, [
                el('div', { class: 'card-main' }, [
                    el('div', { class: 'card-title', text: title }),
                    meta ? el('div', { class: 'card-meta', text: meta }) : null,
                    item.matchSnippet
                        ? el('div', { class: 'match-snippet', text: item.matchSnippet })
                        : null,
                    badges.childElementCount ? badges : null,
                ]),
                button('\u203a', {
                    class: 'card-open',
                    title: 'Edit this item',
                    'aria-label': `Edit ${item.title}`,
                    onclick: (event) => {
                        event.stopPropagation();
                        this.selectItem(item.id).then(() => this.editSelectedItem());
                    },
                }),
            ]);
            this.itemList.appendChild(card);
        }
    }

    renderTableRows() {
        clear(this.tableBody);
        if (!this.items.length) {
            this.tableBody.appendChild(el('tr', {}, [el('td', {
                class: 'empty',
                colspan: String(this.columns.length),
                text: 'No items match the current filters.',
            })]));
            return;
        }
        for (const item of this.items) {
            const selected = item.id === this.selectedItemId;
            const row = el('tr', {
                class: selected ? 'selected' : '',
                tabindex: '0',
                'aria-selected': selected ? 'true' : 'false',
                onclick: () => this.selectItem(item.id),
                ondblclick: () => this.editSelectedItem(),
                onkeydown: (event) => {
                    if (event.key === 'Enter') {
                        this.selectItem(item.id).then(() => this.editSelectedItem());
                        event.preventDefault();
                    }
                },
                onfocus: () => {
                    if (this.selectedItemId !== item.id) this.selectItem(item.id);
                },
            });
            this.columns.forEach((column) => {
                const cell = el('td', {
                    class: column.key === 'id' ? 'numeric' : '',
                    text: this.cellText(item, column),
                    'data-label': column.label,
                });
                // Why the search found it, under the title it belongs to.
                if (column.key === 'title' && item.matchSnippet) {
                    cell.appendChild(el('div', {
                        class: 'match-snippet',
                        text: item.matchSnippet,
                        title: item.matchSnippet,
                    }));
                }
                cell.hidden = this.isHidden(column);
                row.appendChild(cell);
            });
            this.tableBody.appendChild(row);
        }
    }

    renderPagination() {
        const pages = this.totalPages();
        this.pageLabel.textContent =
            `Page ${this.page + 1} of ${pages} (${this.totalCount} total)`;
        this.firstButton.disabled = this.page <= 0;
        this.prevButton.disabled = this.page <= 0;
        this.nextButton.disabled = this.page >= pages - 1;
        this.lastButton.disabled = this.page >= pages - 1;
    }

    updateActions() {
        const hasSelection = this.selectedItemId !== null
            && this.items.some((item) => item.id === this.selectedItemId);
        this.editButton.disabled = !hasSelection;
        this.deleteButton.disabled = !hasSelection;
    }

    async goToPage(page) {
        const target = Math.min(Math.max(0, page), this.totalPages() - 1);
        if (target === this.page) return;
        this.page = target;
        await this.refreshItems();
    }

    resetPaginationAndRefresh() {
        this.page = 0;
        return this.refreshItems();
    }

    toggleSort(index) {
        if (this.sortColumn === index) {
            this.sortOrder = this.sortOrder === 'Ascending' ? 'Descending' : 'Ascending';
        } else {
            this.sortColumn = index;
            this.sortOrder = 'Ascending';
        }
        this.updateSortIndicators();
        this.updateSortControl();
        this.resetPaginationAndRefresh();
    }

    // --- Selection and preview -------------------------------------------
    async selectItem(itemId) {
        this.selectedItemId = itemId;
        writeLocal(STORAGE.lastItem, String(itemId));
        const index = this.items.findIndex((item) => item.id === itemId);
        const rows = this.viewMode === 'list' ? this.itemList.children
                                              : this.tableBody.children;
        [...rows].forEach((row, position) => {
            const selected = position === index;
            row.classList.toggle('selected', selected);
            row.setAttribute('aria-selected', selected ? 'true' : 'false');
        });
        this.updateActions();
        this.updatePreviewVisibility();
        if (await this.loadPreview(itemId)) api.logItemRead(itemId).catch(() => {});
    }

    // Shows the item's current content and links. Returns false when it could
    // not, or when another item was selected while this one was loading.
    async loadPreview(itemId) {
        try {
            const loaded = await api.getItem(itemId, ['links', 'backlinks']);
            if (this.selectedItemId !== itemId) return false;
            const fields = await this.fieldsOfType(loaded.item.itemTypeId);
            if (this.selectedItemId !== itemId) return false;
            clear(this.contentPreview);
            if (loaded.item.content) renderMarkdown(this.contentPreview, loaded.item.content);
            else this.contentPreview.appendChild(
                el('p', { class: 'hint', text: 'This item has no content yet.' }));
            const images = imageSection(fields, loaded.item.fieldValues);
            if (images) this.contentPreview.appendChild(images);
            this.renderLinksPreview(loaded.links, loaded.backlinks);
            return true;
        } catch (error) {
            if (error.isUnauthorized || this.selectedItemId !== itemId) return false;
            clear(this.contentPreview);
            this.contentPreview.appendChild(
                el('p', { class: 'hint', text: `Error loading content: ${error.message}` }));
            clear(this.linksPreview);
            return false;
        }
    }

    // The fields of a type, for the images in the preview. Only types with an
    // Image field need them; the answer is kept until the next refresh.
    async fieldsOfType(typeId) {
        if (!(typeId > 0)) return [];
        this.fieldCache = this.fieldCache || new Map();
        if (!this.fieldCache.has(typeId)) {
            const loading = api.fields(typeId);
            loading.catch(() => this.fieldCache.delete(typeId));
            this.fieldCache.set(typeId, loading);
        }
        try {
            return await this.fieldCache.get(typeId);
        } catch {
            return [];
        }
    }

    renderLinksPreview(links, backlinks) {
        clear(this.linksPreview);
        const line = (label, entries, titleOf) => {
            const container = el('p', { class: 'links-line' },
                [el('strong', { text: `${label}: ` })]);
            if (!entries.length) {
                container.appendChild(document.createTextNode('None'));
                return container;
            }
            entries.forEach((link, index) => {
                if (index > 0) container.appendChild(document.createTextNode(', '));
                const title = titleOf(link);
                container.appendChild(el('a', {
                    href: '#',
                    class: 'item-link',
                    text: `${title} (${linkDescription(link)})`,
                    onclick: (event) => {
                        event.preventDefault();
                        this.navigateToTitle(title);
                    },
                }));
            });
            return container;
        };
        this.linksPreview.appendChild(line('Links', links, (link) => link.toItemTitle));
        this.linksPreview.appendChild(line('Backlinks', backlinks, (link) => link.fromItemTitle));
        if (links.length || backlinks.length) {
            this.linksPreview.appendChild(el('p', { class: 'links-line' }, [el('a', {
                href: '#',
                class: 'item-link',
                text: 'Relationship graph',
                onclick: (event) => {
                    event.preventDefault();
                    this.showGraph().catch((error) => errorDialog(error.message));
                },
            })]));
        }
    }

    // The items around the selected one; the one chosen there is shown here.
    async showGraph() {
        if (this.selectedItemId === null) {
            await messageDialog('Relationship graph', 'Select an item first.');
            return;
        }
        const chosen = await openGraph(this.selectedItemId);
        if (chosen === null) return;
        const loaded = await api.getItem(chosen);
        await this.navigateToTitle(loaded.item.title, chosen);
    }

    // A [[wiki link]] in the content: its item, or the offer to create it.
    async openWikiLink(target) {
        let itemId;
        try {
            itemId = await api.resolveItem(target.title, target.disambiguation);
        } catch (error) {
            if (error.isUnauthorized) return;
            if (error.status !== 404) {
                await errorDialog(error.message);
                return;
            }
            const name = formatItemTitle(target.title, target.disambiguation);
            if (await confirmDialog('Create item', `No item is called '${name}'. Create it?`)) {
                await this.addItem(target.title, target.disambiguation);
            }
            return;
        }
        try {
            const loaded = await api.getItem(itemId);
            await this.navigateToTitle(loaded.item.title, itemId);
        } catch (error) {
            if (!error.isUnauthorized) await errorDialog(error.message);
        }
    }

    // Following a link clears the filters and searches for the target, like the
    // desktop link view, and selects the item with itemId or the first one.
    async navigateToTitle(title, itemId = null) {
        this.filters = {
            groupId: 0, typeId: 0, id: '', title: '', disambiguation: '', alias: '',
            tag: '', flag: '', status: '', understanding: '', pinned: '', values: {},
        };
        for (const [key, control] of this.filterControls) {
            control.value = typeof this.filters[key] === 'number' ? '' : this.filters[key];
        }
        this.propertyFilters = [];
        this.updatePropertyFilterButton();
        this.searchInput.value = title;
        await this.refreshTypes();
        await this.resetPaginationAndRefresh();
        if (this.items.length) {
            const index = Math.max(0, this.items.findIndex((item) => item.id === itemId));
            await this.selectItem(this.items[index].id);
            const rows = this.viewMode === 'list' ? this.itemList.children
                                                  : this.tableBody.children;
            if (rows[index]) rows[index].focus();
        }
    }

    // --- Actions ----------------------------------------------------------
    async groupIdForNewItem() {
        if (this.filters.groupId > 0) return this.filters.groupId;
        const typeGroup = this.selectedTypeGroupId();
        if (typeGroup > 0) return typeGroup;
        const defaultGroupId = await api.defaultGroupId();
        await this.refreshGroups();
        return defaultGroupId;
    }

    // Enter in the search field: show what matches the text, and add it as a
    // new item only when nothing does.
    async searchOrAdd() {
        const text = this.searchInput.value.trim();
        if (!text) return;
        await this.resetPaginationAndRefresh();
        if (this.searchInput.value.trim() !== text) return; // Typing went on.
        if (this.totalCount === 0) {
            await this.quickAdd();
            return;
        }
        const match = this.items.find((item) => namedExactly(item, text))
            || (this.items.length === 1 ? this.items[0] : null);
        if (match) await this.selectItem(match.id);
        // On a touch screen the keyboard would go on covering the result.
        if (window.matchMedia('(pointer: coarse)').matches) this.searchInput.blur();
    }

    // Items anywhere whose title, full title or alias is exactly this text,
    // ignoring case, whatever the current filters show.
    async itemsNamed(text) {
        const [byTitle, byAlias] = await Promise.all([
            api.queryItems({ columnFilters: { title: text }, limit: 1000 }),
            api.queryItems({ columnFilters: { alias: text }, limit: 1000 }),
        ]);
        const found = new Map();
        for (const item of [...byTitle.items, ...byAlias.items]) {
            if (namedExactly(item, text)) found.set(item.id, item);
        }
        return [...found.values()];
    }

    // Returns true when the new item should be added after all.
    async confirmAnotherItem(title, groupId, existing) {
        // The database holds one item per title and group, so that one can
        // only be shown, not added again.
        const twin = existing.find((item) => item.groupId === groupId
            && item.title === title && !item.disambiguation);
        if (twin) {
            const show = await confirmDialog('Already in Lexicon',
                `"${title}" already exists in the group ${twin.groupName}.`,
                { acceptLabel: 'Show it', cancelLabel: 'Close' });
            if (show) await this.showItem(twin);
            return false;
        }
        const lines = existing.slice(0, 5).map((item) => {
            const name = formatItemTitle(item.title, item.disambiguation);
            const byTitle = item.title.toLocaleLowerCase() === title.toLocaleLowerCase()
                || name.toLocaleLowerCase() === title.toLocaleLowerCase();
            return `- ${name} (${item.groupName})${byTitle ? '' : `, alias "${title}"`}`;
        });
        if (existing.length > lines.length) lines.push(`- and ${existing.length - lines.length} more`);
        const choice = await openDialog({
            title: 'Already in Lexicon',
            body: el('p', {
                class: 'confirm',
                text: `"${title}" already names:\n${lines.join('\n')}\n\nAdd another item called "${title}"?`,
            }),
            acceptLabel: 'Add anyway',
            extraActions: [{ label: 'Show it', onClick: ({ close }) => close('show') }],
            onAccept: () => 'add',
        });
        if (choice === 'show') await this.showItem(existing[0]);
        return choice === 'add';
    }

    async showItem(item) {
        this.searchInput.value = item.title;
        await this.resetPaginationAndRefresh();
        await this.selectItem(item.id);
    }

    async quickAdd() {
        const title = this.searchInput.value.trim();
        if (!title) return;
        try {
            const groupId = await this.groupIdForNewItem();
            const existing = await this.itemsNamed(title);
            if (existing.length && !(await this.confirmAnotherItem(title, groupId, existing))) return;
            await api.createItem({
                item: {
                    groupId,
                    itemTypeId: this.filters.typeId > 0 ? this.filters.typeId : null,
                    title,
                },
            });
            this.searchInput.value = '';
            await this.refreshAll();
        } catch (error) {
            if (!error.isUnauthorized) await errorDialog(error.message);
        }
    }

    // An idea, caught quickly: a title and plain text, saved to Default without
    // a type whatever the filters show.
    async openInbox() {
        const title = el('input', { type: 'text', maxlength: '2000', autocomplete: 'off' });
        const content = el('textarea', { rows: '8', class: 'inbox-content', placeholder: 'Plain text' });
        const saved = await openDialog({
            title: 'Inbox',
            body: el('div', {}, [
                el('p', { class: 'hint', text: 'Saved to Default, without a type. Sort it out later.' }),
                field('Title:', title),
                field('Idea:', content),
            ]),
            acceptLabel: 'Save',
            initialFocus: title,
            // "Enter a title." or "already exists" goes once the title changes.
            clearErrorOn: [title],
            // A stray tap beside the dialog must not throw the idea away.
            closeOnBackdrop: false,
            onAccept: async ({ fail }) => {
                const text = title.value.trim();
                if (!text) {
                    fail('Enter a title.');
                    title.focus();
                    return undefined;
                }
                try {
                    const groupId = await api.defaultGroupId();
                    const result = await api.createItem({
                        item: { groupId, itemTypeId: null, title: text, content: content.value },
                    });
                    return result.id;
                } catch (error) {
                    fail(error.message);
                    return undefined;
                }
            },
        });
        if (!saved) return;
        await this.refreshGroups();
        await this.refreshItems();
    }

    async addItem(title = this.searchInput.value.trim(), disambiguation = '') {
        try {
            const groupId = await this.groupIdForNewItem();
            const savedId = await openItemEditor({
                itemId: null,
                groups: this.groups,
                draft: {
                    groupId,
                    itemTypeId: this.filters.typeId > 0 ? this.filters.typeId : null,
                    title,
                    disambiguation,
                    status: 'None',
                    understanding: 'Unknown',
                    pinned: false,
                    content: '',
                    tags: [], flags: [], aliases: [], properties: [], fieldValues: {},
                },
            });
            if (savedId) {
                this.selectedItemId = savedId;
                await this.refreshAll();
            }
        } catch (error) {
            if (!error.isUnauthorized) await errorDialog(error.message);
        }
    }

    // Changes a closed or discarded tab never saved: offer to carry on with
    // the most recent ones. The others wait until their item is opened.
    async resumeDraft() {
        const stored = latestDraft();
        if (!stored) return;
        const choice = await askAboutDraft(stored, 'Later');
        if (choice === 'discard') clearDraft(stored.itemId);
        if (choice !== 'use') return;
        let { itemId, state } = stored;
        if (itemId) {
            try {
                await api.getItem(itemId);
            } catch (error) {
                if (error.status !== 404) throw error;
                await messageDialog('Unsaved changes',
                    'The item was deleted in the meantime, so your changes open as a new item.');
                clearDraft(itemId);
                itemId = null;
                // Link IDs belonged to the deleted item.
                const unsaved = (link) => ({ ...link, id: null });
                state = { ...state, links: state.links.map(unsaved),
                    backlinks: state.backlinks.map(unsaved) };
            }
        }
        const savedId = await openItemEditor({ itemId, groups: this.groups, restore: state });
        if (savedId) {
            this.selectedItemId = savedId;
            await this.refreshAll();
        }
    }

    async editSelectedItem() {
        if (this.selectedItemId === null) return;
        try {
            const savedId = await openItemEditor({
                itemId: this.selectedItemId,
                groups: this.groups,
            });
            if (savedId) await this.refreshAll();
        } catch (error) {
            if (!error.isUnauthorized) await errorDialog(error.message);
        }
    }

    async deleteSelectedItem() {
        if (this.selectedItemId === null) return;
        const item = this.items.find((candidate) => candidate.id === this.selectedItemId);
        if (!item) return;
        const confirmed = await confirmDialog('Delete item',
            `Delete item '${item.title}'?`, { danger: true });
        if (!confirmed) return;
        try {
            await api.deleteItem(item.id);
            this.selectedItemId = null;
            clear(this.contentPreview);
            clear(this.linksPreview);
            this.updatePreviewVisibility();
            await this.refreshAll();
        } catch (error) {
            if (!error.isUnauthorized) await errorDialog(error.message);
        }
    }

    async chooseColumns() {
        const visibility = await openColumnDialog(CONFIGURABLE_COLUMNS, this.columnVisibility);
        if (!visibility) return;
        this.columnVisibility = visibility;
        // A web preference, stored per browser. The desktop keeps its own.
        writeLocal(STORAGE.columns, JSON.stringify(visibility));
        // A hidden column must not keep filtering the result set.
        const clearFilter = (label, key) => {
            if (visibility[label]) return;
            this.filters[key] = '';
            const control = this.filterControls.get(key);
            if (control) control.value = '';
        };
        clearFilter('Disambiguation', 'disambiguation');
        clearFilter('Tags', 'tag');
        clearFilter('Flags', 'flag');
        clearFilter('Aliases', 'alias');
        clearFilter('Status', 'status');
        clearFilter('Understanding', 'understanding');
        clearFilter('Pinned', 'pinned');
        this.applyColumnVisibility();
        this.placeFilterWidgets();
        await this.resetPaginationAndRefresh();
    }

    async choosePropertyFilters() {
        const filters = await openPropertyFilterDialog(this.propertyFilters);
        if (!filters) return;
        this.propertyFilters = filters;
        this.updatePropertyFilterButton();
        await this.resetPaginationAndRefresh();
    }

    updatePropertyFilterButton() {
        this.propertyFilterButton.textContent = this.propertyFilters.length
            ? `Filter Properties (${this.propertyFilters.length})...`
            : 'Filter Properties...';
    }
}
