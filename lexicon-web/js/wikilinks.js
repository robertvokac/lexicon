// Wiki links in item content: [[Title]], [[Title [disambiguation]]], and
// either with |shown text - the syntax of lexicon-core/WikiLinks.h. Only
// marked is needed here, so this module also runs under Node for its tests.
import { Marked } from '../vendor/marked.esm.js';

const WIKI_LINK = /^\[\[([^[\]|\n]+?)(?:\s*\[([^[\]\n]*)\])?(?:\s*\|([^[\]\n]+))?\]\]/;

// The link a rendered wiki link carries: this prefix and the encoded target.
export const ITEM_HREF = '#item:';

function escapeHtml(text) {
    return String(text).replace(/[&<>"']/g, (ch) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[ch]));
}

export function formatTarget(title, disambiguation) {
    return disambiguation ? `${title} [${disambiguation}]` : title;
}

// Marked runs an inline extension outside code spans and code blocks only,
// so [[x]] in an example keeps its brackets.
export const wikiLinkExtension = {
    name: 'wikiLink',
    level: 'inline',
    start(source) {
        const index = source.indexOf('[[');
        return index < 0 ? undefined : index;
    },
    tokenizer(source) {
        const match = WIKI_LINK.exec(source);
        if (!match || !match[1].trim()) return undefined;
        return {
            type: 'wikiLink',
            raw: match[0],
            title: match[1].trim(),
            disambiguation: (match[2] || '').trim(),
            label: (match[3] || '').trim(),
        };
    },
    renderer(token) {
        const href = ITEM_HREF + encodeURIComponent(formatTarget(token.title, token.disambiguation));
        return `<a href="${href}" class="wiki-link">${escapeHtml(token.label || token.title)}</a>`;
    },
};

// A separate instance, so finding links never depends on how the renderer
// was set up.
const finder = new Marked({ gfm: true });
finder.use({ extensions: [wikiLinkExtension] });

// Every wiki link in the text, in order, skipping code: { title,
// disambiguation, label }.
export function findWikiLinks(text) {
    const links = [];
    finder.walkTokens(finder.lexer(String(text ?? '')), (token) => {
        if (token.type === 'wikiLink') {
            links.push({ title: token.title, disambiguation: token.disambiguation, label: token.label });
        }
    });
    return links;
}

// { title, disambiguation } of a rendered wiki link's href.
export function parseItemHref(href) {
    const target = decodeURIComponent(String(href).slice(ITEM_HREF.length));
    const open = target.endsWith(']') ? target.lastIndexOf(' [') : -1;
    return open > 0
        ? { title: target.slice(0, open).trim(), disambiguation: target.slice(open + 2, -1).trim() }
        : { title: target.trim(), disambiguation: '' };
}
