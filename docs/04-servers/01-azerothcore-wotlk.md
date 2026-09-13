# AzerothCore — WotLK 3.3.5a Server (with mod-playerbots)

**The best-supported WotLK core. Free AI playerbots included. Recommended first server.**

| | |
|---|---|
| **Core** | [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) (GPLv3) |
| **Expansion** | WotLK 3.3.5a (build 12340) |
| **Bots** | [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) |
| **Database** | MySQL 8 / MariaDB |
| **Ports** | 3306 (DB) · 3724 (auth) · 8085 (world) |

---

## 1. What you need

| Item | Notes |
|------|-------|
| AzerothCore source | `git clone` the repo |
| mod-playerbots | Clone into `modules/mod-playerbots` before building |
| Visual Studio 2022+ | With C++ desktop workload |
| CMake 3.16+ | |
| MySQL 8.0 / MariaDB | Server + client dev files |
| OpenSSL, Boost | Bundled or from package manager |
| WotLK client | [3.3.5a guide](../03-clients/03-wotlk.md) |
| Data files | dbc/maps/vmaps/mmaps (see [data files](../05-data-files/01-dbc.md)) |

## 2. Build (Windows, short version)

```powershell
# 1. Clone the core
git clone https://github.com/azerothcore/azerothcore-wotlk.git
cd azerothcore-wotlk

# 2. Add modules (bots!)
git clone https://github.com/mod-playerbots/mod-playerbots.git modules/mod-playerbots

# 3. Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_INSTALL_PREFIX=..\AC\build\bin `
      -DTOOLS=1 -DSCRIPTS=static -DMODULES=static

# 4. Build
cmake --build build --config Release -j 8
```

Output goes to `build/bin/Release/`: `worldserver.exe`, `authserver.exe`, plus tools.

## 3. Database setup

```sql
CREATE USER 'acore'@'localhost' IDENTIFIED BY '<DB_PASS>';
CREATE DATABASE acore_auth;
CREATE DATABASE acore_characters;
CREATE DATABASE acore_world;
CREATE DATABASE acore_playerbots;
GRANT ALL ON acore_auth.* TO 'acore'@'localhost';
GRANT ALL ON acore_characters.* TO 'acore'@'localhost';
GRANT ALL ON acore_world.* TO 'acore'@'localhost';
GRANT ALL ON acore_playerbots.* TO 'acore'@'localhost';
```

Import in order:
1. `data/sql/base/db_auth/` → `acore_auth`
2. `data/sql/base/db_characters/` → `acore_characters`
3. `data/sql/base/db_world/` → `acore_world`
4. `data/sql/updates/` (auth, characters, world, playerbots — apply in date order)
5. `modules/mod-playerbots/data/sql/playerbots/base/` → `acore_playerbots`

> The `mysql` CLI works fine for imports; for large files set `max_allowed_packet=64M` first.

## 4. Data files

Place in `bin/Release/Data/` (or set `DataDir` in the confs):
```
Data/
├── dbc/      ← from the client (extractors)
├── maps/
├── vmaps/
├── mmaps/
└── cameras/
```
See [data file guides](../05-data-files/01-dbc.md) and [extraction](../05-data-files/06-extraction-guide.md).

## 5. Configuration

Edit in `bin/Release/`:
- `worldserver.conf` — DB connections, `DataDir`, rates, `AiPlayerbot.*` settings
- `authserver.conf` — auth DB connection

Minimal changes:
```ini
LoginDatabaseInfo     = "127.0.0.1;3306;acore;<DB_PASS>;acore_auth"
WorldDatabaseInfo     = "127.0.0.1;3306;acore;<DB_PASS>;acore_world"
CharacterDatabaseInfo = "127.0.0.1;3306;acore;<DB_PASS>;acore_characters"
PlayerbotsDatabaseInfo= "127.0.0.1;3306;acore;<DB_PASS>;acore_playerbots"
DataDir               = "<SERVER_ROOT>/Data"
```

## 6. Playerbots (mod-playerbots)

Key settings in `worldserver.conf`:
```ini
AiPlayerbot.Enabled = 1
AiPlayerbot.RandomBotAutologin = 1
AiPlayerbot.MinRandomBots = 500
AiPlayerbot.MaxRandomBots = 5000
AiPlayerbot.RandomBotsPerInterval = 500
```
In-game (GM account):
```
.bot add <character>     # add a bot to your party
.bot <command>           # bot management commands
```

**Boot tip:** with thousands of random bots, the worldserver takes a while to become "ready". Lower `MinRandomBots` for faster boots.

## 7. Account creation

From the worldserver console:
```
account create <user> <pass>
account set gmlevel <user> 3 -1
```
Login in the client with that user + realmlist `127.0.0.1`.

## 8. Start order (always)

```
1. MariaDB/MySQL
2. authserver.exe
3. worldserver.exe
```
Our sanitized start scripts are in [scripts/](../../scripts/).

## 9. Common issues

| Problem | Fix |
|---------|-----|
| worldserver crashes at startup | DB not imported / wrong schema — check console error |
| "Map file ... not found" | Data files missing/misplaced — check `DataDir` |
| Realms list empty in client | authserver DB `realmlist` address wrong |
| Bots not spawning | `AiPlayerbot.Enabled` + playerbots DB imported |
| Slow boot with 5000 bots | Reduce `MinRandomBots` |

---

**Next:** the [file structures guide](../06-file-structures/01-server-tree.md) explains every folder, or jump to [LegionCore](../04-servers/02-legioncore-735.md).
