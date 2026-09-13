# Tools — The Complete Toolbelt

Every tool you need to extract, build, manage, and mod a WoW private server.

---

## 1. Server core toolchain

| Tool | Purpose | Where |
|------|---------|-------|
| **CMake** 3.16+ | Configure the build | cmake.org |
| **Visual Studio** 2019/2022 | Compile (Windows) | microsoft.com |
| **Boost** | Core dependency | bundled / package manager |
| **OpenSSL 1.1.x** | Crypto | bundled |
| **MariaDB / MySQL** | Database server + dev libs | mariadb.org |

---

## 2. Data extraction tools

Built with your core (`-DTOOLS=1`) or from official releases:

| Tool | Input | Output | Time |
|------|-------|--------|------|
| `mapextractor` | Client | `dbc/` + `maps/` | 10–30 min |
| `vmap4extractor` | Client | `Buildings/` (raw models) | 30–90 min |
| `vmap4assembler` | `Buildings/` | `vmaps/` | 10–30 min |
| `mmaps_generator` | `maps/` + `vmaps/` | `mmaps/` | **2–12 h** |

**Tips:**
- Run them **inside the client folder** or pass `-i <CLIENT_ROOT>`.
- `mmaps_generator <mapId>` generates a single map — use it for testing instead of all maps.
- Version-match your tools to your build (see [known issues](../05-data-files/07-known-issues.md)).

---

## 3. Client data tools (MPQ / CASC)

| Tool | Purpose |
|------|---------|
| **MPQ Editor** (Ladik's) | Browse/extract old-client MPQ archives |
| **CASCExplorer** | Browse/extract modern CASC data |
| **WDBX Editor** | View/edit DBC/DB2 tables (all versions) |
| **WoW.Export / listfile tools** | Dump CASC file lists (for extraction) |

Use these to:
- Inspect what's inside a client before extracting
- Verify a client is complete (file listings)
- Pull single files (icons, models, DBCs)

---

## 4. Database tools

| Tool | Purpose |
|------|---------|
| **HeidiSQL** | Free, excellent MySQL/MariaDB GUI (recommended) |
| **DBeaver** | Cross-platform DB client |
| **mysql CLI** | Scripted imports/exports |

Handy CLI for big imports:
```bash
mysql --max_allowed_packet=64M -u <user> -p <database> < file.sql
```

---

## 5. GM / remote administration

| Tool | Purpose |
|------|---------|
| **SOAP** (built into cores) | Run GM commands remotely (scripts, automation) |
| **WoW Console** | The server console (account creation, shutdown) |
| **GM addons** | In-game admin panels (e.g. SPPLegionAdmin for Legion) |

See [09-gm-soap.md](09-gm-soap.md) for usage and templates.

---

## 6. Bot systems

| System | Expansion | Notes |
|--------|-----------|-------|
| **mod-playerbots** | WotLK 3.3.5a | Mature, thousands of random bots, `.bot` commands |
| **LegionBotAI** | Legion 7.3.5 | Ours — [docs](../projects/legionbotai/) |

---

## 7. Client-side tools

| Tool | Purpose |
|------|---------|
| **Addon managers** | Install/update addons |
| **WDBX/MPQ viewers** | Inspect client data |
| **Patched executables** | Point the client at your server (see client guides) |
| **Upscalers** (Magpie/Lossless Scaling) | Make old clients look sharper (DX9/DX11) |

---

## 8. Recommended toolkit summary

**Bare minimum to run a server:**
1. Core source + CMake + Visual Studio
2. MariaDB + HeidiSQL
3. The four extractors
4. A full client
5. A text editor (VS Code) for configs

**Nice to have:**
- WDBX Editor (DB2/DBC tweaking)
- CASCExplorer (client archaeology)
- SOAP scripts for automation (see [scripts/](../scripts/))
