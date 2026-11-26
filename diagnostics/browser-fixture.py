from http.server import BaseHTTPRequestHandler, HTTPServer
import os
from pathlib import Path
import sys
from time import sleep

PAGE = b"""<!doctype html><meta charset="utf-8"><title>Aqua Frame browser fixture</title>
<button style="position:absolute;top:16px;left:16px;width:220px;height:42px"
onclick="document.documentElement.requestFullscreen()">Enter fullscreen</button>
<a href="/download" style="position:absolute;top:92px;left:16px">Download test file</a>
"""

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        cookie = self.headers.get('Cookie', '')
        with Path(os.environ['AQUA_BROWSER_PROBE']).open('a', encoding='utf-8') as log:
            log.write(f'{self.path}|{cookie}\n')
        if self.path == '/download':
            data = b'Fixture from the Aqua Frame Windows test.\n'
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Disposition', 'attachment; filename=browser-fixture.txt')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        sleep(.13)
        self.send_response(200)
        if self.path == '/normal':
            self.send_header('Set-Cookie', 'normal=1; Path=/')
        if self.path == '/private':
            self.send_header('Set-Cookie', 'secret=1; Path=/')
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)

HTTPServer(('127.0.0.1', int(sys.argv[1])), Handler).serve_forever()
