// The main window: search and actions, the filtered item table with its filter
// row, pagination, the Markdown preview and the link/backlink preview.
import { api } from './api.js';
import { confirmDialog, errorDialog } from './dialogs.js';
import { openItemEditor } from './itemEdit.js';
import { renderMarkdown } from './markdown.js';
import { openColumnDialog, openPropertyFilterDialog } from './overviews.js';
import {
    button, clear, debounce, el, fillDatalist, fillSelect, ITEM_STATUSES, joinValues,
    linkDescription, readLocal, statusLabel, typeDisplayName, UNDERSTANDING_LEVELS,
    understandingLabel, writeLocal,
} from './utils.js';

const BASE_COLUMNS = [
    { key: 'id', label: 'Id', filter: 'id' },
    { key: 'group', label: 'Group', filter: 'group' },
    { key: 'type', label: 'Type', filter: 'type' },
    { key: 'title', label: 'Title', filter: 'title' },
    { key: 'disambiguation', label: 'Disambiguation', filter: 'disambiguation', configurable: true },
    { key: 'tags', label: 'Tags', filter: 'tag', configurable: true },
    { key: 'flags', label: 'Flags', filter: 'flag', configurable: true },
    { key: 'aliases', label: 'Aliases', filter: 'alias', configurable: true },
    { key: 'status', label: 'Status', filter: 'status', configurable: true },
    { key: 'understanding', label: 'Understanding', filter: 'understanding', configurable: true },
    { key: 'pinned', label: 'Pinned', filter: 'pinned', configurable: true },
];
const CONFIGURABLE_COLUMNS = BASE_COLUMNS.filter((column) => column.configurable)
    .map((column) => column.label);
const PAGE_SIZES = [10, 20, 50, 100];
const STORAGE = {
    pageSize: 'lexicon.web.pageSize',
    columns: 'lexicon.web.columns',
    lastItem: 'lexicon.web.lastItemId',
};

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
            placeholder: 'Search title, disambiguation, alias, tag, or flag...',
            list: 'search-suggestions',
            autocomplete: 'off',
        });
        this.searchSuggestions = el('datalist', { id: 'search-suggestions' });
        this.quickAddButton = button('Add', {
            class: 'primary',
            title: 'Quick add the search text as a new item',
        });
        this.addButton = button('Add ...', { class: 'secondary' });
        this.editButton = button('Edit', { class: 'secondary', disabled: true });
        this.deleteButton = button('Delete', { class: 'secondary', disabled: true });
        this.columnsButton = button('Columns...', { class: 'secondary' });
        this.propertyFilterButton = button('Filter Properties...', { class: 'secondary' });

        const actionBar = el('section', { class: 'action-bar' }, [
            el('label', { class: 'search-label', for: 'search-input', text: 'Search:' }),
            this.searchInput,
            this.searchSuggestions,
            el('div', { class: 'action-buttons' }, [
                this.quickAddButton, this.addButton, this.editButton,
                this.deleteButton, this.columnsButton, this.propertyFilterButton,
            ]),
        ]);

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
        this.linksPreview = el('div', { class: 'links-preview' });

        this.root.appendChild(actionBar);
        this.root.appendChild(el('div', { class: 'table-wrapper' }, [this.table]));
        this.root.appendChild(pagination);
        this.root.appendChild(el('section', { class: 'preview-area' }, [
            this.contentPreview,
            this.linksPreview,
        ]));

        this.buildHeader();
        this.connect();
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

    connect() {
        const search = debounce(() => this.resetPaginationAndRefresh(), 250);
        this.searchInput.addEventListener('input', search);
        this.searchInput.addEventListener('keydown', (event) => {
            if (event.key === 'Enter') {
                // Enter is the fast path for the mobile capture workflow.
                event.preventDefault();
                this.quickAdd();
            }
        });
        this.quickAddButton.addEventListener('click', () => this.quickAdd());
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
    }

    // --- Data ------------------------------------------------------------
    selectedTypeGroupId() {
        const type = this.types.find((candidate) => candidate.id === this.filters.typeId);
        return type && type.groupId ? type.groupId : 0;
    }

    async refreshAll() {
        await this.refreshGroups();
        await this.refreshTypes();
        await this.refreshTagsAndFlags();
        await this.refreshSuggestions();
        await this.refreshItems();
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
        if (column.field) return (item.fieldValues || {})[String(column.field.id)] || '';
        return '';
    }

    renderRows() {
        clear(this.tableBody);
        if (!this.items.length) {
            this.tableBody.appendChild(el('tr', {}, [el('td', {
                class: 'empty',
                colspan: String(this.columns.length),
                text: 'No items match the current filters.',
            })]));
            this.updateActions();
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
                cell.hidden = this.isHidden(column);
                row.appendChild(cell);
            });
            this.tableBody.appendChild(row);
        }
        this.updateActions();
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
        this.resetPaginationAndRefresh();
    }

    // --- Selection and preview -------------------------------------------
    async selectItem(itemId) {
        this.selectedItemId = itemId;
        writeLocal(STORAGE.lastItem, String(itemId));
        const index = this.items.findIndex((item) => item.id === itemId);
        [...this.tableBody.children].forEach((row, position) => {
            const selected = position === index;
            row.classList.toggle('selected', selected);
            row.setAttribute('aria-selected', selected ? 'true' : 'false');
        });
        this.updateActions();
        try {
            const loaded = await api.getItem(itemId, ['links', 'backlinks']);
            clear(this.contentPreview);
            if (loaded.item.content) renderMarkdown(this.contentPreview, loaded.item.content);
            else this.contentPreview.appendChild(
                el('p', { class: 'hint', text: 'This item has no content yet.' }));
            this.renderLinksPreview(loaded.links, loaded.backlinks);
            api.logItemRead(itemId).catch(() => {});
        } catch (error) {
            if (error.isUnauthorized) return;
            clear(this.contentPreview);
            this.contentPreview.appendChild(
                el('p', { class: 'hint', text: `Error loading content: ${error.message}` }));
            clear(this.linksPreview);
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
    }

    // Following a link clears the filters and searches for the target, like the
    // desktop link view.
    async navigateToTitle(title) {
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
            await this.selectItem(this.items[0].id);
            const row = this.tableBody.children[0];
            if (row) row.focus();
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

    async quickAdd() {
        const title = this.searchInput.value.trim();
        if (!title) return;
        try {
            const groupId = await this.groupIdForNewItem();
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

    async addItem() {
        try {
            const groupId = await this.groupIdForNewItem();
            const savedId = await openItemEditor({
                itemId: null,
                groups: this.groups,
                draft: {
                    groupId,
                    itemTypeId: this.filters.typeId > 0 ? this.filters.typeId : null,
                    title: this.searchInput.value.trim(),
                    disambiguation: '',
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
