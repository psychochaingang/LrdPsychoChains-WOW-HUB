# LegionBotAI — Real Player Bots for Legion 7.3.5 (LegionCore)

**A from-scratch playerbot system for LegionCore 7.3.5.** Not a port — the old CMaNGOS/AzerothCore playerbots don't compile or work on 7.3.5 (different `Player` API, sessions, packets, LFG). This was written specifically against the LegionCore codebase.

> **What it does:** spawns **real characters** (actual `Player` objects with DB-backed characters) that join your party, follow you, fight, tank, heal, loot, buff, use potions, queue for dungeons via the LFG system, and even play *your* character for you.

---

## ✨ Features

| Feature | Details |
|---------|---------|
| **Real characters** | Bots are actual character rows in the DB — persist, level, gear up |
| **Full party** | Auto-join your group, party frames show them online |
| **Follow** | Formation-based (tank close, DPS flanking, healer back) with ground snapping |
| **Combat** | Auto-attack, chase, class rotations, threat, taunts |
| **Roles** | Tank / Healer / DPS with role-aware targeting |
| **Healing** | Triage (tank priority), emergency saves, shields, HoTs, mana conservation |
| **Healer DPS** | Healers attack when everyone is healthy |
| **Loot** | Bots loot corpses they kill |
| **Potions** | Health + mana potions, auto-used at thresholds |
| **Buffs** | Party-wide buffs maintained (Kings, Might, Fortitude, Battle Shout, Horn of Winter) |
| **Debuffs** | Demoralizing Shout, Frost Fever, Shadow Word: Pain |
| **LFG / Dungeon Finder** | Auto-answers role checks, auto-accepts dungeon proposals, handles the 1-healer limit |
| **Dungeon entry** | Follows you through instance portals (manual map transfer) |
| **Self-AI** | `.lbot self` — the bot AI plays *your* character (attacks, abilities, potions) |
| **Resurrection** | Dead bots revive automatically after combat |
| **Stability** | Self-healing registry, safe teardown, crash-free session handling |

---

## 🎮 Commands

All commands work in-game **and** from console/SOAP.

| Command | What it does |
|---------|--------------|
| `.lbot team` | Spawn the full 4-bot team (auto-picks Horde or Alliance bots by your faction) |
| `.lbot spawn <characterName>` | Spawn a specific character as a bot |
| `.lbot dismiss` | Dismiss all your bots |
| `.lbot info` | Diagnostic: mode, level, role, HP, combat, victim, gear, last cast |
| `.lbot level sync` | Bots level WITH you (default) - they match your level |
| `.lbot level max` | Set the whole team to max level |
| `.lbot level <1-110>` | Set the whole team to a fixed level |
| `.lbot autogear` | Re-equip the team with gear that fits their level |
| `.lbot assist full` | Bots fight everything you fight (default) |
| `.lbot assist defend` | Bots only fight mobs that attack you |
| `.lbot assist chill` | Bots never start fights (follow/heal/buff only - they still fight back if attacked) |
| `.lbot follow` | Bots follow you (default movement mode) |
| `.lbot stay` | Bots hold their position where they are (combat still works) |
| `.lbot attack` | Order the whole team onto YOUR current target (works in any assist mode) |
| `.lbot come` | Call the whole team to your position (unstuck them) |
| `.lbot aggro me` | YOU hold aggro - bots never taunt or boost threat (play as the tank) |
| `.lbot aggro bot` | The tank bot holds aggro (default) |
| `.lbot self` | Toggle self-AI (the bot AI plays your character) |
| `.lbot rescue` | Revive + teleport home (unstuck) |
| `.lbot creatures` | Spawn the NPC creature companion team (older system) |

**Console/SOAP form:** `.lbot <playerName> <command>` — e.g. `.lbot MyChar team`

### The default teams
**Horde**
| Bot | Class | Role |
|-----|-------|------|
| **Bulwark** | Blood Death Knight (Goblin) | Tank |
| **Lovley** | Holy Paladin (Blood Elf) | Healer (DPS when free) |
| **Ember** | Fury Warrior (Undead) | DPS |
| **Faith** | Holy Priest (Undead) | Healer (DPS when free) |

**Alliance**
| Bot | Class | Role |
|-----|-------|------|
| **Aegis** | Blood Death Knight (Human) | Tank |
| **Seraphine** | Holy Paladin (Draenei) | Healer (DPS when free) |
| **Rook** | Fury Warrior (Human) | DPS |
| **Elowen** | Holy Priest (Night Elf) | Healer (DPS when free) |

*(Character names/classes are configurable in `LegionBotMgr.cpp` — the team command spawns whatever characters you list for each faction.)*

---

## 🎚️ Leveling & Autogear (playerbot-style)

Bots behave like real characters that level alongside you:

