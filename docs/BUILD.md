# Building the agent `.pkg`

## Recommended: GitHub Actions

Workflow: `.github/workflows/build.yml`  
Artifact name: `ps4-save-remote_<sha>`

1. Create your GitHub repository (fork or empty).
2. Point `origin` at it (keep `upstream` = Apollo if you want).
3. Push `main` → wait for **Build package** → download `.pkg`.

## Docker (optional)

```powershell
powershell -File scripts\build-pkg.ps1
```

First build is slow (toolchain + libs). Output under `dist\`.

Requires Docker Desktop. This machine did not have Docker at last check.

## Native Linux/macOS

Install [Open Orbis SDK](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/) and the libraries listed in Apollo’s README, then:

```bash
make createzip
make
```

Produces `IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg`.
