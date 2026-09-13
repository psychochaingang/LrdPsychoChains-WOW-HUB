# WotLK Custom Scripts (AzerothCore)

Custom systems we built for our WotLK 3.3.5a AzerothCore server. All GPLv2 (AzerothCore derivative).

---

## Files

| File | What it does |
|------|--------------|
| `sanguinith_controller.cpp` | **The Sanguinith (Bloodbinder)** — a self-bot foundation: a Darkfallen-morphed character (Vaelith, guid 5000) with a Sanguine Aura, a **Vitae** (custom mana) meter, a Vampiric Thrall minion, and a bleed kit (Blood Strike, Rend, Blood Boil) |
| `black_legion_roach.cpp` | **Black Legion Loot Roach** — a passive pet that follows its owner and automatically loots nearby corpses into the owner's bags |
| `custom_script_loader.cpp` | Registers the custom scripts with the core |

---

## The Sanguinith (sanguinith_controller.cpp)

A custom "self-bot" character concept:

| Component | Details |
|-----------|---------|
| **Character** | guid 5000 — "Vaelith", Darkfallen model morph + scale |
| **Sanguine Aura** | Visual aura (91035) |
| **Vitae meter** | Custom resource (mana) that decays over time |
| **Vampiric Thrall** | A summoned minion (entry 91100) |
| **Bleed kit** | Blood Strike (91031), Rend (91032), Blood Boil (91033) — script-managed bleeds with per-target tracking |

**Design notes:**
- Bleeds are script-managed in a `std::map<targetGuid, {ownerGuid, remainingDuration}>` and ticked by a player script — no reliance on the aura system for the custom mechanic.
- The morph/scale/aura are applied on login and maintained by the player script.
- ⚠️ **Combat was disabled for this character** during our testing (it was a crash source on our build) — the foundation works, the combat kit is parked. See the repo history/notes.

---

## The Loot Roach (black_legion_roach.cpp)

A quality-of-life pet:
- Passive `CreatureAI` — follows its owner
- Scans nearby corpses and loots them **directly into the owner's bags**
- Uses the core's loot system (`LootMgr`) with distance + lootability checks

Great example code for: custom pet AI, grid scanning, auto-loot logic.

---

## Installation

1. Copy the files into your AzerothCore source:
   ```
   <AC_SRC>/src/server/scripts/Custom/
   ```
2. Register in `src/server/scripts/ScriptLoader.cpp`:
   ```cpp
   void AddSC_sanguinith_controller();
   void AddSC_black_legion_roach();
   // ...
   AddSC_sanguinith_controller();
   AddSC_black_legion_roach();
   ```
3. Rebuild the worldserver.
4. Create the required DB entries (custom spells 91031-91035, creature 91100, character guid 5000) — see the SQL references inside the file.

---

## Notes

- Custom spell/creature IDs (91xxx) are ours — change them if they collide with your DB.
- These were built and tested against a 3.3.5a AzerothCore build with `mod-playerbots`.

## License

GPLv2 — part of the AzerothCore Project derivative. See the repo [LICENSE](../../LICENSE).
