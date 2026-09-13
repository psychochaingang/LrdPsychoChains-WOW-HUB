import os, sys, shutil
from torrentool.api import Torrent

TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.tracker.cl:1337/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
]

OUT = r"D:\LrdPsychoChains-WOW-HUB\torrents"
STAGE = r"D:\LrdPsychoChains-WOW-HUB-staging"
HUB = "LrdPsychoChains WOW HUB"

results = []

def hardlink_set(setname, files):
    """Create a staging folder of hardlinks (no data copy) so multi-file sets torrent cleanly."""
    folder = os.path.join(STAGE, setname)
    os.makedirs(folder, exist_ok=True)
    for src in files:
        dst = os.path.join(folder, os.path.basename(src))
        if os.path.exists(dst):
            continue
        if not os.path.exists(src):
            print(f"  MISSING: {src}")
            continue
        try:
            os.link(src, dst)
        except OSError:
            shutil.copy2(src, dst)  # fallback (shouldn't happen on same volume)
    return folder

def make(src, outname, comment):
    out = os.path.join(OUT, outname)
    if not os.path.exists(src):
        print(f"MISSING: {src}")
        return
    print(f"hashing: {src}", flush=True)
    t = Torrent.create_from(src)
    t.announce_urls = TRACKERS
    t.comment = comment
    t.to_file(out)
    magnet = t.magnet_link if hasattr(t, "magnet_link") else t.get_magnet()
    with open(out + ".magnet.txt", "w", encoding="utf-8") as f:
        f.write(magnet + "\n")
    size_mb = os.path.getsize(out) / 1024 / 1024
    print(f"  OK {outname} ({size_mb:.1f} MB torrent)", flush=True)
    results.append((outname, magnet))

# ---------- single files ----------
make(r"D:\Downloads\ChromieCraft_3.3.5a.zip",
     "wotlk-3.3.5a-chromiecraft-client.torrent",
     f"WotLK 3.3.5a (12340) ChromieCraft client - {HUB}")

make(r"D:\LegionClient\Wow_Exes.zip",
     "wow-exes-collection.torrent",
     f"WoW client executables collection - {HUB}")

make(r"D:\TurtleWoW\_downloads\patch-3-fix-working-talent-and-textures.rar",
     "patch-3-turtle-1.17.2.torrent",
     f"Turtle WoW 1.17.2 talent/texture patch - {HUB}")

# ---------- multi-file sets (hardlink staging) ----------
vanilla = hardlink_set("vanilla-turtle-1.17.1-client", [
    rf"D:\TurtleWoW\_downloads\World.of.Warcraft.1.17.1_build_7100.part{i}.rar" for i in range(1, 6)
])
make(vanilla, "vanilla-turtle-1.17.1-client.torrent",
     f"Vanilla / Turtle WoW 1.17.1 (7100) client (5 parts) - {HUB}")

repack = hardlink_set("release-repack-win-x64", [
    rf"D:\TurtleWoW\_downloads\Release_Repack_win_x64_New.part{i}.rar" for i in range(1, 4)
])
make(repack, "release-repack-azerothcore.torrent",
     f"Server repack (core + data + tools) - {HUB}")

# ---------- folders ----------
make(r"D:\Downloads\SPPNXT", "sppnxt-legion-7.3.5-repack.torrent",
     f"SPPNXT Legion 7.3.5 repack + data - {HUB}")

make(r"D:\TBC-2.4.3.8606-Repack", "tbc-2.4.3-8606-client-repack.torrent",
     f"TBC 2.4.3 (8606) client + repack - {HUB}")

make(r"D:\LegionClient\WoW_Legion_7.3.5.26972", "legion-7.3.5.26972-client.torrent",
     f"Legion 7.3.5 (26972) full client - {HUB}")

make(r"D:\TurtleWoW\LegionData", "legion-server-data-26972.torrent",
     f"Legion 7.3.5 server data (dbc/maps/vmaps/mmaps/cameras/gt) - {HUB}")

# ---------- write summary ----------
with open(os.path.join(OUT, "MAGNETS.md"), "w", encoding="utf-8") as f:
    f.write("# Torrent Magnet Links\n\n")
    f.write(f"Generated for the {HUB}.\n\n")
    for name, magnet in results:
        f.write(f"## {name}\n```\n{magnet}\n```\n\n")

print(f"\nDONE - {len(results)} torrents created")
