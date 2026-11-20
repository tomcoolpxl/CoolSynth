"""
The listening pass: a page of checks for the user's ears, each with buttons that set the sound up in the running
CoolSynth and play it, through the app's MCP server (Prefs > Claude (MCP), http://127.0.0.1:47613/mcp).

    python -I tools/listening-pass/server.py [--port 47620] [--items items.json] [--marks marks.json]

Serves page.html and the items on 127.0.0.1 only, and answers the page's buttons:
    POST /setup {"id"}            the item's sound, built and shown on the panel's page its knobs are on
    POST /play  {"id", "try"?}    the item's notes, or one of its tries (values set from the item's sound, then notes)
    GET  /marks, POST /marks {"id", "v", "note"}   the user's marks, kept in a JSON file Claude reads back
Standard library only.
"""
import argparse
import json
import os
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
MCP_URL = "http://127.0.0.1:47613/mcp"
ARGS = None
LOCK = threading.Lock()  # one conversation with the app at a time
COUNTER = [0]


class AppNotAnswering(Exception):
    pass


def call(name, arguments):
    COUNTER[0] += 1
    body = json.dumps({"jsonrpc": "2.0", "id": COUNTER[0], "method": "tools/call",
                       "params": {"name": name, "arguments": arguments}}).encode()
    request = urllib.request.Request(MCP_URL, data=body, headers={"Content-Type": "application/json",
                                                                  "Accept": "application/json, text/event-stream"})
    try:
        with urllib.request.urlopen(request, timeout=90) as response:
            text = response.read().decode("utf-8")
    except (urllib.error.URLError, ConnectionError, TimeoutError) as e:
        raise AppNotAnswering(str(e))
    if text.startswith("event:") or text.startswith("data:"):
        text = "".join(line[5:].strip() for line in text.splitlines() if line.startswith("data:"))
    reply = json.loads(text)
    if "error" in reply:
        return "ERROR " + reply["error"].get("message", "")
    return "\n".join(p.get("text", "") for p in reply["result"].get("content", []) if p.get("type") == "text")


def load_items():
    with open(ARGS.items, encoding="utf-8") as f:
        return json.load(f)


def find(items, ident):
    for item in items["items"]:
        if item["id"] == ident:
            return item
    raise KeyError(ident)


def show(view):
    """Brings a slot's knob page (or the layer's Home page) into view, as the user would by hand."""
    call("click", {"name": "layer.A"})
    if view.get("slot"):
        call("click", {"name": "chain.A." + view["slot"]})
    call("panel", {"control": "knob", "number": 5, "action": "turn", "steps": -8})
    if view.get("page", 0):
        call("panel", {"control": "knob", "number": 5, "action": "turn", "steps": view["page"]})


def build(items, item, values=None, view=None):
    """The item's sound from scratch, then any extra values; returns what did not take."""
    problems = []
    actions = (items["bases"][item["base"]] if item.get("base") else []) + item.get("setup", [])
    for name, arguments in actions:
        answer = call(name, arguments)
        if answer.startswith("ERROR"):
            problems.append(name + ": " + answer)
    if values:
        answer = call("set_values", {"values": values})
        for key, value in values.items():
            if value not in answer:
                problems.append(key + " asked " + value + ": " + answer.strip().replace("\n", "; "))
    show(view or item.get("view", {}))
    return problems


def play(steps):
    heard = []
    for step in steps:
        answer = call("play", {"notes": step["notes"], "velocity": step.get("vel", 100),
                               "seconds": step.get("hold", 1.0), "release": step.get("rel", 0.0)})
        heard.append(answer.strip())
    return heard


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def reply(self, code, payload, kind="application/json"):
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "page.html"), "rb") as f:
                return self.reply(200, f.read(), "text/html; charset=utf-8")
        if self.path == "/items":
            return self.reply(200, load_items())
        if self.path == "/marks":
            return self.reply(200, read_marks())
        if self.path == "/app":
            try:
                with LOCK:
                    sound = json.loads(call("get_sound", {}))
                return self.reply(200, {"ok": True, "sound": sound.get("sound", "")})
            except AppNotAnswering:
                return self.reply(200, {"ok": False})
        self.reply(404, {"error": "not found"})

    def do_POST(self):
        try:
            request = self.body()
            if self.path == "/marks":
                marks = read_marks()
                marks[request["id"]] = {"v": request.get("v", ""), "note": request.get("note", "")}
                with open(ARGS.marks, "w", encoding="utf-8") as f:
                    json.dump(marks, f, indent=2, ensure_ascii=False)
                return self.reply(200, {"ok": True})
            items = load_items()
            item = find(items, request["id"])
            with LOCK:
                if self.path == "/setup":
                    problems = build(items, item)
                    return self.reply(200, {"ok": True, "problems": problems})
                if self.path == "/play":
                    index = request.get("try")
                    if index is None:
                        heard = play(item["play"])
                        return self.reply(200, {"ok": True, "heard": heard, "problems": []})
                    attempt = item["tries"][index]
                    if attempt.get("preset"):  # another sound to compare with
                        call("load_preset", {"name": attempt["preset"]})
                        show(attempt.get("view", {}))
                        problems = []
                    else:
                        problems = build(items, item, attempt.get("values"), attempt.get("view"))
                    heard = play(attempt.get("play", item["play"]))
                    return self.reply(200, {"ok": True, "heard": heard, "problems": problems})
            self.reply(404, {"error": "not found"})
        except AppNotAnswering:
            self.reply(503, {"ok": False, "error": "CoolSynth is not answering. Open the app and switch on Prefs > Claude (MCP)."})
        except Exception as e:  # shown on the page, so a broken item says what broke
            self.reply(500, {"ok": False, "error": type(e).__name__ + ": " + str(e)})


def read_marks():
    try:
        with open(ARGS.marks, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def main():
    global ARGS
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=47620)
    parser.add_argument("--items", default=os.path.join(HERE, "items.json"))
    parser.add_argument("--marks", default=os.path.join(HERE, "marks.json"))
    ARGS = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", ARGS.port), Handler)
    print("Listening pass on http://127.0.0.1:%d" % ARGS.port, flush=True)
    server.serve_forever()


main()
