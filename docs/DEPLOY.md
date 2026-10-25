# Deploy on jb4

## What you need

1. Jailbroken PS4 on the same LAN as your PC (GoldHEN / similar).
2. Built package: `IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg`
3. PC companion: `pc/ps4_save_remote.py` (Python 3.10+)

## Build the `.pkg`

This Windows machine typically has no Open Orbis SDK. Use GitHub Actions (workflow already in `.github/workflows/build.yml`):

1. Create **your** empty GitHub repo (do **not** push to bucanero/apollo-ps4).
2. In this folder:
   ```powershell
   git remote add origin https://github.com/<you>/<your-repo>.git
   git add -A
   git commit -m "PS4 Save Remote agent + PC companion"
   git push -u origin main
   ```
   (`upstream` already points at Apollo for reference only.)
3. Actions → **Build package** → download artifact `ps4-save-remote_*.pkg`.

Or build on Linux/macOS with [Open Orbis SDK](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/) and dependencies listed in the Apollo README, then `make`.

## Install on PS4

1. Copy the `.pkg` via USB, FTP, or remote package installer.
2. Install with Package Installer.
3. Launch **PS4 Save Remote**.
4. You should see a notification: `http://<ip>:8741`.
5. Leave the app open while using the PC tool (API runs in-process).

Stock **Apollo** can stay installed — this uses Title ID `PSRM00001`.

## PC companion

```powershell
cd pc
python ps4_save_remote.py
```

1. Enter PS4 IP → **Connect / Ping**
2. **Select encrypted save folder** (`…/SAVEDATA/<16-hex>/<CUSAxxxxx>/`)
3. **Decrypt on PS4** → edit files in the opened folder
4. **Resign same Account-ID** → **Save sealed package**

Offline UI smoke test (no console): `pc\run_mock_agent.bat` then companion with IP `127.0.0.1`.  
Automated: `python pc\test_e2e_mock.py` → expects `ALL_OK`.

## Drop zones on console

| Path | Role |
|------|------|
| `/data/fakeusb/PS4/SAVEDATA/<account>/<CUSA>/` | Encrypted in/out (USB layout) |
| `/data/ps4save-remote/inbox/` | Uploads |
| `/data/ps4save-remote/outbox/` | Decrypt zips |

## GPL

This project is derived from [Apollo Save Tool](https://github.com/bucanero/apollo-ps4) (GPL-3.0). If you distribute the modified agent, provide corresponding source.
