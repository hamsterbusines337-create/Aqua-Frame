from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sys
from urllib.parse import urlsplit

page = b"""<!doctype html><meta charset=utf-8><title>Aqua Frame timing fixture</title>
<style>body{margin:22px;background:#0b1628;color:#e6f1ff;font:16px system-ui}
#track{position:relative;background:#203b59;width:520px;height:80px;overflow:hidden}
#piece{position:absolute;left:0;top:16px;width:48px;height:48px;background:#00b8e6;border-radius:50%;will-change:transform}</style>
<h1>Browser frame timing fixture</h1><div id="track"><div id="piece"></div></div>
<script>
const query = new URLSearchParams(location.search);
const jank = query.get('load') === 'jank';
const label = query.get('engine');
const piece = document.getElementById('piece');
let start, last, nextJank = 0, reported = false, hidden = false;
const intervals = [];
document.addEventListener('visibilitychange', () => hidden ||= document.hidden);
function animate(t) {
    start ??= t;
    const elapsed = t - start;
    if (last && elapsed <= 6500 && elapsed > 500) intervals.push(t - last);
    last = t;
    piece.style.transform = 'translateX(' + (elapsed * .18 % 470) + 'px)';
    if (jank && t >= nextJank) {
        nextJank = t + 390;
        const end = performance.now() + 65;
        while (performance.now() < end) {}
    }
    if (!reported && elapsed > 6550 && intervals.length > 0) {
        reported = true;
        intervals.sort((a, b) => a - b);
        const n = intervals.length;
        fetch('/result', {method: 'POST', body: JSON.stringify({engine:label, workload:query.get('load'),
            hidden, samples:n, raf_fps:1000*n/intervals.reduce((a,b)=>a+b,0),
            p95_ms:intervals[Math.ceil(n*.95)-1], p99_ms:intervals[Math.ceil(n*.99)-1]})});
    }
    requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
</script>"""

class Fixture(BaseHTTPRequestHandler):
    def do_GET(self):
        if urlsplit(self.path).path != "/frames":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(page)))
        self.end_headers()
        self.wfile.write(page)

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        if self.path != "/result" or not 1 <= size <= 2048:
            self.send_error(400)
            return
        result = json.loads(self.rfile.read(size))
        with Path(os.environ["AQUA_FRAMES_REPORT"]).open("a", encoding="utf-8") as report:
            report.write(json.dumps(result) + "\n")
        self.send_response(204)
        self.end_headers()

ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Fixture).serve_forever()
