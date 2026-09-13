# SOURCES — Where to Download Everything

**Tiers = priority levels.** Start at Tier 1; fall back to Tier 2; use Tier 3 for guaranteed-match data.

> ⚠️ We host no game files. We link. Verify everything with [CHECKSUMS.txt](CHECKSUMS.txt) or the source's own hashes. Links die — search the names if one is gone.

---

## Tier 1 — Official & Preservation Sources (safest)

### Server cores (open source, always current)
| Core | Link |
|------|------|
| AzerothCore (WotLK) | `https://github.com/azerothcore/azerothcore-wotlk` |
| mod-playerbots | `https://github.com/mod-playerbots/mod-playerbots` |
| LegionCore 7.3.5 | `https://github.com/dufernst/LegionCore-7.3.5` |
| TrinityCore (all builds + tools) | `https://github.com/TrinityCore/TrinityCore` |
| CMaNGOS (Vanilla/TBC/WotLK) | `https://github.com/cmangos` |

### Tools
| Tool | Where |
|------|-------|
| TrinityCore tool releases | GitHub → Releases → tag matching your build (e.g. `7.3.5/26972`) |
| Extractors | Built with your core (`-DTOOLS=1`) |
| HeidiSQL | `https://www.heidisql.com` |
| WDBX Editor | GitHub community releases |
| CASCExplorer | GitHub community releases |

### Clients (project sites)
| Client | Where |
|--------|-------|
| Turtle WoW 1.17.x | Turtle WoW project site |
| WotLK 3.3.5a | ChromieCraft (public download) |
| Legion 7.3.5 | UWoW / EmuCoach repack (the data source LegionCore expects) |
| Archive.org preservation | `https://archive.org` — search "World of Warcraft [version]" |

---

## Tier 2 — Community Repacks & Mirrors

Where full client + data packs circulate. **Verify the build and the data quality** (see [known issues](../docs/05-data-files/07-known-issues.md)).

| Package | Typical source | Search terms |
|---------|---------------|--------------|
| Vanilla 1.17 client | Turtle community | `World of Warcraft 1.17.1 build 7100` |
| TBC 2.4.3 client | repack communities | `WoW 2.4.3 8606 client` |
| WotLK 3.3.5a client | private-server communities | `ChromieCraft 3.3.5a client` |
| Legion 7.3.5 client | repack communities | `WoW Legion 7.3.5 26972 client` |
| Legion data (dbc/maps/vmaps/mmaps) | repack communities | `Legion 26972 server data` |
| Legion repack (core+DB+data) | repack communities | `LegionCore 7.3.5 repack` |

> ⚠️ Repack data quality varies wildly — two repacks of the same version had completely different vmap quality for us. Always test with the [vmap logger](../docs/05-data-files/07-known-issues.md).

---

## Tier 3 — Build It Yourself (guaranteed match)

| What | How |
|------|-----|
| dbc | `mapextractor -e 2` (or repack data if client DB2s are stubs) |
| maps | `mapextractor -e 1` |
| vmaps | `vmap4extractor` → `vmap4assembler` |
| mmaps | `mmaps_generator` (version-patched if needed) |

Full walkthrough: [docs/05-data-files/06-extraction-guide.md](../docs/05-data-files/06-extraction-guide.md)

---

## Our Own Files (in this repo)

| File | Where |
|------|-------|
| LegionBotAI code + docs | [projects/legionbotai/](../projects/legionbotai/) |
| WotLK custom scripts | [projects/wotlk-custom/](../projects/wotlk-custom/) |
| Start/stop/SOAP templates | [scripts/](../scripts/) |
| All checksums | [CHECKSUMS.txt](CHECKSUMS.txt) |

---

## Direct Download Links (archive.org mirrors)

> Uploaded packages appear here as they become available. Each entry links directly to the archive.org item; users can download straight from the item page or via its auto-generated torrent.

| Package | Version | Size | Link |
|---------|---------|------|------|
| *(pending uploads)* | | | |

---

## Source Verification Checklist

Before trusting ANY download:
1. ✅ Client build matches (exe version / `.build.info`)
2. ✅ Data build matches the server
3. ✅ SHA256 matches [CHECKSUMS.txt](CHECKSUMS.txt) (for our packages)
4. ✅ Vmaps load (test with the logger) — see [known issues](../docs/05-data-files/07-known-issues.md)
5. ✅ Server boots without DB2/DB errors