- **`sync` mode (default):** a level-1 character gets **level-1 bots** wearing the **same starting
  outfit a fresh character gets** (from the game's CharStartOutfit data), then gear upgrades as they
  level - picked from the game's real item data (class-appropriate armor, weapons and quality curve:
  white → green → blue).
- **Ability gating:** bots only use abilities their level would have learned - no endgame spells
  while leveling (keeps them from trivializing low-level content).
- **`max` mode:** `.lbot level max` sets the team to max level with the endgame set.
- **fixed mode:** `.lbot level <n>` pins the team at a specific level.
- **`.lbot autogear`** re-equips the team on demand at their current level.
- **`.lbot aggro me|bot`** - who holds aggro: `me` lets you tank (bots never taunt or boost threat),
  `bot` is the default (the tank bot holds aggro).
- The chosen settings are **saved per character** (table `character_legionbot_settings` in the
  characters DB, created automatically) and survive restarts.
- Gear cache + level sync run cheaply in the background (2s tick, no DB spam).

**Recommended:** create the bot characters at **level 1** in the SQL below - the level mode
takes over from there. `character_homebind` is still mandatory.

---

## 📦 Installation

1. Copy the files into your LegionCore source:
   ```
   <LEGIONCORE_SRC>/src/server/scripts/Custom/
   ├── LegionBotAI.cpp
   ├── LegionBotMgr.cpp
   └── CustomStartups.cpp   (optional — custom startup hooks)
   ```

2. Register them in `src/server/scripts/ScriptLoader.cpp`:
   ```cpp
   // near the other AddSC_ declarations:
   void AddSC_LegionBotAI();
   void AddSC_LegionBotMgr();

   // inside AddScripts():
   AddSC_LegionBotAI();
   AddSC_LegionBotMgr();
   ```

3. Create the bot characters in the `characters` DB (see below).

4. Build:
   - Reconfigure CMake (GLOB picks up new files):
     ```powershell
     $env:BOOST_ROOT = "<SERVER_ROOT>\deps\boost_1_70_0"
     cmake .
     ```
   - Build the worldserver (stop it first — exe lock):
     ```powershell
     MSBuild src\server\worldserver\worldserver.vcxproj -p:Configuration=Release -m
     ```

5. Start the server, log in, and run `.lbot team`.

### Creating bot characters (SQL)
Bots are normal characters. Create them in `characters`:

```sql
INSERT INTO characters
  (guid, account, name, race, class, gender, level, map, position_x, position_y, position_z, orientation,
   taximask, online, specialization, currentpetnumber, petslot)
VALUES
  (900000, 3, 'Bulwark', 9, 6, 0, 1, 1, -10225.2, -2401.87, 28.11, -0.20,
   '', 0, 250, 0, 0);

INSERT INTO character_homebind (guid, mapId, zoneId, posX, posY, posZ)
VALUES (900000, 1, 1637, 1629.36, -4373.63, 31.2);
```

**Requirements:**
- A dedicated account (e.g. `BOT@BOT`) - bots log in on it
- Unique guid (900000+ recommended)
- **`character_homebind` row is mandatory** (login fails without it)
- `specialization` set to the spec ID (Blood=250, Holy Paladin=65, Fury=72, Holy Priest=257)

**Alliance team example** (Human DK / Draenei Paladin / Human Warrior / Night Elf Priest, Stormwind homebind):

```sql
INSERT INTO characters
  (guid, account, name, race, class, gender, level, map, position_x, position_y, position_z, orientation,
   taximask, online, specialization, currentpetnumber, petslot)
VALUES
  (900010, 3, 'Aegis',     1,  6, 0, 1, 0, -8949.95, -132.493, 83.5312, 0, '', 0, 250, 0, 0),
  (900011, 3, 'Seraphine', 11, 2, 1, 1, 0, -8949.95, -132.493, 83.5312, 0, '', 0,  65, 0, 0),
  (900012, 3, 'Rook',      1,  1, 0, 1, 0, -8949.95, -132.493, 83.5312, 0, '', 0,  72, 0, 0),
  (900013, 3, 'Elowen',    4,  5, 1, 1, 0, -8949.95, -132.493, 83.5312, 0, '', 0, 257, 0, 0);

INSERT INTO character_homebind (guid, mapId, zoneId, posX, posY, posZ) VALUES
  (900010, 0, 1519, -8949.95, -132.493, 83.5312),
  (900011, 0, 1519, -8949.95, -132.493, 83.5312),
  (900012, 0, 1519, -8949.95, -132.493, 83.5312),
  (900013, 0, 1519, -8949.95, -132.493, 83.5312);
```

---

## 🧠 How it works (architecture)

