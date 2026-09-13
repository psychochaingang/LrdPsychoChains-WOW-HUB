# Game Client File & Folder Structure — Every Folder Explained

Annotated breakdown of a WoW client install (works for Vanilla → Legion; modern clients differ only in data storage format).

---

## The full tree

```
<CLIENT_ROOT>/
├── Wow.exe / Wow-64.exe     ← the game client (32/64-bit)
├── Launcher.exe             ← official launcher (ignore for private servers)
│
├── Data/                    ← THE game data (the big folder)
│   ├── common.MPQ ...       ← [old clients] loose MPQ archives
│   ├── data/                ← [modern clients] CASC archives
│   ├── indices/             ← [modern] CASC indexes
│   ├── config/              ← [modern] CASC config
│   ├── enUS/ enGB/ ruRU/... ← locale folders
│   │   └── realmlist.wtf    ← [Vanilla/TBC/WotLK] server address
│   └── expansion.MPQ, lichking.MPQ, patch.MPQ...
│
├── Interface/
│   └── AddOns/              ← ALL addons go here
│       └── <AddonName>/
│           ├── <AddonName>.toc   ← addon manifest (Interface version!)
│           └── <AddonName>.lua   ← addon code
│
├── WTF/                     ← per-account settings
│   ├── Config.wtf           ← main client config (portal/realmlist for Legion)
│   └── Account/<name>/      ← keybinds, macros, per-character settings
│
├── Cache/                   ← shader + data cache (safe to delete)
├── Logs/                    ← client logs
├── Errors/                  ← crash reports
├── Screenshots/             ← your screenshots
├── Utils/                   ← repair/CE tools
└── .build.info              ← [Legion+] THE version file (verify here!)
```

---

## Critical files

### `realmlist.wtf` (Vanilla / TBC / WotLK)
The only thing you must edit for private servers:
```
set realmlist 127.0.0.1
set patchlist 127.0.0.1
```
Lives in `Data/<locale>/` (e.g. `Data/enUS/`). If a root-level copy exists, it usually wins — edit both.

### `WTF/Config.wtf` (Legion+)
```
SET portal "127.0.0.1"
SET realmlist "127.0.0.1"
```

### `.build.info` (Legion+)
The definitive version check:
```
...|2018-07-01T00:14:09Z|7.3.5.26972|http://blzddist1-a.akamaihd.net...
```
**Always verify this instead of trusting folder names.**

### `Interface/AddOns/<addon>/<addon>.toc`
The `## Interface:` line must match your client:
| Expansion | Interface |
|-----------|-----------|
| Vanilla 1.12 | 11200 |
| TBC 2.4.3 | 20400 |
| WotLK 3.3.5a | 30300 |
| Legion 7.3.5 | 70300 |

Wrong number → addon shows as "out of date" (or won't load).

---

## Data folder: two eras

### Old clients (Vanilla / TBC / WotLK) — MPQ archives
```
Data/
├── common.MPQ          ← base game
├── common-2.MPQ        ← patches
├── expansion.MPQ       ← TBC content
├── lichking.MPQ        ← WotLK content
├── patch.MPQ
├── patch-2.MPQ
└── patch-3.MPQ
```
All data is inside these archives. You can't browse them without an MPQ tool (see [tools](../07-tools.md)).

### Modern clients (Legion+) — CASC storage
```
Data/
├── data/               ← content-addressed data files
├── indices/            ← indexes mapping file IDs → data
├── config/             ← CDN/config info
└── enUS/ ruRU/ ...     ← locale folders
```
- **Full install:** ~60+ GB of data files.
- **Streamed cache:** partial data; downloads on demand. **Cannot be extracted properly** (see [known issues](../05-data-files/07-known-issues.md)).

---

## Cache/WTF quick rules

| Folder | Delete to fix... | Effect |
|--------|------------------|--------|
| `Cache/` | weird rendering, missing models | Rebuilds on next launch |
| `WTF/Account/<x>/` | broken keybinds/settings | Resets that character's settings |
| `Interface/AddOns/` | addon errors | Removes addons (back up first!) |
| `Data/` | NEVER | The game itself |

---

## Client sizes (for verifying a full install)

| Expansion | Approx. size | Format |
|-----------|-------------|--------|
| Vanilla/Turtle | ~5–8 GB | MPQ |
| TBC | ~8 GB | MPQ |
| WotLK | ~16 GB | MPQ |
| Legion | ~65–70 GB | CASC |

If your Legion client is much smaller than ~60 GB, it's a streamed cache — replace it before trying to extract data.

---

## Where addons live (all versions)

```
Interface/AddOns/<AddonName>/<AddonName>.toc
Interface/AddOns/<AddonName>/<AddonName>.lua
```
Multi-folder addons keep everything inside their own folder. Never put loose `.lua` files directly in `AddOns/`.
