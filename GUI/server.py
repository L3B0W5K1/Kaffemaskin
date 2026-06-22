#!/usr/bin/env python3
"""
Tiny single-file server: serves espresso_remote.html and proxies /api/* to
the Leshan demo server. Sidesteps CORS — both the page and the API are
same-origin from the browser's point of view.

Drop this next to espresso_remote.html, then:
    python3 serve.py                          # defaults to localhost:8080
    python3 serve.py --leshan http://192.168.1.42:8080
    python3 serve.py --port 9000

Open http://<this-host>:9000/  in any browser. In the GUI, leave the
"Leshan" field empty so requests go to the same origin (this script).
"""

import argparse
import http.server
import socketserver
import urllib.request
import urllib.error
from pathlib import Path

HTML_FILE = Path(__file__).parent / "espresso_remote.html"


class Handler(http.server.BaseHTTPRequestHandler):
    leshan_url = "http://localhost:8080"

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._serve_html()
        elif self.path.startswith("/api/"):
            self._proxy("GET")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path.startswith("/api/"):
            self._proxy("POST")
        else:
            self.send_error(404)

    def do_PUT(self):
        if self.path.startswith("/api/"):
            self._proxy("PUT")
        else:
            self.send_error(404)

    def _serve_html(self):
        if not HTML_FILE.exists():
            self.send_error(500, f"{HTML_FILE.name} not found next to serve.py")
            return
        data = HTML_FILE.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _proxy(self, method):
        url = self.leshan_url.rstrip("/") + self.path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else None
        req = urllib.request.Request(url, data=body, method=method)
        for h in ("Content-Type", "Accept"):
            v = self.headers.get(h)
            if v:
                req.add_header(h, v)
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                payload = resp.read()
                self.send_response(resp.status)
                self.send_header("Content-Type",
                                 resp.headers.get("Content-Type", "application/json"))
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
        except urllib.error.HTTPError as e:
            payload = e.read() or b""
            self.send_response(e.code)
            self.send_header("Content-Type",
                             e.headers.get("Content-Type", "text/plain"))
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as e:
            msg = f"proxy error: {e}".encode()
            self.send_response(502)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)

    def log_message(self, fmt, *args):
        # Compact one-line log
        print(f"{self.command:4s} {self.path}  -> {args[1] if len(args) > 1 else '-'}")


class ReusableTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--leshan", default="http://localhost:8080",
                   help="Leshan demo server base URL (default: %(default)s)")
    p.add_argument("--port", type=int, default=9000,
                   help="Port to serve on (default: %(default)s)")
    p.add_argument("--host", default="0.0.0.0",
                   help="Bind address (default: %(default)s)")
    args = p.parse_args()

    Handler.leshan_url = args.leshan

    with ReusableTCPServer((args.host, args.port), Handler) as srv:
        print(f"GUI:    http://{args.host}:{args.port}/")
        print(f"Proxy:  /api/*  ->  {args.leshan}")
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            print("\nbye")
