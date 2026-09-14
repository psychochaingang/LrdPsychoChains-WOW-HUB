# MANIFEST — Master File List

Every package in the hub: what it is, its version, size, SHA256, and where it goes.
Spreadsheet version: [manifest.csv](manifest.csv) · Verify with [CHECKSUMS.txt](CHECKSUMS.txt)

---

## Game Clients

| # | Package | Version | Size | SHA256 (short) | Extract to |
|---|---------|---------|------|----------------|------------|
| 1 | `World.of.Warcraft.1.17.1_build_7100.part1-5.rar` | Vanilla/Turtle 1.17.1 (7100) | 7.71 GB | `dbfbd206…` | `<CLIENT_ROOT>\Turtle-1.17.2` |
| 2 | `patch-3-fix-working-talent-and-textures.rar` | Turtle 1.17.2 patch | 1.86 GB | `368a338f…` | over client folder |
| 3 | `ChromieCraft_3.3.5a.zip` | WotLK 3.3.5a (12340) | 16.46 GB | `fed61210…` | `<CLIENT_ROOT>\WotLK` |
| 4 | TBC client (in repack folder) | TBC 2.4.3 (8606) | ~8 GB | — | `<CLIENT_ROOT>\TBC` |
| 5 | Legion client (`WoW_Legion_7.3.5.26972`) | Legion 7.3.5 (26972) | 67.1 GB | *(torrent)* | `<CLIENT_ROOT>\Legion` |
| 6 | `Wow_Exes.zip` | WoW exes (many builds) | 8.07 GB | `56734bdc…` | copy matching exe to client root |

## Servers & Data

| # | Package | Version | Size | SHA256 (short) | Extract to |
|---|---------|---------|------|----------------|------------|
| 7 | `Release_Repack_win_x64_New.part1-3.rar` | AzerothCore repack (data + tools) | 3.99 GB | `f9826713…` | `<SERVER_ROOT>` |
| 8 | `(SPPNXT-V9.10.0) [7.3.5] Legion - With Data Files.7z` | Legion 7.3.5 repack | 5.42 GB | `9a407f49…` | `<SERVER_ROOT>\SPPNXT` |
| 9 | `SPPNXTLegion-Data.7z` | Legion server data | 5.15 GB | `dcf40bb5…` | `<SERVER_ROOT>\SPPNXT\Data` |
| 10 | LegionData (`dbc/maps/vmaps/mmaps/cameras/gt`) | 26972 data set | 18.1 GB | *(torrent)* | `<SERVER_ROOT>\Data` |

## Our Code (in this repo)

| # | Package | Version | Size | Location |
|---|---------|---------|------|----------|
| 11 | LegionBotAI (code + docs) | 1.0 | <1 MB | [projects/legionbotai/](../projects/legionbotai/) |
| 12 | WotLK Custom Scripts | 1.0 | <1 MB | [projects/wotlk-custom/](../projects/wotlk-custom/) |
| 13 | Start/stop/SOAP templates | 1.0 | <1 MB | [scripts/](../scripts/) |

---

## Full SHA256 hashes

```
fed612104085999e8875f077a5ed1e055aac7faaab413a33fed6a23bc2339732  ChromieCraft_3.3.5a.zip
9a407f490960f35dddac6e94ac04b465cf2e8958ba0096c35ead7b95f82afc31  (SPPNXT-V9.10.0) [7.3.5] Legion - With Data Files.7z
dcf40bb5495495852e2229f0f8174eb6f06be94eac278820b6e108693c5cfac9  SPPNXTLegion-Data.7z
56734bdce52aec561bfba530c484e37b88861f3154032db9e4fa66aeba3b94fd  Wow_Exes.zip
368a338f0a98b7d52665672421bd5a66e19a5dac5720cbabf5d8fd604f3def70  patch-3-fix-working-talent-and-textures.rar
f9826713076f3b0f120e65b2ac56a1a4c74d2103aededcb3142ceba5913d4aa2  Release_Repack_win_x64_New.part1.rar
678a4b43218d7e0ba624260d0519ad433f82da39802623712e322b9d77d3ac92  Release_Repack_win_x64_New.part2.rar
835feff0e34ac73d1e93a0d33f9b626ed8bb8fcc20873d7af695b0ad491e7f9b  Release_Repack_win_x64_New.part3.rar
dbfbd2063080659ed06dce92f29b8d35ecd2a7391406ee8e96972ef3c1a60fd1  World.of.Warcraft.1.17.1_build_7100.part1.rar
d0d17a57e4b319c8030d09c2bd525e398d373198fe3b6ac2259d5f1e394802fd  World.of.Warcraft.1.17.1_build_7100.part2.rar
44b6ada053d8e482e2a106d3c9b4dc17490eb1f904917fd6766fd5a316cf1684  World.of.Warcraft.1.17.1_build_7100.part3.rar
1aa5f0b32e05f027ef0cf660894d144fb232edd16fa34004388d64f9eafce36b  World.of.Warcraft.1.17.1_build_7100.part4.rar
2f70a7b218b7d3f258a46e849aac6d88a1327eda5f1bc1222fb55fd40e61a0a8  World.of.Warcraft.1.17.1_build_7100.part5.rar
```

---

## Notes

- **Multi-part archives**: extract from `part1` — the extractor finds the rest automatically.
- **SHA256 verification**:
  ```powershell
  Get-FileHash -Algorithm SHA256 "file.rar"
  ```
- **Torrent packages** (client folders + LegionData) get their hashes when the torrents are created.
- **Distribution**: [SOURCES.md](SOURCES.md) (direct links) · torrents + archive.org mirrors listed there as they go live.
