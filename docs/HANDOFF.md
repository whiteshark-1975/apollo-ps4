# Status handoff (agent progress)

## Done

- Cloned Apollo → this workspace; rebranded package **PS4 Save Remote** / `PSRM00001`
- LAN API module `source/ps4api.c` auto-starts on app launch (port **8741**)
- Endpoints: ping, info, upload, decrypt, resign, download, sealed zip
- Hardened: URL-decode query params, recursive `param.sfo` find, SFO null checks, FW keyset gate
- PC companion `pc/ps4_save_remote.py` + mock agent + `test_e2e_mock.py` (ALL_OK offline)
- Docs: API, DEPLOY, BUILD, INSTALL_CHECKLIST, TROPHIES, HANDOFF
- CI workflow + Dockerfile + `scripts/build-pkg.ps1`
- `upstream` remote = original Apollo (do **not** push fork commits there)

## Artifact ready

- **`.pkg` built:** `dist/IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg` (~22 MB)
- Rebuild anytime: `powershell -File scripts\build-pkg.ps1` (Docker Desktop running)

## Still blocked on hardware

1. **jb4 online** — install pkg, launch, ping from PC (see INSTALL_CHECKLIST.md)

## When jb4 is up

Follow [INSTALL_CHECKLIST.md](INSTALL_CHECKLIST.md).

## Defaults locked while AFK

Python+tk PC UI · open LAN API · trophies phase 2 only · Jack→Jack Account-ID
