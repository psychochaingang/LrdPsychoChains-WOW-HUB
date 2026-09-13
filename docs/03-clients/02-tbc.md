# The Burning Crusade Client — 2.4.3 (Build 8606)

| | |
|---|---|
| **Version** | 2.4.3 (build 8606) |
| **Type** | 32-bit (`Wow.exe`) |
| **Size** | ~8 GB |
| **Server core** | CMaNGOS-TBC / TrinityCore 2.4.3 |

---

## 1. What you should have

Either:
- A full client folder containing `Wow.exe`, `Data/`, `Interface/`, `WTF/`, or
- A repack folder (like our reference: `TBC-2.4.3.8606-Repack`) which bundles the client **and** server files together

## 2. Verify the build

1. Right-click `Wow.exe` → Properties → Details → **FileVersion: `2, 4, 3, 8606`**
2. `Data/` should contain MPQ files (`common.MPQ`, `expansion.MPQ`, `lichking.MPQ`...)

**⚠️ Do not confuse with 2.4.3 builds other than 8606** — the last TBC build is 8606. Earlier builds have different DBCs.

## 3. Configure for a private server

TBC reads the realmlist from **`realmlist.wtf`** in the client root (or `Data/enUS/`):

1. Open/create `realmlist.wtf` in the client root
2. Contents:
   ```
   set realmlist 127.0.0.1
   set patchlist 127.0.0.1
   ```
3. Save

If the file lives in `Data/enUS/realmlist.wtf`, edit that one instead — the root copy overrides it in some builds.

## 4. Account creation

TBC-era cores create accounts via the console (`mangosd`/`worldserver` console):
```
account create <username> <password>
account set gmlevel <username> 3
```
*(For AzerothCore/TrinityCore-style cores the syntax is `.account create` — see the specific server guide.)*

## 5. Common issues

| Problem | Cause | Fix |
|---------|-------|-----|
| "Connected" then disconnect | Build mismatch with server | Verify 8606 on both sides |
| Realmlist ignored | A second realmlist.wtf exists | Check both root and `Data/enUS/` |
| Crashes on character screen | Missing `Data` patches | Re-extract the full client |
| Realm shows offline | authserver not running / wrong DB | See [server guides](../04-servers/) |

## 6. Addons

Put **2.4.3-compatible** addons in `Interface/AddOns/`. Addons made for 3.3.5+ will not load.

---

**Next:** [WotLK client](03-wotlk.md) · [Legion client](04-legion.md)
