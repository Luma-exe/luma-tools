"""Local proxy that forwards every request to tools.lumaplayground.com so the
preview tool (locked to localhost) can drive against real prod content.

Run via launch.json: serves on :8765, mirrors prod 1:1."""

import http.server, urllib.request, urllib.parse, ssl, sys

UPSTREAM = "https://tools.lumaplayground.com"
PORT = 8771
CTX = ssl.create_default_context()


class Proxy(http.server.BaseHTTPRequestHandler):
    def do_GET(self):   self._forward("GET")
    def do_POST(self):  self._forward("POST")
    def do_HEAD(self):  self._forward("HEAD")
    def log_message(self, *a, **k): pass

    def _forward(self, method):
        url = UPSTREAM + self.path
        body = None
        if method == "POST":
            length = int(self.headers.get("Content-Length", "0") or "0")
            body = self.rfile.read(length) if length else None
        req = urllib.request.Request(url, data=body, method=method)
        # Deliberately DO NOT forward Accept-Encoding — we want uncompressed
        # bytes from prod so we can pass them straight through to the browser
        # without dealing with gzip.
        for h in ("Content-Type", "Accept", "User-Agent"):
            v = self.headers.get(h)
            if v: req.add_header(h, v)
        req.add_header("Accept-Encoding", "identity")
        try:
            r = urllib.request.urlopen(req, context=CTX, timeout=30)
            data = r.read()
            self.send_response(r.status)
            for k, v in r.headers.items():
                kl = k.lower()
                # Strip headers that block the browser from running the page
                # as if it were localhost (CSP, COEP/COOP for non-tool routes,
                # Set-Cookie, Strict-Transport-Security).
                if kl in ("content-security-policy", "set-cookie",
                          "strict-transport-security", "content-length",
                          "transfer-encoding", "content-encoding"):
                    continue
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            self.send_response(502)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(("Proxy error: " + str(e)).encode())


if __name__ == "__main__":
    print(f"prod_proxy listening on http://127.0.0.1:{PORT}", flush=True)
    http.server.HTTPServer(("127.0.0.1", PORT), Proxy).serve_forever()
