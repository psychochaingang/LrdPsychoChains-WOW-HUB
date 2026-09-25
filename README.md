# LrdPsychoChains WOW HUB — LegionCore + AzerothCore

**Everything you need to run your own WoW private server and game clients — files, folders, versions, tools, and the exact traps nobody else documents.**

> **What this hub is:** a complete, honest, tested guide collection for setting up WoW game clients (Vanilla → Legion) and private servers (AzerothCore 3.3.5a, LegionCore 7.3.5), plus our custom **LegionBotAI** playerbot system.
>
> **What this hub is not:** a file dump. We don't host Blizzard game files here (read [why](docs/01-legal-and-hosting.md)). We tell you **exactly what to download, from where, what version, how to verify it, and where every file goes** — with direct links where possible.

---

## 🚀 Start Here

| I want to... | Go to |
|--------------|-------|
| Understand the whole journey | [docs/00-start-here.md](docs/00-start-here.md) |
| Know what can/can't be shared (and why) | [docs/01-legal-and-hosting.md](docs/01-legal-and-hosting.md) |
| See every version + build number | [sheets/VERSIONS.md](sheets/VERSIONS.md) |
| Download something | [sheets/SOURCES.md](sheets/SOURCES.md) |
| Know where every file goes | [sheets/PLACEMENT.md](sheets/PLACEMENT.md) |
| See the master file list | [sheets/MANIFEST.md](sheets/MANIFEST.md) |
| Verify a download | [sheets/CHECKSUMS.txt](sheets/CHECKSUMS.txt) |
| Launch the game (12.1 + TrinityCore) | [MidnightCore Launcher](docs/10-launcher.md) |

## 🎮 Game Clients (we provide guides + links + hashes)

| Version | Build | Status | Guide |
|---------|-------|--------|-------|
| Vanilla / Turtle WoW | 1.17.1 (7100) | ✅ tested | [guide](docs/03-clients/01-vanilla-turtle.md) |
| The Burning Crusade | 2.4.3 (8606) | ✅ tested | [guide](docs/03-clients/02-tbc.md) |
| Wrath of the Lich King | 3.3.5a (12340) | ✅ tested | [guide](docs/03-clients/03-wotlk.md) |
| Legion | 7.3.5 (26972) | ✅ tested | [guide](docs/03-clients/04-legion.md) |

## 🖥️ Servers

| Core | Expansion | Guide |
|------|-----------|-------|
| **AzerothCore** + mod-playerbots | WotLK 3.3.5a | [guide](docs/04-servers/01-azerothcore-wotlk.md) |
| **LegionCore** + LegionBotAI | Legion 7.3.5 | [guide](docs/04-servers/02-legioncore-735.md) |

## 🤖 Our Projects

| Project | Description |
|---------|-------------|
| [**LegionBotAI**](projects/legionbotai/) | Real player-character bots for LegionCore 7.3.5 — tank/healer/DPS team, LFG dungeon finder integration, formations, loot, potions, party buffs, self-AI |
| [WotLK Custom Scripts](projects/wotlk-custom/) | AzerothCore custom systems (party controller, custom scripts) |

> **🤖 Want to test the Legion bots without compiling?** [**LegionBotAI Server Pack v1.2**](https://github.com/psychochaingang/LrdPsychoChains-WOW-HUB/releases/tag/v1.2) — prebuilt LegionCore 7.3.5 worldserver with the bot system compiled in, base databases, bot account + 8 ready-made bot characters and a quick-start guide. Mirror: [archive.org](https://archive.org/details/lrdpsychochains-legionbotai-server-pack).

## 🕹 Game Launcher (WoW 12.1 + TrinityCore)

| Guide | What it covers |
|-------|----------------|
| [MidnightCore Launcher](docs/10-launcher.md) | All-in-one desktop launcher for a self-hosted WoW 12.1 (build 69933) + TrinityCore 12.1.0 server - starts MariaDB/servers hidden, patches the Arxan-protected client, live loading timer, multi-box safe |

> **🕹 Download:** [**MidnightCore Launcher 0.1.0-preview**](https://github.com/psychochaingang/LrdPsychoChains-WOW-HUB/releases/tag/launcher-v0.1.0-preview) - no game files included; bring your own licensed client and private server.

## 📦 Data Files (dbc / maps / vmaps / mmaps)

| Guide | What it covers |
|-------|----------------|
| [Data file overview](docs/05-data-files/01-dbc.md) | What each folder is and does |
| [Extraction guide](docs/05-data-files/06-extraction-guide.md) | How to generate them yourself |
| [Known issues](docs/05-data-files/07-known-issues.md) | **Our hard-won fixes** — vmap load failures, mmap version mismatches, swapped tile naming |

## 🛠️ Troubleshooting

The [troubleshooting encyclopedia](docs/08-troubleshooting.md) covers everything we hit while building these servers — including the issues that took days to solve and that no other guide mentions.

---

## ⚠️ Legal Notice

This repository contains **only** original documentation, our own code (GPLv3), and links. No Blizzard game files are hosted here. Read [docs/01-legal-and-hosting.md](docs/01-legal-and-hosting.md) before using any downloads.

## License

- **Documentation:** CC BY-SA 4.0
- **Code:** GPLv3 (derivative of TrinityCore / AzerothCore / LegionCore, both GPLv3)
