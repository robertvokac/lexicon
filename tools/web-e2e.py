#!/usr/bin/env python3
"""End-to-end check of lexicon-web in a real headless browser.

Starts a LexiconServer on a fresh database, serves lexicon-web, and drives
Chrome or Chromium through what a person does: sign in, catch an idea in the
Inbox, edit and save it, search, add a group and a type, review, add cards
and quiz them - one item and its neighbourhood - resume a Mass Insert draft,
set an alarm and dismiss it when it rings, and sign out. Every step is also checked on the
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
from datetime import date, timedelta
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


q = json.dumps


def wait_for_health(base, what="LexiconServer"):
    deadline = time.time() + 20
    while True:
        try:
            urllib.request.urlopen(base + "/api/v1/health", timeout=2).read()
            return
        except OSError:
            if time.time() > deadline:
                sys.exit(f"{what} did not start.")
            time.sleep(0.2)


def check_served_client(browser, binary, database, chrome_free_port=free_port):
    """--web-dir: the server serves the client itself, on its own port and origin."""
    port = chrome_free_port()
    base = f"http://127.0.0.1:{port}"
    process = subprocess.Popen(
        [binary, "--database", str(database), "--port", str(port), "--web-dir", str(WEB),
         "--no-session-file", "--quiet"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        wait_for_health(base, "LexiconServer with --web-dir")
        # The root lands on the client, without any --allowed-origin.
        browser.navigate(base + "/")
        browser.wait("!!document.getElementById('login-server')", "the client the server serves")
        browser.js("localStorage.clear(); sessionStorage.clear(); true")
        browser.navigate(base + "/")
        browser.wait("document.readyState === 'complete' && !document.getElementById('login-view').hidden",
                     "the login form")
        where = browser.js("window.location.pathname")
        if where != "/web/":
            raise Failure(f"the root led to '{where}', not to /web/.")
        prefilled = browser.js("document.getElementById('login-server').value")
        if prefilled != base:
            raise Failure(f"the client points at '{prefilled}', not at the server that served it.")
        browser.js(f"""(() => {{ const set = (id, value) => {{ const field = document.getElementById(id);
            field.value = value; field.dispatchEvent(new Event('input', {{bubbles: true}})); }};
            set('login-username', {q(USER)}); set('login-password', {q(PASSWORD)});
            document.getElementById('login-submit').click(); return true; }})()""")
        browser.wait("document.getElementById('login-view').hidden", "the main view of the served client")
        if browser.errors:
            raise Failure(f"JavaScript error: {browser.errors[0]}")
        browser.js("document.getElementById('logout-button').click(); true")
        browser.wait("document.querySelector('dialog[open] .dialog-title')?.textContent === 'Log out'",
                     "the logout confirmation")
        browser.js("[...document.querySelectorAll('dialog[open] button')].find(e => e.textContent.trim() === 'Log out').click(); true")
        browser.wait("!document.getElementById('login-view').hidden", "the login form again")
    finally:
        process.terminate()
        process.wait(timeout=10)


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

    @step("protect unsaved Board changes")
    def _():
        click("button", "Boards")
        b.wait(dialog_open("Choose a Board"), "the Board picker")
        click("dialog[open] button", "Open")
        b.wait(dialog_open("Board — Main"), "the Board viewer")
        click("dialog[open] button", "Edit")
        b.wait(dialog_open("Edit Board — Main"), "the Board editor")
        type_into("dialog[open] .markdown-source", "Unsaved Board text")
        click("dialog[open] button", "Cancel")
        b.wait(dialog_open("Unsaved Board changes"), "the Board discard confirmation")
        click("dialog[open] button", "Keep editing")
        b.wait("![...document.querySelectorAll('dialog[open] .dialog-title')].some(e => e.textContent === 'Unsaved Board changes')",
               "the discard confirmation to close")
        b.js("new Promise(resolve => setTimeout(() => resolve(true), 100))")
        b.wait(dialog_open("Edit Board — Main"), "the protected Board editor")
        click("dialog[open] button", "Cancel")
        b.wait(dialog_open("Unsaved Board changes"), "the second Board discard confirmation")
        click("dialog[open] button", "Discard")
        b.wait("[...document.querySelectorAll('dialog[open] .dialog-title')].some(e => e.textContent === 'Board — Main')",
               "the Board viewer after discard")
        click("dialog[open] button", "Boards")
        b.wait(dialog_open("Choose a Board"), "the Board picker after discard")
        click("dialog[open] button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the Board dialogs to close")

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
        if not item or item["groupName"] != "Default" or item.get("itemTypeName") != "Inbox":
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
        # A word only the content has: the row says why it was found.
        type_into(".search-input", "came from")
        b.wait("""(() => { const rows = [...document.querySelectorAll('tbody tr')];
            return rows.length === 1 && rows[0].textContent.includes('Pointer provenance'); })()""",
               "the content match")
        b.wait("""(() => { const snippet = document.querySelector('tbody tr .match-snippet');
            return !!snippet && snippet.textContent.includes('came from'); })()""",
               "the snippet saying why")
        # An item whose title is the whole answer needs no snippet.
        type_into(".search-input", "lifetime")
        b.wait("""(() => { const rows = [...document.querySelectorAll('tbody tr')];
            return rows.length === 1 && rows[0].textContent.includes('Object lifetime'); })()""",
               "the title match")
        b.wait("!document.querySelector('tbody tr .match-snippet')",
               "no snippet where the title is the answer")
        type_into(".search-input", "")
        b.wait("document.querySelectorAll('tbody tr').length >= 2", "the full list again")

    @step("remember the table row height")
    def _():
        menu("View", "Table row height...")
        b.wait(dialog_open("Normal row height in pixels"), "the row-height setting")
        type_into("dialog[open] input[type=number]", "52")
        click("dialog[open] button", "Apply")
        b.wait("!document.querySelector('dialog[open]')", "the row-height setting to close")
        expected = "document.querySelector('.item-table').style.getPropertyValue('--item-row-height') === '52px'"
        b.wait(expected, "the new row height")
        if b.js("localStorage.getItem('lexicon.web.rowHeight')") != "52":
            raise Failure("The row height was not stored in this browser.")

        b.navigate(web)
        b.wait("document.readyState === 'complete' && document.getElementById('login-view').hidden",
               "the restored main view")
        b.wait(expected, "the restored row height")

    @step("zoom the relationship graph and fill the window with it")
    def _():
        client = api["client"]
        client.call("POST", "/links", {"fromItemId": client.item("Pointer provenance")["id"],
                                       "toItemId": client.item("Object lifetime")["id"], "linkType": "Related"})
        b.js("""[...document.querySelectorAll('tbody tr')].find(r => r.textContent.includes('Pointer provenance')).click(); true""")
        b.wait("!!document.querySelector('tbody tr.selected')", "the selection")
        menu("View", "Relationship graph...")
        b.wait("document.querySelectorAll('dialog[open] .graph-node').length === 2", "both items in the graph")
        centred = """(() => { const canvas = document.querySelector('dialog[open] .graph-canvas').getBoundingClientRect();
            const node = document.querySelector('dialog[open] .graph-node.centre circle').getBoundingClientRect();
            return Math.abs((node.left + node.right - canvas.left - canvas.right) / 2) <= 3
                && Math.abs((node.top + node.bottom - canvas.top - canvas.bottom) / 2) <= 3; })()"""
        b.wait(centred, "the first item at the centre of the graph canvas")
        width = "parseFloat(document.querySelector('dialog[open] svg.graph').style.width)"
        fitted = b.js(width)
        click("dialog[open] button", "+")
        click("dialog[open] button", "+")
        b.wait(f"Math.abs({width} - {fitted} * 1.5625) <= 1", "the graph zoomed in twice")
        click("dialog[open] button", "\u2212")
        b.wait(f"Math.abs({width} - {fitted} * 1.25) <= 1", "the graph zoomed out")
        click("dialog[open] button", "Fit")
        b.wait(f"Math.abs({width} - {fitted}) <= 1", "the graph fitted again")
        window = "window.innerWidth"
        click("dialog[open] button", "Full screen")
        b.wait(f"document.querySelector('dialog[open]').getBoundingClientRect().width === {window}"
               " && document.querySelector('dialog[open] .graph-canvas').clientHeight > window.innerHeight * 0.6",
               "the graph filling the window")
        # Escape leaves full screen and keeps the graph open.
        b.call("Input.dispatchKeyEvent", session=b.session, type="keyDown", key="Escape", code="Escape",
               windowsVirtualKeyCode=27)
        b.call("Input.dispatchKeyEvent", session=b.session, type="keyUp", key="Escape", code="Escape",
               windowsVirtualKeyCode=27)
        b.wait(f"document.querySelector('dialog[open] .graph-canvas') !== null"
               f" && document.querySelector('dialog[open]').getBoundingClientRect().width < {window}",
               "the graph back in its dialog")
        b.js("document.querySelector('dialog[open] .graph-node:not(.centre)').dispatchEvent(new MouseEvent('click', {bubbles: true})); true")
        b.wait("document.querySelector('dialog[open] .graph-node.centre title')?.textContent.includes('Object lifetime')",
               "the clicked item becoming the graph centre")
        b.wait(centred, "the clicked item at the centre of the graph canvas")
        click("dialog[open] button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the graph to close")

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
        kinds = api["client"].call("GET", "/types")["types"]
        if "Term" not in [kind["name"] for kind in kinds]:
            raise Failure("The server has no type Term.")
        term = next(kind for kind in kinds if kind["name"] == "Term")
        priority = api["client"].call("POST", f"/types/{term['id']}/fields", {
            "name": "Priority", "description": "Batch priority", "dataType": "Enum",
            "position": 0, "enumOptions": ["Low", "High"],
        })["field"]
        picture = api["client"].call("POST", f"/types/{term['id']}/fields", {
            "name": "Picture", "description": "Batch image", "dataType": "Image",
            "position": 1, "enumOptions": [],
        })["field"]
        due = api["client"].call("POST", f"/types/{term['id']}/fields", {
            "name": "Due", "description": "Batch date", "dataType": "Date",
            "position": 2, "enumOptions": [],
        })["field"]
        api["mass_field_id"] = priority["id"]
        api["mass_image_field_id"] = picture["id"]
        api["mass_date_field_id"] = due["id"]

    @step("resume and finish a Mass Insert worksheet")
    def _():
        menu("Manage", "Mass Insert...")
        b.wait(dialog_open("Choosing one adds a column"), "the Mass Insert scope")
        selected = b.js("""(() => {
            const select = document.querySelector('dialog[open] select');
            const option = [...select.options].find(entry => entry.textContent === 'Default');
            if (!option) return false;
            select.value = option.value;
            select.dispatchEvent(new Event('change', {bubbles: true}));
            return true;
        })()""")
        if not selected:
            raise Failure("Mass Insert offered no Default group.")
        b.wait("[...[...document.querySelectorAll('dialog[open] select')][1].options]"
               ".some(entry => entry.textContent.startsWith('Term'))", "the optional Type choice")
        chose_type = b.js("""(() => {
            const select = [...document.querySelectorAll('dialog[open] select')][1];
            const option = [...select.options].find(entry => entry.textContent.startsWith('Term'));
            if (!option) return false;
            select.value = option.value;
            select.dispatchEvent(new Event('change', {bubbles: true}));
            return true;
        })()""")
        if not chose_type:
            raise Failure("Mass Insert offered no Term type.")
        click("dialog[open] button", "Continue")
        b.wait("!!document.querySelector('dialog[open] .mass-insert-table')", "the Mass Insert worksheet")
        b.wait("[...document.querySelectorAll('dialog[open] th')].some(entry => entry.textContent === 'Priority')",
               "the Type value column")
        type_into("dialog[open] tbody tr:nth-child(1) td:nth-child(1) input", "Bulk web one")
        type_into("dialog[open] tbody tr:nth-child(1) td:nth-child(3) input", "Web alias, Batch alias")
        type_into("dialog[open] tbody tr:nth-child(1) td:nth-child(4) input", "batch, web")
        type_into("dialog[open] tbody tr:nth-child(1) td:nth-child(9) textarea", "# Bulk web one")
        type_into("dialog[open] tbody tr:nth-child(1) td:nth-child(10) textarea", "source=Mass Insert")
        type_into("dialog[open] tbody tr:nth-child(1) td:nth-child(11) select", "High")
        # Dismissing the file chooser fires a bubbling cancel at the file input;
        # it must not close the worksheet the way Escape does.
        b.js("""(() => {
            const input = document.querySelector(
                'dialog[open] tbody tr:nth-child(1) td:nth-child(12) input[type=file]');
            input.dispatchEvent(new Event('cancel', {bubbles: true}));
            return true;
        })()""")
        if not b.js("!!document.querySelector('dialog[open] .mass-insert-table')"):
            raise Failure("Cancelling the Mass Insert image chooser closed the worksheet.")
        uploaded = b.js("""(() => {
            const input = document.querySelector(
                'dialog[open] tbody tr:nth-child(1) td:nth-child(12) input[type=file]');
            if (!input) return false;
            const encoded = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=';
            const bytes = Uint8Array.from(atob(encoded), character => character.charCodeAt(0));
            const transfer = new DataTransfer();
            transfer.items.add(new File([bytes], 'bulk-picture.png', {type: 'image/png'}));
            Object.defineProperty(input, 'files', {value: transfer.files, configurable: true});
            input.dispatchEvent(new Event('change', {bubbles: true}));
            return true;
        })()""")
        if not uploaded:
            raise Failure("Mass Insert offered no Image upload control.")
        b.wait("document.querySelector('dialog[open] .mass-insert-image-status')?.textContent"
               ".includes('PNG image')", "the Mass Insert image upload")
        # A Date cell is typed as YYYY-MM-DD, not in the browser locale's
        # MM/DD/YYYY, and its button still offers the browser's calendar.
        date_cell = b.js("""(() => {
            const cell = document.querySelector('dialog[open] tbody tr:nth-child(1) .mass-insert-date');
            const text = cell?.querySelector('input[type=text]');
            return {placeholder: text?.placeholder,
                    button: cell?.querySelector('button')?.getAttribute('aria-label'),
                    picker: cell?.querySelector('input[type=date]')?.className};
        })()""")
        if date_cell != {"placeholder": "YYYY-MM-DD", "button": "Pick a date", "picker": "mass-insert-date-picker"}:
            raise Failure(f"The Mass Insert Date cell is not a YYYY-MM-DD field with a calendar: {date_cell!r}")
        click("dialog[open] tbody tr:nth-child(1) .mass-insert-date button", "\U0001F4C5")
        b.js("""(() => {
            const picker = document.querySelector('dialog[open] tbody tr:nth-child(1) .mass-insert-date-picker');
            picker.value = '2026-10-10';
            picker.dispatchEvent(new Event('change', {bubbles: true}));
            return true;
        })()""")
        if b.js("document.querySelector('dialog[open] tbody tr:nth-child(1) .mass-insert-date input[type=text]').value") \
                != "2026-10-10":
            raise Failure("Picking a day in the calendar did not fill the Date cell as YYYY-MM-DD.")
        click("dialog[open] button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the backed-up worksheet to close")
        b.wait("!!localStorage.getItem('lexicon.web.massInsertDrafts')", "the local Mass Insert backup")

        menu("Manage", "Mass Insert...")
        b.wait(dialog_open("locally backed-up row"), "the resume choice")
        b.call("Input.dispatchKeyEvent", session=b.session, type="keyDown", key="Escape", code="Escape",
               windowsVirtualKeyCode=27)
        b.call("Input.dispatchKeyEvent", session=b.session, type="keyUp", key="Escape", code="Escape",
               windowsVirtualKeyCode=27)
        b.wait("!document.querySelector('dialog[open]')", "the resume choice to cancel")
        if not b.js("!!localStorage.getItem('lexicon.web.massInsertDrafts')"):
            raise Failure("Cancelling the resume choice discarded the Mass Insert draft.")
        menu("Manage", "Mass Insert...")
        b.wait(dialog_open("locally backed-up row"), "the unchanged resume choice")
        click("dialog[open] button", "Resume")
        b.wait("document.querySelector('dialog[open] .mass-insert-table tbody tr td input')?.value === 'Bulk web one'",
               "the restored Mass Insert row")
        b.wait("document.querySelector('dialog[open] .mass-insert-image-status')?.textContent"
               ".includes('PNG image')", "the restored Mass Insert image")
        click("dialog[open] button", "Add row")
        type_into("dialog[open] tbody tr:nth-child(2) td:nth-child(1) input", "Bulk web two")
        click("dialog[open] button", "Add row")
        b.js("[...document.querySelectorAll('dialog[open] tbody button')].at(-1).click(); true")
        b.wait("document.querySelectorAll('dialog[open] .mass-insert-table tbody tr').length === 2",
               "a Mass Insert row to be removed")
        click("dialog[open] button", "Add 10 rows")
        b.wait("document.querySelectorAll('dialog[open] .mass-insert-table tbody tr').length === 12",
               "Add 10 rows to add ten Mass Insert rows")
        type_into("dialog[open] tbody tr:nth-child(2) .mass-insert-date input[type=text]", "10/10/2026")
        click("dialog[open] button", "Insert items")
        b.wait(dialog_open("Row 2: Due must be a valid date as YYYY-MM-DD."), "the Mass Insert date check")
        if api["client"].item("Bulk web one"):
            raise Failure("Mass Insert created items before rejecting an invalid date.")
        type_into("dialog[open] tbody tr:nth-child(2) .mass-insert-date input[type=text]", "")
        click("dialog[open] button", "Insert items")
        b.wait("!document.querySelector('dialog[open]')", "Mass Insert to finish")
        first = api["client"].item("Bulk web one")
        second = api["client"].item("Bulk web two")
        if not first or not second:
            raise Failure("The server did not receive both Mass Insert rows.")
        if first["itemTypeName"] != "Term" \
                or first["fieldValues"].get(str(api["mass_field_id"])) != "High" \
                or first["fieldValues"].get(str(api["mass_date_field_id"])) != "2026-10-10" \
                or not first["fieldValues"].get(str(api["mass_image_field_id"]), "").startswith("image/png:") \
                or first["aliases"] != ["Batch alias", "Web alias"] or first["tags"] != ["batch", "web"] \
                or first["content"] != "# Bulk web one" \
                or first["properties"] != [{"key": "source", "value": "Mass Insert"}]:
            raise Failure(f"The first Mass Insert item is incomplete: {first!r}.")
        if b.js("!!localStorage.getItem('lexicon.web.massInsertDrafts')"):
            raise Failure("The completed Mass Insert draft was not cleared.")

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

    top = "dialog[open]:last-of-type"

    def press(key, code, virtual):
        for kind in ("keyDown", "keyUp"):
            b.call("Input.dispatchKeyEvent", session=b.session, type=kind, key=key, code=code,
                   windowsVirtualKeyCode=virtual, text=key if kind == "keyDown" else "")

    def cards_of(title):
        return api["client"].call("GET", f"/items/{api['client'].item(title)['id']}/cards")["cards"]

    def select_item(title):
        b.js(f"""[...document.querySelectorAll('tbody tr')].find(r => r.textContent.includes({q(title)})).click(); true""")
        b.wait("!!document.querySelector('tbody tr.selected')", "the selection")

    @step("add and edit an item's cards")
    def _():
        select_item("Pointer provenance")
        b.wait("[...document.querySelectorAll('.preview-actions a')].some(a => a.textContent === 'Cards')",
               "the item's Cards link")
        click(".preview-actions a", "Cards")
        b.wait(dialog_open("Questions to ask yourself"), "the card manager")
        b.wait(f"document.querySelector({q(top + ' .cards-item-title')}).textContent === 'Pointer provenance'",
               "the manager naming its item")
        click(f"{top} button", "Add...")
        b.wait("!!document.getElementById('card-question')", "the card editor")
        click(f"{top} button", "Save")
        b.wait(dialog_open("Enter a question."), "a blank card refused")
        type_into("#card-question", "Co znamená řetězec?\nstd::uint64_t")
        type_into("#card-answer", "Příliš žluťoučký kůň\n指针")
        click(f"{top} button", "Save")
        b.wait("!document.getElementById('card-question')", "the editor to close")
        b.wait(f"document.querySelectorAll({q(top + ' .card-table tr[data-id]')}).length === 1", "the card in the list")
        stored = cards_of("Pointer provenance")
        if len(stored) != 1 or stored[0]["question"] != "Co znamená řetězec?\nstd::uint64_t" \
                or stored[0]["answer"] != "Příliš žluťoučký kůň\n指针":
            raise Failure(f"The server holds {stored!r}.")
        if stored[0]["successCount"] != 0 or stored[0]["failureCount"] != 0 or stored[0]["lastAttempt"] is not None:
            raise Failure("A new card was not unanswered.")
        # The text is shown as text, lines and all.
        shown = b.js(f"document.querySelector({q(top + ' .card-table td.card-text')}).textContent")
        if shown != "Co znamená řetězec?\nstd::uint64_t":
            raise Failure(f"The list shows {shown!r}.")
        b.js(f"document.querySelector({q(top + ' .card-table tr[data-id]')}).click(); true")
        click(f"{top} button", "Edit...")
        b.wait("!!document.getElementById('card-question') && !!document.querySelector('.card-statistics')",
               "the editor with the statistics")
        type_into("#card-question", "Co je řetězec?")
        click(f"{top} button", "Save")
        b.wait("!document.getElementById('card-question')", "the editor to close")
        if cards_of("Pointer provenance")[0]["question"] != "Co je řetězec?":
            raise Failure("The server did not keep the edit.")
        click(f"{top} button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the manager to close")

    @step("quiz: Show answer, Yes, then No by the keyboard")
    def _():
        client = api["client"]
        before = client.item("Pointer provenance")
        client.call("POST", f"/items/{before['id']}/cards",
                    {"question": "What does pointer provenance describe?", "answer": "Where a pointer came from."})
        select_item("Pointer provenance")
        menu("View", "Card quiz...")
        b.wait(f"document.querySelector({q(top + ' .quiz-progress')})?.textContent === '1 / 2'", "the first card")
        b.wait(f"document.querySelector({q(top + ' .quiz-question')}).textContent === 'Co je řetězec?'"
               f" && document.querySelector({q(top + ' .quiz-answer-box')}).hidden", "the question, its answer hidden")
        click(f"{top} button", "Show answer")
        b.wait(f"document.querySelector({q(top + ' .quiz-answer')}).textContent === 'Příliš žluťoučký kůň\\n指针'",
               "the answer")
        if cards_of("Pointer provenance")[0]["lastAttempt"] is not None:
            raise Failure("Showing the answer recorded an attempt.")
        click(f"{top} button", "Yes")
        b.wait(f"document.querySelector({q(top + ' .quiz-progress')}).textContent === '2 / 2'", "the second card")
        first = cards_of("Pointer provenance")[0]
        if first["successCount"] != 1 or first["failureCount"] != 0:
            raise Failure(f"Yes left {first!r}.")
        press(" ", "Space", 32)
        b.wait(f"!document.querySelector({q(top + ' .quiz-answer-box')}).hidden", "Space to show the answer")
        press("n", "KeyN", 78)
        b.wait(f"(document.querySelector({q(top + ' .quiz-done')})?.textContent || '') === 'Cards: 2\\nYes: 1\\nNo: 1'",
               "the summary")
        second = cards_of("Pointer provenance")[1]
        if second["failureCount"] != 1 or second["successCount"] != 0:
            raise Failure(f"No left {second!r}.")
        stamp = __import__("re").compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
        if not all(stamp.match(card["lastAttempt"] or "") for card in (first, second)):
            raise Failure(f"The attempts carry no UTC time: {first['lastAttempt']!r}, {second['lastAttempt']!r}.")
        after = client.item("Pointer provenance")
        if (after["understanding"], after.get("reviewedAt"), after["revision"]) != \
                (before["understanding"], before.get("reviewedAt"), before["revision"]):
            raise Failure("A card answer changed the item's review.")
        click(f"{top} button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the quiz to close")

    @step("quiz the neighbourhood from the relationship graph")
    def _():
        client = api["client"]
        client.call("POST", f"/items/{client.item('Object lifetime')['id']}/cards",
                    {"question": "When does an object's lifetime end?", "answer": "When its storage is released."})
        select_item("Pointer provenance")
        menu("View", "Relationship graph...")
        b.wait("document.querySelectorAll('dialog[open] .graph-node').length === 2", "the graph")
        click("dialog[open] button", "Quiz cards")
        b.wait(f"document.querySelector({q(top + ' .quiz-scope-summary')})?.textContent === '3 card(s) from 2 item(s)'",
               "a quiz over both items")
        checked = b.js(f"""(() => {{ const radios = document.querySelectorAll({q(top + ' .quiz-scope input')});
            return [radios[1].checked, document.querySelector({q(top + ' .quiz-depth')}).value]; }})()""")
        if checked != [True, "2"]:
            raise Failure(f"The quiz is not the graph's neighbourhood at depth 2: {checked!r}.")
        titles = set()
        for _ in range(3):
            b.wait(f"!!document.querySelector({q(top + ' .quiz-source')})", "a card")
            titles.add(b.js(f"document.querySelector({q(top + ' .quiz-source')}).textContent"))
            click(f"{top} button", "Show answer")
            click(f"{top} button", "Yes")
            b.wait(f"document.querySelector({q(top + ' .quiz-answer-box')}).hidden"
                   f" || !document.querySelector({q(top + ' .quiz-end')}).hidden", "the next card")
        if titles != {"Item: Pointer provenance", "Item: Object lifetime"}:
            raise Failure(f"The cards came from {titles!r}.")
        if cards_of("Object lifetime")[0]["successCount"] != 1:
            raise Failure("The neighbour's card was not answered.")
        click(f"{top} button", "Close")
        b.wait("document.querySelectorAll('dialog[open]').length === 1", "back to the graph")
        click("dialog[open] button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the graph to close")

    @step("delete a card")
    def _():
        select_item("Pointer provenance")
        menu("Manage", "Cards of selected item...")
        b.wait(f"document.querySelectorAll({q(top + ' .card-table tr[data-id]')}).length === 2", "the card manager")
        b.js(f"document.querySelector({q(top + ' .card-table tr[data-id]')}).click(); true")
        click(f"{top} button", "Delete")
        b.wait(dialog_open("Delete the card"), "the confirmation")
        click(f"{top} button", "Yes")
        b.wait(f"document.querySelectorAll({q(top + ' .card-table tr[data-id]')}).length === 1", "one card left")
        left = cards_of("Pointer provenance")
        if len(left) != 1 or left[0]["question"] != "What does pointer provenance describe?":
            raise Failure(f"The server holds {left!r}.")
        click(f"{top} button", "Close")
        b.wait("!document.querySelector('dialog[open]')", "the manager to close")

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

    @step("Study Plan overview and actions")
    def _():
        # Format the local calendar date explicitly; locale formatting varies by browser.
        today = b.js("(() => { const d = new Date(); return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`; })()")
        end = b.js("(() => { const d = new Date(); d.setDate(d.getDate()+9); return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`; })()")
        created = api["client"].call("POST", "/study-plans", {
            "item": "Effective Modern C++", "group": "Programming", "type": "Book", "unitType": "Page",
            "firstUnit": 101, "lastUnit": 300, "currentProgress": 101,
            "startDate": today, "endDate": end, "studyDaysMask": 127,
        })["studyPlan"]
        menu("Manage", "Study Plan...")
        b.wait(dialog_open("Study Plan"), "the Study Plan dialog")
        click("dialog[open] button", "Add Study Plan")
        b.wait("!!document.querySelector('dialog[open] .study-editor')", "the Study Plan editor")
        dates = b.js("""[...document.querySelectorAll('dialog[open] .study-editor input[placeholder="YYYY-MM-DD"]')]
            .map(input => ({type: input.type, value: input.value}))""")
        if dates != [{"type": "text", "value": today}, {"type": "text", "value": today}]:
            raise Failure(f"Study Plan dates do not use YYYY-MM-DD text fields: {dates!r}")
        fields = b.js("""[...document.querySelectorAll('dialog[open] .study-editor .form-row label')]
            .map(label => label.textContent)""")
        if fields[:3] != ["Item:", "Group:", "Type:"]:
            raise Failure(f"Study Plan form field order is wrong: {fields!r}")
        type_into("dialog[open] .study-editor .form-row:first-child input", "Example")
        type_into('dialog[open] .study-editor input[placeholder="YYYY-MM-DD"]', "27.09.2026")
        click("dialog[open]:has(.study-editor) button", "Save")
        b.wait("[...document.querySelectorAll('dialog[open]')].some(d => d.querySelector('.study-editor') && d.querySelector('.dialog-error')?.textContent.includes('YYYY-MM-DD'))",
               "the date format validation")
        click("dialog[open]:has(.study-editor) button", "Cancel")
        b.wait("!document.querySelector('.study-editor')", "the Study Plan editor to close")
        b.wait("!!document.querySelector('.study-card')", "the Study Plan card")
        expected = api["client"].call("GET", "/study-plans/overview?date=" + today)["plans"][0]
        card = b.js("document.querySelector('.study-card').textContent")
        for label in ("Programming · Book", f"Current progress: page 101", f"Expected progress: page {expected['expectedProgress']}",
                      f"Expected unit range today: pages {expected['expectedUnitStart']}–{expected['expectedUnitEnd']}",
                      "Behind by:", "Recommended today:", "Planned pace:", "Required now:", "Status:"):
            if label not in card:
                raise Failure(f"Study Plan card omits {label!r}: {card}")
        click(".study-card button", "Edit")
        b.wait("!!document.querySelector('dialog[open] .study-editor')", "the saved Study Plan editor")
        saved_group = b.js("document.querySelector('dialog[open] .study-editor .form-row:nth-child(2) input').value")
        if saved_group != "Programming":
            raise Failure(f"Study Plan editor lost its group: {saved_group!r}")
        type_into("dialog[open] .study-editor .form-row:nth-child(2) input", "Advanced C++")
        click("dialog[open]:has(.study-editor) button", "Save")
        b.wait("!document.querySelector('.study-editor')", "the saved Study Plan editor to close")
        saved_group = api["client"].call("GET", "/study-plans/" + str(created["id"]))["studyPlan"]["group"]
        if saved_group != "Advanced C++":
            raise Failure(f"Study Plan form did not save its group: {saved_group!r}")
        if os.environ.get("LEXICON_STUDY_SHOT"):
            b.screenshot(os.environ["LEXICON_STUDY_SHOT"])
        click(".study-card button", "Mark plan complete")
        click("dialog[open] button", "Yes")
        b.wait("document.querySelector('[data-section=finished] summary')?.textContent === 'Finished (1)'", "completed plan in Finished")
        if b.js("document.querySelectorAll('.study-card').length") != 0:
            raise Failure("Finished plan is rendered while its section is collapsed")
        click(".study-browse summary", "Finished (1)")
        b.wait("document.querySelector('[data-section=finished] .study-card') !== null", "the expanded Finished section")
        if b.js("document.querySelectorAll('.study-card').length") != 1:
            raise Failure("Completed plan appears in multiple dashboard sections")
        if b.js("[...document.querySelectorAll('.study-card button')].some(e => e.textContent.trim() === 'Mark plan complete')"):
            raise Failure("Completed plan still offers Mark plan complete")
        click("dialog[open] button", "Close")
        calendar_day = date.fromisoformat(today)
        future = api["client"].call("POST", "/study-plans", {
            "item": "Future book", "type": "Book", "unitType": "Page",
            "firstUnit": 101, "lastUnit": 300, "currentProgress": 0,
            "startDate": (calendar_day + timedelta(days=10)).isoformat(),
            "endDate": (calendar_day + timedelta(days=19)).isoformat(), "studyDaysMask": 127,
        })["studyPlan"]
        past = api["client"].call("POST", "/study-plans", {
            "item": "Past book", "type": "Book", "unitType": "Page",
            "firstUnit": 101, "lastUnit": 300, "currentProgress": 0,
            "startDate": (calendar_day - timedelta(days=19)).isoformat(),
            "endDate": (calendar_day - timedelta(days=10)).isoformat(), "studyDaysMask": 127,
        })["studyPlan"]
        menu("Manage", "Study Plan...")
        b.wait("document.querySelectorAll('.study-browse').length === 2", "the collapsed plan sections")
        click(".study-browse summary", "Upcoming (1)")
        click(".study-browse summary", "Finished (2)")
        b.wait("document.querySelectorAll('.study-card').length === 3", "future and past plans")
        future_card = b.js("[...document.querySelectorAll('.study-card')].find(c => c.querySelector('h3').textContent === 'Future book').textContent")
        past_card = b.js("[...document.querySelectorAll('.study-card')].find(c => c.querySelector('h3').textContent === 'Past book').textContent")
        if "Expected progress: 0" not in future_card or "Expected unit range today: —" not in future_card:
            raise Failure("Future plan does not display zero expected progress and no daily range")
        if "Expected progress: page 300" not in past_card or "Expected unit range today: —" not in past_card:
            raise Failure("Past plan does not display absolute last unit and no daily range")
        if b.js("document.querySelectorAll('.study-card').length") != 3:
            raise Failure("A plan appears in more than one section")
        click("dialog[open] button", "Close")
        api["client"].call("DELETE", "/study-plans/" + str(created["id"]))
        api["client"].call("DELETE", "/study-plans/" + str(future["id"]))
        api["client"].call("DELETE", "/study-plans/" + str(past["id"]))

    @step("203 Study Plans: collapsed sections, search and pagination")
    def _():
        today = date.fromisoformat(b.js("(() => { const d = new Date(); return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`; })()"))
        ids = []
        for index in range(203):
            future = 3 <= index < 103
            created = api["client"].call("POST", "/study-plans", {
                "item": f"Book {index:03}", "group": "C++ Library" if index == 87 else "",
                "note": "Read templates next" if index == 88 else "",
                "type": "Book", "unitType": "Page", "firstUnit": 1, "lastUnit": 300,
                "currentProgress": 300 if index >= 103 else 0,
                "startDate": (today + timedelta(days=10 if future else 0)).isoformat(),
                "endDate": (today + timedelta(days=30)).isoformat(), "studyDaysMask": 127,
            })["studyPlan"]
            ids.append(created["id"])
        menu("Manage", "Study Plan...")
        b.wait("document.querySelectorAll('.study-card').length === 3", "only the three active cards")
        if b.js("[...document.querySelectorAll('.study-browse')].some(d => d.open)"):
            raise Failure("Inactive sections should start collapsed")
        click(".study-browse summary", "Upcoming (100)")
        upcoming = '.study-browse[data-section="upcoming"]'
        finished = '.study-browse[data-section="finished"]'
        b.wait(f"document.querySelectorAll('{upcoming} .study-card').length === 10", "ten future plans")
        names = []
        for page in range(10):
            names += b.js(f"[...document.querySelectorAll('{upcoming} .study-card h3')].map(e => e.textContent)")
            if page < 9:
                click(upcoming + " button", "Next")
        if names != [f"Book {i:03}" for i in range(3, 103)]:
            raise Failure("Pagination skips or duplicates future plans")
        if not b.js(f"[...document.querySelectorAll('{upcoming} button')].find(e => e.textContent === 'Next').disabled"):
            raise Failure("Next is enabled on the last page")
        # Editing a book keeps the page and the expanded section after the server refresh.
        click(upcoming + " .study-card button", "Edit")
        type_into("dialog[open] .study-editor .form-row:nth-child(2) input", "Updated group")
        click("dialog[open]:has(.study-editor) button", "Save")
        b.wait(f"document.querySelector('{upcoming} .study-card .hint')?.textContent.includes('Updated group')", "updated future book on the same page")
        if "Page 10 of 10" not in b.js(f"document.querySelector('{upcoming} .study-pagination').textContent"):
            raise Failure("Editing resets the future page")
        type_into(upcoming + ' input[type="search"]', "c++ library")
        b.wait(f"document.querySelectorAll('{upcoming} .study-card').length === 1", "search across all pages")
        if b.js(f"document.querySelector('{upcoming} .study-card h3').textContent") != "Book 087":
            raise Failure("Group search missed a book on another page")
        type_into(upcoming + ' input[type="search"]', "TEMPLATES")
        if b.js(f"document.querySelector('{upcoming} .study-card h3').textContent") != "Book 088":
            raise Failure("Note search is not case insensitive")
        type_into(upcoming + ' input[type="search"]', "missing book")
        if b.js(f"document.querySelectorAll('{upcoming} .study-card').length") != 0:
            raise Failure("An empty search renders plans")
        type_into(upcoming + ' input[type="search"]', "Book 0")
        click(".study-browse summary", "Finished (100)")
        b.wait(f"document.querySelectorAll('{finished} .study-card').length === 10", "ten completed plans")
        click(finished + " button", "Next")
        if b.js(f"document.querySelector('{upcoming} input').value") != "Book 0":
            raise Failure("Paging Finished changes the Upcoming search")
        type_into(finished + ' input[type="search"]', "Book 20")
        if b.js(f"document.querySelectorAll('{finished} .study-card').length") != 3:
            raise Failure("Finished search does not cover the full collection")
        # Removing the last matching book must clamp its page and keep the query.
        type_into(finished + ' input[type="search"]', "")
        for _ in range(9):
            click(finished + " button", "Next")
        type_into(finished + ' input[type="search"]', "Book 20")
        for remaining in (2, 1, 0):
            click(finished + " .study-card button", "Delete")
            click("dialog[open] button", "Yes")
            b.wait(f"document.querySelectorAll('{finished} .study-card').length === {remaining}", "the refreshed finished search after deletion")
        if b.js(f"document.querySelector('{finished} input').value") != "Book 20":
            raise Failure("Deleting resets the search")
        type_into(finished + ' input[type="search"]', "")
        for _ in range(9):
            click(finished + " button", "Next")
        for remaining in range(6, -1, -1):
            click(finished + " .study-card button", "Delete")
            click("dialog[open] button", "Yes")
            expected = remaining if remaining else 10
            b.wait(f"document.querySelectorAll('{finished} .study-card').length === {expected}", "delete a last-page book and clamp the page")
        if "Page 9 of 9" not in b.js(f"document.querySelector('{finished} .study-pagination').textContent"):
            raise Failure("Deleting the last page does not return to the preceding page")
        click(".study-browse summary", "Upcoming (100)")
        b.wait(f"document.querySelectorAll('{upcoming} .study-card').length === 0", "collapsing removes inactive cards")
        if os.environ.get("LEXICON_STUDY_PAGED_SHOT"):
            b.screenshot(os.environ["LEXICON_STUDY_PAGED_SHOT"])
        click("dialog[open] button", "Close")
        for plan_id in ids:
            # Some books were deleted through the UI already.
            if plan_id not in ids[193:203]:
                api["client"].call("DELETE", "/study-plans/" + str(plan_id))

    @step("sign out")
    def _():
        b.js("document.getElementById('logout-button').click(); true")
        b.wait(dialog_open("Log out"), "the logout confirmation")
        click("dialog[open] button", "Log out")
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
        wait_for_health(server)
        browser = Browser(chrome, work / "profile")
        try:
            passed = run(browser, web, server)
            check_served_client(browser, options.server, database)
            print("ok  the server serves the client itself")
            passed += 1
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
