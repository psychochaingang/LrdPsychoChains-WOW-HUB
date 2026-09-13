# PLACEMENT — Where Every File Goes

Extract/download something from this hub? Find it here, see exactly where it belongs.

---

## 1. Game Clients

| Package | Extract to | Then |
|---------|-----------|------|
| `World.of.Warcraft.1.17.1_build_7100.part1-5.rar` | `<CLIENT_ROOT>\Turtle-1.17.2\` | Extract part1 → all parts merge |
| `patch-3-fix-working-talent-and-textures.rar` | **over** the client folder | Overwrite when asked → 1.17.2 |
| `ChromieCraft_3.3.5a.zip` | `<CLIENT_ROOT>\WotLK\` | Extract zip |
| TBC client (repack folder) | `<CLIENT_ROOT>\TBC\` | Copy client folder out |
| `WoW Legion 7.3.5.26972` (RAR parts) | `<CLIENT_ROOT>\Legion\` | Extract all parts together |
| `Wow_Exes.zip` | anywhere | Copy the matching patched exe into the client root |

**After extraction, verify the build:**
- WotLK: `Wow.exe` → Properties → Details → `3,3,5,12340`
- Legion: `.build.info` → `7.3.5.26972`

---

## 2. Server Cores & Repacks

| Package | Extract to | Notes |
|---------|-----------|-------|
| `Release_Repack_win_x64_New.part1-3.rar` | `<SERVER_ROOT>\` | Repack: core + data + tools |
| `(SPPNXT-V9.10.0) [7.3.5] Legion - With Data Files.7z` | `<SERVER_ROOT>\SPPNXT\` | Full repack with data |
| `SPPNXTLegion-Data.7z` | `<SERVER_ROOT>\SPPNXT\Data\` | Server data files |
| AzerothCore / LegionCore source | `git clone` → build | See server guides |

---

## 3. Server Data Files

| Data | Goes to | Used by |
|------|---------|---------|
| `dbc/` | `<SERVER_ROOT>\Data\dbc\` | Both cores |
| `maps/` | `<SERVER_ROOT>\Data\maps\` | Both cores |
| `vmaps/` | `<SERVER_ROOT>\Data\vmaps\` | Both cores |
| `mmaps/` | `<SERVER_ROOT>\Data\mmaps\` | Both cores |
| `cameras/` | `<SERVER_ROOT>\Data\cameras\` | Both cores |
| `gt/` | `<SERVER_ROOT>\Data\gt\` | Both cores |

Set in `worldserver.conf`:
```ini
DataDir = "<SERVER_ROOT>/Data"
```

---

## 4. Our Custom Code

### LegionBotAI (LegionCore)
| File | Goes to |
|------|---------|
| `LegionBotAI.cpp` | `<LEGIONCORE_SRC>\src\server\scripts\Custom\` |
| `LegionBotMgr.cpp` | `<LEGIONCORE_SRC>\src\server\scripts\Custom\` |
| `CustomStartups.cpp` | `<LEGIONCORE_SRC>\src\server\scripts\Custom\` |

Then register in `ScriptLoader.cpp` (see [project README](../projects/legionbotai/README.md)):
```cpp
void AddSC_LegionBotAI();
void AddSC_LegionBotMgr();
...
AddSC_LegionBotAI();
AddSC_LegionBotMgr();
```
Rebuild the worldserver afterwards.

### WotLK Custom Scripts (AzerothCore)
| File | Goes to |
|------|---------|
| `sanguinith_controller.cpp` | `<AC_SRC>\src\server\scripts\Custom\` |
| `black_legion_roach.cpp` | `<AC_SRC>\src\server\scripts\Custom\` |
| `custom_script_loader.cpp` | `<AC_SRC>\src\server\scripts\Custom\` |

Add the loader to `ScriptLoader.cpp`, then rebuild.

---

## 5. Scripts & Addons

| File | Goes to |
|------|---------|
| `scripts/start_all.bat` | `<SERVER_ROOT>\bin\Release\` |
| `scripts/stop_all.bat` | `<SERVER_ROOT>\bin\Release\` |
| `scripts/soap.ps1` | `<SERVER_ROOT>\bin\Release\` |
| Simple DPS Meter addon | `<CLIENT_ROOT>\Interface\AddOns\SimpleDPS\` |

Edit the paths inside the scripts to match your install (they use `<SERVER_ROOT>` placeholders).

---

## 6. Databases

| SQL | Import into |
|-----|-------------|
| auth base + updates | `auth` (Legion) / `acore_auth` (WotLK) |
| characters base + updates | `characters` / `acore_characters` |
| world base + updates | `world` / `acore_world` |
| hotfixes base + updates (Legion) | `hotfixes` |
| playerbots SQL (WotLK) | `acore_playerbots` |

Import order and commands: see each [server guide](../docs/04-servers/).

---

## 7. Quick sanity checklist after placement

- [ ] Client exe version matches server build
- [ ] `DataDir` points at the folder containing `dbc/maps/vmaps/mmaps`
- [ ] Databases imported in order, no errors
- [ ] `realmlist`/`portal` points at your server IP
- [ ] Server start order: **DB → auth → world**
- [ ] Vmaps load (test with logger) — [known issues](../docs/05-data-files/07-known-issues.md)
