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

## Torrents (magnet links)

Direct downloads via BitTorrent. `.torrent` files live in [torrents/](../torrents/), full magnet list in [torrents/MAGNETS.md](../torrents/MAGNETS.md).

> **To download:** paste a magnet into qBittorrent/Transmission/any client, or open the `.torrent` file.
> **To verify:** all packages are listed in [MANIFEST.md](MANIFEST.md) / [CHECKSUMS.txt](CHECKSUMS.txt).
> **Seeding:** the more people seed, the faster it goes — please seed after downloading.

| Package | Version | Size | Magnet |
|---------|---------|------|--------|
| Legion client | 7.3.5.26972 | 67.1 GB | `magnet:?xt=urn:btih:68642a8f58e98f74ff22af46f27d468d29e64022` |
| Legion server data | 26972 | 18.1 GB | `magnet:?xt=urn:btih:6acb140de202983257b8f4dda916563a2e39786f` |
| WotLK client (ChromieCraft) | 3.3.5a.12340 | 16.5 GB | `magnet:?xt=urn:btih:ce9546ab11d0b470c5ae4a60b5ff2adee2e1033b` |
| TBC client + repack | 2.4.3.8606 | 8 GB | `magnet:?xt=urn:btih:fb18f8b184cf0d1d4d4ccb85c8c9ff1794441c62` |
| Vanilla / Turtle client | 1.17.1.7100 | 7.7 GB | `magnet:?xt=urn:btih:2b6522c74bbdf6a826aebfdb8c83c15fb29683e4` |
| Turtle patch | 1.17.2 | 1.9 GB | `magnet:?xt=urn:btih:58c136753a83eea43716ff76d5610bf1ab708075` |
| SPPNXT Legion repack + data | 7.3.5 | 10.6 GB | `magnet:?xt=urn:btih:87a3e4eb6f78308881230e514b946a1da2ef7fb2` |
| Server repack (AzerothCore) | — | 4 GB | `magnet:?xt=urn:btih:5166f349eb9f26b63924d8f852e661701cb499f9` |
| WoW exes collection | many builds | 8.1 GB | `magnet:?xt=urn:btih:0af401133ca768f7847c50efccb81a0d649cf276` |

**Seeding note:** if the swarm is slow, that's normal for a fresh torrent — leave your client running and others will connect via DHT. The Legion client is the biggest (67 GB) — expect it to take a while.

## Archive.org Mirrors

> archive.org items (direct download + their auto-seeded torrents) will be listed here as they are uploaded.
> Each item page gives a direct download link for every file.

| Package | Version | Size | Link |
|---------|---------|------|------|
| *(uploads in progress)* | | | |

---

## Source Verification Checklist

Before trusting ANY download:
1. ✅ Client build matches (exe version / `.build.info`)
2. ✅ Data build matches the server
3. ✅ SHA256 matches [CHECKSUMS.txt](CHECKSUMS.txt) (for our packages)
4. ✅ Vmaps load (test with the logger) — see [known issues](../docs/05-data-files/07-known-issues.md)
5. ✅ Server boots without DB2/DB errors
