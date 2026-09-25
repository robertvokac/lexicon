# lexicon-web

The Lexicon web client: a static frontend of HTML, CSS and vanilla JavaScript
modules. It talks to `LexiconServer` over REST and nothing else.

There is no build step, no bundler, no transpiler, no `node_modules` and no
`npm install`. **The files in this directory are exactly the files the browser
runs.**

## Contents

```text
lexicon-web/
├── index.html            application shell and login form
├── config.example.js     optional deployment configuration
├── favicon.svg           tab icon, the desktop icon redrawn as a vector
├── favicon-32.png        the same for browsers without SVG icons
├── apple-touch-icon.png  home screen icon for iOS
├── README.md             this file
├── css/
│   └── lexicon.css       both themes, layout and responsive rules
├── js/
│   ├── api.js            the only module that speaks HTTP
│   ├── account.js        password and session management
│   ├── app.js            shell, login, menus, themes
│   ├── alarms.js         recurring reminders and linked items
│   ├── cardquiz.js       one sitting of a card quiz, without the page
│   ├── cards.js          an item's cards and the card quiz
│   ├── dialogs.js        modal dialogs and list editors
│   ├── drafts.js         unsaved item edits kept in the browser
│   ├── groups.js         group manager
│   ├── history.js        item versions and Trash
│   ├── highlight.js      C++ highlighting for code blocks
│   ├── itemEdit.js       item editor with its six tabs
│   ├── items.js          main window: table, filters, pagination, preview
│   ├── markdown.js       Markdown rendering and sanitization
│   ├── overviews.js      tag/flag/alias overviews, property and column dialogs
│   ├── types.js          type and field manager
│   └── utils.js          DOM and domain helpers
└── vendor/
    ├── marked.esm.js     marked 18.0.13 (MIT) - Markdown parsing
    ├── marked.LICENSE
    ├── purify.es.mjs     DOMPurify 3.4.15 (Apache-2.0 OR MPL-2.0) - sanitizing
    └── dompurify.LICENSE
```

Those two vendored libraries are the only third-party code in the client. They
exist because writing a Markdown parser and an HTML sanitizer by hand is how
cross-site scripting bugs are born. Everything else is application code.

## Deploy

### The short way: let LexiconServer serve it

```bash
LexiconServer --database ~/lexicon/lexicon.db --web-dir /path/to/lexicon-web
```

