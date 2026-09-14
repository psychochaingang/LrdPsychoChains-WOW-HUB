<#
    Upload packages to archive.org (Internet Archive)
    ==================================================
    archive.org gives each item:
      - a direct download link for every file
      - an auto-generated torrent seeded by THEIR servers (no need to seed from your PC)

    SETUP (one time):
      1. Create an account at https://archive.org
      2. Get your S3 keys: https://archive.org/account/s3.php
      3. Install the tool:  python -m pip install internetarchive
      4. Configure:         ia configure      (paste your access + secret keys)

    CONFIGURE YOUR PATHS:
      Edit the $packages table below - replace the <PLACEHOLDER> paths
      with the real paths on your system.

    USAGE:
      .\upload-to-archiveorg.ps1 -Package legion-client
      .\upload-to-archiveorg.ps1 -Package all

    FIELD NOTES (learned the hard way - keep these in mind):
      1. Do NOT pass "collection=..." metadata unless your account has upload
         rights for that collection. A plain upload lands in "opensource".
         A wrong collection makes every file fail with "Access Denied".
      2. Keep descriptions COMMA-FREE. The ia CLI splits comma values into
         multiple entries, which breaks the per-file metadata and rejects
         every upload with "Uploaded content is unacceptable".
      3. archive.org rejects multi-volume RAR sets ("error verifying rar or
         cbr file"). Wrap part1..partN.rar in ONE store-mode zip and upload
         the zip instead (users unzip, then unpack the RARs).
      4. Items with tens of thousands of small files hit the per-item task
         queue ration ("bucket_tasks_queued exceeds rationed amount"). Pack
         those folders into one zip per folder before uploading, or use
         upload-worker.py which skips by size and resumes cleanly.
      5. For big re-runs prefer scripts/upload-worker.py over `ia upload`:
         it skips files that already exist (by size, no hashing) and only
         uploads what is missing.
#>
param(
    [ValidateSet("legion-client","legion-data","wotlk-client","tbc-client","vanilla-client","turtle-patch","sppnxt","repack","exes","all")]
    [string]$Package = "all"
)

$IA = "ia"   # from the internetarchive python package

# package -> @{ path; identifier; title; description }
# Replace <PLACEHOLDER> paths with your real paths.
# NOTE: no commas in any description (see field note 2).
$packages = @{
    "legion-client"  = @{ path = "<LEGION_CLIENT_DIR>";    identifier = "lrdpsychochains-legion-7.3.5.26972-client"; title = "Legion 7.3.5.26972 Game Client"; desc = "Full World of Warcraft Legion 7.3.5 build 26972 client - verify SHA256 in the hub" }
    "legion-data"    = @{ path = "<SERVER_DATA_ARCHIVE>";  identifier = "lrdpsychochains-legion-26972-server-data-archives"; title = "Legion 7.3.5.26972 Server Data Archives"; desc = "Legion server data - maps mmaps vmaps as zip archives" }
    "wotlk-client"   = @{ path = "<WOTLK_CLIENT_ARCHIVE>"; identifier = "lrdpsychochains-wotlk-3.3.5a-12340-client"; title = "WotLK 3.3.5a 12340 Client"; desc = "World of Warcraft Wrath of the Lich King 3.3.5a build 12340 client" }
    "tbc-client"     = @{ path = "<TBC_REPACK_DIR>";       identifier = "lrdpsychochains-tbc-2.4.3.8606-client"; title = "TBC 2.4.3 8606 Client + Repack"; desc = "World of Warcraft The Burning Crusade 2.4.3 build 8606 client and repack" }
    "vanilla-client" = @{ path = "<VANILLA_STAGING_DIR>";  identifier = "lrdpsychochains-vanilla-turtle-1.17.1-client"; title = "Vanilla Turtle WoW 1.17.1 Client"; desc = "World of Warcraft 1.17.1 build 7100 client - 5 parts" }
    "turtle-patch"   = @{ path = "<TURTLE_PATCH_ARCHIVE>"; identifier = "lrdpsychochains-turtle-1.17.2-patch"; title = "Turtle WoW 1.17.2 Patch"; desc = "Talent and texture patch for Turtle WoW 1.17.2" }
    "sppnxt"         = @{ path = "<SPPNXT_DIR>";           identifier = "lrdpsychochains-sppnxt-legion-repack"; title = "SPPNXT Legion 7.3.5 Repack + Data"; desc = "SPPNXT V9.10.0 Legion 7.3.5 repack with data files" }
    "repack"         = @{ path = "<REPACK_STAGING_ZIP>";   identifier = "lrdpsychochains-server-repack"; title = "WoW Server Repack"; desc = "WoW server repack - core + data + tools" }
    "exes"           = @{ path = "<EXES_ARCHIVE>";         identifier = "lrdpsychochains-wow-exes-collection"; title = "WoW Client Executables Collection"; desc = "WoW client executables for many builds" }
}

function Upload-Package($name) {
    $p = $packages[$name]
    if (-not (Test-Path -LiteralPath $p.path)) {
        Write-Host "MISSING: $($p.path) - skipping $name" -ForegroundColor Yellow
        return
    }
    Write-Host "=== uploading: $name ($($p.path)) ===" -ForegroundColor Cyan
    & $IA upload $p.identifier $p.path `
        --metadata "title=$($p.title)" `
        --metadata "mediatype=software" `
        --metadata "creator=psychochaingang" `
        --metadata "description=$($p.desc) - LrdPsychoChains WOW HUB" `
        --retries 10
    Write-Host "done: https://archive.org/details/$($p.identifier)" -ForegroundColor Green
}

if ($Package -eq "all") {
    foreach ($name in $packages.Keys) { Upload-Package $name }
} else {
    Upload-Package $Package
}

Write-Host ""
Write-Host "After uploads finish, copy the item links into sheets/SOURCES.md (archive.org mirrors section)."
