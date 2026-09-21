# Running LexiconServer

`LexiconServer` is the Qt-free REST server for Lexicon. It opens the same
SQLite database as the desktop client, composes the same
`LexiconApplication` services, and exposes them as JSON under `/api/v1`.

> **LexiconServer never serves lexicon-web.** It answers REST calls and nothing
> else. The web client is static content you deploy wherever you like.

## Architecture

```text
                   Qt Widgets (lexicon-qt)
                             │
                             ▼
                    LexiconApplication ──────────────┐
                             ▲                       │
                             │                       │
                   SQLite Repository            HTTP Adapter
                  (lexicon-storage-sqlite)      (lexicon-http)
                             │                       │
                             ▼                       ▼
                        lexicon.db              REST / JSON
                                                     │
                                                     ▼
                                          static lexicon-web
                                       (HTML + CSS + vanilla JS)
```

Both clients call the same application services in the same process model:
the desktop calls them in-process, the server calls them from its request
handlers. No business logic lives in the HTTP layer, and no HTTP concept
reaches `lexicon-core`, `lexicon-application` or `lexicon-storage-sqlite`.

| Target | Role |
| --- | --- |
| `lexicon-http` | JSON transport, authentication, sessions, CORS, TLS, routes |
| `LexiconServer` | Composition root: `SqliteRepository` + `LexiconApplication` + HTTP |

