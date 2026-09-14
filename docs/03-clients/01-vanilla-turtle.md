# Vanilla / Turtle WoW Client — 1.17.1 (Build 7100)

**The smallest, fastest client to set up. Perfect first client.**

| | |
|---|---|
| **Version** | 1.17.1 (build 7100), updated to 1.17.2 by Turtle's patcher |
| **Type** | 32-bit (`WoW.exe`) |
| **Size** | ~7.7 GB (5-part RAR archive) |
| **Server core** | Turtle WoW core / vMaNGOS |

> **Note on "1.17"**: Turtle WoW uses its own version numbering (not Blizzard's 1.12.1/5875). Their client is a heavily patched Vanilla client with modern content. It is **not** compatible with standard 1.12 servers, and vice versa.

---

## 1. What you should have

A 5-part RAR set (typical names):

```
World.of.Warcraft.1.17.1_build_7100.part1.rar
World.of.Warcraft.1.17.1_build_7100.part2.rar
World.of.Warcraft.1.17.1_build_7100.part3.rar
World.of.Warcraft.1.17.1_build_7100.part4.rar
World.of.Warcraft.1.17.1_build_7100.part5.rar
```

Plus optionally a patch archive (e.g. `patch-3-fix-working-talent-and-textures.rar`) for updated talents/textures.

## 2. Extract correctly

1. Put **all parts in the same folder**.
2. Right-click **part1** → Extract (7-Zip/WinRAR auto-detects the rest).
3. You should end up with a folder containing `WoW.exe`, `Data/`, `Interface/`, `WTF/`, etc.

**Verify:**
- `WoW.exe` reports FileVersion `1, 12, 1, 5875` — that is **normal** (Turtle keeps Blizzard's exe version). The Turtle build shows in `TWPatcher.exe` (1.1.6.0) and the extracted folder name `World of Warcraft 1.17.1_build_7100`.
- The `Data/` folder contains MPQ files (`common.MPQ`, `patch.MPQ`, `patch-3.MPQ`...)

## 3. Configure it for a private server

Vanilla/Turtle clients read the server address from **`realmlist.wtf`**:

1. Open `Data/enUS/realmlist.wtf` (or `Data/enGB/realmlist.wtf`) with Notepad
2. Replace the contents with:
   ```
   set realmlist 127.0.0.1
   set patchlist 127.0.0.1
   ```
   *(or your server's IP/domain)*
3. Save.

If the file doesn't exist, create it as `realmlist.wtf` in the `Data/<locale>/` folder.

## 4. Apply the Turtle patch (if using it)

If you have the talent/texture patch:
1. Extract it **over** your client folder (overwrite when asked)
2. The client version becomes 1.17.2
3. Keep a backup of the pre-patch client if you might play elsewhere

## 5. Common issues

| Problem | Cause | Fix |
|---------|-------|-----|
| "Unable to connect" | Wrong realmlist | Check `realmlist.wtf` points at your server |
| Client opens to a black screen | Missing `Data` files / bad extraction | Re-extract all 4 parts |
| Realm list empty | Server `authserver` not running | Start authserver first (see server guides) |
| Characters missing | Wrong database | Check the server's `characters` DB |

## 6. Addons (optional)

Vanilla addons go in `Interface/AddOns/`. Use **1.12-compatible** addons only — modern ones won't load. Turtle has its own addon pack if you play there.

---

**Next:** [TBC client](02-tbc.md) or [WotLK client](03-wotlk.md) or jump to a [server guide](../04-servers/).
