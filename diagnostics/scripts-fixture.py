import os
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlsplit
from pathlib import Path
import sys

PAGE = b"""<!doctype html><meta charset="utf-8"><title>Aqua Scripts fixture</title>
<h1>Aqua Scripts integration fixture</h1><script>
setTimeout(() => {
    const probe = new URL(location.href).searchParams.get("probe");
    const dom = document.documentElement.dataset.aquaFixture || "none";
    const world = typeof window.aquaIsolatedFixture;
    const plain = document.documentElement.dataset.aquaPlain || "none";
    if (probe) fetch("/probe?" + new URLSearchParams({probe, dom, world, plain}));
    document.body.append(" DOM: " + dom + " | page global: " + world + " | JS: " + plain);
}, 1600);
</script>"""

class Fixture(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path == "/probe":
            data = parse_qs(parsed.query)
            with Path(os.environ["AQUA_SCRIPT_PROBE"]).open("a", encoding="utf8") as log:
                log.write(":".join(data[key][0] for key in ("probe", "dom", "world", "plain")) + "\n")
            self.send_response(204)
            self.end_headers()
            return
        if parsed.path == "/":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(PAGE)))
            self.end_headers()
            self.wfile.write(PAGE)
            return
        self.send_error(404)

HTTPServer(("127.0.0.1", int(sys.argv[1])), Fixture).serve_forever()
