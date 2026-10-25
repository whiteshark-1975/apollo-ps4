#!/usr/bin/env python3
"""End-to-end checks against mock_agent (no PS4 required)."""

from __future__ import annotations

import json
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import zipfile
from http.server import HTTPServer
from pathlib import Path

import mock_agent
from ps4_save_remote import parse_save_folder, pack_encrypted_zip


def http_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as r:
        return json.loads(r.read().decode())


def http_put(url: str, data: bytes) -> dict:
    req = urllib.request.Request(
        url, data=data, method="PUT",
        headers={"Content-Type": "application/octet-stream", "Content-Length": str(len(data))},
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode())


def http_download(url: str, dest: Path) -> None:
    with urllib.request.urlopen(url, timeout=30) as r, open(dest, "wb") as f:
        f.write(r.read())


def test_parse_and_pack() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        account = "aabbccddeeff0011"
        title = "CUSA12345"
        dirname = "SLOT01"
        cusa = root / "PS4" / "SAVEDATA" / account / title
        cusa.mkdir(parents=True)
        (cusa / dirname).write_bytes(b"ENC_VOLUME")
        (cusa / f"{dirname}.bin").write_bytes(b"SEALEDKEY" + b"\x00" * 80)

        a, t, d, folder = parse_save_folder(cusa)
        assert a == account and t == title and d == dirname, (a, t, d)
        assert folder.resolve() == cusa.resolve(), (folder, cusa)

        z = root / "enc.zip"
        pack_encrypted_zip(cusa, a, t, z)
        with zipfile.ZipFile(z) as zf:
            names = zf.namelist()
        assert any(f"PS4/SAVEDATA/{account}/{title}/{dirname}" in n for n in names)
        assert any(n.endswith(f"{dirname}.bin") for n in names)
        print("parse_and_pack OK")


def test_api_flow() -> None:
    srv = HTTPServer(("127.0.0.1", 18741), mock_agent.Handler)
    th = threading.Thread(target=srv.serve_forever, daemon=True)
    th.start()
    time.sleep(0.2)
    base = "http://127.0.0.1:18741"
    try:
        ping = http_json(f"{base}/api/ping")
        assert ping.get("ok") is True
        put = http_put(f"{base}/api/upload?name=encrypted.zip", b"fakezip")
        assert put.get("ok") is True
        dec = http_json(f"{base}/api/decrypt?account=aabbccddeeff0011&title=CUSA12345&dir=SLOT01")
        assert dec.get("ok") is True
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "plain.zip"
            http_download(f"{base}{dec['download']}", out)
            assert out.stat().st_size > 0
            with zipfile.ZipFile(out) as zf:
                assert any("param.sfo" in n or "save.dat" in n for n in zf.namelist())
        res = http_json(
            f"{base}/api/resign?"
            + urllib.parse.urlencode({"account": "aabbccddeeff0011", "title": "CUSA12345", "dir": "SLOT01"})
        )
        assert res.get("ok") is True
        with tempfile.TemporaryDirectory() as tmp:
            sealed = Path(tmp) / "sealed.zip"
            http_download(f"{base}/api/sealed?account=aabbccddeeff0011&title=CUSA12345", sealed)
            assert sealed.stat().st_size > 0
        print("api_flow OK")
    finally:
        srv.shutdown()


if __name__ == "__main__":
    test_parse_and_pack()
    test_api_flow()
    print("ALL_OK")
