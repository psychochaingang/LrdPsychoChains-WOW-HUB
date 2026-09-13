# GM Commands & SOAP — Admin Guide

Everything you need to run a server day-to-day: accounts, characters, and remote control via SOAP.

---

## 1. Account creation

### WotLK (AzerothCore / TrinityCore)
From the **worldserver console**:
```
account create <username> <password>
account set gmlevel <username> 3 -1     # 0=player, 3=GM, 4=admin
```

### Legion (LegionCore) — Battle.net style
```
.bnetaccount create <email> <password>   # creates name@realm
.account create <user> <pass>            # game account, link to bnet
.bnetaccount set gmlevel <email> 6 1     # 6 = administrator
```

**SEC levels:** 0 player · 1 moderator · 2 GM · 3 senior GM · 4 admin · 5 console · **6 administrator (Legion)** · **7 console (Legion)**

### Password hash (manual account creation)
Legion stores a SHA256-based hash:
- `CalculateShaPassHash(upper(USER), upper(PASS))` → SHA256 hex → **reversed outer hex**
- Columns: `auth.battlenet_accounts` (email + sha_pass_hash), `auth.account` (username + linked bnet id)

> When in doubt, create accounts with the console commands — never hand-edit hashes unless you must.

---

## 2. In-game GM commands (the essentials)

| Command | What it does |
|---------|--------------|
| `.gm on` / `.gm off` | Toggle GM mode |
| `.levelup 10` | Level up |
| `.additem <id> [count]` | Give an item |
| `.send items <player> "subj" "body" id:count` | Mail items (works from console/SOAP!) |
| `.tele <location>` | Teleport (e.g. `.tele orgrimmar`) |
| `.go xyz <x> <y> <z> [map]` | Teleport to coordinates |
| `.revive` | Resurrect target |
| `.unaura <spellId>` | Remove an aura/debuff |
| `.morph <id>` | Change model |
| `.npc add <id>` | Spawn an NPC |
| `.lookup item <name>` | Find item IDs |
| `.lookup spell <name>` | Find spell IDs |
| `.server info` | Server status |
| `.saveall` | Force character saves |

**Console caveat:** some commands are flagged `console=false` and can't run via SOAP (e.g. `.tele`, `.go`). Console-safe ones include `.revive`, `.send items`, `.lookup`, `.server`.

---

## 3. SOAP — remote command execution

SOAP lets scripts (or you) run commands without being in-game. Great for automation and for bots.

### Enable it
`worldserver.conf`:
```ini
SOAP.Enabled = 1
SOAP.IP      = 127.0.0.1
SOAP.Port    = 7879
```
> If 7878 is busy (common — WSL/relays use it), use **7879**.

### The request format
POST XML to `http://127.0.0.1:<port>/` with HTTP Basic auth (a GM account):

```xml
<?xml version="1.0" encoding="utf-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"
                   xmlns:ns1="urn:TC">
  <SOAP-ENV:Body>
    <ns1:executeCommand>
      <command>server info</command>
    </ns1:executeCommand>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>
```

### PowerShell helper
See [scripts/soap.ps1](../scripts/soap.ps1) — usage:
```powershell
Invoke-Soap -Command "server info"
Invoke-Soap -Command "send items MyChar ""Gear"" ""For you"" 6948:1"
```

### Why SOAP matters
- Automate account creation, item delivery, server checks
- Drive bot systems (our LegionBotAI commands work via SOAP)
- Rescue stuck characters (e.g. `.revive` + hearthstone)

---

## 4. Database admin quickies

| Task | SQL |
|------|-----|
| Find a character | `SELECT guid,name,level FROM characters.characters WHERE name='X';` |
| Fix stuck position | Update `position_x/y/z` in `characters` |
| Reset a password | Use the console command instead |
| Grant GM | `UPDATE auth.account_access SET gmlevel=6 WHERE id=<acctId>;` |
| Realm address | `UPDATE auth.realmlist SET address='127.0.0.1' WHERE id=1;` |

---

## 5. Server maintenance

| Task | How |
|------|-----|
| Save all characters | `.saveall` (console/SOAP) |
| Announce | `.announce <message>` |
| Restart safely | `.server shutdown 10` then restart |
| Backup DBs | `mysqldump` the four DBs |
| Backup data | Copy the `Data/` folder (or at least keep a known-good copy) |

**Golden rule:** stop the worldserver before replacing executables or data files.
