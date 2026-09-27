import assert from 'node:assert/strict';
import { test } from 'node:test';

import { api } from '../js/api.js';
import { foreignKeyControl } from '../js/foreignKey.js';

class Element {
    constructor(tag) {
        this.tagName = tag;
        this.children = [];
        this.listeners = new Map();
        this.attributes = new Map();
        this.value = '';
        this.textContent = '';
        this.hidden = false;
    }
    setAttribute(key, value) { this.attributes.set(key, value); }
    removeAttribute(key) { this.attributes.delete(key); }
    addEventListener(name, handler) {
        const handlers = this.listeners.get(name) || [];
        handlers.push(handler);
        this.listeners.set(name, handlers);
    }
    appendChild(child) { this.children.push(child); return child; }
    replaceChildren(...children) { this.children = children; }
    dispatchEvent(event) {
        for (const handler of this.listeners.get(event.type) || []) handler(event);
        return true;
    }
    focus() {}
}

test('title suggestions are limited to the target type and save the chosen ID', async () => {
    const oldDocument = globalThis.document;
    const oldQuery = api.queryItems;
    const oldGet = api.getItem;
    globalThis.document = {
        createElement: (tag) => new Element(tag),
        createTextNode: (value) => ({ textContent: value }),
    };
    let request;
    api.getItem = async (id) => ({ item: { id: Number(id), itemTypeId: 8, title: 'Original' } });
    api.queryItems = async (query) => {
        request = query;
        return { items: [{ id: 42, itemTypeId: 8, title: 'Vector space', disambiguation: '' }] };
    };
    try {
        const changed = [];
        const control = foreignKeyControl({ id: 5, targetItemTypeId: 8 }, '7', (value) => changed.push(value));
        const [input, options] = control.children;
        await Promise.resolve();
        assert.match(input.value, /Original/);
        assert.equal(control.value, '7');

        input.value = 'vector';
        input.dispatchEvent(new Event('input'));
        assert.equal(control.value, 'vector', 'typing invalidates the previous ID until a result is chosen');
        await new Promise((resolve) => setTimeout(resolve, 230));
        assert.equal(request.typeId, 8);
        assert.equal(request.columnFilters.title, 'vector');
        assert.equal(options.children.length, 1);
        options.children[0].dispatchEvent(new Event('click'));
        assert.equal(control.value, '42');
        assert.match(input.value, /Vector space/);
        assert.equal(changed.at(-1), '42');
    } finally {
        globalThis.document = oldDocument;
        api.queryItems = oldQuery;
        api.getItem = oldGet;
    }
});
