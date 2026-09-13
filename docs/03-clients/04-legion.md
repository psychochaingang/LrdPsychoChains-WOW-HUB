# Legion Client — 7.3.5 (Build 26972)

**The newest client in this hub. Hardest to set up — this guide covers every trap.**

| | |
|---|---|
| **Version** | 7.3.5 (build **26972**) |
| **Type** | 64-bit (`Wow-64.exe`) + 32-bit (`Wow.exe`) |
| **Size** | ~67 GB (extracted) |
| **Server core** | **LegionCore** + [LegionBotAI](../../projects/legionbotai/) |
| **Login** | Battle.net style (`email` + password, via bnetserver) |

---

## 1. ⚠️ TRAP #1: Mislabeled archives

Our archive parts were labeled **"WoW Legion 7.3.5.26792"** — they actually contained **26972**.

**26792 does not exist as a final 7.3.5 build.** Always verify:

1. Open `.build.info` in the client root (text file)
2. Find the `Version` column at the end of the data row:
   ```
   ...|2018-07-01T00:14:09Z|7.3.5.26972|http://blzddist1-a.akamaihd.net...
   ```
3. Or right-click `Wow-64.exe` → Properties → Details → `7.3.5.26972`

**Never trust folder names. Always check `.build.info`.**

## 2. ⚠️ TRAP #2: Streamed Battle.net CASC caches

Some downloads are **incomplete streaming caches**, not full clients. They look right (they launch, they have `.build.info`) but files download on demand from Blizzard.

**Symptoms:**
- The `Data/` folder is much smaller than ~60 GB
- An `indices/` folder with only a few entries
- DB2 files are tiny stubs → extractors fail: `Invalid Map.db2 file format`
- Extraction tools crash while reading CASC data
- Missing dungeon geometry (mobs fall through floors on the server!)

**How to tell:** a full Legion install has `Data/data/*.idx` files and ~60+ GB. A streamed cache has partial data and downloads as you play.

**Why it matters:** if your client is a streamed cache, you **cannot extract proper data files** from it. Get a full client.

## 3. Client structure

```
WoW_Legion_7.3.5.26972/
├── .build.info              ← version verification lives here
├── .agent.db                ← Battle.net agent state
├── Wow-64.exe               ← main client (use the PATCHED one)
├── Wow.exe                  ← 32-bit fallback
├── Data/
│   ├── data/                ← CASC archives (*.idx + data files)
│   ├── indices/
│   ├── config/
│   └── enUS/ ruRU/ ...      ← locale folders
├── Interface/
│   └── AddOns/              ← addons go here
├── WTF/                     ← settings, account cache
│   ├── Config.wtf           ← main client config
│   └── Account/             ← per-account data
├── Cache/                   ← shader/cache data (deletable)
├── Logs/
└── Utils/
```

## 4. ⚠️ TRAP #3: Patched vs clean executables

- **Clean `Wow-64.exe`** → connects to Blizzard retail (will fail/disconnect on a private server)
- **Patched `Wow-64.exe`** → points at `127.0.0.1` (your server)

We keep a patched exe set for builds **26124 / 26365 / 26972** (see `Wow_Exes.zip` in the manifest). Steps:

1. **Back up** your clean `Wow-64.exe` first (`Wow-64_Clean_Backup.exe`)
2. Copy the patched `Wow-64.exe` into the client root
3. Launch it — it will connect to your bnetserver

## 5. Configure the client for your server

Legion uses **Battle.net-style config** (`Uwow.wtf` / `Config.wtf`) instead of realmlist:

1. Open `WTF/Config.wtf` (create if missing) and ensure:
   ```
   SET portal "127.0.0.1"
   SET realmlist "127.0.0.1"
   ```
2. If your repack uses a custom config file (e.g. `Uwow.wtf`), set the same keys there
3. Login with your **bnet account email** (e.g. `admin@admin`) + password

## 6. First login checklist

| Step | Check |
|------|-------|
| bnetserver running | port **1119** listening |
| worldserver running | port **8085** listening |
| Account created | via worldserver console (see [LegionCore guide](../04-servers/02-legioncore-735.md)) |
| Patched exe used | clean exe → Blizzard, patched → you |
| `Config.wtf` portal | `127.0.0.1` |

## 7. Common issues

| Problem | Cause | Fix |
|---------|-------|-----|
| Disconnects at login | Using the clean exe | Use the patched `Wow-64.exe` |
| "Connecting..." forever | bnetserver down / port blocked | Start bnetserver; check port 1119 |
| Wrong password | Legion uses bnet email format | Account must be `name@realm` style |
| Crashes entering dungeons | Client data missing (streamed cache) | Get a full client |
| No addons | Legion addons differ | Use 7.3.5-compatible addons only |

## 8. Addons (Legion-specific)

Legion addon folder: `Interface/AddOns/`. Our custom addon (Simple DPS Meter) and examples live in [projects/](../../projects/).

---

**Next:** [LegionCore server guide](../04-servers/02-legioncore-735.md) — the core this client pairs with.
