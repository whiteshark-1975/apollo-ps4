#!/usr/bin/env python3
"""
PS4 Save Remote — Windows/PC companion

Talks to the jb4 agent on LAN:
  1. Select encrypted save folder (…/SAVEDATA/<16-hex>/<CUSA>/)
  2. Handshake (/api/ping)
  3. Upload → decrypt → edit locally → resign → download sealed package

Jack in → Jack out (Account-ID from folder name is preserved).
"""

from __future__ import annotations

import json
import re
import shutil
import tempfile
import tkinter as tk
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

ACCOUNT_RE = re.compile(r"^[0-9a-fA-F]{16}$")
DEFAULT_PORT = 8741
APP_TITLE = "PS4 Save Remote"
CONFIG_PATH = Path.home() / ".ps4_save_remote.json"


def http_json(url: str, timeout: float = 120.0) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "PS4SaveRemote-PC/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_put(url: str, data: bytes, timeout: float = 600.0) -> dict:
    req = urllib.request.Request(
        url,
        data=data,
        method="PUT",
        headers={
            "User-Agent": "PS4SaveRemote-PC/1.0",
            "Content-Type": "application/octet-stream",
            "Content-Length": str(len(data)),
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_download(url: str, dest: Path, timeout: float = 600.0) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": "PS4SaveRemote-PC/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp, open(dest, "wb") as out:
        shutil.copyfileobj(resp, out)


def parse_save_folder(folder: Path) -> tuple[str, str, str, Path]:
    """
    Accept either:
      .../SAVEDATA/<account>/<CUSA>/
    or the volume folder itself:
      .../SAVEDATA/<account>/<CUSA>/<dirname>/   (with sibling <dirname>.bin)
    Returns account, title_id, dir_name, cusa_dir
    """
    folder = folder.resolve()
    if not folder.is_dir():
        raise ValueError("Not a folder")

    # If user picked the volume directory, step up to CUSA
    parent = folder.parent
    account_candidate = parent.parent.name if parent else ""
    if ACCOUNT_RE.match(account_candidate) and (folder.with_suffix(".bin").is_file() or (parent / f"{folder.name}.bin").is_file()):
        cusa_dir = parent
        dir_name = folder.name
        title_id = cusa_dir.name
        account = cusa_dir.parent.name
        return account.lower(), title_id, dir_name, cusa_dir

    # User picked CUSA folder
    account = folder.parent.name
    if not ACCOUNT_RE.match(account):
        # …/SAVEDATA/<account>/<CUSA>
        if folder.parent.name.upper() == "SAVEDATA" or folder.name.upper() == "SAVEDATA":
            raise ValueError("Select the CUSA folder under SAVEDATA/<Account-ID>/")
        raise ValueError(
            f"Parent folder must be 16-hex Account-ID, got '{account}'. "
            "Expected …/SAVEDATA/<16-hex>/<CUSAxxxxx>/"
        )

    title_id = folder.name
    # Find first volume: file without extension that has matching .bin
    dir_name = None
    for p in folder.iterdir():
        if p.is_file() and p.suffix.lower() == ".bin":
            vol = p.with_suffix("")
            if vol.is_file() or (folder / vol.name).exists():
                # volume is often a file named same as dir without .bin
                candidate = p.name[:-4]
                if (folder / candidate).exists():
                    dir_name = candidate
                    break
        if p.is_dir() and (folder / f"{p.name}.bin").is_file():
            dir_name = p.name
            break

    if not dir_name:
        # common: volume is a file (not directory) named DIR next to DIR.bin
        bins = [p for p in folder.iterdir() if p.is_file() and p.suffix.lower() == ".bin"]
        if len(bins) == 1:
            dir_name = bins[0].stem
        else:
            raise ValueError("Could not find save volume + matching .bin in folder")

    return account.lower(), title_id, dir_name, folder


def pack_encrypted_zip(cusa_dir: Path, account: str, title_id: str, dest_zip: Path) -> None:
    """Zip as PS4/SAVEDATA/<account>/<title>/… for agent extract into fakeusb."""
    prefix = f"PS4/SAVEDATA/{account}/{title_id}"
    with zipfile.ZipFile(dest_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in cusa_dir.rglob("*"):
            if path.is_file():
                arc = f"{prefix}/{path.relative_to(cusa_dir).as_posix()}"
                zf.write(path, arc)


def load_config() -> dict:
    try:
        if CONFIG_PATH.is_file():
            return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except Exception:
        pass
    return {}


def save_config(data: dict) -> None:
    try:
        CONFIG_PATH.write_text(json.dumps(data, indent=2), encoding="utf-8")
    except Exception:
        pass


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("720x520")
        self.minsize(640, 480)

        cfg = load_config()
        self.save_folder: Path | None = None
        self.account = tk.StringVar()
        self.title_id = tk.StringVar()
        self.dir_name = tk.StringVar()
        self.ps4_ip = tk.StringVar(value=str(cfg.get("ip", "192.168.1.")))
        self.port = tk.StringVar(value=str(cfg.get("port", DEFAULT_PORT)))
        self.status = tk.StringVar(value="Select a save folder, then Connect.")
        self.work_dir: Path | None = None

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _persist(self) -> None:
        save_config({"ip": self.ps4_ip.get().strip(), "port": self.port.get().strip()})

    def _on_close(self) -> None:
        self._persist()
        self.destroy()

    def _build(self) -> None:
        pad = {"padx": 10, "pady": 6}
        frm = ttk.Frame(self)
        frm.pack(fill=tk.BOTH, expand=True, **pad)

        ttk.Label(frm, text=APP_TITLE, font=("Segoe UI", 16, "bold")).pack(anchor=tk.W)
        ttk.Label(
            frm,
            text="Decrypt / resign community saves on your jailbroken PS4. Same Account-ID out as in.",
        ).pack(anchor=tk.W)

        row = ttk.Frame(frm)
        row.pack(fill=tk.X, pady=8)
        ttk.Label(row, text="PS4 IP").pack(side=tk.LEFT)
        ttk.Entry(row, textvariable=self.ps4_ip, width=18).pack(side=tk.LEFT, padx=6)
        ttk.Label(row, text="Port").pack(side=tk.LEFT)
        ttk.Entry(row, textvariable=self.port, width=6).pack(side=tk.LEFT, padx=6)
        ttk.Button(row, text="Connect / Ping", command=self.on_ping).pack(side=tk.LEFT, padx=6)

        ttk.Button(frm, text="1. Select encrypted save folder…", command=self.on_select).pack(anchor=tk.W, pady=4)

        info = ttk.Frame(frm)
        info.pack(fill=tk.X, pady=4)
        for label, var in (
            ("Account-ID", self.account),
            ("Title ID", self.title_id),
            ("Dir name", self.dir_name),
        ):
            r = ttk.Frame(info)
            r.pack(fill=tk.X)
            ttk.Label(r, text=f"{label}:", width=12).pack(side=tk.LEFT)
            ttk.Label(r, textvariable=var).pack(side=tk.LEFT)

        btns = ttk.Frame(frm)
        btns.pack(fill=tk.X, pady=10)
        ttk.Button(btns, text="2. Decrypt on PS4", command=self.on_decrypt).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Open plaintext folder", command=self.on_open_work).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="3. Resign same Account-ID", command=self.on_resign).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Save sealed package…", command=self.on_export_sealed).pack(side=tk.LEFT, padx=4)

        ttk.Label(frm, textvariable=self.status, wraplength=680).pack(fill=tk.X, pady=8)
        ttk.Label(
            frm,
            text="Agent must be running on the PS4 (install PSRM00001.pkg and launch). LAN only.",
            foreground="#444",
        ).pack(anchor=tk.W)

    def base_url(self) -> str:
        return f"http://{self.ps4_ip.get().strip()}:{self.port.get().strip()}"

    def on_ping(self) -> None:
        try:
            data = http_json(f"{self.base_url()}/api/ping", timeout=5)
            self._persist()
            ver = data.get("version", "?")
            st = data.get("status", "")
            self.status.set(f"Connected — agent v{ver} ({st})")
            messagebox.showinfo(APP_TITLE, f"Handshake OK\n{json.dumps(data, indent=2)}")
        except Exception as e:
            self.status.set(f"Ping failed: {e}")
            messagebox.showerror(APP_TITLE, f"Cannot reach agent:\n{e}")

    def on_select(self) -> None:
        path = filedialog.askdirectory(title="Select CUSA folder under SAVEDATA/<Account-ID>/")
        if not path:
            return
        try:
            account, title, dirname, cusa = parse_save_folder(Path(path))
        except ValueError as e:
            messagebox.showerror(APP_TITLE, str(e))
            return
        self.save_folder = cusa
        self.account.set(account)
        self.title_id.set(title)
        self.dir_name.set(dirname)
        self.status.set(f"Selected {cusa}")

    def on_decrypt(self) -> None:
        if not self.save_folder:
            messagebox.showwarning(APP_TITLE, "Select a save folder first")
            return
        account = self.account.get()
        title = self.title_id.get()
        dirname = self.dir_name.get()
        try:
            self.status.set("Packing encrypted zip…")
            self.update_idletasks()
            with tempfile.TemporaryDirectory() as tmp:
                zpath = Path(tmp) / "encrypted.zip"
                pack_encrypted_zip(self.save_folder, account, title, zpath)
                self.status.set("Uploading to PS4…")
                self.update_idletasks()
                http_put(
                    f"{self.base_url()}/api/upload?name=encrypted.zip",
                    zpath.read_bytes(),
                )
                self.status.set("Decrypting (mount)…")
                self.update_idletasks()
                q = urllib.parse.urlencode(
                    {"account": account, "title": title, "dir": dirname}
                )
                result = http_json(f"{self.base_url()}/api/decrypt?{q}", timeout=300)
                if not result.get("ok"):
                    raise RuntimeError(result.get("error", result))
                dl = result.get("download") or f"/api/download?file={Path(result['zip']).name}"
                work = Path(tempfile.mkdtemp(prefix="ps4sr_plain_"))
                zip_out = work / "plaintext.zip"
                http_download(f"{self.base_url()}{dl}", zip_out)
                extract_dir = work / "edit"
                extract_dir.mkdir()
                with zipfile.ZipFile(zip_out, "r") as zf:
                    zf.extractall(extract_dir)
                self.work_dir = extract_dir
            self.status.set(f"Decrypted → {self.work_dir}")
            messagebox.showinfo(APP_TITLE, f"Plaintext ready:\n{self.work_dir}\n\nEdit files, then Resign.")
        except Exception as e:
            self.status.set(f"Decrypt failed: {e}")
            messagebox.showerror(APP_TITLE, str(e))

    def on_open_work(self) -> None:
        if not self.work_dir or not self.work_dir.exists():
            messagebox.showwarning(APP_TITLE, "No plaintext folder yet — decrypt first")
            return
        # Windows
        import os

        os.startfile(self.work_dir)  # type: ignore[attr-defined]

    def _find_plain_root(self) -> Path:
        if not self.work_dir:
            raise RuntimeError("No plaintext folder")
        # Prefer folder that contains sce_sys/param.sfo
        for p in [self.work_dir, *self.work_dir.rglob("*")]:
            if p.is_dir() and (p / "sce_sys" / "param.sfo").is_file():
                return p
        raise RuntimeError("param.sfo not found under plaintext folder")

    def on_resign(self) -> None:
        if not self.account.get():
            messagebox.showwarning(APP_TITLE, "Select save / Account-ID first")
            return
        try:
            plain = self._find_plain_root()
            self.status.set("Packing plaintext…")
            self.update_idletasks()
            with tempfile.TemporaryDirectory() as tmp:
                zpath = Path(tmp) / "plaintext.zip"
                with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
                    for path in plain.rglob("*"):
                        if path.is_file():
                            zf.write(path, path.relative_to(plain).as_posix())
                http_put(
                    f"{self.base_url()}/api/upload?name=plaintext.zip",
                    zpath.read_bytes(),
                )
                q = urllib.parse.urlencode(
                    {
                        "account": self.account.get(),
                        "title": self.title_id.get(),
                        "dir": self.dir_name.get(),
                    }
                )
                self.status.set("Resigning (patch_sfo + seal)…")
                self.update_idletasks()
                result = http_json(f"{self.base_url()}/api/resign?{q}", timeout=300)
                if not result.get("ok"):
                    raise RuntimeError(result.get("error", result))
            self.status.set(f"Resigned OK → {result.get('out')}")
            messagebox.showinfo(
                APP_TITLE,
                "Resigned under the same Account-ID.\n"
                f"On PS4: {result.get('out')}\n\n"
                "Use 'Save sealed package' after copying via FTP, or pull from fakeusb.",
            )
        except Exception as e:
            self.status.set(f"Resign failed: {e}")
            messagebox.showerror(APP_TITLE, str(e))

    def on_export_sealed(self) -> None:
        if not self.account.get() or not self.title_id.get():
            messagebox.showwarning(APP_TITLE, "Select a save first")
            return
        dest = filedialog.asksaveasfilename(
            title="Save sealed package zip",
            defaultextension=".zip",
            filetypes=[("Zip", "*.zip")],
            initialfile=f"sealed_{self.account.get()}_{self.title_id.get()}.zip",
        )
        if not dest:
            return
        try:
            q = urllib.parse.urlencode(
                {"account": self.account.get(), "title": self.title_id.get()}
            )
            self.status.set("Downloading sealed zip…")
            self.update_idletasks()
            http_download(f"{self.base_url()}/api/sealed?{q}", Path(dest))
            self.status.set(f"Saved {dest}")
            messagebox.showinfo(APP_TITLE, f"Sealed package saved:\n{dest}")
        except Exception as e:
            self.status.set(f"Export failed: {e}")
            messagebox.showerror(APP_TITLE, str(e))


def main() -> None:
    App().mainloop()


if __name__ == "__main__":
    main()
