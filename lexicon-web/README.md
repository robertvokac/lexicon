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
├── README.md             this file
├── css/
│   └── lexicon.css       both themes, layout and responsive rules
├── js/
│   ├── api.js            the only module that speaks HTTP
│   ├── app.js            shell, login, menus, themes
│   ├── dialogs.js        modal dialogs and list editors
│   ├── groups.js         group manager
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

4. Open the page and sign in.

`LexiconServer` never serves these files. The API and the frontend are
deployed independently and may live on completely different hosts.

## Local development

Serve the directory with any static HTTP server - `file://` does not work
because browsers refuse ES modules and cross-origin requests from it:

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

## What the client stores in your browser

| Key | Storage | Purpose |
| --- | --- | --- |
| `lexicon.web.token` | `sessionStorage` | Bearer session token, gone when the tab closes |
| `lexicon.web.username` | `sessionStorage` | Prefills the login form |
| `lexicon.web.apiBaseUrl` | `localStorage` | Last server URL used |
| `lexicon.web.theme` | `localStorage` | Light or dark mode |
| `lexicon.web.pageSize` | `localStorage` | Rows per page |
| `lexicon.web.columns` | `localStorage` | Which optional columns are visible |
| `lexicon.web.lastItemId` | `localStorage` | Reselects the last item you looked at |

The token lives in `sessionStorage` on purpose: a browser restart requires a
new sign-in. There is no long-lived "remember me" token.

These preferences are per browser. They never touch the desktop client's
settings, so switching the web theme does not change the Qt theme, and hiding a
column here does not hide it there.

## Requirements

A current browser with ES modules, `<dialog>`, `fetch` and CSS custom
properties: Firefox 98+, Chrome/Edge 103+, Safari 15.4+.

If the page is served over HTTPS and the API URL is plain `http://` on another
host, the client refuses with a clear message instead of letting the browser
block the request as mixed content.
