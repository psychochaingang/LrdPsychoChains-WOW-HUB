# Scripts

Sanitized templates for running and automating your server. **Replace the `<PLACEHOLDERS>` before use.**

---

## Files

| File | Purpose |
|------|---------|
| `start_all.bat.template` | Starts MariaDB (if needed) → bnetserver → worldserver. Also stops any other server holding the ports |
| `stop_all.bat.template` | Kills whatever listens on 8085/1119 (leaves MariaDB running) |
| `soap.ps1` | Run GM commands remotely over SOAP |

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

## WotLK variant

For AzerothCore, the same pattern works — swap `bnetserver.exe` for `authserver.exe` and the ports (auth = 3724). The start script's structure is identical.
