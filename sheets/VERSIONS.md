# VERSIONS — Quick Reference Sheet

Every version in this hub at a glance. Full details in [docs/02-version-matrix.md](../docs/02-version-matrix.md).

---

## Game Clients

| Expansion | Version | Build | Bits | Our package | Server core |
|-----------|---------|-------|------|-------------|-------------|
| Vanilla / Turtle | 1.17.1 | 7100 | 32 | 5-part RAR | Turtle / vMaNGOS |
| Turtle (patched) | 1.17.2 | — | 32 | patch RAR | Turtle core |
| The Burning Crusade | 2.4.3 | **8606** | 32 | repack folder | CMaNGOS-TBC |
| Wrath of the Lich King | 3.3.5a | **12340** | 32 | ChromieCraft zip | **AzerothCore** |
| Legion | 7.3.5 | **26972** | 64 | RAR parts + exes zip | **LegionCore** |

## Server Cores

| Core | Version | Source | Notes |
|------|---------|--------|-------|
| AzerothCore | latest | github.com/azerothcore/azerothcore-wotlk | WotLK + mod-playerbots |
| LegionCore | 7.3.5 | github.com/dufernst/LegionCore-7.3.5 | Legion + LegionBotAI |
| TrinityCore | per-branch | github.com/TrinityCore/TrinityCore | Tools for all builds |

## Our Custom Packages

| Package | Version | Contents |
|---------|---------|----------|
| LegionBotAI | 1.0 | LegionCore playerbot system (code + docs) |
| WotLK Custom Scripts | 1.0 | AzerothCore custom scripts |
| Simple DPS Meter | 1.0 | Legion 7.3.5 addon |

## Data File Format Versions (7.3.5)

| Data | Format | Runtime expects |
|------|--------|-----------------|
| maps | **v1.9** | v1.9 |
| mmaps tiles | generator **v9** | v9 |
| mmaps Detour | **7** | 7 |
| vmaps | **VMAP_4.8** | VMAP_4.8 |

## Ports

| Port | Service |
|------|---------|
| 3306 | MySQL/MariaDB |
| 3724 | WotLK auth |
| 1119 | Legion bnetserver |
| 8085 | World |
| 7879 | SOAP (configurable) |

## Verify Anything (PowerShell)

```powershell
# Client build
(Get-Item "Wow.exe").VersionInfo.FileVersion

# Legion definitive version
Select-String "7\.3\.5" ".build.info"

# Download integrity
Get-FileHash -Algorithm SHA256 "file.rar"
```
