# Extraction Guide — Generating Your Own Data Files

The most reliable (and most legally defensible) way to get `dbc / maps / vmaps / mmaps`: **extract them from a client you possess**.

---

## 0. Prerequisites

| Item | Why |
|------|-----|
| A **full** client (not a streamed cache!) | Extractors read the client's raw data |
| The **extractor tools for your exact build** | Formats change between builds |
| ~30 GB free disk space | Extracted data is large |
| Time | vmaps: 30–90 min · mmaps: 2–12 h |

### ⚠️ Before you start: is your client full or streamed?

| Check | Full client | Streamed cache |
|-------|-------------|----------------|
| `Data/` size (Legion) | ~60+ GB | much smaller |
| `Data/data/*.idx` | large indexes | tiny/partial |
| Extraction | works | fails with stub DB2 errors |

If streamed: get a full client first. There is no workaround — the files simply aren't there.

---

## 1. Get the right tools

**Option A — build them with your core** (recommended):
- AzerothCore: `cmake -DTOOLS=1` → builds `mapextractor`, `vmap4extractor`, `vmap4assembler`, `mmaps_generator`
- LegionCore: same tools, named the same

**Option B — official releases:**
- TrinityCore GitHub → Releases → pick the tag matching your build (e.g. `7.3.5/26972`) → download the tools package

> ⚠️ Tool version must match your data format. A 3.3.5 tool cannot read 7.3.5 clients.

---

## 2. The extraction pipeline

Run everything **inside the client folder** (or point the tools at it).

### Step 1 — DBC / DB2
```bash
mapextractor -i <CLIENT_ROOT> -o <OUTPUT>/dbc -e 2
```
> **Legion gotcha:** the client's DB2s may be stubs (streamed cache). Use the repack's `dbc` folder instead if extraction fails with `Invalid Map.db2 file format`.

### Step 2 — Maps
```bash
mapextractor -i <CLIENT_ROOT> -o <OUTPUT> -e 1
```
Produces `maps/<mapId>_<X>_<Y>.map` (format **v1.9** for 7.3.5).

### Step 3 — VMaps (three stages!)
```bash
# 3a. Extract raw models → Buildings/ folder
vmap4extractor -d <CLIENT_ROOT>

# 3b. Assemble into vmtree/vmtile/vmo
vmap4assembler Buildings vmaps
```
> **Legion gotcha:** `vmap4extractor` needs `DBFilesClient\FileDataComplete.dbc` — Blizzard **removed this file** from modern CASC data. Without it the extractor exits. Workarounds: patch the tool to skip gameobject models, or use a community-supplied copy.

### Step 4 — MMaps (the long one)
```bash
# all maps (hours)
mmaps_generator

# single map (fast — great for testing)
mmaps_generator 0

# single tile
mmaps_generator 0 32 32
```
> **Legion gotcha:** the generator must match your **map format** (v1.9) and write the right **generator version** (v9). If it refuses your maps ("wrong version"), you need the version patched/rebuilt for your build. Full story in [known issues](07-known-issues.md).

---

## 3. Output layout

```
<OUTPUT>/
├── dbc/
├── maps/
├── vmaps/
├── mmaps/
└── cameras/   (from the client, copy)
└── gt/        (from the repack/tools, copy)
```

Copy into `<SERVER_ROOT>/Data/` and set `DataDir` in `worldserver.conf`.

---

## 4. Verify the extraction

| Check | Expected |
|-------|----------|
| `maps/` file count | Thousands of `_X_Y.map` files |
| `maps` format | first bytes: `MAPS` + `v1.9` |
| `vmaps/` | `.vmtree` per map + many `.vmtile` + `.vmo` models |
| `mmaps/` | `.mmap` + `.mmtile` per map |
| VMap load test | enable `VMAPS` logging, enter world, check for `could not load` spam |

### Byte-level checks (PowerShell)
```powershell
# map version
$b = [IO.File]::ReadAllBytes("Data\maps\0_32_48.map")
[Text.Encoding]::ASCII.GetString($b[0..7])     # -> MAPSv1.9

# mmap tile version (last 4 bytes of the 16-byte header region)
$t = [IO.File]::ReadAllBytes("Data\mmaps\0.mmap")
"mmap size: $($t.Length)"                      # 28 bytes = Detour params
```

---

## 5. Common extraction failures

| Error | Cause | Fix |
|-------|-------|-----|
| `Invalid Map.db2 file format` | Missing/stub DB2s | Use repack dbc, or patch tool |
| `Invalid FileDataComplete.dbc` | Removed from CASC | Patch extractor to skip gameobjects |
| `...is the wrong version` | Tool vs data version mismatch | Use tools matching your build |
| Extractor crashes (access violation) | Missing client files (streamed cache) | Get a full client |
| mmaps take forever | Normal! | Generate only the maps you need |

---

## 6. Why "extract it yourself" is the best path

- ✅ Works forever — no dead links, no takedowns
- ✅ You control the version match
- ✅ Most legally defensible (your client, personal use)
- ❌ Takes time (especially mmaps)
- ❌ Requires a full client

If you can't extract (streamed client, no tools for your build), fall back to a matching **repack** — but always verify with the [known issues](07-known-issues.md) checklist afterward.
