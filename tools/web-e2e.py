#!/usr/bin/env python3
"""End-to-end check of lexicon-web in a real headless browser.

Starts a LexiconServer on a fresh database, serves lexicon-web, and drives
Chrome or Chromium through what a person does: sign in, catch an idea in the
Inbox, edit and save it, search, add a group and a type, review, set an alarm
and dismiss it when it rings, and sign out. Every step is also checked on the
server through the REST API, and any uncaught JavaScript error fails the run.

Usage:
    python3 tools/web-e2e.py --server build/LexiconServer [--chrome PATH] [--artifacts DIR]

Standard library only: Chrome is driven over --remote-debugging-pipe, so no
WebSocket or browser-automation package is needed. On a failure the page is
saved as a screenshot in --artifacts. This file is not part of lexicon-web.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "lexicon-web"
USER = "e2e"
PASSWORD = "an end to end password"


class Failure(Exception):
    pass


class Browser:
    """Chrome over the DevTools pipe: JSON messages separated by NUL bytes."""

    def __init__(self, chrome, profile):
        to_chrome_read, self.to_chrome = os.pipe()
        self.from_chrome, from_chrome_write = os.pipe()

        def pipes():
            os.dup2(to_chrome_read, 3)
            os.dup2(from_chrome_write, 4)

        args = [chrome, "--headless=new", "--remote-debugging-pipe", f"--user-data-dir={profile}",
                "--no-first-run", "--no-default-browser-check", "--disable-gpu", "--hide-scrollbars",
                "--window-size=1280,900", "--lang=en-US", "about:blank"]
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            args.insert(1, "--no-sandbox")  # Chrome refuses its sandbox as root, in containers.
        self.process = subprocess.Popen(args, pass_fds=(3, 4), preexec_fn=pipes,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(to_chrome_read)
        os.close(from_chrome_write)
        self.next_id = 0
        self.answers = {}
        self.arrived = threading.Condition()
        self.errors = []
        threading.Thread(target=self.read, daemon=True).start()
        target = self.call("Target.createTarget", url="about:blank")["targetId"]
        self.session = self.call("Target.attachToTarget", targetId=target, flatten=True)["sessionId"]
        for domain in ("Page.enable", "Runtime.enable"):
            self.call(domain, session=self.session)

    def read(self):
        buffer = b""
        while True:
            chunk = os.read(self.from_chrome, 65536)
            if not chunk:
                return
            buffer += chunk
            while b"\0" in buffer:
                raw, buffer = buffer.split(b"\0", 1)
                message = json.loads(raw)
                if message.get("method") == "Runtime.exceptionThrown":
                    details = message["params"]["exceptionDetails"]
                    self.errors.append(details.get("exception", {}).get("description") or details.get("text"))
                with self.arrived:
                    if "id" in message:
                        self.answers[message["id"]] = message
                    self.arrived.notify_all()

    def call(self, method, session=None, **params):
        self.next_id += 1
        ident = self.next_id
        message = {"id": ident, "method": method, "params": params}
        if session:
            message["sessionId"] = session
        os.write(self.to_chrome, json.dumps(message).encode() + b"\0")
        deadline = time.time() + 30
        with self.arrived:
            while ident not in self.answers:
                if not self.arrived.wait(timeout=max(0.0, deadline - time.time())):
                    raise Failure(f"The browser did not answer {method}.")
        answer = self.answers.pop(ident)
        if "error" in answer:
            raise Failure(f"{method}: {answer['error'].get('message')}")
        return answer.get("result", {})

    def js(self, expression):
        result = self.call("Runtime.evaluate", session=self.session, expression=expression,
                           awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in result:
            raise Failure(f"Script failed: {result['exceptionDetails'].get('exception', {}).get('description')}")
        return result.get("result", {}).get("value")

    def wait(self, expression, what, timeout=15):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.js(expression):
                return
            time.sleep(0.1)
        raise Failure(f"Timed out waiting for {what}.")

    def navigate(self, url):
        self.call("Page.navigate", session=self.session, url=url)

    def screenshot(self, path):
        data = self.call("Page.captureScreenshot", session=self.session, format="png")["data"]
        pathlib.Path(path).write_bytes(base64.b64decode(data))

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class Api:
    def __init__(self, base):
        self.base = base
        self.token = self.call("POST", "/auth/login", {"username": USER, "password": PASSWORD})["token"]

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + "/api/v1" + path, data=data, method=method)
        if data is not None:
            request.add_header("Content-Type", "application/json")
        if getattr(self, "token", None):
            request.add_header("Authorization", "Bearer " + self.token)
        with urllib.request.urlopen(request, timeout=15) as response:
            text = response.read()
            return json.loads(text) if text else None

    def item(self, title):
        found = self.call("POST", "/items/query", {"searchText": title, "limit": 50})["items"]
        matches = [item for item in found if item["title"] == title]
        return self.call("GET", f"/items/{matches[0]['id']}")["item"] if matches else None


def run(browser, web, server):
    b = browser
    q = json.dumps

    def click(selector, text, prefix=False):
        match = f"e.textContent.trim().startsWith({q(text)})" if prefix else f"e.textContent.trim() === {q(text)}"
        clicked = b.js(f"""(() => {{
            const node = [...document.querySelectorAll({q(selector)})]
                .find(e => {match} && e.offsetParent !== null);
            if (!node) return false;
            node.scrollIntoView({{block: 'center'}});
            node.click();
            return true; }})()""")
        if not clicked:
            raise Failure(f"No visible {selector} reading '{text}'.")

    def type_into(selector, value):
        if not b.js(f"""(() => {{
            const node = [...document.querySelectorAll({q(selector)})].filter(e => e.offsetParent !== null).pop();
            if (!node) return false;
            node.value = {q(value)};
            node.dispatchEvent(new Event('input', {{bubbles: true}}));
            node.dispatchEvent(new Event('change', {{bubbles: true}}));
            return true; }})()"""):
            raise Failure(f"No visible field {selector}.")

    def menu(top, entry):
        click(".menu-trigger", top)
        click(".menu-item", entry)

    def dialog_open(text):
        return f"[...document.querySelectorAll('dialog[open]')].some(d => d.textContent.includes({q(text)}))"

    steps = []

    def step(name):
        def register(function):
            steps.append((name, function))
            return function
        return register

    api = {}

    @step("sign in")
    def _():
        b.navigate(web)
        b.wait("!!document.getElementById('login-server')", "the login form")
        b.js("localStorage.clear(); sessionStorage.clear(); true")
        b.navigate(web)
        b.wait("document.readyState === 'complete' && !document.getElementById('login-view').hidden", "the login form")
        type_into("#login-server", server)
        type_into("#login-username", USER)
        type_into("#login-password", PASSWORD)
        b.js("document.getElementById('login-submit').click(); true")
        b.wait("document.getElementById('login-view').hidden", "the main view")
        api["client"] = Api(server)

    @step("catch an idea in the Inbox")
    def _():
        click("button", "Inbox")
        b.wait(dialog_open("Saved to Default"), "the Inbox")
        type_into("dialog[open] input[type=text]", "Pointer provenance")
        type_into("dialog[open] textarea", "Where a pointer came from.")
        click("dialog[open] button", "Save")
        b.wait("[...document.querySelectorAll('tbody tr')].some(r => r.textContent.includes('Pointer provenance'))",
               "the idea in the list")
        item = api["client"].item("Pointer provenance")
        if not item or item["groupName"] != "Default" or item.get("itemTypeId") is not None:
            raise Failure(f"The server holds {item!r}.")

    @step("edit and save an item")
    def _():
        b.js("""[...document.querySelectorAll('tbody tr')].find(r => r.textContent.includes('Pointer provenance')).click(); true""")
        b.wait("!!document.querySelector('tbody tr.selected')", "the selection")
        click("button", "Edit")
        b.wait("!!document.querySelector('dialog[open] .markdown-source')", "the item editor")
        click("dialog[open] .tab", "Content")
        type_into("dialog[open] .markdown-source", "Provenance is **where** a pointer came from.")
        click("dialog[open] button", "Save")
        b.wait("!document.querySelector('dialog[open] .markdown-source')", "the editor to close")
        b.wait("[...document.querySelectorAll('.content-preview strong')].some(e => e.textContent === 'where')",
               "the saved content in the preview")
        if api["client"].item("Pointer provenance")["content"] != "Provenance is **where** a pointer came from.":
            raise Failure("The server did not keep the new content.")

    @step("search")
    def _():
        click("button", "Inbox")
        b.wait(dialog_open("Saved to Default"), "the Inbox")
        type_into("dialog[open] input[type=text]", "Object lifetime")
        click("dialog[open] button", "Save")
        b.wait("document.querySelectorAll('tbody tr').length >= 2", "both items")
        type_into(".search-input", "provenance")
        b.wait("""(() => { const rows = [...document.querySelectorAll('tbody tr')];
            return rows.length === 1 && rows[0].textContent.includes('Pointer provenance'); })()""", "one match")
        type_into(".search-input", "")
        b.wait("document.querySelectorAll('tbody tr').length >= 2", "the full list again")

    @step("add a group")
    def _():
        menu("Manage", "Groups...")
        b.wait(dialog_open("Manage groups"), "the group manager")
        click("dialog[open] button", "Add")
        b.wait(dialog_open("Add group"), "the group form")
        type_into("dialog[open] input[type=text]", "C++")
        click("dialog[open] button", "Save")
        b.wait("[...document.querySelectorAll('dialog[open] li')].some(e => e.textContent.includes('C++'))", "the new group")
        click("dialog[open] button", "Close")
        if "C++" not in [group["name"] for group in api["client"].call("GET", "/groups")["groups"]]:
            raise Failure("The server has no group C++.")

    @step("add a type")
    def _():
        menu("Manage", "Types...")
        b.wait(dialog_open("Manage types"), "the type manager")
        click("dialog[open] button", "Add")
        b.wait(dialog_open("Available in"), "the type form")
        type_into("dialog[open] input[type=text]", "Term")
        click("dialog[open] button", "Save")
        b.wait("[...document.querySelectorAll('dialog[open] li')].some(e => e.textContent.includes('Term'))", "the new type")
        click("dialog[open] button", "Close")
        if "Term" not in [kind["name"] for kind in api["client"].call("GET", "/types")["types"]]:
            raise Failure("The server has no type Term.")

    @step("review")
    def _():
        menu("View", "Review...")
        b.wait(dialog_open("never reviewed"), "a review card")
        click("dialog[open] button", "Show answer")
        b.wait("[...document.querySelectorAll('dialog[open] .review-rating')].every(e => !e.disabled)", "the answer")
        # The ratings say when the item comes back: "Good (2 days)".
        click("dialog[open] button", "Good", prefix=True)
        deadline = time.time() + 10
        while True:
            reviewed = [api["client"].item(title) for title in ("Pointer provenance", "Object lifetime")]
            if any(item["understanding"] == "Recognized" and item.get("reviewedAt") for item in reviewed):
                break
            if time.time() > deadline:
                raise Failure("The server recorded no review.")
            time.sleep(0.2)
        b.wait(dialog_open("Object lifetime"), "the next card")
        click("dialog[open] button", "Close")

    @step("set an alarm and dismiss it when it rings")
    def _():
        menu("Manage", "Alarms...")
        b.wait("!!document.querySelector('.alarm-table')", "the alarms")
        click(".alarms button", "Add...")
        b.wait("!!document.getElementById('alarm-title')", "the alarm form")
        type_into("#alarm-title", "Tea")
        type_into("#alarm-fires-at", "2020-01-01T10:00")
        click("dialog[open] button", "Save")
        b.wait("[...document.querySelectorAll('.alarm-table td')].some(e => e.textContent === 'Tea')", "the alarm in the list")
        click("dialog[open] button", "Close")
        b.wait("!document.querySelector('.alarm-bell').hidden", "the alarm to ring")
        click(".alarm-card button", "Dismiss")
        b.wait("document.querySelector('.alarm-bell').hidden", "the ringing to stop")
        alarm = api["client"].call("GET", "/alarms")["alarms"][0]
        if not alarm.get("dismissedAt"):
            raise Failure("The server did not keep the dismissal.")

    @step("sign out")
    def _():
        b.js("document.getElementById('logout-button').click(); true")
        b.wait("!document.getElementById('login-view').hidden", "the login form")

    passed = 0
    for name, function in steps:
        try:
            function()
        except Failure as failure:
            raise Failure(f"{name}: {failure}")
        if b.errors:
            raise Failure(f"{name}: JavaScript error: {b.errors[0]}")
        print(f"ok  {name}")
        passed += 1
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--server", required=True, help="the LexiconServer binary")
    parser.add_argument("--chrome", default=os.environ.get("CHROME"), help="Chrome or Chromium (default: found on PATH)")
    parser.add_argument("--artifacts", default=".", help="where a failure screenshot goes")
    options = parser.parse_args()
    chrome = options.chrome or next((shutil.which(name) for name in
                                     ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser")
                                     if shutil.which(name)), None)
    if not chrome:
        sys.exit("No Chrome or Chromium found; pass --chrome.")

    work = pathlib.Path(tempfile.mkdtemp(prefix="lexicon-web-e2e-"))
    server_process = browser = web_server = None
    try:
        database = work / "lexicon.db"
        subprocess.run([options.server, "auth", "set-user", "--database", str(database)],
                       input=f"{USER}\n{PASSWORD}\n{PASSWORD}\n", text=True, check=True, capture_output=True)
        web_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(WEB)))
        threading.Thread(target=web_server.serve_forever, daemon=True).start()
        web = f"http://127.0.0.1:{web_server.server_address[1]}/"
        port = free_port()
        server = f"http://127.0.0.1:{port}"
        server_process = subprocess.Popen(
            [options.server, "--database", str(database), "--port", str(port), "--allowed-origin", web.rstrip("/"),
             "--no-session-file", "--quiet"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        deadline = time.time() + 20
        while True:
            try:
                urllib.request.urlopen(server + "/api/v1/health", timeout=2).read()
                break
            except OSError:
                if time.time() > deadline:
                    sys.exit("LexiconServer did not start.")
                time.sleep(0.2)
        browser = Browser(chrome, work / "profile")
        try:
            passed = run(browser, web, server)
        except Failure as failure:
            shot = pathlib.Path(options.artifacts) / "web-e2e-failure.png"
            try:
                browser.screenshot(shot)
                print(f"Screenshot: {shot}")
            except Failure:
                pass
            print(f"FAIL {failure}")
            sys.exit(1)
        print(f"web-e2e: all {passed} steps passed")
    finally:
        if browser:
            browser.close()
        if server_process:
            server_process.terminate()
            server_process.wait(timeout=10)
        if web_server:
            web_server.shutdown()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
