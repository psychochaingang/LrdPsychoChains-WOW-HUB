# MidnightCore Launcher - WoW 12.1 + TrinityCore 12.1.0

**One-click desktop launcher for a self-hosted WoW 12.1 (build 69933) client + TrinityCore 12.1.0 server.**

> **What this is:** an all-in-one launcher that starts your database and server stack, patches the
> (Arxan-protected) 12.1 client through the bundled official engine, and fades into the game when the
> login screen is ready.
>
> **What this is not:** a file dump. No game client, no game data, no server software is bundled.
> You supply your own licensed client and run your own private server - same policy as this hub.

**Download:** [MidnightCore Launcher 0.1.0-preview](https://github.com/psychochaingang/LrdPsychoChains-WOW-HUB/releases/tag/launcher-v0.1.0-preview)

---

## What it does

| Step | What happens |
|------|--------------|
| 1. PLAY | Launcher checks/starts MariaDB (3306), bnetserver (2119), worldserver (9085) - hidden |
| 2. Patch | Drives the bundled, unmodified Burralis engine to patch the client (Arxan blocks third-party patchers) |
| 3. Timer | Live "Loading World of Warcraft... Ns" - launcher fades away only when the login screen appears |
| 4. Play | Log in. Multiple instances are allowed (multi-box) - each PLAY tracks its own client |

Also included: Settings (client folder + portal), Verify client, Logs, Game folder, CLI (`cli\mcml.exe`).

**Menu extras:** background music (volume slider + on/off; starts on PLAY, stops and minimizes the launcher once the game loads), a **MidnightBotAI status card** (green/red dot + ON/OFF switch) with a per-launch "enable MidnightBotAI?" Yes/No prompt, a **Stop servers** button (worldserver -> bnetserver -> MariaDB), a clean-slate server stop when the launcher opens, and a cold start that holds 60 s then waits for the world port so the realm list is ready when the launcher fades.

## Why MidnightCore (instead of another launcher?)

- **Built for this stack** - WoW 12.1 (build 69933) + TrinityCore 12.1.0 + the LrdPsychoChains
  layout: it knows where your client, server stack and MariaDB live and starts them for you.
- **MidnightBotAI is coming - and it will only work with this launcher.** Our next-generation
  playerbot AI integration is exclusive to MidnightCore Launcher; other launchers will not run it.
- **All-in-one** - database + servers start hidden, the Arxan-protected client is patched through
  the bundled engine, a live timer shows the load, then the launcher fades into the login screen.
- **Multi-box friendly** - each PLAY tracks its own game instance.
- **Free and open** - launcher code is MIT, no telemetry, no account, self-hosted only.

## Requirements

| Need | Notes |
|------|-------|
| Windows x64 | + **.NET 10 Desktop Runtime** (for the GUI) |
| Your own WoW 12.1 client | build **69933**; no client files are provided |
| TrinityCore 12.1.0 server | bnetserver + worldserver (guides: [docs/04-servers](04-servers/)) |
| MariaDB | any recent version; usual data folder |

## Placement (defaults; env overrides)

| Item | Where |
|------|-------|
| Game client | `F:\TC12-client\` (set once in Settings) |
| Server stack | `D:\TC12\server\` (`MCML_SERVER_DIR`) |
| MariaDB | `D:\MariaDB\` (`MCML_MYSQL_DIR`) |
| Portal | `<client>\WTF\Config.wtf` -> `SET portal "127.0.0.1:2119"` |

## Verify

| Check | Expected |
|-------|----------|
| Engine SHA-256 | `B5359A972F760905F572AAFC4066D945F6382CEC2A521D555DD58D2861EF7C1C` |
| Zip SHA-256 | `DC45ED1D07E1BBBFBE7656E87C74964843C8D4F8D9732E14647BC02C455F1959` |
| Client build | 12.1.0.69933 (`mcml --dry-run`) |
| Ports | 3306 / 2119 / 9085 |

## Legal & credits

Read [Legal & Hosting](01-legal-and-hosting.md) before sharing anything.

- No Blizzard game files or game data are hosted or bundled - the pack tells the launcher where
  to find your own client and server.
- The patching engine is redistributed **unmodified** (signature intact) with its EULA and
  anti-cheating agreement attached, per its terms: private, non-commercial, development/education use.
- Keep servers private; never sell access/items/gold or the pack; do not connect to official Blizzard
  services; do not reverse engineer the bundled engine.
- Licensing: launcher code MIT; its docs CC BY-SA 4.0; engine under Burralis EULA; TrinityCore/MariaDB GPL-2.0.

## Known limits

- The bundled official engine performs the patching; our own native patcher ships as a diagnostic
  (`mcml --native`) and is not used by default.
- Built/tested on Windows with the stack above; other layouts work via Settings + env vars.
