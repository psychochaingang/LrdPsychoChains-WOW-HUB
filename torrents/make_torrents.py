import os, shutil
from torrentool.api import Torrent

# ============================================================
#  CONFIGURE YOUR PATHS (replace the <PLACEHOLDERS>)
# ============================================================
ARCHIVES      = r"<ARCHIVES_DIR>"        # folder with client archives (zips/rars)
LEGION_CLIENT = r"<LEGION_CLIENT_DIR>"   # full Legion 7.3.5.26972 client folder
SERVER_DATA   = r"<SERVER_DATA_DIR>"     # Legion server data folder (dbc/maps/vmaps/mmaps/...)
TBC_REPACK    = r"<TBC_REPACK_DIR>"      # TBC client/repack folder
SPPNXT_DIR    = r"<SPPNXT_DIR>"          # SPPNXT repack folder
HUB           = r"<HUB_REPO_DIR>"        # this repo folder
STAGE         = r"<STAGING_DIR>"         # scratch folder for multi-file sets (hardlinks)
# ============================================================

TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://tracker.dler.org:6969/announce",
    "udp://tracker2.dler.org:80/announce",
    "udp://explodie.org:6969/announce",
    "udp://tracker.bittor.pw:1337/announce",
    "udp://tracker.qu.ax:6969/announce",
]

OUT = os.path.join(HUB, "torrents")
HUB_NAME = "LrdPsychoChains WOW HUB"

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
make(os.path.join(ARCHIVES, "ChromieCraft_3.3.5a.zip"),
     "wotlk-3.3.5a-chromiecraft-client.torrent",
     f"WotLK 3.3.5a (12340) ChromieCraft client - {HUB_NAME}")

make(os.path.join(ARCHIVES, "Wow_Exes.zip"),
     "wow-exes-collection.torrent",
     f"WoW client executables collection - {HUB_NAME}")

make(os.path.join(ARCHIVES, "patch-3-fix-working-talent-and-textures.rar"),
     "patch-3-turtle-1.17.2.torrent",
     f"Turtle WoW 1.17.2 talent/texture patch - {HUB_NAME}")

# ---------- multi-file sets (hardlink staging) ----------
vanilla = hardlink_set("vanilla-turtle-1.17.1-client", [
    os.path.join(ARCHIVES, f"World.of.Warcraft.1.17.1_build_7100.part{i}.rar") for i in range(1, 6)
])
make(vanilla, "vanilla-turtle-1.17.1-client.torrent",
     f"Vanilla / Turtle WoW 1.17.1 (7100) client (5 parts) - {HUB_NAME}")

repack = hardlink_set("release-repack-win-x64", [
    os.path.join(ARCHIVES, f"Release_Repack_win_x64_New.part{i}.rar") for i in range(1, 4)
])
make(repack, "release-repack-azerothcore.torrent",
     f"Server repack (core + data + tools) - {HUB_NAME}")

# ---------- folders ----------
make(SPPNXT_DIR, "sppnxt-legion-7.3.5-repack.torrent",
     f"SPPNXT Legion 7.3.5 repack + data - {HUB_NAME}")

make(TBC_REPACK, "tbc-2.4.3-8606-client-repack.torrent",
     f"TBC 2.4.3 (8606) client + repack - {HUB_NAME}")

make(LEGION_CLIENT, "legion-7.3.5.26972-client.torrent",
     f"Legion 7.3.5 (26972) full client - {HUB_NAME}")

make(SERVER_DATA, "legion-server-data-26972.torrent",
     f"Legion 7.3.5 server data (dbc/maps/vmaps/mmaps/cameras/gt) - {HUB_NAME}")

# ---------- write summary ----------
with open(os.path.join(OUT, "MAGNETS.md"), "w", encoding="utf-8") as f:
    f.write("# Torrent Magnet Links\n\n")
    f.write(f"Generated for the {HUB_NAME}.\n\n")
    for name, magnet in results:
        f.write(f"## {name}\n```\n{magnet}\n```\n\n")

print(f"\nDONE - {len(results)} torrents created")
