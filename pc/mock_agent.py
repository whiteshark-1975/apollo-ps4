#!/usr/bin/env python3
"""Local mock of the PS4 agent for PC UI testing without a console."""

from __future__ import annotations

import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent / "_mock_data"
INBOX = ROOT / "inbox"
OUTBOX = ROOT / "outbox"


class Handler(BaseHTTPRequestHandler):
    def _json(self, code: int, obj: dict) -> None:
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        u = urlparse(self.path)
        if u.path == "/api/ping":
            self._json(
                200,
                {
                    "ok": True,
                    "service": "PS4SaveRemote",
                    "version": "mock-1.0",
                    "ip": "127.0.0.1",
                    "port": 8741,
                    "status": "mock",
                },
            )
            return
        if u.path == "/api/info":
            self._json(200, {"ok": True, "version": "mock", "status": "mock"})
            return
        if u.path == "/api/decrypt":
            OUTBOX.mkdir(parents=True, exist_ok=True)
            z = OUTBOX / "decrypt_mock.zip"
            # minimal zip with sce_sys placeholder
            import zipfile

            with zipfile.ZipFile(z, "w") as zf:
                zf.writestr("sce_sys/param.sfo", b"MOCK")
                zf.writestr("save.dat", b"edit-me")
            self._json(
                200,
                {"ok": True, "zip": str(z), "download": f"/api/download?file={z.name}"},
            )
            return
        if u.path == "/api/download":
            qs = parse_qs(u.query)
            name = (qs.get("file") or [""])[0]
            path = OUTBOX / name
            if not path.is_file():
                self._json(404, {"ok": False, "error": "missing"})
                return
            data = path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if u.path == "/api/resign":
            self._json(200, {"ok": True, "out": "/data/fakeusb/PS4/SAVEDATA/mock/", "account": "mock"})
            return
        if u.path == "/api/sealed":
            OUTBOX.mkdir(parents=True, exist_ok=True)
            z = OUTBOX / "sealed_mock.zip"
            import zipfile

            with zipfile.ZipFile(z, "w") as zf:
                zf.writestr("PS4/SAVEDATA/deadbeefdeadbeef/CUSA00000/slot", b"sealed")
                zf.writestr("PS4/SAVEDATA/deadbeefdeadbeef/CUSA00000/slot.bin", b"key")
            data = z.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        self._json(404, {"ok": False, "error": "not found"})

    def do_PUT(self) -> None:  # noqa: N802
        u = urlparse(self.path)
        if u.path != "/api/upload":
            self._json(404, {"ok": False})
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        qs = parse_qs(u.query)
        name = (qs.get("name") or ["upload.bin"])[0]
        INBOX.mkdir(parents=True, exist_ok=True)
        (INBOX / name).write_bytes(body)
        self._json(200, {"ok": True, "path": str(INBOX / name), "bytes": length})

    def log_message(self, fmt: str, *args) -> None:
        print("%s - %s" % (self.address_string(), fmt % args))


if __name__ == "__main__":
    ROOT.mkdir(parents=True, exist_ok=True)
    print("Mock agent http://127.0.0.1:8741  (Ctrl+C to stop)")
    HTTPServer(("127.0.0.1", 8741), Handler).serve_forever()
