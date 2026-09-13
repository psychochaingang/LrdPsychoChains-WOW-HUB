# Known Issues — The Data File Traps (And Every Fix We Found)

These are the real, tested problems we hit building a Legion 7.3.5 server — the ones that took days and that no other guide documents. If your creatures fall through floors, walk through walls, or your extractors crash, **the answer is in here.**

---

## Issue #1 — Mobs fall through dungeon floors 🔥

### Symptoms
- Creatures in dungeons sink through the floor when they aggro/chase
- They attack from under the map
- The player can also fall through in some spots

### Root causes (there are two!)

#### Cause A: Broken/incomplete vmaps
**Diagnosis:** enable the `VMAPS` debug logger and watch the server log while entering a dungeon.

`worldserver.conf`:
```ini
Logger.VMAPS = 60,2,Console World Errors
Logger.MMAPS = 59,2,Console World Errors
```
Restart, enter the dungeon, then count the results:
```powershell
Select-String "could not load" worldserver.log | Measure-Object
Select-String "loading file"   worldserver.log | Measure-Object
```
**If "could not load" ≈ everything and "loading file" ≈ 0 → your vmap set is broken.**

The log looks like:
```
VMapManager2: could not load '.../vmaps/Pa_Stone_Stairs_Long01.wmo.vmo'
```

**What we found:** one repack's vmaps had **0 of 14,003 models loading** (files existed with the right magic but failed deep in the binary tree parse — a tool-version mismatch during the repack's extraction). Another repack's vmaps loaded **1,705 models** with only 209 failures.

**Fix:** replace the vmaps with a compatible set:
```powershell
# swap the vmaps folder (keep a backup!)
Rename-Item "<SERVER_ROOT>\Data\vmaps" "vmaps_broken"
Copy-Item "<GOOD_SOURCE>\vmaps" "<SERVER_ROOT>\Data\vmaps" -Recurse
```
Or extract them yourself with tools matching your build → [extraction guide](06-extraction-guide.md).

#### Cause B: Navmesh (mmaps) built from broken vmaps
Even with good vmaps, the **mmaps** may have been baked from the broken set — creatures then path on a broken navmesh.

**Fix:** regenerate the mmaps **after** fixing the vmaps:
```bash
# workspace with junctions/links to Data\maps and Data\vmaps + empty mmaps/
mmaps_generator <mapId>     # e.g. 960 (Jade Serpent)
```
Then copy the generated `.mmap`/`.mmtile` files over the old ones and restart.

### Emergency workaround
If you can't fix the data yet, a server-side safety net can lift fallen creatures back up (see our LegionBotAI code for an example that teleports creatures to the player's floor level when they drop more than a few yards below).

---

## Issue #2 — mmaps version mismatches 🔥

### Symptoms
- Creatures ignore walls, walk through geometry
- Log lines like:
  ```
  MMAP:loadMap: 00003248.mmtile was built with generator v7, expected v9
  ```
- Or the generator refuses your maps:
  ```
  maps/0961_30_26.map is the wrong version, please extract new .map files
  ```

### The version table (7.3.5)

| Thing | Runtime expects | Common mismatch |
|-------|-----------------|-----------------|
| `.mmtile` generator version | **9** | repacks built with v7 |
| `.mmap`/Detour version | **7** | — |
| `.map` format | **v1.9** | tools built for v1.8 |

### Fix
Patch the **generator** to match your data + runtime, then regenerate:

1. `mmaps_generator/TerrainBuilder.cpp` → `MAP_VERSION_MAGIC = "v1.9"` (to read your maps)
2. `mmaps_generator/MapBuilder.cpp` → `#define MMAP_VERSION 9` (to write what the runtime accepts)
3. Rebuild the tool, regenerate the maps, install, restart.

> The header structs are identical between versions — only the constants differ, so patching is safe.

---

## Issue #3 — vmtile filename order is swapped

`maps/` files are named `<map>_<X>_<Y>.map` but **`vmaps/` tiles are `<map>_<Y>_<X>.vmtile`** (Y first!).

This is *not* a bug — but it makes coverage comparisons misleading. If you compare map tiles vs vmap tiles and see "no overlap", you're comparing X-Y against Y-X. Don't panic, and don't "fix" it.

---

## Issue #4 — Streamed CASC clients (extraction impossible)

### Symptoms
- Extractors fail instantly: `Invalid Map.db2 file format`
- `FileDataComplete.dbc` missing (Blizzard removed it)
- Extractors crash with access violations
- Client is much smaller than expected

### Cause
The "client" is a **Battle.net streaming cache**, not a full install. Files download on demand, and the DB2s present are stubs.

### Fix
- Get a **full client** for extraction, or
- Use a repack's data files (verify with Issues #1/#2!), or
- Patch the extractor to skip missing pieces (e.g. gameobject models) — but you'll still lack streamed map data.

---

## Issue #5 — "Invalid Map.db2" on the server

| Cause | Fix |
|-------|-----|
| Data from a different build | Match build (26972 ↔ 26972) |
| Stub DB2s from a streamed client | Use repack `dbc/` |
| Wrong `DataDir` | Fix `worldserver.conf` |
| `hotfixes` DB empty (Legion) | Import the hotfixes SQL |

---

## Issue #6 — XP / leveling feels wrong (WotLK)

Some repacks ship a bad `gt/xp.txt`. Regenerate `gt` with the official tools for your build, or take the `gt` folder from a known-good source.

---

## Diagnostic cheat-sheet

| Symptom | First check | Then |
|---------|-------------|------|
| Mobs fall through floors | VMap "could not load" count | Fix vmaps → regenerate mmaps |
| Creatures ignore walls | mmap generator version in log | Regenerate mmaps with v9 |
| Server won't start (DB2 errors) | `dbc/` build match | Replace with repack data |
| Extractors crash | Client full or streamed? | Get a full client |
| Generator "wrong version" | `maps` format vs tool | Patch tool constants, rebuild |

---

## The lessons

1. **Version-match everything** — client, dbc, maps, vmaps, mmaps, core.
2. **Repack data is not equal** — two repacks of the same expansion had wildly different vmap quality.
3. **Test the data, don't trust it** — the vmap logger tells you the truth in 5 minutes.
4. **Extract it yourself** when possible — it's the only guaranteed-matching source.
