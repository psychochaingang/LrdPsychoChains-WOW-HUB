# Data Files Overview — dbc / maps / vmaps / mmaps / cameras / gt

The server needs six kinds of client-extracted data. Get any of them wrong and you get crashes, mobs falling through floors, or spells that can't find targets.

---

## Quick reference

| Folder | Contains | Purpose | Format version | Fatal if wrong? |
|--------|----------|---------|----------------|-----------------|
| `dbc/` | DB2/DBC tables (spells, items, maps...) | All static game data | Build-specific | ☠️ Yes — server won't start |
| `maps/` | Terrain heightmaps | Ground height, LoS | **v1.9** (7.3.5) | ⚠️ Mobs sink/float |
| `vmaps/` | Collision geometry (.vmtree/.vmtile/.vmo) | Walls, buildings, LoS | VMAP_4.8 | ⚠️ **Mobs fall through floors** |
| `mmaps/` | Navmesh (.mmap/.mmtile) | NPC pathfinding | generator **v9** | ⚠️ Creatures ignore walls / fall |
| `cameras/` | Camera data | Cinematic cameras | build-specific | 🟡 Mostly cosmetic |
| `gt/` | Game tables (xp, combat ratings...) | Formulas | build-specific | ⚠️ Wrong XP curves |

---

## 1. `dbc/` — the database client files

**What:** Blizzard's static data tables — every spell, item, creature, map, talent, etc.

**Legion note:** 7.x moved most data to **DB2** format. In LegionCore these live in the `hotfixes` **database** (SQL), not just files. The server loads DB2 data from both the `dbc/` folder and the `hotfixes` DB.

**Critical facts:**
- Must match the client build exactly (26972 data for a 26972 server).
- Streamed Battle.net caches contain **stub DB2s** (empty shells) — extractors fail with `Invalid Map.db2 file format`.
- The `dbc/` folder from a repack is the reliable source.

---

## 2. `maps/` — terrain heightmaps

**What:** Per-tile terrain height data extracted from the client (`<mapId>_<X>_<Y>.map`).

**Format version matters:** 7.3.5 uses **v1.9** (older cores used v1.8). The file starts with:
```
MAPS | v1.9 | ...
```
If a tool says *"is the wrong version, please extract new .map files"*, it expects a different version than your files.

**Used for:** ground height under players/creatures, line-of-sight over terrain, fall damage, water.

---

## 3. `vmaps/` — collision geometry

**What:** Static object collision (buildings, walls, stairs) in three file types:
- `<map>.vmtree` — spatial tree per map
- `<map>_<Y>_<X>.vmtile` — per-tile object references ⚠️ **note the Y_X order (swapped vs maps!)**
- `<model>.vmo` — the actual collision models

**Used for:** line-of-sight (spells), collision, height above/below terrain.

**⚠️ THE BIG ONE:** broken or incomplete vmaps are the #1 cause of **mobs falling through dungeon floors**. We spent days on this — full diagnosis in [known issues](07-known-issues.md).

**How to tell your vmaps are bad:** enable the `VMAPS` logger (see [known issues](07-known-issues.md)) and look for:
```
VMapManager2: could not load '.../vmaps/SomeModel.wmo.vmo'
```
If the count of "could not load" is high (or everything fails), your vmap set is broken.

---

## 4. `mmaps/` — navigation mesh (navmesh)

**What:** Baked pathfinding data generated *from* the vmaps + maps:
- `<mapId>.mmap` — navmesh parameters (Detour)
- `<mapId><X><Y>.mmtile` — navmesh tiles (note: X Y order here, matching the map tiles)

**Used for:** creature pathfinding, chasing, following — everything that makes NPCs walk around walls instead of into them.

**Version rules (we hit all of these):**
- `.mmtile` header contains a **generator version** — the runtime expects **v9** in 7.3.5.
- Contains a **Detour navmesh version** — must equal the runtime's `DT_NAVMESH_VERSION` (7 in 7.3.5).
- Generated **from** the vmaps — if the vmaps are broken, the navmesh is broken too.

---

## 5. `cameras/` — camera data

**What:** Cinematic camera definitions. Rarely breaks anything; copy from the same repack/build.

---

## 6. `gt/` — game tables

**What:** Text tables with formulas and curves (`xp.txt`, `CombatRatings*.txt`, `gtNpcTotalHp*.txt`...).

**Known issue:** some repacks ship a bad `xp.txt` causing wrong XP progression. Regenerate with the official tools for your build if leveling feels wrong.

---

## Where do I get all this?

Three options, safest first:

1. **A matching repack** — a package with the core + DB + data already extracted (easiest; use one for the *same build*).
2. **Community data packs** — pre-extracted `dbc/maps/vmaps/mmaps` for your build.
3. **Extract it yourself** — from a **full** client you possess (most reliable, most legal). → [Extraction guide](06-extraction-guide.md)

⚠️ Whatever you use: **verify the build** and read [known issues](07-known-issues.md) before blaming the core.

---

## Folder placement (both cores)

```
<SERVER_ROOT>/Data/
├── dbc/
├── maps/
├── vmaps/
├── mmaps/
├── cameras/
└── gt/
```
Set `DataDir` in `worldserver.conf` to point at `Data/`.
