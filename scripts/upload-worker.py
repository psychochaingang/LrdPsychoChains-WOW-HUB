#!/usr/bin/env python3
"""
archive.org upload worker (skip-by-size, resumable)
===================================================

Why this exists instead of plain `ia upload`:
  - `ia upload` re-uploads files that already exist on the item. For large
    items that means re-sending tens of GB after every interruption.
  - `-c/--checksum` (the ia skip mode) re-reads every local file to compute
    hashes first - hours of disk I/O on multi-GB sets.
  - This worker fetches the item's remote file list once, skips any file
    whose size already matches, and uploads only what is missing. Restarts
    resume almost instantly.
  - It also avoids the per-item task-queue ration problem: run several
    workers in parallel (one per subfolder or package).

Usage:
    python upload-worker.py <identifier> <source> [title] [description]

    <source> can be a single file or a folder. Folder files are stored under
    their full local path (matching `ia upload` conventions) so a later run
    of either tool recognizes them.

    Set UPLOAD_THREADS env var to parallelize within one worker (default 6).
    Keep concurrency modest (2-3 workers x 4-6 threads) - archive.org
    rate-limits per account ("Please reduce your request rate").

Requirements:
    python -m pip install internetarchive
    ia configure   (one time, with your S3 keys)

LrdPsychoChains WOW HUB
"""

import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

from internetarchive import get_item

THREADS = int(os.environ.get("UPLOAD_THREADS", "6"))


def main():
    if len(sys.argv) < 3:
        print("usage: upload-worker.py <identifier> <source> [title] [description]")
        sys.exit(1)

    identifier = sys.argv[1]
    source = sys.argv[2]
    title = sys.argv[3] if len(sys.argv) > 3 else identifier
    desc = sys.argv[4] if len(sys.argv) > 4 else "LrdPsychoChains WOW HUB"

    metadata = {
        "title": title,
        "mediatype": "software",
        "creator": "psychochaingang",
        "description": desc,
    }

    item = get_item(identifier)

    print(f"[{identifier}] fetching remote file list...", flush=True)
    remote = {}
    try:
        for f in item.get_files():
            remote[f.name] = f.size
    except Exception as e:
        print(f"[{identifier}] warning: could not fetch remote list: {e}", flush=True)

    print(f"[{identifier}] remote files: {len(remote)}", flush=True)

    work = []
    if os.path.isfile(source):
        work.append((source, os.path.basename(source)))
    else:
        for root, dirs, files in os.walk(source):
            for name in files:
                local = os.path.join(root, name)
                key = local.replace("\\", "/")
                work.append((local, key))

    todo = []
    skipped = 0
    skipped_bytes = 0
    for local, key in work:
        try:
            lsize = os.path.getsize(local)
        except OSError:
            continue
        rsize = remote.get(key)
        if rsize is not None and int(rsize) == lsize:
            skipped += 1
            skipped_bytes += lsize
            continue
        todo.append((local, key, lsize))

    print(
        f"[{identifier}] total={len(work)} skip={skipped} ({skipped_bytes/1024**3:.2f} GB) "
        f"to_upload={len(todo)} threads={THREADS}",
        flush=True,
    )

    def upload_one(entry):
        local, key, lsize = entry
        try:
            item.upload_file(local, key=key, metadata=metadata, retries=10, verbose=False)
            return (key, lsize, None)
        except Exception as e:
            return (key, lsize, e)

    uploaded = 0
    uploaded_bytes = 0
    failed = 0
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=THREADS) as ex:
        futures = [ex.submit(upload_one, e) for e in todo]
        for fut in as_completed(futures):
            key, lsize, err = fut.result()
            if err is None:
                uploaded += 1
                uploaded_bytes += lsize
            else:
                failed += 1
                print(f"[{identifier}] FAILED {key}: {err}", flush=True)
            done = uploaded + failed
            if done % 100 == 0:
                rate = done / max(0.01, (time.time() - t0) / 60)
                print(
                    f"[{identifier}] progress {done}/{len(todo)} "
                    f"({rate:.0f} files/min, {uploaded_bytes/1024**3:.2f} GB uploaded, {failed} failed)",
                    flush=True,
                )

    mins = (time.time() - t0) / 60
    print(
        f"[{identifier}] DONE in {mins:.1f} min: uploaded={uploaded} "
        f"({uploaded_bytes/1024**3:.2f} GB), skipped={skipped} "
        f"({skipped_bytes/1024**3:.2f} GB), failed={failed}",
        flush=True,
    )
    if failed:
        sys.exit(2)


if __name__ == "__main__":
    main()
