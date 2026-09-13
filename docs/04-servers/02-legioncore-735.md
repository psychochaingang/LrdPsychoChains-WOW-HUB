# LegionCore — Legion 7.3.5 Server (with LegionBotAI)

**The Legion 7.3.5 emulator we run. Includes our custom playerbot system.**

| | |
|---|---|
| **Core** | LegionCore 7.3.5 (dufernst fork) |
| **Expansion** | Legion 7.3.5 (build 26972) |
| **Bots** | [LegionBotAI](../../projects/legionbotai/) (our code) |
| **Database** | MariaDB 10+/MySQL |
| **Ports** | 3306 (DB) · 1119 (bnetserver) · 8085 (world) · 7879 (SOAP) |

---

## 1. What you need

| Item | Notes |
|------|-------|
| LegionCore source | GitHub (dufernst/LegionCore-7.3.5) |
| Visual Studio 2019/2022 | C++ desktop workload |
| CMake | |
| MariaDB/MySQL | Server + dev libs |
| Boost 1.70 | Place in `deps/boost_1_70_0`, set `BOOST_ROOT` |
| OpenSSL 1.1.1 | `deps/OpenSSL-Win64` |
| MySQL client lib | MariaDB Connector/C |
| Legion 7.3.5.26972 client | [client guide](../03-clients/04-legion.md) |
| Data files (26972) | dbc/maps/vmaps/mmaps/cameras/gt — see [data files](../05-data-files/01-dbc.md) |

## 2. Build

```powershell
$env:BOOST_ROOT = "<SERVER_ROOT>\deps\boost_1_70_0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_INSTALL_PREFIX=..\LegionBuild\bin `
      -DTOOLS=1
cmake --build build --config Release -j 8
```

Outputs: `bin/Release/worldserver.exe`, `bnetserver.exe`, and the tools:
`mapextractor`, `vmap4extractor`, `vmap4assembler`, `mmaps_generator`.

## 3. Database setup

Four databases (note: Legion splits hotfixes):
```sql
CREATE USER 'legion'@'localhost' IDENTIFIED BY '<DB_PASS>';
CREATE DATABASE auth;         -- accounts, realmlist, bnet accounts
CREATE DATABASE characters;   -- characters, inventory, auras...
CREATE DATABASE world;        -- creatures, quests, spawns, scripts
CREATE DATABASE hotfixes;     -- DB2 mirrors (items, spells...)
```
Import the 2020-era base SQL + every file in `sql/updates/` **in date order** (auth → characters → world → hotfixes).

> **Critical:** if your MariaDB uses a newer default row format, tables may import as `COMPACT`. We hit schema errors until tables were `ROW_FORMAT=DYNAMIC`. If the worldserver complains at startup, convert: `ALTER TABLE <t> ROW_FORMAT=DYNAMIC;`

## 4. Data files

Place in `Data/` (set `DataDir` in `worldserver.conf`):
```
Data/
├── dbc/       ← DB2 files (NOT client DBFilesClient dumps — use repack data)
├── maps/      ← terrain heightmaps (format v1.9)
├── vmaps/     ← collision (vmtree/vmtile/vmo)
├── mmaps/     ← navmesh (v9)
├── cameras/
└── gt/
```

**⚠️ Legion data is the hardest part.** Read:
- [Data files overview](../05-data-files/01-dbc.md)
- [Extraction guide](../05-data-files/06-extraction-guide.md)
- [Known issues — the vmap/mmaps saga](../05-data-files/07-known-issues.md)

## 5. Configuration

`worldserver.conf` / `bnetserver.conf` essentials:
```ini
# worldserver.conf
LoginDatabaseInfo     = "127.0.0.1;3306;legion;<DB_PASS>;auth"
WorldDatabaseInfo     = "127.0.0.1;3306;legion;<DB_PASS>;world"
CharacterDatabaseInfo = "127.0.0.1;3306;legion;<DB_PASS>;characters"
HotfixDatabaseInfo    = "127.0.0.1;3306;legion;<DB_PASS>;hotfixes"
DataDir               = "<SERVER_ROOT>/Data"
SOAP.Enabled          = 1
SOAP.Port             = 7879
```
```ini
# bnetserver.conf
LoginDatabaseInfo = "127.0.0.1;3306;legion;<DB_PASS>;auth"
```

> **Port note:** if 7878 is taken (it often is — e.g. by WSL relay), set SOAP to **7879**.

## 6. Accounts (Battle.net style!)

Legion uses **email-format accounts** with a SHA256-based hash:

```
# from the worldserver console:
.bnetaccount create <email> <password>        # creates name@realm
.bnetaccount set gmlevel <email> 6 1
.account create <user> <pass>                 # game account, linked to bnet
```

Or create directly in `auth.battlenet_accounts` + `auth.account` (the hash is calculated with the core's `CalculateShaPassHash`: uppercase user+pass, SHA256, reversed outer hex — see [09-gm-soap.md](../09-gm-soap.md)).

**SEC levels:** 3 = player, 6 = administrator (`.account set gmlevel user 6 1`), 7 = console.

## 7. Start order

```
1. MariaDB
2. bnetserver.exe      (port 1119)
3. worldserver.exe     (port 8085 + SOAP 7879)
```
Sanitized scripts: [scripts/](../../scripts/).

## 8. SOAP (remote admin)

SOAP lets you run GM commands without being in-game — invaluable for scripting/automation:
```powershell
# see scripts/soap.ps1 template
Invoke-Soap -Command "server info"
Invoke-Soap -Command ".lbot team"
```

## 9. Common issues

| Problem | Cause | Fix |
|---------|-------|-----|
| worldserver: "Cannot connect to world database" | DB down / creds / row format | Start MariaDB; verify conf; `ROW_FORMAT=DYNAMIC` |
| "Invalid Map.db2" | Wrong data version / stub DB2s | Use 26972 repack data |
| Mobs fall through dungeon floors | Broken/incomplete vmaps | [Known issues](../05-data-files/07-known-issues.md) |
| Creatures ignore walls | mmap version mismatch | Regenerate mmaps (v9) |
| Can't log in | Using clean client exe | Use the patched exe + bnetserver running |
| LFG role check fails with 2 healers | Core rejects >1 healer | [LegionBotAI docs](../../projects/legionbotai/) |

---

**Next:** [LegionBotAI documentation](../../projects/legionbotai/) — the bot system that makes this server fun.
