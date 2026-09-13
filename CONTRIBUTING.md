# Contributing to LrdPsychoChains WOW HUB

Thanks for wanting to help! This hub exists to make private-server setup painless. Contributions that fix wrong info, add missing traps, or improve guides are very welcome.

---

## What we need most

| Priority | Contribution |
|----------|--------------|
| 🔥 High | **Corrections** — wrong version numbers, broken steps, outdated links |
| 🔥 High | **New pitfalls** — a trap you hit that isn't documented |
| ⭐ Medium | **New guides** — another core, expansion, or tool |
| ⭐ Medium | **Translations** — of the existing docs |
| 🟢 Low | Typos, formatting, clearer wording |

## What we cannot accept

- ❌ **Game files, clients, data packs, or repacks** — we host nothing copyrighted (see [legal](docs/01-legal-and-hosting.md))
- ❌ **Direct download links to pirated/copyrighted content** in the repo itself (put sources in an issue for review instead)
- ❌ **Server advertising** — this is a documentation hub, not a server list
- ❌ **Personal information** — no IPs, credentials, or personal paths

---

## How to contribute

### Reporting a problem or missing info
1. Open an **Issue**
2. Include: what you were doing, what you expected, what happened
3. Paste the exact error text and your versions (client build + core)

### Submitting a fix or guide
1. **Fork** the repo
2. Create a branch: `fix/<topic>` or `docs/<topic>`
3. Make your change — keep the existing style:
   - Markdown, tables for quick reference
   - Real error text in code blocks
   - Version numbers always included
4. Open a **Pull Request** describing what and why

### Style rules for docs
- **Be specific:** "build 12340" beats "the WotLK build"
- **Show the symptom first**, then the cause, then the fix
- **Include the exact commands/errors** — copy-paste friendly
- **No fluff** — this hub is a reference, not a blog
- Use `<PLACEHOLDERS>` for anything user-specific (paths, IPs, credentials)

---

## Code contributions

Code in `projects/` is GPLv3 (derivative of GPLv3 cores). By contributing you agree to license your changes the same way.

Keep the existing code style:
- Match the surrounding formatting
- Comment *why*, not *what*
- No personal info, no hardcoded credentials/paths

---

## Questions?

Open an issue with the `question` label. If it's about a specific core, include the core + version.
