# MidnightBotAI - real player bots for TrinityCore 12.1.0 (WoW 12.1)

A server-side AI party for your own character: a tank, a healer and damage dealers that
follow, fight, level and gear up alongside the player.

## The team
- **Tank** (paladin or warrior): opens fights from range, taunts mobs off the player and
  other bots, holds threat.
- **Healer** (priest, paladin, druid or shaman): keeps the tank and party alive, and helps
  with damage when everyone is healthy.
- **Damage** (mage, warlock and other classes): focus the tank's target, keep their distance
  and recover line of sight when a target moves behind terrain.

## What it does
- Real characters: bots persist, level with the owner, learn their spells and spend talent
  points automatically.
- Combat: per-class ability rotations, cast wind-ups, capped and sensible damage numbers,
  visible/audible hits.
- Movement: personal spacing so bots never stack, line-of-sight recovery, casters retreat
  when attacked in melee.
- Party play: auto-binds to the owner, joins the group, respects group loot rules and never
  rolls against the owner (loot handling can be configured).
- Progression: automatic level-appropriate gear upgrades.
- Feedback: in-game damage/healing meter and per-bot status details.

## Commands (in-game, as the owner)
- `.mb pa` - bind and party all bots
- `.mb boostall` - bring the whole team to your level
- `.mb a` / `.mb f` - assist / follow on or off
- `.mb stats` - damage and healing meter
- `.mb loot` - view or set group loot rules
- `.mb st` - per-bot status

## Configuration
Config keys: MidnightBotAI.Enable, Spawning, MaxBots, AutoSpawn, AutoParty, Bootstrap,
LevelSync, AutoTalents, LootRoll, CombatChat, Stats, LootMethod, LootThreshold.
See `source/conf/MidnightBotAI.conf.dist` for descriptions and defaults.

## Layout
- `source/` - the module (drop into the server's scripts folder and register it in the
  scripts build, plus the conf file).
