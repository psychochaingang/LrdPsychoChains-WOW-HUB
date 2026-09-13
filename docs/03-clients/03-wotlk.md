# Wrath of the Lich King Client — 3.3.5a (Build 12340)

**The most popular private-server client. Best bot support (mod-playerbots).**

| | |
|---|---|
| **Version** | 3.3.5a (build 12340) |
| **Type** | 32-bit (`Wow.exe`) |
| **Size** | ~16.5 GB |
| **Server core** | **AzerothCore** + mod-playerbots |

---

## 1. What you should have

A full client (our reference: `ChromieCraft_3.3.5a`), typically delivered as one large zip:

```
ChromieCraft_3.3.5a.zip   (~16.5 GB)
```

Inside: `Wow.exe`, `Data/`, `Interface/`, `WTF/`, `Cache/`, etc.

## 2. Verify the build

1. Right-click `Wow.exe` → Properties → Details → **FileVersion: `3, 3, 5, 12340`**
2. `Data/` contains: `common.MPQ`, `common-2.MPQ`, `expansion.MPQ`, `lichking.MPQ`, `patch.MPQ`, `patch-2.MPQ`, `patch-3.MPQ`

## 3. Configure for a private server

WotLK reads **`realmlist.wtf`**:

1. Edit `Data/enUS/realmlist.wtf` (create it if missing)
2. Contents:
   ```
   set realmlist 127.0.0.1
   set patchlist 127.0.0.1
   ```
3. Save

> **Tip:** delete or edit the root `realmlist.wtf` too if present — one of them will win.

## 4. Recommended settings for bot-heavy servers

When running with **mod-playerbots**, the client only matters for *your* experience, but these help:

- **Enable the combat log** (needed for DPS meters): `/combatlog`
- **Addons:** put 3.3.5a addons in `Interface/AddOns/`
- **Camera/performance:** `/console maxfps 60` — bots run server-side, no client cost

## 5. Common issues

| Problem | Cause | Fix |
|---------|-------|-----|
| "Unable to connect" | realmlist wrong / server not up | Check realmlist + start authserver first |
| Realm list empty | authserver DB wrong | Check `acore_auth.realmlist` address |
| WotLK addons not showing | Wrong addon version | Only 3.3.5a-compatible addons |
| Login screen loops | Wrong build | Verify `12340` |
| Characters not appearing | Wrong realm ID | Check realm id in auth DB matches |

## 6. Addons worth having

- **DPS meter**: Recount / Skada (3.3.5a versions)
- **Playerbots UI**: many mod-playerbots servers include a bot control addon
- **Auction/Bag/Quest addons**: any 3.3.5a pack

---

**Next:** [AzerothCore server guide](../04-servers/01-azerothcore-wotlk.md) — the core this client pairs with.
