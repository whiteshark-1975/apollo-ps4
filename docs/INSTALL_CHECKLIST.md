# Install checklist (when jb4 is online)

## A. Get the `.pkg`

Already built locally:

- [x] `dist\IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg` (~22 MB)

Rebuild if needed: `powershell -File scripts\build-pkg.ps1` (Docker Desktop on)

Package ID: `IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg`  
Title on XMB: **PS4 Save Remote**

## B. PS4

- [ ] Console jailbroken, on same LAN as PC
- [ ] Install `.pkg` (USB / FTP / remote installer)
- [ ] Launch **PS4 Save Remote** and leave it open
- [ ] Note notification IP (`http://x.x.x.x:8741`)

## C. PC

- [ ] Python 3.10+
- [ ] `pc\run.bat` (or `python pc\ps4_save_remote.py`)
- [ ] Enter IP → **Connect / Ping** → JSON `ok: true`

## D. One real save

- [ ] Select `…/SAVEDATA/<16-hex>/<CUSAxxxxx>/`
- [ ] Confirm Account-ID / Title / Dir filled from folder name
- [ ] **Decrypt on PS4** → edit something small in plaintext folder
- [ ] **Resign same Account-ID**
- [ ] **Save sealed package** → unzip and confirm path still under that 16-hex Account-ID

## E. Not in v1

- [ ] Trophies — see `docs/TROPHIES.md` (phase 2)
