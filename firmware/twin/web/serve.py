#!/usr/bin/env python3
"""serve.py [PORT]: serves the virtual CHOMPI on http://localhost:PORT (8765).

Every load of the page first runs ../build.sh wasm, which rebuilds the twin only when the
firmware or the twin changed (a check takes under a second, a rebuild about 20 s). So
reloading the page always plays the working tree as it is now. A build that fails shows its
messages instead of the page. Nothing is cached, so the browser always gets the fresh build.
"""
import http.server
import os
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "..", "build.sh")
lock = threading.Lock()


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=HERE, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if self.path.split("?")[0] in ("/", "/index.html"):
            with lock:
                result = subprocess.run([BUILD, "wasm"], capture_output=True, text=True)
            out = (result.stdout + result.stderr).strip()
            if out:
                print(out, flush=True)
            if result.returncode != 0:
                body = ("<!doctype html><meta charset=utf-8><title>FRIZZ twin: build failed</title>"
                        "<body style='background:#16171a;color:#d8dae0;font:14px monospace'>"
                        "<h1>The twin didn't build</h1><pre>%s</pre>"
                        % out.replace("&", "&amp;").replace("<", "&lt;")).encode()
                self.send_response(500)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
        super().do_GET()

    def log_message(self, fmt, *args):
        pass  # only the builds are worth showing


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
print("the twin: http://localhost:%d  (reload the page after a change; Ctrl-C stops it)" % port,
      flush=True)
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
