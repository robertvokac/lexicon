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

Credentials are stored in `lexicon-auth.json` next to the database (override
with `--auth-file`), created with owner-only permissions:

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

## Security model

- **One user.** A configured user name and password, nothing else. No
  registration, roles, teams, OAuth or multi-tenancy.
- **Password storage.** scrypt through OpenSSL with `N=65536, r=8, p=1`, a
  fresh 16-byte random salt per password, a 32-byte derived key, and a
  versioned record so the cost can be raised later. Hashes are compared in
  constant time. Tests use deliberately cheap parameters; the CLI always
  writes production ones.
- **Sessions.** On a successful login the server generates 256 bits from the
  OpenSSL CSPRNG and returns it as an opaque base64url token. Only the SHA-256
  of the token is kept in memory. Sessions expire on idle timeout and on
  absolute lifetime, are dropped by logout, are capped in number, and disappear
  entirely when the server restarts. The password is never usable as a token.
- **Brute force.** Failed logins are counted per client address in a bounded
  table, with a global backstop against address rotation. Over the threshold,
  logins answer 429 with `Retry-After`; a successful login clears the client's
  counter.
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

## Known limitations

- One user account, by design.
- Sessions live in memory only, so a restart signs every client out.
- The global login backstop means a determined attacker rotating addresses can
  make logins answer 429 for the length of the window. Raise
  `--login-max-failures-total`, or set it to 0, if that trade-off is wrong for
  your deployment.
- Writes are serialized; this is not a multi-user concurrent server.
- Blob storage maintenance (scan, verify, garbage collect) stays in the desktop
  client and on the server machine. It is local file system maintenance, so it
  is deliberately not reachable over HTTP.
- No WebSockets, no push, no offline sync. The web client refreshes on demand.
