# PS4 Save Remote

**PC tool + jailbroken PS4 agent** for decrypting and resigning encrypted PS4 saves over LAN.

Built for the save-editing community: people send `PS4/SAVEDATA/<Account-ID>/…` trees; you return sealed saves for **the same Account-ID** (Jack in → Jack out).

> Fork of [Apollo Save Tool (PS4)](https://github.com/bucanero/apollo-ps4) guts — mount / `patch_sfo` / zip — exposed as a headless HTTP API. **GPL-3.0.**

Not CitiesBot. Not a game pirate toolkit. Trophies are researched for a later phase (`docs/TROPHIES.md`).

## Deploy kit (ready to copy)

| Path | What |
|------|------|
| `dist\IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg` | Install on jb4 |
| `deploy\PS4-Save-Remote-kit.zip` | pkg + PC app + quickstart in one zip |
| `deploy\QUICKSTART.txt` | 1-page install/use steps |

## Pieces

| Piece | What |
|-------|------|
| PS4 `.pkg` (`PSRM00001`) | Agent: auto-starts API on port **8741** |
| `pc/ps4_save_remote.py` | Calm Windows UI: select folder → decrypt → edit → resign |

## Quick flow

1. Install & launch agent on jb4 (see [docs/DEPLOY.md](docs/DEPLOY.md)).
2. On PC: ping → select `SAVEDATA/<16-hex>/<CUSA>/` → decrypt → edit → resign → save sealed zip.
3. Crypto is **console PFS mount/umount**, not PC-side AES of sealed keys.

## API

See [docs/API.md](docs/API.md). Handshake:

```text
GET http://<ps4-ip>:8741/api/ping
```

## Build

See [docs/BUILD.md](docs/BUILD.md). Short version:

- **GitHub Actions** (recommended): push to your repo → download artifact  
- **Docker**: `scripts\build-pkg.ps1` → `dist\*.pkg`  
- Offline PC UI test: `pc\run_mock_agent.bat` then `pc\run.bat` (IP `127.0.0.1`)

Install when jb4 is ready: [docs/INSTALL_CHECKLIST.md](docs/INSTALL_CHECKLIST.md)

## License

GNU GPL v3 — inherits from Apollo Save Tool. Copyright notices retained; this fork adds the Save Remote API and PC companion.
