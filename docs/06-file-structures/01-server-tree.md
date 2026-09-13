# Server File & Folder Structure — Every File Explained

Complete annotated breakdown of a private server installation (AzerothCore or LegionCore layout).

---

## The full tree

```
<SERVER_ROOT>/
├── worldserver.exe          ← the game server (maps, creatures, players, AI)
├── authserver.exe           ← WotLK auth server (AzerothCore only)
├── bnetserver.exe           ← Legion auth server (LegionCore only, port 1119)
│
├── worldserver.conf         ← main server config (DBs, rates, DataDir, SOAP)
├── worldserver.conf.dist    ← default template (never edit; copy over .conf)
├── authserver.conf          ← auth DB config (AzerothCore)
├── bnetserver.conf          ← auth DB config (LegionCore)
│
├── Data/                    ← all extracted client data
│   ├── dbc/                 ← DB2/DBC tables (spells, items, maps...)
│   ├── maps/                ← terrain heightmaps  <map>_<X>_<Y>.map
│   ├── vmaps/               ← collision          <map>.vmtree, <map>_<Y>_<X>.vmtile, <model>.vmo
│   ├── mmaps/               ← navmesh            <map>.mmap, <map><X><Y>.mmtile
│   ├── cameras/             ← camera definitions
│   └── gt/                  ← game tables (xp.txt, CombatRatings*.txt...)
│
├── logs/                    ← server logs (if configured)
├── Crashes/                 ← crash dumps (created on crash)
│
├── sql/                     ← SQL files (base + updates) if bundled
│   ├── base/
│   └── updates/
│
└── tools/                   ← extractors (if built alongside)
    ├── mapextractor.exe
    ├── vmap4extractor.exe
    ├── vmap4assembler.exe
    └── mmaps_generator.exe
```

---

## File-by-file

### Executables

| File | What it does | Port |
|------|--------------|------|
| `worldserver.exe` | The world: maps, creatures, combat, AI, players | 8085 |
| `authserver.exe` | WotLK authentication | 3724 |
| `bnetserver.exe` | Legion Battle.net auth | 1119 |

### Configuration

| File | Notes |
|------|-------|
| `worldserver.conf` | The one you edit. DB connections, `DataDir`, rates, loggers, SOAP, bot settings |
| `*.conf.dist` | Reference defaults. Never edit directly — copy entries into `.conf` |
| `bnetserver.conf` | Auth DB for Legion |
| `authserver.conf` | Auth DB for WotLK |

### Data folders (see [data files guide](../05-data-files/01-dbc.md))

| Folder | Size (Legion example) | Critical? |
|--------|----------------------|-----------|
| `dbc/` | ~0.8 GB | ☠️ Server won't start |
| `maps/` | ~0.84 GB | ⚠️ Terrain height |
| `vmaps/` | ~4.8 GB | ⚠️ Collision — mob falling issues |
| `mmaps/` | ~5.7 GB | ⚠️ Pathfinding |
| `cameras/` | small | 🟡 Cosmetic |
| `gt/` | small | ⚠️ Formulas |

### Runtime files

| File/Folder | Created when | Safe to delete? |
|-------------|--------------|-----------------|
| `Crashes/` | On crash (dump + txt) | ✅ Yes |
| `logs/` | Runtime | ✅ Yes |
| `worldserver.pdb` | If built with debug info | ✅ Yes (but useful for crash analysis!) |
| `worldserver.map` | If linked with /MAP | ✅ Yes (useful for crash address lookup) |

---

## Recommended production layout

```
<SERVER_ROOT>/
├── bin/Release/          ← executables + confs + Data (what the .exe needs)
├── source/               ← the core source code (git)
├── build/                ← CMake build tree
├── data-backup/          ← backups of your working Data/
└── scripts/              ← start/stop/backup scripts
```

Keep **backups of `Data/`** and your **databases** — data re-acquisition is the painful part.

---

## What the server writes where

| Action | Writes to |
|--------|-----------|
| Account/character changes | MySQL `auth`/`characters` DBs |
| Crash | `Crashes/` (dump) |
| Logs | console + configured log files |
| Mail/items/auctions | `characters` DB |

---

## Ports summary

| Port | Service | Core |
|------|---------|------|
| 3306 | MySQL/MariaDB | both |
| 3724 | Auth | AzerothCore |
| 1119 | Battle.net auth | LegionCore |
| 8085 | World | both |
| 7878/7879 | SOAP (remote admin) | both |

For friends to connect, forward **8085 + auth port** on your router and set the realmlist address to your public IP. Keep SOAP/DB ports closed to the internet.
