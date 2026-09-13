# Start Here — The Complete Journey

This document explains **everything** you need to go from "I want my own WoW server" to "I'm playing with bots in a dungeon" — the full picture, in order.

---

## 1. What you actually need (the four pillars)

Every WoW private server needs **four things**:

| Pillar | What it is | Where it goes |
|--------|-----------|---------------|
| **1. The Server Core** | The open-source emulator software (AzerothCore, LegionCore, TrinityCore...) | Built into `worldserver.exe` + `bnetserver.exe` |
| **2. The Database** | MySQL/MariaDB storing accounts, characters, world data | `auth`, `characters`, `world`, `hotfixes` databases |
| **3. The Data Files** | Client-extracted data: `dbc`, `maps`, `vmaps`, `mmaps`, `cameras`, `gt` | Server `Data/` folder |
| **4. The Game Client** | The WoW client matching your server's build | Your gaming PC |

**The golden rule:** the **client build** and the **server build** must match exactly.
A 7.3.5.26972 client needs a 7.3.5.26972 server and 26972 data files. Mixing builds = broken spells, crashes, and hours of confusion.

---

## 2. Pick your expansion

| Expansion | Build | Best core | Difficulty | Notes |
|-----------|-------|-----------|-----------|-------|
| Vanilla / Turtle | 1.17.1 (7100) | vMaNGOS / Turtle core | Medium | Smallest files, most stable emulation |
| TBC | 2.4.3 (8606) | CMaNGOS-TBC | Medium | Good emulation |
| WotLK | 3.3.5a (12340) | **AzerothCore** | ⭐ Easiest | Best-supported core + **mod-playerbots** (AI bots!) |
| Legion | 7.3.5 (26972) | **LegionCore** | Hard | Newest here; needs our custom **LegionBotAI** for bots |

**If it's your first server: start with WotLK (AzerothCore).** It has the most guides, the most stable code, and mod-playerbots gives you AI party members.

**If you want Legion content: LegionCore works** — but you'll need the data files from a matching repack and our guides below (the community documentation is nearly nonexistent, so we wrote it).

---

## 3. The build order (do it in this order!)

```
 1. Get the client          → verify the build number FIRST (don't trust folder names!)
 2. Get/build the core      → AzerothCore or LegionCore
 3. Set up the database     → MariaDB/MySQL + import SQL
 4. Get the data files      → repack, download, or extract yourself
 5. Configure               → .conf files, ports, SOAP
 6. Start: DB → auth → world
 7. Create your account     → console command
 8. Configure the client    → realmlist / bnet config + patched exe
 9. Log in and play
10. Optional: add bots      → mod-playerbots (WotLK) or LegionBotAI (Legion)
```

---

## 4. The traps that waste days (we solved them — read these)

| Trap | Symptom | Where we explain it |
|------|---------|---------------------|
| Mislabeled client archives | Wrong build, "invalid DB2" errors | [Client guides](03-clients/) |
| Streamed Battle.net CASC caches | "Complete" client missing files, stub DB2s | [Legion client guide](03-clients/04-legion.md) |
| Incomplete repack vmaps | Mobs fall through dungeon floors | [Known issues](05-data-files/07-known-issues.md) |
| mmap version mismatch | Creatures ignore navmesh, walk through walls | [Known issues](05-data-files/07-known-issues.md) |
| DB schema mismatch | worldserver crashes on startup | [Server guides](04-servers/) |
| Bot sessions | Bots vanish seconds after spawning | [LegionBotAI docs](../projects/legionbotai/) |
| LFG role check | Bots never queue / "wrong roles" | [LegionBotAI docs](../projects/legionbotai/) |

---

## 5. How this hub is organized

```
README.md                 ← you are in the front door
docs/                     ← the full guides
  00-start-here.md        ← this file
  01-legal-and-hosting.md ← what can/can't be shared
  02-version-matrix.md    ← version details + pairing rules
  03-clients/             ← per-version client guides
  04-servers/             ← per-core server guides
  05-data-files/          ← dbc/maps/vmaps/mmaps + extraction + known issues
  06-file-structures/     ← every folder/file explained
  07-tools.md             ← the toolbelt
  08-troubleshooting.md   ← the encyclopedia
  09-gm-soap.md           ← admin commands + remote control
projects/                 ← our original code (LegionBotAI, WotLK scripts)
sheets/                   ← the quick-reference sheets (versions, sources, placement, manifest, checksums)
scripts/                  ← sanitized start/stop/automation templates
```

---

## 6. Quick links to the sheets

- **[VERSIONS.md](../sheets/VERSIONS.md)** — every version + build number at a glance
- **[SOURCES.md](../sheets/SOURCES.md)** — where to download everything
- **[PLACEMENT.md](../sheets/PLACEMENT.md)** — where every file/folder goes
- **[MANIFEST.md](../sheets/MANIFEST.md)** — the master file list
- **[CHECKSUMS.txt](../sheets/CHECKSUMS.txt)** — verify your downloads

---

**Next step:** read [01-legal-and-hosting.md](01-legal-and-hosting.md) so you understand what's safe, then grab your client guide.
