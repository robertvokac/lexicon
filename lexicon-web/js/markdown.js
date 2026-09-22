// Markdown rendering. The parser output is sanitized before it ever touches
// the DOM, and no other module assigns generated HTML anywhere.
import { marked } from '../vendor/marked.esm.js';
import DOMPurify from '../vendor/purify.es.mjs';
import { highlightCode } from './highlight.js';
import { ITEM_HREF, parseItemHref, wikiLinkExtension } from './wikilinks.js';

export { findWikiLinks } from './wikilinks.js';

marked.setOptions({ gfm: true, breaks: false, headerIds: false, mangle: false });
marked.use({ extensions: [wikiLinkExtension] });

// Links in item content point outside the application, so they open in a new
// tab without handing the target a window reference. Links to items stay.
DOMPurify.addHook('afterSanitizeAttributes', (node) => {
    if (node.tagName === 'A' && node.hasAttribute('href')
        && !node.getAttribute('href').startsWith(ITEM_HREF)) {
        node.setAttribute('target', '_blank');
        node.setAttribute('rel', 'noopener noreferrer nofollow');
    }
});

const SANITIZE_OPTIONS = {
    ALLOWED_TAGS: [
        'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'br', 'hr',
        'strong', 'em', 'del', 'ins', 'sub', 'sup', 'small',
        'blockquote', 'pre', 'code', 'span',
        'ul', 'ol', 'li', 'dl', 'dt', 'dd',
        'table', 'thead', 'tbody', 'tfoot', 'tr', 'th', 'td',
        'a', 'img',
    ],
    ALLOWED_ATTR: ['href', 'title', 'alt', 'src', 'class', 'align', 'target', 'rel'],
    ALLOWED_URI_REGEXP: /^(?:https?:|mailto:|tel:|#item:|data:image\/(?:png|jpeg|gif|webp);base64,)/i,
    FORBID_TAGS: ['script', 'style', 'iframe', 'object', 'embed', 'form', 'input', 'svg', 'math'],
    FORBID_ATTR: ['style', 'srcset', 'formaction', 'onerror', 'onload'],
};

export function markdownToSafeHtml(text) {
    const rendered = marked.parse(String(text ?? ''), { async: false });
    return DOMPurify.sanitize(rendered, SANITIZE_OPTIONS);
}

export function renderMarkdown(target, text) {
    target.innerHTML = markdownToSafeHtml(text);
    highlightCode(target);
}

// Calls onItemLink({ title, disambiguation }) for a click on a wiki link in
// root instead of following it.
export function bindItemLinks(root, onItemLink) {
    root.addEventListener('click', (event) => {
        const anchor = event.target.closest ? event.target.closest('a.wiki-link') : null;
        if (!anchor || !root.contains(anchor)) return;
        event.preventDefault();
        onItemLink(parseItemHref(anchor.getAttribute('href')));
    });
}
