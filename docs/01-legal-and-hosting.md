# Legal & Hosting — What Can and Cannot Be Shared

**Read this before downloading or sharing anything.** This document explains the rules we operate under, why this repo contains no game files, and how to use community downloads responsibly.

---

## 1. Why this repository contains zero Blizzard game files

World of Warcraft's client software and all of its data are **owned by Blizzard Entertainment**. The EULA and Terms of Service forbid redistributing client files — including old, unsupported versions.

On top of the legal side, there are **hard technical limits**:

| Limit | GitHub reality |
|-------|----------------|
| Max file size | 100 MB per file (hard block) |
| Recommended repo size | under 1 GB |
| Practical repo ceiling | ~5 GB |
| Our collection | **~134 GB** (clients + data + repacks) |

So hosting the files here is both **against copyright** and **physically impossible**. This repo hosts only:

- Documentation and guides (our own writing)
- Our original code (GPLv3 — see section 6)
- Config templates and scripts we wrote
- **Links** to where files can be obtained

---

## 2. What IS safe to share (and what this hub does)

| Item | Safe? | Why |
|------|-------|-----|
| Guides, file lists, folder structures, version numbers | ✅ Yes | Facts and original writing |
| Our own code (LegionBotAI, scripts, configs) | ✅ Yes | Our original work (GPLv3) |
| Links to open-source cores (AzerothCore, TrinityCore, LegionCore) | ✅ Yes | GPLv3 projects |
| Links to community download pages | ✅ Yes | Linking is not hosting |
| Blizzard client files | ❌ No | Copyright |
| Extracted game data (dbc/maps/vmaps/mmaps) | ❌ No | Extracted from copyrighted client |
| Repacks bundling client data | ❌ No | Same problem |
| SQL database dumps from repacks | ❌ No | Contain copyrighted world data |

---

## 3. The honest grey areas

We're not lawyers and this isn't legal advice. Here's the community's actual reality:

- **Community client re-uploads** (archive.org, MEGA, Google Drive, torrents): This is where all old clients circulate. It is a **preservation/tolerance grey zone** — Blizzard generally goes after *large public servers* and *sellers*, not individuals downloading an old client for personal use. We link, you decide.
- **Repacks**: The *core* (server software) is legitimate open source. The *data* bundled inside was extracted from a Blizzard client — that part is grey. The cleanest path is extracting data from a client **you possess** (see our [extraction guide](05-data-files/06-extraction-guide.md)).
- **"Abandonware"**: The community argument that dead versions are fair game. It is **not a legal shield** — just the practical reality of a 20-year-old emulation scene.

---

## 4. Rules for using this hub safely

1. **Download from the linked sources at your own discretion.** We don't host, we don't guarantee availability, and we don't control those sites.
2. **Never re-upload client files to GitHub** (or any public host tied to this project).
3. **Keep your server private/personal.** Public servers attract cease-and-desist letters. The lowest-risk mode is you + friends.
4. **Never sell access, items, or gold.** Monetizing is what turns a hobby into a legal target.
5. **Verify everything** with our [checksums](../sheets/CHECKSUMS.txt) — corrupted or tampered downloads cause crashes that look like server bugs.

---

## 5. Takedown reality

If a repo or archive.org item receives a DMCA notice:

- **GitHub**: the repo is disabled, you get a strike; you can counter-notice (legally risky) or comply. A docs-only repo gives no reason for a notice.
- **archive.org**: individual items can be removed; the account usually survives. The community norm is: re-upload elsewhere, keep going.
- **Torrents**: no central host to take down — that's why the scene uses them.

This is why our structure is: **docs live forever in Git, files live in multiple replaceable places.**

---

## 6. Licensing of this repository

| Part | License | Notes |
|------|---------|-------|
| Documentation (`docs/`, `sheets/`, `README.md`) | **CC BY-SA 4.0** | Share and adapt with attribution |
| Code (`projects/`, `scripts/`) | **GPLv3** | Required: it modifies GPLv3 cores (TrinityCore/AzerothCore/LegionCore) |

Everything is provided **as-is, without warranty**. You are responsible for how you use it.

---

**Bottom line:** we document everything, host nothing copyrighted, verify with hashes, and let you choose your own sources. That's the safest way this hobby works.
