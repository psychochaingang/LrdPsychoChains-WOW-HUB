# Version Matrix — Every Version We Have + How They Pair

This is the detailed version guide. The quick table lives in [sheets/VERSIONS.md](../sheets/VERSIONS.md).

---

## 1. What a "build number" is (and why it matters more than anything)

A WoW installation is identified by **expansion + version + build**:

```
7.3.5.26972
│ │ │  └── build number  ← THIS is what matters
│ │ └───── patch
│ └─────── minor
└───────── major (expansion)
```

- **Client and server must match the same build.**
- The build determines: the DBC/DB2 file formats, the network protocol, and the extracted data formats (`maps` v1.9, `vmaps`, `mmaps` v9).
- A build mismatch produces errors that *look* unrelated: "Invalid Map.db2", spells failing, creatures falling through floors, crashes on login.

**Rule #1: verify the build of every file you download. Never trust folder names or archive labels.**

---

## 2. The master matrix (everything in this hub)

| Expansion | Version | Build | Type | Our source | Server core | Status |
|-----------|---------|-------|------|-----------|-------------|--------|
| Vanilla / Turtle WoW | 1.17.1 → 1.17.2 | 7100 (custom) | 32-bit | 5-part RAR archive | Turtle core / vMaNGOS | ✅ tested |
| The Burning Crusade | 2.4.3 | **8606** | 32-bit | repack folder | CMaNGOS-TBC / TC 2.4.3 | ✅ tested |
| Wrath of the Lich King | 3.3.5a | **12340** | 32-bit | ChromieCraft zip | **AzerothCore** + mod-playerbots | ✅ tested |
| Legion | 7.3.5 | **26972** | 64-bit | RAR parts (mislabeled "26792"!) | **LegionCore** + LegionBotAI | ✅ tested |
| Legion (old patched exes) | 7.3.5 | 26124 / 26365 | 64-bit | Wow_Exes.zip | older LegionCore builds | archive |

### Server data builds

| Data set | Matches client | Notes |
|----------|---------------|-------|
| `dbc/` (DB2 files) | 26972 | From the same repack as the server |
| `maps/` | 26972 | Terrain heightmaps (format v1.9) |
| `vmaps/` | 26972 | Collision geometry (see [known issues](05-data-files/07-known-issues.md)) |
| `mmaps/` | 26972 | Navmesh (generator v9) |
| `cameras/`, `gt/` | 26972 | Cameras + game tables |

---

## 3. How to verify what you downloaded (step by step)

### The client executable
1. Right-click `Wow.exe` (or `Wow-64.exe`) → **Properties** → **Details** tab
2. Look at **FileVersion**:
   - WotLK → `3, 3, 5, 12340`
   - TBC → `2, 4, 3, 8606`
   - Legion → `7.3.5.26972`

### Legion's `.build.info` (definitive)
Open `.build.info` in a text editor — the `Version` column at the end shows e.g. `7.3.5.26972`:

```
eu|1|...|...|...||tpr/wow|blzddist1-a.akamaihd.net ...||2018-07-01T00:14:09Z|7.3.5.26972|...
```

### Archive integrity
Use the hashes in [sheets/CHECKSUMS.txt](../sheets/CHECKSUMS.txt):
```powershell
Get-FileHash -Algorithm SHA256 "your-download.rar"
```
Compare with the sheet. Any mismatch = corrupted or wrong file.

---

## 4. The traps we personally hit (so you don't repeat them)

### ❌ Mislabeled archives
Our Legion RAR parts were labeled **"7.3.5.26792"** — they actually contained **26972**.
**Always verify the exe / .build.info. Never trust the folder or archive name.**

### ❌ Streamed Battle.net CASC caches
Some "clients" are actually incomplete streaming caches (the files download on demand from Blizzard). Symptoms:
- `.build.info` present, client launches, but random files are missing
- DB2 files are tiny stubs → extractors fail with "Invalid Map.db2 file format"
- Extraction tools crash while reading CASC data

**How to tell:** check the `Data/` folder size. A full Legion install is ~65 GB; a streamed cache is much smaller and has an `indices/` folder with incomplete data.

### ❌ Patched vs clean executables
- **Clean** `Wow.exe` → connects to Blizzard's servers (retail).
- **Patched** `Wow.exe` → connects to `127.0.0.1` (your server).
Keep both! Store the clean exe as backup before patching. Our [Legion client guide](03-clients/04-legion.md) covers this.

### ❌ Version-mismatched data files
Using 26365 data with a 26972 server (or vice versa) produces:
- "Invalid Map.db2" / DB2 format errors
- Vmap/mmaps load failures ("wrong version")
- Creatures falling through dungeon floors

See [known issues](05-data-files/07-known-issues.md) for the full diagnosis of each.

---

## 5. Which core pairs with which client

| Client build | Recommended core | Repo | Bots available |
|--------------|------------------|------|----------------|
| 1.17.x | Turtle WoW core / vMaNGOS | community | no (vanilla) |
| 2.4.3.8606 | CMaNGOS-TBC / TrinityCore 2.4.3 | github | no |
| 3.3.5a.12340 | **AzerothCore** | github.com/azerothcore | ✅ **mod-playerbots** |
| 7.3.5.26972 | **LegionCore** | github.com/dufernst | ✅ **LegionBotAI** (ours) |

---

## 6. Where to get each version

Full link lists live in [sheets/SOURCES.md](../sheets/SOURCES.md). Summary:

- **Vanilla / Turtle**: Turtle WoW project site; archive.org preservation items
- **TBC 2.4.3**: community repacks; archive.org
- **WotLK 3.3.5a**: ChromieCraft (public download), Warmane, archive.org
- **Legion 7.3.5**: UWoW/EmuCoach repack (the data source LegionCore expects), community repacks, archive.org
- **Server cores**: their GitHub repositories (always free, always current)
- **Tools**: official TrinityCore tool releases for each build tag

> ⚠️ We don't host or guarantee any of these. Always verify with [CHECKSUMS.txt](../sheets/CHECKSUMS.txt) or the source's own hashes.

---

## 7. Why we can't just include them

142 GB of copyrighted client data + GitHub's 100 MB/file limit + DMCA = impossible. See [01-legal-and-hosting.md](01-legal-and-hosting.md) for the full explanation and our safe distribution model.
