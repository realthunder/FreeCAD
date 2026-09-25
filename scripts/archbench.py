"""Benchmark archive-backed blob storage against one file per blob.

docs/FileBlobsManager.md sec 14.2 (as run on the monitored Windows laptop,
2026-09-14); the starting point of the pack store's phase 0, sec 15.4.

Run in the directory the transient store really lives in, because what is
being measured is the monitored filesystem, not the code.

usage: archbench.py <document.FCStd> <workdir> [legs]
"""

import os
import random
import shutil
import struct
import sys
import time
import zipfile
import zlib

src, work = sys.argv[1], sys.argv[2]
legs = sys.argv[3].split(",") if len(sys.argv) > 3 else ["copy", "stored", "pack", "files"]
os.makedirs(work, exist_ok=True)


def clock():
    return time.perf_counter()


z = zipfile.ZipFile(src)
infos = [i for i in z.infolist() if i.filename.startswith("blobs/")]
flags = sum(1 for i in infos if i.flag_bits & 0x8)
print(
    "entries %d, data-descriptor flag on %d, %.1f MB -> %.1f MB"
    % (
        len(infos),
        flags,
        sum(i.file_size for i in infos) / 1e6,
        sum(i.compress_size for i in infos) / 1e6,
    )
)
order = list(range(len(infos)))
random.seed(1)
random.shuffle(order)


def local_data_offset(fh, info):
    fh.seek(info.header_offset)
    hdr = fh.read(30)
    n, m = struct.unpack("<HH", hdr[26:30])
    return info.header_offset + 30 + n + m


def read_all(path, entries, inflate):
    """One persistent handle: seek + read (+ raw inflate) per entry, random order."""
    t = clock()
    total = 0
    with open(path, "rb", buffering=0) as fh:
        for k in order:
            off, csize, usize, method = entries[k]
            fh.seek(off)
            b = fh.read(csize)
            if method == 8:
                b = zlib.decompress(b, -15)
            total += len(b)
    return clock() - t, total


results = {}

# A. the archive as it is: one copy, entries inflated on read.
if "copy" in legs:
    dst = os.path.join(work, "copy.FCStd")
    t = clock()
    shutil.copyfile(src, dst)
    tc = clock() - t
    t = clock()
    zz = zipfile.ZipFile(dst)
    with open(dst, "rb") as fh:
        entries = [
            (local_data_offset(fh, i), i.compress_size, i.file_size, i.compress_type)
            for i in zz.infolist()
            if i.filename.startswith("blobs/")
        ]
    ti = clock() - t
    tr, n = read_all(dst, entries, True)
    tr2, _ = read_all(dst, entries, True)
    results["A copy+inflate"] = (tc, ti, tr, tr2)

# B. inflated but still a zip: ZIP_STORED, read by seek.
if "stored" in legs:
    dst = os.path.join(work, "stored.zip")
    t = clock()
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as out:
        for i in infos:
            out.writestr(i.filename, z.read(i))
    tc = clock() - t
    t = clock()
    zz = zipfile.ZipFile(dst)
    with open(dst, "rb") as fh:
        entries = [
            (local_data_offset(fh, i), i.compress_size, i.file_size, i.compress_type)
            for i in zz.infolist()
        ]
    ti = clock() - t
    tr, n = read_all(dst, entries, False)
    tr2, _ = read_all(dst, entries, False)
    results["B stored zip"] = (tc, ti, tr, tr2)

# C. a plain pack, no zip structure at all: bytes back to back, index in memory.
if "pack" in legs:
    dst = os.path.join(work, "blobs.pack")
    t = clock()
    entries = []
    off = 0
    with open(dst, "wb") as out:
        for i in infos:
            b = z.read(i)
            out.write(b)
            entries.append((off, len(b), len(b), 0))
            off += len(b)
    tc = clock() - t
    tr, n = read_all(dst, entries, False)
    tr2, _ = read_all(dst, entries, False)
    results["C raw pack"] = (tc, 0.0, tr, tr2)

# D. what the store does today: one file per blob, written then read back.
if "files" in legs:
    d = os.path.join(work, "files")
    os.makedirs(d, exist_ok=True)
    datas = [z.read(i) for i in infos]
    t = clock()
    names = []
    for k, b in enumerate(datas):
        p = os.path.join(d, "%05d.brp" % k)
        with open(p, "wb") as out:
            out.write(b)
        names.append(p)
    tc = clock() - t
    t = clock()
    for k in order:
        with open(names[k], "rb") as fh:
            fh.read()
    tr = clock() - t
    results["D file per blob"] = (tc, 0.0, tr, float("nan"))

print("%-18s %9s %9s %11s %11s" % ("leg", "create s", "index s", "read-all s", "read-again s"))
for k, (tc, ti, tr, tr2) in results.items():
    print("%-18s %9.2f %9.3f %11.2f %11.2f" % (k, tc, ti, tr, tr2))
