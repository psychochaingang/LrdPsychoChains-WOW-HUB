# Scripts

Sanitized templates for running and automating your server. **Replace the `<PLACEHOLDERS>` before use.**

---

## Files

| File | Purpose |
|------|---------|
| `start_all.bat.template` | Starts MariaDB (if needed) → bnetserver → worldserver. Also stops any other server holding the ports |
| `stop_all.bat.template` | Kills whatever listens on 8085/1119 (leaves MariaDB running) |
| `soap.ps1` | Run GM commands remotely over SOAP |
| `upload-to-archiveorg.ps1` | Bulk-upload the packages to archive.org (uses the `ia` CLI) |
| `upload-worker.py` | Resumable archive.org upload worker - skips files by size, uploads only what is missing |

## Setup

1. Copy the `.template` files and rename (drop `.template`).
2. Edit the placeholders:
   - `<SERVER_ROOT>` — folder containing `worldserver.exe`
   - `<MYSQL_ROOT>` — your MariaDB install folder
   - `<GM_USER>:<GM_PASSWORD>` — a GM account for SOAP
3. Put them next to your server executables (or anywhere — they use absolute paths).

## SOAP usage

```powershell
# server status
.\soap.ps1 -Command "server info"

# send mail with items (quote carefully in PowerShell)
.\soap.ps1 -Command 'send items MyChar "Gear" "Enjoy" 6948:1'

# drive LegionBotAI
.\soap.ps1 -Command "lbot MyChar team"
```

**Notes:**
- `SOAP.Port` in `worldserver.conf` must match `$SoapUrl` (default 7879; 7878 is often taken).
- SOAP runs with the GM account's permissions — keep the endpoint on `127.0.0.1` and never expose it publicly.
- Commands flagged `console=false` (like `.tele`, `.go`) can't run over SOAP.

## archive.org uploads

```powershell
# one-time setup
python -m pip install internetarchive
ia configure        # paste your S3 keys from https://archive.org/account/s3.php

# bulk upload (simple path)
.\upload-to-archiveorg.ps1 -Package all

# resumable worker (recommended for big re-runs)
python upload-worker.py my-item-id "D:\path\to\folder" "Item title" "Item description"
```

**Field notes (learned the hard way):**
1. Do **not** pass `collection=...` metadata unless your account has upload rights there. A wrong collection fails every file with "Access Denied".
2. Keep descriptions **comma-free** — the `ia` CLI splits comma values and every upload is rejected.
3. archive.org rejects **multi-volume RAR sets**. Wrap `part1..partN.rar` in one store-mode zip and upload the zip.
4. Items with **tens of thousands of small files** hit the per-item task queue ration ("bucket_tasks_queued exceeds rationed amount"). Pack such folders into one zip per folder, or upload with `upload-worker.py` in parallel per subfolder.
5. If a file's name starts with a dot, archive.org stores it only in version history (hidden-file quirk). Rename it if it must be visible.

## WotLK variant

For AzerothCore, the same pattern works — swap `bnetserver.exe` for `authserver.exe` and the ports (auth = 3724). The start script's structure is identical.
