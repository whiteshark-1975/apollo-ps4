# PS4 Save Remote — LAN API

Base URL: `http://<ps4-ip>:8741`

LAN only. Do not port-forward this agent to the internet.

## Endpoints

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/ping` | Handshake / health |
| GET | `/api/info` | User ID, operator Account-ID, paths |
| PUT/POST | `/api/upload?name=FILE` | Raw body → `/data/ps4save-remote/inbox/FILE` |
| GET | `/api/decrypt?account=&title=&dir=` | Mount locked save → plaintext zip in outbox |
| GET | `/api/resign?account=&title=&dir=` | Plaintext → sealed under **same** Account-ID |
| GET | `/api/download?file=` | Download file from outbox |
| GET | `/api/sealed?account=&title=` | Zip sealed `fakeusb` tree for that owner |

### Special upload names

- `encrypted.zip` — extracted under `/data/fakeusb/` (expect `PS4/SAVEDATA/<account>/<CUSA>/…`)
- `plaintext.zip` — used by resign (root should contain `sce_sys/param.sfo`)

### Account-ID rule

`account` is always the **16-hex PSN Account-ID** from the USB folder name. Resign patches `param.sfo` to that ID and writes:

`/data/fakeusb/PS4/SAVEDATA/<account>/<TITLE_ID>/<dir>` + `<dir>.bin`

Jack in → Jack out.

## Example handshake

```bash
curl http://192.168.1.50:8741/api/ping
```
