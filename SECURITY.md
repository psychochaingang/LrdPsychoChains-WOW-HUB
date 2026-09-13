# Security Policy

## Scope

This repository contains **documentation, our own code, and torrent metadata**. It does not host or run any servers.

That means the security surface is:

| Item | Risk |
|------|------|
| Documentation | Misleading setup steps (see below) |
| `scripts/` templates | Copy-pasted with insecure placeholders |
| `projects/` code | Runs on *your* server if you install it |
| Torrent files | Verify hashes before extracting/executing |

## Reporting a vulnerability

**For issues in this repository** (our code, scripts, docs):

1. Open a **private security advisory** via the repo's *Security* tab → *Report a vulnerability*
2. Or open an issue **without** sensitive details and ask for a private channel

Please include:
- What's affected (file + section)
- How to reproduce
- Impact

We'll respond as fast as we can (this is a hobby project — no SLA).

## For issues in the cores themselves

We don't maintain AzerothCore, TrinityCore, or LegionCore. Report those upstream:
- AzerothCore: their GitHub issues
- TrinityCore: their GitHub issues
- LegionCore: their GitHub issues

## Security notes for users (read this!)

1. **Verify every download** with [CHECKSUMS.txt](sheets/CHECKSUMS.txt) before extracting or running anything.
2. **Never expose** these ports to the internet:
   - `3306` (MySQL/MariaDB)
   - `7878`/`7879` (SOAP — full GM control!)
   - `1119`/`3724` (auth) — expose only if you know what you're doing
3. **Change every default password** — DB users, GM accounts, SOAP accounts. Our templates use `<PLACEHOLDERS>` on purpose.
4. **Keep SOAP bound to 127.0.0.1.** SOAP executes GM commands — it is a full remote admin interface.
5. **Don't run servers as Administrator** and don't use your personal accounts for a public server.
6. **Torrents**: only seed/download what you're comfortable with; your IP is visible to the swarm.
7. **Old clients** may have known client-side vulnerabilities — run them in a sandbox/user account if concerned.

## Supported versions

Only the current `main` branch of this repo is maintained. Older versions of the docs may contain outdated/unsafe instructions — always use the latest.
