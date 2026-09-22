// node --test lexicon-web/tests: the parts of the web client that need no DOM.
import assert from 'node:assert/strict';
import test from 'node:test';
import { Marked } from '../vendor/marked.esm.js';
import { findWikiLinks, parseItemHref, wikiLinkExtension } from '../js/wikilinks.js';

test('wiki links are found outside code', () => {
    const links = findWikiLinks('See [[Monoid]], [[Monoid [algebra]]], [[ Group [algebra] | groups ]]\n\n'
        + '`[[code]]`\n\n```\n[[fenced]]\n```\n\n    [[indented]]\n\n- in a [[List item]]\n\n'
        + '| a |\n| - |\n| [[In table]] |\n\n\\[[escaped]]');
    assert.deepEqual(links, [
        { title: 'Monoid', disambiguation: '', label: '' },
        { title: 'Monoid', disambiguation: 'algebra', label: '' },
        { title: 'Group', disambiguation: 'algebra', label: 'groups' },
        { title: 'List item', disambiguation: '', label: '' },
        { title: 'In table', disambiguation: '', label: '' },
    ]);
});

test('a wiki link renders as an item link with escaped text', () => {
    const marked = new Marked({ gfm: true });
    marked.use({ extensions: [wikiLinkExtension] });
    const html = marked.parse('A [[Monoid [algebra]|<b>monoid</b>]] and `[[x]]`.');
    assert.match(html, /<a href="#item:Monoid%20%5Balgebra%5D" class="wiki-link">&lt;b&gt;monoid&lt;\/b&gt;<\/a>/);
    assert.match(html, /<code>\[\[x\]\]<\/code>/);
});

test('an item href splits into title and disambiguation', () => {
    assert.deepEqual(parseItemHref('#item:Monoid%20%5Balgebra%5D'), { title: 'Monoid', disambiguation: 'algebra' });
    assert.deepEqual(parseItemHref('#item:C%2B%2B'), { title: 'C++', disambiguation: '' });
});