The client is then served read-only at `/web` on the server's own port, and
`/` redirects there. Being same-origin it needs no `--allowed-origin`, no
`config.js` - the server supplies one pointing at its own origin - and no
second web server. The headers below are sent for you. See
[docs/server.md](../docs/server.md#serving-the-web-client).

### The static way: any web server

1. Copy the whole directory to any static HTTP(S) host:

   ```bash
   rsync -a lexicon-web/ user@host:/var/www/lexicon/
   ```

   GitHub Pages, Netlify, nginx, Apache, a Raspberry Pi - anything that serves
   files works. The client is pure static content.

2. Point it at your server. Either copy `config.example.js` to `config.js` and
   set the API URL:

   ```js
   window.LEXICON_CONFIG = {
       apiBaseUrl: 'https://api.lexicon.example.com',
   };
   ```

   or leave it out and type the URL on the login screen. The last URL used is
   remembered in `localStorage`; `config.js` only supplies the default.

   Never put a password or a token in `config.js`: it is served to every
   visitor.

3. Allow this origin on the server, exactly:

   ```bash
   LexiconServer --allowed-origin https://lexicon.example.com
   ```

4. Make the static host send the headers below.

5. Open the page and sign in.

For a separate static host, the API and frontend are deployed independently
and may live on different hosts.

### Headers for the static host

`index.html` carries its own Content Security Policy, so only the page's own
files may run: no inline script, no event handler attributes, no `eval`. Four
things a `<meta>` tag cannot do are left to the host:

| Header | Value | Why |
| --- | --- | --- |
| `Cache-Control` | `no-cache` on `.html`, `.js`, `.css` | Without it browsers cache ES modules heuristically, so after an update a reload can mix a new `index.html` with an old `dialogs.js`. `no-cache` still caches; it only revalidates. |
| `Content-Security-Policy` | `frame-ancestors 'none'` | Nobody may frame the page to trick clicks. Only valid as a header. |
| `X-Frame-Options` | `DENY` | The same for older browsers. |
| `X-Content-Type-Options` | `nosniff` | Scripts and styles run only with their real MIME type. |

nginx:

```nginx
location / {
    root /var/www/lexicon;
    add_header Cache-Control "no-cache" always;
    add_header Content-Security-Policy "frame-ancestors 'none'" always;
    add_header X-Frame-Options "DENY" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header Referrer-Policy "no-referrer" always;
}
```

Caddy:

```caddy
lexicon.example.com {
    root * /var/www/lexicon
    file_server
    header {
        Cache-Control "no-cache"
        Content-Security-Policy "frame-ancestors 'none'"
        X-Frame-Options "DENY"
        X-Content-Type-Options "nosniff"
        Referrer-Policy "no-referrer"
    }
}
```

Hosts that do not let you set headers (plain GitHub Pages) still work; you
lose the framing protection and may need a hard reload after an update.

## Local development

Serve the directory with a static HTTP server - `file://` does not work
because browsers refuse ES modules and cross-origin requests from it. The
repository has one that sends the headers above:

```bash
python3 tools/serve-web.py --port 8080
```

It binds to `127.0.0.1` unless you pass `--bind`. Any other static server
works too, but without `Cache-Control: no-cache` you will be pressing
Ctrl+Shift+R after every change:

```bash
cd lexicon-web
python3 -m http.server 8080 --bind 127.0.0.1
```

Then run the server with that origin allowed:

```bash
LexiconServer --database ~/lexicon.db \
  --allowed-origin http://127.0.0.1:8080 \
  --allowed-origin http://localhost:8080
```

Open <http://127.0.0.1:8080/> and log in with the user you created with
`LexiconServer auth set-user`.

### Tests

```bash
node --test lexicon-web/tests/*.test.mjs
python3 tools/web-e2e.py --server build/LexiconServer
```

The first runs the unit tests of the modules that need no browser: wiki
links, the graph layout, alarm times, image values, the card quiz session. Name the files: Node 22
reads a directory argument as a module. The second drives the whole client
in headless Chrome or Chromium against a fresh `LexiconServer` - sign in,
Inbox, edit and save, search, groups, types, review, cards and their quiz
(one item, and a neighbourhood from the graph), alarms, sign out - and
checks each step through the REST API; any uncaught JavaScript error fails
it. It needs only Python and the browser, and CI runs both on every push.

## What the client stores in your browser

| Key | Storage | Purpose |
| --- | --- | --- |
| `lexicon.web.token` | `sessionStorage` | Bearer session token, gone when the tab closes |
| `lexicon.web.username` | `sessionStorage` | Prefills the login form |
| `lexicon.web.apiBaseUrl` | `localStorage` | Last server URL used |
| `lexicon.web.theme` | `localStorage` | Light or dark mode |
| `lexicon.web.pageSize` | `localStorage` | Rows per page |
| `lexicon.web.columns` | `localStorage` | Whether the attribute and value columns are visible |
| `lexicon.web.lastItemId` | `localStorage` | Reselects the last item you looked at |
| `lexicon.web.viewMode` | `localStorage` | Table, list, or automatic |
| `lexicon.web.tableHeight` | `localStorage` | Where you put the splitter |
| `lexicon.web.codeLanguage` | `localStorage` | Language of the last code block, offered for the next |
| `lexicon.web.drafts` | `localStorage` | Item edits not yet saved, per user and server |

The token lives in `sessionStorage` on purpose: a browser restart requires a
new sign-in. There is no long-lived "remember me" token.

Drafts are the one place item text is kept in the browser. While the item
editor is open, its state is written to `lexicon.web.drafts` every second it
changes and whenever the page is hidden, because a phone browser discards a
background tab without warning and switching to a PDF reader is enough. The
next sign-in offers the latest draft (Continue editing, Discard, Later), and
opening an item that has one asks first. Saving or cancelling removes it; a
cancel forced by an expired session keeps it for the next sign-in. Logout
removes every draft, so signing out leaves no item text behind.

These preferences are per browser. They never touch the desktop client's
settings, so switching the web theme does not change the Qt theme, and hiding a
column here does not hide it there.

## Desktop parity

The web client is built against the Qt client as its functional specification.
Everything below behaves the same way in both:

| Qt | Web |
| --- | --- |
| `MainWindow` search, Add, Add..., Edit, Delete, Hide attributes, Hide values, Filter Properties... | the same action row |
| `FilterHeaderView` filter row, including dynamic type field filters | a second header row with the same widgets |
| Column sorting, pagination (10/20/50/100), page label | the same, sorted and paged by the server |
| Markdown content preview and the link/backlink line with clickable targets | the same, links reset the filters and search for the target |
| `ItemEditDialog` General, Content, Values, Metadata, Links, Backlinks | the same six tabs, saved in one atomic request |
| Markdown toolbar (B, I, H2-H4, lists, quote, rule, code, code block, link, table) | the same buttons with a live preview |
| Type change confirmation before field values are discarded | the same confirmation and counts |
| Overwrite, Reload or Cancel when the item was saved elsewhere in the meantime | the same choice, also for a restored draft |
| **Inbox**: a title and plain text saved to Default with the type Inbox | the same button, in the overflow menu on a phone |
| `GroupManagerDialog`, `ItemTypeManagerDialog` with their destructive warnings | the same dialogs, counts and wording |
| Image values: thumbnail, **Choose image...**, **View...**, **Save as...**, **Clear**, pictures in the preview | the same, pictures fetched with the session and shown from `blob:` URLs (`js/images.js`, `js/imagevalue.js`) |
| `Manage -> Alarms...`: the alarms in a table, add, edit, delete | the same table and form, the time in the browser's time zone (`js/alarms.js`, `js/alarmtime.js`) |
| The **Alarm** window with Dismiss and Snooze, and a tray notification, while the client runs | a panel over the page and a browser notification while the page is open, asked for every 30 seconds and when the tab comes back (`js/alarmbell.js`) |
| `PropertyFilterDialog` (key exact, value contains, empty value matches any) | the same semantics with Add/Edit/Remove/Clear/Apply |
| `ValueListDialog` for all tags, flags and aliases | the same value and usage count tables |
| Light and dark themes | the same, stored per browser |
| `File -> Export...` and `File -> Import...` | the same, as a download and an upload |
| `View -> Review...` with Again, Hard, Good and Easy | the same cards and keys 1 to 4, Space shows the answer |
| `[[Title]]` links in the content, the `[[ ]]` button and **Add links from content** | the same, rendered by a `marked` extension (`js/wikilinks.js`) |
| `View -> Relationship graph...` | the same graph as SVG, also from the link preview; Space centres, Enter opens |
| **Cards...** of an item: question, answer, Success, Failure, Last attempt; Add, Edit, Delete | the same from `Manage -> Cards of selected item...`, the preview's **Cards** link and the item editor's **Cards...** (`js/cards.js`); on a phone each card is a block |
| `View -> Card quiz...` over this item or its neighbourhood 1 to 3 links away; Show answer, then Yes or No; the graph's **Quiz cards** | the same, also from the preview's **Card quiz** link; Space shows the answer, Y and N answer, a held key answers once (`js/cardquiz.js`) |
| Resizable split between the item list and the preview | a draggable splitter whose position is remembered |
| `CodeHighlighter` for `cpp` code blocks: keywords, strings, comments | the same colours in both themes, plus preprocessor directives and `#include <header>` |

### Two readings of the same list

`View -> Table view` is the desktop reading: every column, the filter row in
the table header, sorting by clicking a header. `View -> List view` is the same
data as cards - title, group and type, then status, understanding, pinned, tags
and any type field values - with the filters in a stacked panel behind a
`Filters` button and a `Sort` control beside it. Both use the same filter
widgets, the same server-side query and the same selection.

`View -> Automatic view` (the default) picks the table on a wide window and the
list on a phone. The choice is remembered per browser, so you can have the full
table on a phone if that is what you want.

On a phone the menu opens as a drawer over the page, the secondary actions move
behind a single overflow button so search and quick add keep the row, and the
preview appears only once something is selected. The item editor fills the
screen, and its Content tab shows the source or the preview, switched with a
Source/Preview control, rather than both squeezed under the keyboard. Fields
use 16px text so iOS does not zoom into them, and fields for titles, aliases,
tags and content turn off automatic capitals, autocorrection and spell
checking, so `std::move` stays `std::move`.

Additions for taking notes quickly, which the desktop does not have:

- **Enter in the search field** shows what matches and selects an exact or
  only match; it adds the text as a new item only when nothing matches. A
  phone keyboard's search key sends Enter, so it never creates a duplicate.
- **Add warns about duplicates.** A title the group already has can only be
  shown, since the database holds one item per title and group; a title or
  alias used elsewhere asks before adding another.
- **Several tags or flags at once**: the Add prompt takes `cpp, c++11`.
  Aliases are not split, because `std::map<K, V>` is one alias.
- **The code block prompt remembers the language** of the last block.
- **Unsaved edits survive the tab** (see above), and a tap beside the editor
  does not close it.

Deliberate differences, all of them because a browser is not a desktop:

- **File > Quit becomes File > Logout.** A browser tab has no application to
  quit.
- **Blob fields upload a chosen file** instead of importing a server-side path.
  A web page cannot hand the server a local path, and the server must never
  accept one.
- **Preferences (theme, page size, visible columns) live in this browser**
  rather than in the database, so the web client and the desktop client never
  overwrite each other's settings.
- **Tools > Blob maintenance is not exposed.** Scanning, verifying and garbage
  collecting the blob directory is local file system maintenance; it stays with
  the desktop client and the server machine, and is deliberately not reachable
  over HTTP.

## Requirements

A current browser with ES modules, `<dialog>`, `fetch` and CSS custom
properties: Firefox 98+, Chrome/Edge 103+, Safari 15.4+.

If the page is served over HTTPS and the API URL is plain `http://` on another
host, the client refuses with a clear message instead of letting the browser
block the request as mixed content.
