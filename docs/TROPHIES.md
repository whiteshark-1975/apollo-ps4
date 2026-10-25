# Trophies (phase 2 — not in v1 agent jobs)

Scope for later: local trophy unlock / export for the save-editing community — **not** game piracy.

## How PS4 trophies work (research)

Apollo already implements the practical path:

| Location | Role |
|----------|------|
| `/user/home/<uid>/trophy/data/<NPWR…>/trophy.img` | Encrypted PFS image |
| `…/sealedkey` | Sealed key for that image |
| `/user/home/<uid>/trophy/db/trophy_local.db` | SQLite unlock state (canonical) |

Unlocking in Apollo is primarily **SQLite updates** to `tbl_trophy_flag` / title / group counters (`trophy_unlock` / `trophy_lock`), plus optional mount of `trophy.img` for file export. XMB may need a sync/resign step depending on firmware.

See: [psdevwiki Trophy](https://www.psdevwiki.com/ps4/Trophy), Apollo DeepWiki trophy management.

## Planned API (later)

- `GET /api/trophies` — list sets for current user  
- `POST /api/trophies/unlock` — unlock by title / trophy id  
- Export/import zip of trophy files  

No random “trophy unlocker” binaries will be vendored into this repo (malware risk, unclear licenses). Prefer Apollo’s open implementation and documented DB schema.