Third-party code is vendored and pinned under `3rdparty/`:
[cpp-httplib](https://github.com/yhirose/cpp-httplib) 0.56.0 (MIT) for HTTP and
TLS, and [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 (MIT) for
JSON. Nothing is downloaded during a build.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLEXICON_BUILD_SERVER=ON
cmake --build build --target LexiconServer -j
```

The server builds without Qt. To prove it:

```bash
cmake -S . -B build-server \
  -DLEXICON_BUILD_DESKTOP=OFF -DLEXICON_BUILD_SERVER=ON -DBUILD_TESTING=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
cmake --build build-server -j
ctest --test-dir build-server --output-on-failure
```

Requirements beyond the desktop ones: OpenSSL `libssl` as well as `libcrypto`,
and a threading library.

## First run

### 1. Create the user

The server is a single-user personal installation. There is no registration,
no password reset by email and no account management API. The credential is
created locally on the server machine:

```bash
LexiconServer auth set-user --database ~/lexicon/lexicon.db
```

It asks for a user name, a password and a confirmation. The password is read
with terminal echo disabled and is never a command line argument, so it does
not reach the shell history or the process list. `LexiconServer auth show`
prints the configured user name and the hash parameters, never the hash.

The server reads the credentials once, at startup. After changing the password
restart it; the restart also ends every existing session, so anyone signed in
with the old password is signed out.

Credentials are stored in `lexicon-auth.json` next to the database (override
with `--auth-file`). Changing the password rewrites that file, so the write
goes through an exclusively created temporary file with an unpredictable name
and is then installed atomically - `rename()` on POSIX, `ReplaceFileW()` (or
`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` for a first write) on Windows.
A crash or a concurrent reader sees either the old credentials or the new
ones, never half a document. The file is owner-only: mode `0600` on POSIX, and
on Windows a protected DACL granting the current user alone, because Windows
has no mode bits and an inherited directory ACL is not equivalent.

The document looks like this:

```json
{
  "version": 1,
  "username": "robert",
  "password": {
    "algorithm": "scrypt", "version": 1,
    "n": 65536, "r": 8, "p": 1, "keyLength": 32, "maxMemory": 536870912,
    "salt": "...", "hash": "..."
  }
}
```

Keeping it out of the database means historical Lexicon databases stay exactly
as they were and remain readable by the Qt client.

### 2. Start the server

```bash
LexiconServer --database ~/lexicon/lexicon.db \
  --allowed-origin https://lexicon.example.com
```

Defaults: `127.0.0.1:8628`, no TLS, no allowed origins. Without at least one
`--allowed-origin`, browser clients on another origin are refused - that is
intentional, not a bug.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--database PATH` | `lexicon.db` | SQLite database, shared with the desktop client |
| `--auth-file PATH` | `<database dir>/lexicon-auth.json` | Credentials file |
| `--listen ADDRESS` | `127.0.0.1` | Bind address |
| `--port PORT` | `8628` | TCP port |
| `--tls-cert PATH` | — | PEM certificate chain for embedded HTTPS |
| `--tls-key PATH` | — | PEM private key |
| `--allow-insecure-http` | off | Permit a non-loopback plaintext listener |
| `--allowed-origin ORIGIN` | none | Exact CORS origin, repeatable |
| `--trusted-proxy ADDRESS` | none | Honour `X-Forwarded-For` from this peer, repeatable |
| `--session-idle-timeout S` | `28800` (8 h) | Idle session timeout |
| `--session-max-lifetime S` | `604800` (7 d) | Absolute session lifetime |
| `--max-sessions N` | `32` | Concurrent sessions kept in memory |
| `--max-json-bytes N` | `1048576` | Largest JSON request body |
| `--max-blob-bytes N` | `67108864` | Largest blob upload |
| `--read-timeout S` | `15` | Socket read timeout |
| `--write-timeout S` | `15` | Socket write timeout |
| `--keep-alive-timeout S` | `5` | Keep-alive timeout |
| `--login-max-failures N` | `10` | Failed logins per client before 429 |
| `--login-failure-window S` | `900` | Rate limit window |
| `--login-max-failures-total N` | `200` | Failed logins from all clients before 429 (0 disables) |
| `--login-max-parallel-hashes N` | `2` | Password derivations allowed to run at once |
| `--quiet` | off | Do not log one line per request |

## HTTP, HTTPS and reverse proxies

The recommended production arrangement:

```text
Internet
   │ HTTPS
   ▼
reverse proxy (nginx, Caddy, Apache)
   │ loopback HTTP
   ▼
LexiconServer --listen 127.0.0.1 --port 8628
```

An nginx sketch:

```nginx
server {
    listen 443 ssl;
    server_name api.lexicon.example.com;
    ssl_certificate     /etc/letsencrypt/live/api.lexicon.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/api.lexicon.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8628;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $remote_addr;
        client_max_body_size 64m;
    }
}
```

Add `--trusted-proxy 127.0.0.1` so the rate limiter counts the real client
address. `X-Forwarded-For` is honoured **only** when the peer on the TCP
connection is a configured trusted proxy; otherwise the connection address is
used and the header is ignored.

A reverse proxy is not required. The server terminates TLS itself with
OpenSSL:

```bash
LexiconServer --listen 0.0.0.0 --port 8443 \
  --tls-cert /etc/lexicon/fullchain.pem \
  --tls-key  /etc/lexicon/privkey.pem \
  --allowed-origin https://lexicon.example.com
```

Plain HTTP on a non-loopback address is refused at startup, because it would
send the password and the session token in the clear:

```text
Refusing to serve password authentication over plaintext HTTP on 0.0.0.0.
Configure --tls-cert and --tls-key, bind to 127.0.0.1 behind a reverse proxy,
or pass --allow-insecure-http to override.
```

`--allow-insecure-http` exists for closed test networks and prints a loud
warning. Never use it on the Internet.

## Running as a service

A systemd unit for a server behind a reverse proxy on the same machine:

```ini
# /etc/systemd/system/lexicon.service
[Unit]
Description=Lexicon REST server
After=network.target

[Service]
User=robert
ExecStart=/usr/local/bin/LexiconServer \
    --database /home/robert/lexicon/lexicon.db \
    --allowed-origin https://lexicon.example.com \
    --trusted-proxy 127.0.0.1 --quiet
Restart=on-failure
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/home/robert/lexicon

[Install]
WantedBy=multi-user.target
```

`ReadWritePaths` is the directory that holds the database, the credentials
file and `blobs/`; nothing else needs to be writable. The server stops cleanly
on `SIGTERM` and `SIGINT`. Run it as the user who owns the database, never as
root: everything it creates is owner-only (see [Security model](#security-model)).

Only one server can listen on a port. A second `LexiconServer` on the same
port exits with `Cannot bind ADDR:PORT. Is another LexiconServer, or another
program, already listening on that port?` instead of silently sharing it.

## Using the desktop client at the same time

The server and the Qt client can open the same `lexicon.db` at once; SQLite
arbitrates. Two rules make that work rather than merely not crash:

- **Every connection waits up to 5 seconds for a lock** (`sqlite3_busy_timeout`)
  instead of failing the moment the other process is writing.
- **Every write transaction starts with `BEGIN IMMEDIATE`.** A deferred
  transaction takes the write lock only at its first write; two of them that
  both read first can end up each holding a read lock and waiting for the
  other, a deadlock SQLite resolves by failing one of them at once, without
  waiting. Taking the write lock up front makes writers queue instead.

Measured with two servers on one database and six clients writing as fast as
they could for 30 seconds: before, 98.6 % of the writes failed with `database
is locked`; after, 3 of 2,178 (0.1 %) did, each one a save that had queued
for the full 5 seconds behind the others. A person typing in one client while
another person types in the other never gets near that. When it does happen
the save answers 500 in the web client and shows the storage error on the
desktop, and nothing is half written: trying again is safe.

Neither client pushes changes to the other. The web client shows a desktop
edit on its next search or reload; the desktop client shows a web edit when it
reloads its list.

Keep the database on a local disk. SQLite's locks are only as good as the file
system's, and on network shares (NFS, SMB) they are not reliable enough for
two processes to write safely.

## Backups while the server runs

`lexicon.db` and the `blobs/` directory next to it are the complete data set.
Copying `lexicon.db` with `cp` while something writes to it can capture half a
commit. Use SQLite's online backup, which takes the proper locks:

```bash
sqlite3 ~/lexicon/lexicon.db ".backup '/backup/lexicon-$(date +%F).db'"
rsync -a ~/lexicon/blobs/ /backup/blobs/
```

Blob files are content addressed and never rewritten, so copying them live is
safe. `lexicon-auth.json` holds only a password hash; back it up or recreate
it with `auth set-user`.

## Paths and text encoding

Every path inside Lexicon is a UTF-8 `std::string`. Conversion between those
strings and `std::filesystem::path` is centralized in `lexicon-core/Utf8Path.h`,
which both the SQLite storage and the server use; `lexicon-http/FilePath.h`
handles only the Windows operating system text boundaries, the command line
and the console, which arrive as UTF-16.

That matters on Windows, where three lossy conversions sit on the way in:

- `std::filesystem::path::string()` is the *native narrow* encoding, which
  with MSVC is the active code page. It is not UTF-8, and the standard does
  not promise it will be. (MinGW's libstdc++ happens to produce UTF-8, which
  is exactly the kind of accident that hides the bug until someone builds with
  the other toolchain.)
- The narrow `argv` the C runtime hands to `main` is the active code page
  whatever the compiler. On a code page 1252 machine, `--database
  C:\Users\Jiri\...` with a hacek on the r arrives with the hacek silently
  dropped, naming a directory that does not exist.
- The narrow input of a Windows console is the console input code page, often
  437, which cannot spell a Czech name at all.

So `LexiconServer` takes its arguments from `GetCommandLineW`, reads an
interactive console with `ReadConsoleW`, and converts both to UTF-8 itself. It
also sets the console output code page to UTF-8 so a path printed back is
readable. Redirected input is defined to be UTF-8 already and is read as
bytes, which is what a pipe or a file from any other tool provides.

The result is one encoding everywhere:

| Boundary | Encoding |
| --- | --- |
| Windows command line | UTF-16, converted to UTF-8 |
| Windows interactive console | UTF-16, converted to UTF-8 |
| Redirected standard input | UTF-8 bytes |
| REST and JSON | UTF-8 |
| Core and application strings | UTF-8 `std::string` |
| File system | UTF-8, converted to and from `std::filesystem::path` in `Utf8Path.h` |
| SQLite text | UTF-8 |

A database, credentials file, blob store and upload staging file therefore all
work under a path like `C:\Users\Jiri\Lexicon\lexikon-databaze.db` spelled
with any characters the file system accepts.

## Security model

- **One user.** A configured user name and password, nothing else. No
  registration, roles, teams, OAuth or multi-tenancy.
- **Password storage.** scrypt through OpenSSL with `N=65536, r=8, p=1`, a
  fresh 16-byte random salt per password, a 32-byte derived key, and a
  versioned record so the cost can be raised later. Hashes are compared in
  constant time. Tests use deliberately cheap parameters; the CLI always
  writes production ones.
- **Password hashing never blocks the server.** The session mutex is held only
  to check the rate limit and to copy the stored credential, then released;
  scrypt runs with no lock held, and the mutex is taken again briefly to record
  the outcome. An expensive or repeated login therefore cannot delay
  authenticating an existing session. At most `--login-max-parallel-hashes`
  derivations run at once, so a burst of logins cannot multiply the scrypt
  working set either: two concurrent derivations at the default parameters
  cost about 128 MB, not 128 MB per request.
- **Sessions.** On a successful login the server generates 256 bits from the
  OpenSSL CSPRNG and returns it as an opaque base64url token. Only the SHA-256
  of the token is kept in memory. Sessions expire on idle timeout and on
  absolute lifetime, are dropped by logout, are capped in number, and disappear
  entirely when the server restarts. The password is never usable as a token.
- **Brute force.** Failed logins are counted per client address in a bounded
  table, with a global backstop against address rotation. Over the threshold,
  logins answer 429 with `Retry-After`; a successful login clears the client's
  counter.
- **NUL bytes.** A user name or password containing a NUL character is refused
  at login and by `auth set-user`. scrypt keys an HMAC, and HMAC pads its key
  with zero bytes, so `secret` and `secret` followed by NUL derive the same
  hash; without the check, a password with trailing NULs would be accepted as
  the real one.
- **Files.** On POSIX the server runs with umask `077`, so everything it creates
  (a new database and its journal, blob directories, upload staging files) is
  readable by its owner only. The credentials file and blob files are written
  `0600` explicitly as well. Files that already exist keep their permissions.
- **Browsers.** The API answers cross-origin requests only for the exact origins
  given with `--allowed-origin`; a request carrying any other `Origin` is
  refused with 403 before authentication, which is also what defeats DNS
  rebinding: a hostile page that points its own host name at `127.0.0.1` still
  sends its own origin. The token is a `Bearer` header, never a cookie, so a
  page the browser merely visits cannot ride on a signed-in session. The web
  client restricts script to its own files with a Content Security Policy.
- **Authentication is checked before the body is read,** so an unauthenticated
  request never buys server memory.
- **Logging.** One line per request with method, path and status. Headers,
  bodies, query strings, passwords and tokens are never logged.
- **Errors.** Storage failures are logged server-side and answered with a
  generic message. SQL, database paths and stack traces never leave the
  process.
- **Concurrency.** The SQLite repository owns one connection and is not thread
  safe, so every database and blob operation is serialized behind one mutex.
  Health checks and authentication do not take that lock. Correctness before
  write throughput: this is a personal server.
- **Input.** Strict `Content-Type` checks, body size limits, read and write
  timeouts, rejected malformed IDs and JSON, no endpoint that accepts a
  server-side path, no shell execution, and no SQL reachable through HTTP.

### How it was attacked

Beyond the tests in `tests/ServerAuth.cpp` (logout, expiry, rate limits, CORS,
request hygiene, opaque storage errors, NUL bytes), the server was run against
a copy of a real database and probed without the password:

- every protected route and method with no `Authorization` header and with
  forged ones: random tokens, `Basic` credentials, a lower-case scheme, a tab
  separator, two tokens in one header, a header folded over two lines;
- path tricks aimed at the authentication check: `..` and `%2e%2e`, encoded
  slashes, `%00`, doubled slashes, `;` parameters, upper case, a token in the
  query string, and direct requests for `lexicon.db`, `lexicon-auth.json` and
  `/etc/passwd`;
- unusual methods (`TRACE`, `PROPFIND`, `CONNECT`, lower case, an embedded
  NUL) and `X-HTTP-Method-Override` style headers;
- protocol shapes: HTTP/1.0 and 0.9, absolute-form targets, a pipelined
  second request, and a `Content-Length` plus `Transfer-Encoding` smuggling
  attempt;
- logins with `null`, array, object, boolean and number passwords, missing
  fields, SQL-looking passwords, and NUL-padded user names and passwords -
  the last one found the NUL issue above, now fixed;
- a login from a DNS rebinding page, which carries a foreign `Origin` and
  gets 403;
- 113,326 randomly malformed requests against a build with AddressSanitizer
  and UndefinedBehaviorSanitizer, which stayed up and reported nothing.

None of the 481 requests in the first five groups returned data or a token.
In the web client, markup and script in item titles, in Markdown content and
in `javascript:` links render as text or are stripped by the sanitizer, and a
script, an `onerror` handler and `eval` injected straight into the running page
are all blocked by the Content Security Policy in both Chrome and Firefox.

Login time does not reveal whether a user name exists: an unknown name runs
the same scrypt derivation as a wrong password.

## Known limitations

- One user account, by design.
- The rate limit slows guessing; it does not make a weak password safe. The
  defaults allow 10 failures per address and 200 in total per 15 minutes,
  about 19,000 guesses a day from many addresses. Use a long random password,
  and change any password that has been shown to anyone with `auth set-user`.
- Sessions live in memory only, so a restart signs every client out.
- The global login backstop means a determined attacker rotating addresses can
  make logins answer 429 for the length of the window. Raise
  `--login-max-failures-total`, or set it to 0, if that trade-off is wrong for
  your deployment.
- Writes are serialized; this is not a multi-user concurrent server. A save
  that waits more than 5 seconds for the database lock fails and must be
  repeated (see [Using the desktop client at the same time](#using-the-desktop-client-at-the-same-time)).
- Logins queue behind `--login-max-parallel-hashes`. That is the intended
  trade: bounded memory and CPU under a login flood, at the cost of a slower
  sign-in while one is in progress. Authenticated requests are unaffected.
- The Windows build is cross-compiled with MinGW-w64 and exercised under Wine:
  `auth set-user` and `auth show` through a pipe, the REST API, a blob upload
  and download, and an item with Czech text, all with the database under a
  path like `Uzivatele\Jiri\Lexicon\lexikon-databaze.db` and a user name and
  password carrying diacritics.
- Two things there are **not** backed by an execution. The interactive
  `ReadConsoleW` branch of `auth set-user` is compile-checked only: Wine gives
  a console under a pseudo terminal, but it does not deliver piped input to
  `ReadConsoleW`, so the branch cannot be driven from a script. The UTF-16 to
  UTF-8 conversion it depends on is covered by tests; the console call and its
  mode handling are not. NTFS ACL semantics are likewise not something Wine
  proves. Both want a real Windows machine, which is the one validation this
  document still owes.
- Blob storage maintenance (scan, verify, garbage collect) stays in the desktop
  client and on the server machine. It is local file system maintenance, so it
  is deliberately not reachable over HTTP.
- No WebSockets, no push, no offline sync. The web client refreshes on demand.
