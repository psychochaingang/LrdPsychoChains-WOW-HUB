# Torrents

Torrent files + magnet links for the big packages. Full magnet list: [MAGNETS.md](MAGNETS.md).

## Packages

| Torrent file | Contents | Size |
|--------------|----------|------|
| `legion-7.3.5.26972-client.torrent` | Full Legion 7.3.5 client | 67.1 GB |
| `legion-server-data-26972.torrent` | Legion server data (dbc/maps/vmaps/mmaps/cameras/gt) | 18.1 GB |
| `wotlk-3.3.5a-chromiecraft-client.torrent` | WotLK 3.3.5a client | 16.5 GB |
| `tbc-2.4.3-8606-client-repack.torrent` | TBC 2.4.3 client + repack | 8 GB |
| `vanilla-turtle-1.17.1-client.torrent` | Vanilla/Turtle 1.17.1 client (5 parts) | 7.7 GB |
| `patch-3-turtle-1.17.2.torrent` | Turtle 1.17.2 talent/texture patch | 1.9 GB |
| `sppnxt-legion-7.3.5-repack.torrent` | SPPNXT Legion repack + data | 10.6 GB |
| `release-repack-azerothcore.torrent` | Server repack (core + data + tools) | 4 GB |
| `wow-exes-collection.torrent` | WoW executables (many builds) | 8.1 GB |

## How to use

1. **Open the `.torrent`** in qBittorrent / Transmission / any client, **or** copy a magnet link from [MAGNETS.md](MAGNETS.md).
2. **Wait for peers** — fresh torrents start slow; DHT kicks in within minutes.
3. **Verify** after download with [CHECKSUMS.txt](../sheets/CHECKSUMS.txt) / [MANIFEST.md](../sheets/MANIFEST.md).
4. **Seed** — please leave it seeding so others can download.

## Making your own torrents

`make_torrents.py` (in this folder) is the script that created these — edit the paths at the bottom and run:

```powershell
python make_torrents.py
```

Requirements: `pip install torrentool`

## Notes

- Trackers used are public (opentrackr, openbittorrent, torrent.eu.org, exodus). DHT works even if trackers die.
- A torrent is just metadata — **no copyrighted files are hosted in this repo**.
- If a torrent is dead, check [SOURCES.md](../sheets/SOURCES.md) for alternatives.
