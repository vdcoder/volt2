"""Stand-in for the native C++ side of a volt2 dev session.

One "native thread" owns all app logic, like the UI thread in C++. The HTTP
handler only relays: each synchronous request from the page is one message in,
exactly one reply out. While the native thread waits for the page, it keeps
serving the page's calls, so calls can nest in both directions.
"""
import json, queue, sys, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path

inbox, outbox = queue.Queue(), queue.Queue()
log = []

def js(op):
    """Synchronous native->JS call, made while the page is blocked waiting on us."""
    outbox.put({'type': 'request', 'op': op})
    while True:
        msg = inbox.get()
        if msg['type'] == 'result':
            return msg['value']
        serve_call(msg)          # the page called back into native mid-operation

def serve_call(msg):
    log.append(f"native enter {msg['name']}")
    value = HANDLERS[msg['name']](*msg.get('args', []))
    log.append(f"native leave {msg['name']}")
    outbox.put({'type': 'return', 'value': value})

def on_click():
    name = js({'op': 'get', 'id': 'name', 'prop': 'value'})                  # read
    js({'op': 'set', 'id': 'out', 'prop': 'textContent', 'value': 'Hello ' + name})
    width = js({'op': 'get', 'id': 'out', 'prop': 'offsetWidth'})            # layout read after write
    inner = js({'op': 'click', 'id': 'inner'})   # fires a DOM listener synchronously, which calls native
    return {'name': name, 'width': width, 'inner': inner}

def on_inner():
    # Nested call: page -> native -> page (click) -> native, and we read the page again.
    return 'inner saw: ' + js({'op': 'get', 'id': 'out', 'prop': 'textContent'})

def on_peter(delay=0):
    time.sleep(delay)                     # e.g. the developer is paused at a breakpoint
    flag = js({'op': 'global', 'name': 'peter_pan'})      # the page is blocked right now
    js({'op': 'set', 'id': 'out', 'prop': 'textContent', 'value': f'peter_pan is {flag}'})
    return flag

HANDLERS = {'onPeter': on_peter, 'onClick': on_click, 'onInner': on_inner, 'ping': lambda: 'pong',
            'log': lambda: log}

def native_thread():
    while True:
        serve_call(inbox.get())

class Relay(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"   # keep-alive: one connection for all calls
    disable_nagle_algorithm = True  # TCP_NODELAY: no 40 ms stall on small replies
    def log_message(self, *a): pass
    def do_GET(self):
        body = Path(__file__).with_name('page.html').read_bytes()
        self.send_response(200); self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        msg = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        inbox.put(msg)
        body = json.dumps(outbox.get()).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)

threading.Thread(target=native_thread, daemon=True).start()
ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), Relay).serve_forever()
