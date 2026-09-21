#!/usr/bin/env python3
"""Serve lexicon-web for local use, with headers that keep upgrades visible.

`python3 -m http.server` sends no Cache-Control header, so browsers cache the
JavaScript modules heuristically and a normal reload can keep running an old
itemEdit.js next to a new lexicon.css. This server sends `Cache-Control:
no-cache`, which makes the browser revalidate every file on each load; an
unchanged file costs a 304 response, not a download.

It also sends the headers a static host for lexicon-web should send in
production. See lexicon-web/README.md for the nginx and Caddy equivalents.

Usage:
    python3 tools/serve-web.py [--port 8080] [--bind 127.0.0.1]

Standard library only. This file is not part of the deployable web client.
"""
import argparse
import functools
import http.server
import pathlib

WEB_ROOT = pathlib.Path(__file__).resolve().parent.parent / "lexicon-web"


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        # The client must never run inside another site's frame.
        self.send_header("Content-Security-Policy", "frame-ancestors 'none'")
        self.send_header("X-Frame-Options", "DENY")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--bind", default="127.0.0.1")
    arguments = parser.parse_args()
    handler = functools.partial(Handler, directory=str(WEB_ROOT))
    server = http.server.ThreadingHTTPServer((arguments.bind, arguments.port), handler)
    print(f"Serving {WEB_ROOT} on http://{arguments.bind}:{arguments.port}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
