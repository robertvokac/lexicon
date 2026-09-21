// C++ highlighting for code blocks marked cpp, the web counterpart of the
// desktop CodeHighlighter: keywords, strings and comments, plus preprocessor
// directives. The code element is rebuilt from text nodes and spans, so no
// generated HTML is ever parsed.

const CPP_LANGUAGES = new Set(['cpp', 'c++', 'cxx', 'cc', 'hpp', 'hxx', 'h']);

// The C++23 keywords, the alternative operator spellings, the identifiers
// with a special meaning, and Qt's signals and slots as on the desktop.
const KEYWORDS = new Set(`
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char
    char8_t char16_t char32_t class compl concept const consteval constexpr
    constinit const_cast continue co_await co_return co_yield decltype default
    delete do double dynamic_cast else enum explicit export extern false float
    for friend goto if inline int long mutable namespace new noexcept not not_eq
    nullptr operator or or_eq private protected public register reinterpret_cast
    requires return short signed sizeof static static_assert static_cast struct
    switch template this thread_local throw true try typedef typeid typename
    union unsigned using virtual void volatile wchar_t while xor xor_eq
    final override import module signals slots
`.trim().split(/\s+/));

// Prefixes that may stand before a string or character literal.
const LITERAL_PREFIXES = new Set(['L', 'u', 'U', 'u8']);
const RAW_PREFIXES = new Set(['R', 'LR', 'uR', 'UR', 'u8R']);

const IDENTIFIER_START = /[A-Za-z_]/;
const IDENTIFIER_PART = /\w/;

// Splits C++ source into [{ kind, text }] where kind is 'keyword', 'string',
// 'comment', 'preprocessor' or null for everything else. Joined back together
// the texts are exactly the source.
export function tokenizeCpp(source) {
    const tokens = [];
    let plain = '';
    const emit = (kind, text) => {
        if (plain) tokens.push({ kind: null, text: plain });
        plain = '';
        tokens.push({ kind, text });
    };
    // Where a quoted literal starting at `from` ends: the closing quote, or
    // the end of the line for one left open.
    const quotedEnd = (from) => {
        const quote = source[from];
        let index = from + 1;
        while (index < source.length && source[index] !== quote && source[index] !== '\n') {
            index += source[index] === '\\' ? 2 : 1;
        }
        return Math.min(source[index] === quote ? index + 1 : index, source.length);
    };
    let index = 0;
    let lineStart = true; // Only white space since the last line break.
    while (index < source.length) {
        const ch = source[index];
        const next = source[index + 1];
        if (ch === '/' && next === '/') {
            const end = source.indexOf('\n', index);
            const stop = end < 0 ? source.length : end;
            emit('comment', source.slice(index, stop));
            index = stop;
            continue;
        }
        if (ch === '/' && next === '*') {
            const end = source.indexOf('*/', index + 2);
            const stop = end < 0 ? source.length : end + 2;
            emit('comment', source.slice(index, stop));
            index = stop;
            lineStart = false;
            continue;
        }
        if (ch === '#' && lineStart) {
            const directive = /^#\s*[A-Za-z_]\w*/.exec(source.slice(index));
            if (directive) {
                emit('preprocessor', directive[0]);
                index += directive[0].length;
                lineStart = false;
                // The <header> of an #include reads as a string, as "a.h" does.
                const header = /include$/.test(directive[0])
                    && /^([ \t]*)(<[^>\n]*>)/.exec(source.slice(index));
                if (header) {
                    plain += header[1];
                    emit('string', header[2]);
                    index += header[0].length;
                }
                continue;
            }
        }
        if (ch === '"' || ch === '\'') {
            const stop = quotedEnd(index);
            emit('string', source.slice(index, stop));
            index = stop;
            lineStart = false;
            continue;
        }
        if (IDENTIFIER_START.test(ch)) {
            let stop = index + 1;
            while (stop < source.length && IDENTIFIER_PART.test(source[stop])) stop += 1;
            const word = source.slice(index, stop);
            if (source[stop] === '"' && RAW_PREFIXES.has(word)) {
                // R"delimiter( ... )delimiter"
                const open = source.indexOf('(', stop);
                const delimiter = open < 0 ? '' : source.slice(stop + 1, open);
                const close = open < 0 ? -1 : source.indexOf(`)${delimiter}"`, open);
                const end = close < 0 ? source.length : close + delimiter.length + 2;
                emit('string', source.slice(index, end));
                index = end;
            } else if ((source[stop] === '"' || source[stop] === '\'') && LITERAL_PREFIXES.has(word)) {
                const end = quotedEnd(stop);
                emit('string', source.slice(index, end));
                index = end;
            } else if (KEYWORDS.has(word)) {
                emit('keyword', word);
                index = stop;
            } else {
                plain += word;
                index = stop;
            }
            lineStart = false;
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            // A number, including the ' digit separators of 1'000'000, which
            // must not open a character literal.
            let stop = index + 1;
            while (stop < source.length && /[\w.']/.test(source[stop])) stop += 1;
            plain += source.slice(index, stop);
            index = stop;
            lineStart = false;
            continue;
        }
        plain += ch;
        if (ch === '\n') lineStart = true;
        else if (ch !== ' ' && ch !== '\t' && ch !== '\r') lineStart = false;
        index += 1;
    }
    if (plain) tokens.push({ kind: null, text: plain });
    return tokens;
}

// Highlights every C++ code block under root in place.
export function highlightCode(root) {
    for (const code of root.querySelectorAll('pre > code')) {
        const marker = [...code.classList].find((name) => name.startsWith('language-'));
        if (!marker || !CPP_LANGUAGES.has(marker.slice('language-'.length).toLowerCase())) continue;
        const fragment = document.createDocumentFragment();
        for (const token of tokenizeCpp(code.textContent)) {
            if (!token.kind) {
                fragment.appendChild(document.createTextNode(token.text));
                continue;
            }
            const span = document.createElement('span');
            span.className = `token-${token.kind}`;
            span.textContent = token.text;
            fragment.appendChild(span);
        }
        code.replaceChildren(fragment);
    }
}