```
                    ┌─────────────────────────────┐
                    │  PlayerScript::OnUpdate      │  ← the live hook
                    │  (owner's tick)              │
                    └──────────────┬──────────────┘
                                   │ drives all bots
        ┌──────────────────────────┼──────────────────────────┐
        ▼                          ▼                          ▼
  ┌───────────┐            ┌─────────────┐            ┌──────────────┐
  │ Registries │            │ Bot update  │            │ LFG handler  │
  │ g_legionBots│           │ follow/     │            │ role checks  │
  │ roles/slots│            │ combat/heal │            │ proposals    │
  │ sessions   │            │ loot/buffs  │            │              │
  └───────────┘            └─────────────┘            └──────────────┘
```

### Key design decisions (and why)

#### 1. Socket-less sessions — never registered with the world
Each bot gets a `WorldSession` with a **null socket**. We **never** call `sWorld->AddSession` or `map->AddSession` — both tick the session like a real client, which fails without a socket and logs the player out within seconds. We hold sessions alive ourselves in `g_legionBotSessions`.

#### 2. The owner's tick drives everything
`PlayerScript::OnUpdate` fires for real players — we use the **owner's** update to drive all their bots (follow, combat, healing, looting, LFG). `WorldScript::OnUpdate` is a dead hook in this fork — don't use it.

#### 3. Synchronous character loading
`LoginQueryHolder` + `CharacterDatabase.DelayQueryHolder(holder)` + `future.wait()` — the normal login flow is async; bots need to load synchronously during the spawn command.

#### 4. Manual map transfer (the TeleportTo trap)
`Player::TeleportTo` defers work to the session tick — bots never tick, so they'd never actually teleport. Instead we manually:
```cpp
oldMap->RemovePlayerFromMap(bot, false);   // proper grid/visibility detach
bot->Relocate(...); bot->SetPhaseMask(...);
bot->SetMap(newMap);
botSession->SetMap(newMap);                 // ← critical! map update only ticks matching sessions
newMap->AddPlayerToMap(bot);
sObjectAccessor->AddObject(bot);
```
**The `session->SetMap` line matters**: the map update loop skips players whose session map doesn't match — without it, bots freeze after a transfer.

#### 5. Safe teardown (the dangling-pointer crash)
Deleting a bot without removing it from the map's grid leaves a dangling pointer — the next visibility update crashes (we found this the hard way, inside a transport's update). The correct teardown mirrors the core's logout:
```cpp
bot->CleanupsBeforeDelete();
map->RemovePlayerFromMap(bot, true);   // removes from grid + accessor, then deletes
```

#### 6. Formation & movement
- Roles get distinct formation slots (tank close, DPS flanking, healers back + spread)
- Positions snap to the **ground** (`UpdateGroundPositionZ`) — not the owner's Z
- Movement uses `MovePoint(..., generatePath=false)` for straight-line formation walks
- Attacking bots **always re-issue `MoveChase`** if their motion isn't a chase — otherwise a formation walk can strand them out of melee range

#### 7. Combat & threat
- **Triggered casts with self-managed cooldowns** — bots aren't limited to one ability per GCD; each ability fires on its own cooldown (min 1.5 s). This is the key to bot DPS.
- Tanks add **flat threat per tick** (`target->AddThreat(bot, 120)`) to hold aggro over DPS
- Tank passives (Veteran of the Third War, Blood Presence) are learned for proper tanking
- Facing is set before every cast (`SetFacingToObject`) — melee abilities fail without it

#### 8. LFG (Dungeon Finder) integration
- Bots **auto-answer role checks** with their role
- Bots **auto-accept dungeon-ready proposals** (no client to click)
- **1-healer limit:** dungeons reject groups with 2+ healers. The first healer queues as healer; extra healers queue as **damage** (while still healing in combat)

#### 9. Self-AI (`.lbot self`)
The same update loop can drive the **player's own character**: attack current target, cast class abilities, drink potions.

---

## ⚙️ Tuning

| What | Where |
|------|-------|
| Ability kits | `GetBotSpells()` in `LegionBotAI.cpp` |
| Gear sets | `GetBotGear()` |
| Formation distances | `GetFormationOffset()` |
| Heal thresholds | `FindLowestHpAlly()` + `CastHealAbility()` |
| Potion thresholds | potion block in `LegionBot_OnPlayerUpdate()` |
| Threat per tick | `target->AddThreat(bot, 120.0f)` |
| Team composition | `.lbot team` handler in `LegionBotMgr.cpp` |

---

## ⚠️ Known limitations

- **No artifact weapons** (Legion's main damage source) — bots do less damage than real players; tune creature rates to compensate (`Rate.Creature.Elite.Elite.*` in `worldserver.conf`)
- **No talent trees** — key active talents are learned as spells instead
- **No client visuals for bots' GCD** — triggered casts mean faster ability spam than a human
- **Instance teleports are manual** — no client loading screen, bots just appear

---

## 📜 License

GPLv3 — this code modifies LegionCore (GPLv3). See the repo [LICENSE](../../LICENSE).
