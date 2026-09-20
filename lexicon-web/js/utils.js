// Small DOM and domain helpers. Everything here builds nodes with textContent
// and setAttribute; untrusted strings are never concatenated into HTML.

export function el(tag, options = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(options)) {
        if (value === undefined || value === null) continue;
        if (key === 'class') node.className = value;
        else if (key === 'text') node.textContent = value;
        else if (key === 'dataset') Object.assign(node.dataset, value);
        else if (key === 'style') Object.assign(node.style, value);
        else if (key.startsWith('on') && typeof value === 'function') {
            node.addEventListener(key.slice(2).toLowerCase(), value);
        } else if (typeof value === 'boolean') {
            if (value) node.setAttribute(key, '');
            else node.removeAttribute(key);
        } else {
            node.setAttribute(key, String(value));
        }
    }
    for (const child of [].concat(children)) {
        if (child === undefined || child === null) continue;
        node.appendChild(typeof child === 'string' ? document.createTextNode(child) : child);
    }
    return node;
}

export function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
}

export function button(label, options = {}) {
    return el('button', { type: 'button', ...options, text: label });
}

export function debounce(callback, delay) {
    let timer = 0;
    const wrapped = (...args) => {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => callback(...args), delay);
    };
    wrapped.cancel = () => window.clearTimeout(timer);
    return wrapped;
}

export function fillSelect(select, options, selectedValue) {
    clear(select);
    for (const option of options) {
        const node = el('option', { value: String(option.value), text: option.label });
        if (option.title) node.title = option.title;
        select.appendChild(node);
    }
    if (selectedValue !== undefined && selectedValue !== null) {
        select.value = String(selectedValue);
        if (select.selectedIndex < 0 && select.options.length > 0) select.selectedIndex = 0;
    }
}

export function fillDatalist(datalist, values) {
    clear(datalist);
    for (const value of values) datalist.appendChild(el('option', { value }));
}

// Item status, understanding, link types and field data types use the stable
// symbolic names of the REST API. Labels match the desktop client.
export const ITEM_STATUSES = [
    { value: 'None', label: 'None' },
    { value: 'Draft', label: 'Draft' },
    { value: 'Completed', label: 'Completed' },
];

export const UNDERSTANDING_LEVELS = [
    { value: 'Unknown', label: 'Unknown', title: 'Never encountered' },
    { value: 'Recognized', label: 'Recognized', title: 'Seen before, can identify' },
    { value: 'Understood', label: 'Understood', title: 'Conceptually grasped' },
    { value: 'Practiced', label: 'Practiced', title: 'Can apply in real situations' },
    { value: 'Mastered', label: 'Mastered', title: 'Fully internalized, can teach or innovate' },
];

export const LINK_TYPES = [
    { value: 'IsA', label: 'Is A' },
    { value: 'PartOf', label: 'Part Of' },
    { value: 'Uses', label: 'Uses' },
    { value: 'DependsOn', label: 'Depends On' },
    { value: 'Implements', label: 'Implements' },
    { value: 'Related', label: 'Related' },
    { value: 'Contrasts', label: 'Contrasts' },
    { value: 'AlternativeTo', label: 'Alternative To' },
    { value: 'ParentOf', label: 'Parent Of' },
    { value: 'Custom', label: 'Custom' },
];

export const FIELD_DATA_TYPES = [
    'Integer', 'Float', 'Text', 'Date', 'Time',
    'Timestamp', 'Boolean', 'Enum', 'Blob', 'Other',
].map((value) => ({ value, label: value }));

export function linkTypeLabel(value) {
    const found = LINK_TYPES.find((type) => type.value === value);
    return found ? found.label : 'Link';
}

// "Custom: generalizes" for custom links, the plain label otherwise.
export function linkDescription(link) {
    if (link.linkType === 'Custom' && link.customValue) return `Custom: ${link.customValue}`;
    return linkTypeLabel(link.linkType);
}

export function statusLabel(value) {
    const found = ITEM_STATUSES.find((status) => status.value === value);
    return found ? found.label : 'None';
}

export function understandingLabel(value) {
    const found = UNDERSTANDING_LEVELS.find((level) => level.value === value);
    return found ? found.label : 'Unknown';
}

// "Title [disambiguation]", the format used by the item title suggestions.
export function formatItemTitle(title, disambiguation) {
    return disambiguation ? `${title} [${disambiguation}]` : title;
}

export function splitItemTitle(text) {
    const trimmed = (text || '').trim();
    if (trimmed.endsWith(']') && trimmed.includes(' [')) {
        const index = trimmed.lastIndexOf(' [');
        return {
            title: trimmed.slice(0, index).trim(),
            disambiguation: trimmed.slice(index + 2, trimmed.length - 1).trim(),
        };
    }
    return { title: trimmed, disambiguation: '' };
}

export function typeScopeLabel(type) {
    return type.groupId === null || type.groupId === undefined ? 'All groups' : type.groupName;
}

export function typeDisplayName(type) {
    return `${type.name} (${typeScopeLabel(type)})`;
}

export function joinValues(values) {
    return (values || []).join(', ');
}

export function readLocal(key, fallback) {
    try {
        const stored = window.localStorage.getItem(key);
        return stored === null ? fallback : stored;
    } catch (error) {
        return fallback;
    }
}

export function writeLocal(key, value) {
    try {
        window.localStorage.setItem(key, value);
    } catch (error) {
        // Private browsing modes may refuse storage; preferences are optional.
    }
}

export function readSession(key) {
    try {
        return window.sessionStorage.getItem(key);
    } catch (error) {
        return null;
    }
}

export function writeSession(key, value) {
    try {
        if (value === null) window.sessionStorage.removeItem(key);
        else window.sessionStorage.setItem(key, value);
    } catch (error) {
        // Without session storage the user simply logs in again on reload.
    }
}
