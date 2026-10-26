#!/usr/bin/env python3
"""Stand-in for the prod nginx routing, for step 5 when nginx isn't installed.

Usage: serve.py ROOT [PORT]   (default port 8081)

Follows config/nginx.conf: `try_files $uri $uri.html`, `${uri}index.html`
for trailing slashes, 404.html with status 404 at any depth, /nginx.conf -> 404.
"""
import http.server
import pathlib
import posixpath
import sys
import urllib.parse

ROOT = pathlib.Path(sys.argv[1]).resolve()
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8081
TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "application/javascript",
         ".json": "application/json", ".txt": "text/plain", ".svg": "image/svg+xml",
         ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".webp": "image/webp",
         ".ico": "image/x-icon", ".woff2": "font/woff2", ".ttf": "font/ttf", ".webm": "video/webm"}


def resolve(url_path):
    path = posixpath.normpath(urllib.parse.unquote(url_path))
    if url_path.endswith("/") and path != "/":
        path += "/"
    if path == "/nginx.conf":
        return None
    if path.endswith("/"):
        candidates = [path + "index.html"]
    else:
        candidates = [path, path + ".html"]
    for c in candidates:
        f = (ROOT / c.lstrip("/")).resolve()
        if f.is_file() and ROOT in f.parents:
            return f
    return None


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self, head=False):
        f = resolve(urllib.parse.urlsplit(self.path).path)
        status = 200
        if f is None:
            f, status = ROOT / "404.html", 404
        data = f.read_bytes()
        self.send_response(status)
        self.send_header("Content-Type", TYPES.get(f.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if not head:
            self.wfile.write(data)

    def do_HEAD(self):
        self.do_GET(head=True)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print(f"serving {ROOT} with prod routing on http://localhost:{PORT}/")
    http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
