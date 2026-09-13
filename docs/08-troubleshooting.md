# Troubleshooting Encyclopedia

Every problem we personally hit while building these servers — symptom → cause → fix. Bookmark this page.

---

## Server won't start

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Cannot connect to world database` | DB down / wrong creds / wrong DB name | Start MariaDB; check `worldserver.conf` |
| `DatabasePool world NOT opened` | Same as above | Check the exact error line above it |
| `Invalid Map.db2 file format` | Stub/wrong-build DB2s | Use repack `dbc/` for your build |
| Crash on startup with SQL errors | Schema/row-format mismatch | Re-import updates in order; convert tables `ROW_FORMAT=DYNAMIC` |
| `Map file not found` | `DataDir` wrong | Point `DataDir` at your `Data/` folder |
| Server exits instantly | Missing DLLs / VC redist | Install VC++ Redistributable; run from the bin folder |

## Login / connection

| Symptom | Cause | Fix |
|---------|-------|-----|
| Client "Unable to connect" | Server not running / realmlist | Start DB → auth → world; check realmlist |
| Realm list empty | authserver not running / realm address | Start authserver; check `realmlist` table |
| Disconnects after login (Legion) | Using clean exe | Use the **patched** `Wow-64.exe` |
| "Connecting..." forever (Legion) | bnetserver down / port 1119 blocked | Start bnetserver; check firewall |
| Wrong account format (Legion) | Legion uses bnet emails | Create `name@realm` accounts |
| GM commands rejected | SEC level too low | `.account set gmlevel <user> 6 1` |

## Creatures / world behavior

| Symptom | Cause | Fix |
|---------|-------|-----|
| **Mobs fall through dungeon floors** | Broken vmaps or navmesh | [Known issues #1/#2](05-data-files/07-known-issues.md) |
| Creatures walk through walls | mmap version mismatch | Regenerate mmaps (generator v9) |
| Creatures stand still, won't path | mmaps missing for that map | Generate that map's mmaps |
| Creatures float/sink outdoors | maps version mismatch | Use v1.9 maps |
| No creatures spawn | world DB empty / wrong DB | Import world SQL |
| Mobs don't attack | SmartAI scripts missing | Check `smart_scripts` import |

## Performance

| Symptom | Cause | Fix |
|---------|-------|-----|
| Boot takes 10+ minutes | Thousands of random bots | Lower `MinRandomBots` |
| High world delay | Too many bots/maps loaded | Reduce bot count; check `Map delay` |
| Client stutters | Old client + heavy addons | Reduce addons; use upscaling tools |

## Bots (WotLK — mod-playerbots)

| Symptom | Cause | Fix |
|---------|-------|-----|
| `.bot` command unknown | Module not built | Rebuild with `modules/mod-playerbots` |
| Bots don't spawn | playerbots DB missing/config | Import `acore_playerbots`; enable in conf |
| Bots stuck | No mmaps | Install mmaps |

## Bots (Legion — LegionBotAI)

| Symptom | Cause | Fix |
|---------|-------|-----|
| Bots vanish seconds after spawn | Session registered with the map | See [LegionBotAI docs](../projects/legionbotai/) (never `AddSession`) |
| Bots stack on one spot | Formation slot collision | Our formation system fixes this (see docs) |
| Bots don't attack | Movement/chase interrupted | Our chase re-issue fix (see docs) |
| Bots fall through floor | Broken vmaps | [Known issues #1](05-data-files/07-known-issues.md) |
| LFG: bots don't fill roles | Role check not answered | LegionBotAI auto-answers (see docs) |
| LFG: "wrong roles" with 2 healers | Core rejects >1 healer | Extra healer queues as damage (see docs) |
| Bots die and never return | No resurrection logic | LegionBotAI auto-resurrects after combat |

## Database

| Symptom | Cause | Fix |
|---------|-------|-----|
| Import fails on big SQL | `max_allowed_packet` too small | Set 64M+ before import |
| Tables in wrong format | MariaDB defaults | `ALTER TABLE x ROW_FORMAT=DYNAMIC` |
| Characters lost | Wrong DB restored | Restore `characters` backup |
| "Access denied" | Wrong user/grants | Re-run the GRANT statements |

## Data extraction

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Invalid FileDataComplete.dbc` | Removed from modern CASC | Patch extractor / skip gameobjects |
| Extractor crashes | Streamed (incomplete) client | Get a full client |
| `wrong version` on maps | Tool/data mismatch | Version-match tools |
| mmaps take forever | Normal | Generate single maps as needed |

---

## The universal debugging method (what we actually did)

1. **Read the server log** — the core tells you almost everything at startup.
2. **Enable the right logger** — e.g. `Logger.VMAPS`, `Logger.MMAPS`, `Logger.LFG` (set level `2` for debug).
3. **Test one variable at a time** — change one thing, restart, observe.
4. **Verify data at the byte level** — map/mmap/vmo headers don't lie.
5. **When it crashes** — enable crash dumps + symbols (see below) and read the fault address.

### Crash analysis quick guide (Windows)
1. Build with debug info: linker `/DEBUG` (generates `.pdb`) and `/MAP` (address map).
2. On crash, find the dump in `Crashes/`.
3. Open the `.txt` dump — the fault address is an RVA.
4. Look up the RVA in `worldserver.map` → identifies the exact function.
5. Or open the `.dmp` in WinDbg with the matching PDB for a full symbolized stack.

We solved a dangling-pointer crash this exact way (a deleted player left in the map's grid → crash inside a transport's visibility update).
