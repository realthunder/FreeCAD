"""Benchmark archive-backed blob storage against one file per blob.

docs/FileBlobsManager.md sec 14.2 (as run on the monitored Windows laptop,
2026-09-14); the starting point of the pack store's phase 0, sec 15.4.

Run in the directory the transient store really lives in, because what is
being measured is the monitored filesystem, not the code.

Legs: copy, stored, pack, files (sec 14.2); segments, save (sec 15.7-15.8,
the pack store: zstd members in zip segments written by generation, and a
save that copies members raw instead of deflating every blob again); split
(sec 15.10: the opened archive's blob members copied raw into capped
segments on open, against the single copy of leg A). zstd
comes from the libzstd FreeCAD links (ARCHBENCH_ZSTD, else found next to
the Python library or on the loader path).

usage: archbench.py <document.FCStd> <workdir> [legs]
"""

import ctypes
import ctypes.util
import glob
import hashlib
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
deletes = {}
extra = []

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
    t = clock()
    shutil.rmtree(d)
    deletes["D file per blob"] = clock() - t

# E. the pack store of sec 15.7-15.8: zip segments of zstd members.


def load_zstd():
    names = [os.environ.get("ARCHBENCH_ZSTD", "")]
    names += glob.glob(os.path.join(sys.prefix, "lib", "libzstd.so*"))
    names += glob.glob(os.path.join(sys.prefix, "Library", "bin", "zstd*.dll"))
    names.append(ctypes.util.find_library("zstd") or "")
    for n in names:
        if n:
            try:
                lib = ctypes.CDLL(n)
                lib.ZSTD_compressBound.restype = ctypes.c_size_t
                lib.ZSTD_compress.restype = ctypes.c_size_t
                lib.ZSTD_decompress.restype = ctypes.c_size_t
                lib.ZSTD_isError.restype = ctypes.c_uint
                return lib
            except OSError:
                pass
    return None


ZSTD = load_zstd() if ("segments" in legs or "save" in legs) else None
LEVEL = 3


def zcompress(b):
    cap = ZSTD.ZSTD_compressBound(ctypes.c_size_t(len(b)))
    out = ctypes.create_string_buffer(cap)
    n = ZSTD.ZSTD_compress(out, ctypes.c_size_t(cap), b, ctypes.c_size_t(len(b)), LEVEL)
    assert not ZSTD.ZSTD_isError(ctypes.c_size_t(n))
    return out.raw[:n]


def zdecompress(b, size):
    out = ctypes.create_string_buffer(max(size, 1))
    n = ZSTD.ZSTD_decompress(out, ctypes.c_size_t(size), b, ctypes.c_size_t(len(b)))
    assert not ZSTD.ZSTD_isError(ctypes.c_size_t(n)) and n == size
    return out.raw[:n]


ZSTD_METHOD = 93


def local_header(name, method, crc, csize, usize):
    n = name.encode()
    return (
        struct.pack("<IHHHHHIIIHH", 0x04034B50, 63, 0, method, 0, 0, crc, csize, usize, len(n), 0)
        + n
    )


def central_entry(name, method, crc, csize, usize, offset):
    n = name.encode()
    return (
        struct.pack(
            "<IHHHHHHIIIHHHHHII",
            0x02014B50,
            63,
            63,
            0,
            method,
            0,
            0,
            crc,
            csize,
            usize,
            len(n),
            0,
            0,
            0,
            0,
            0,
            offset,
        )
        + n
    )


def end_record(count, size, offset):
    return struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, count, count, size, offset, 0)


def write_segment(path, members):
    """members: list of (name, method, crc, csize, usize, data). Temporary file,
    flushed, renamed into place -- a generation (sec 15.7)."""
    tmp = path + ".tmp"
    cd = []
    with open(tmp, "wb") as out:
        for name, method, crc, csize, usize, data in members:
            off = out.tell()
            out.write(local_header(name, method, crc, csize, usize))
            out.write(data)
            cd.append(central_entry(name, method, crc, csize, usize, off))
        start = out.tell()
        blob = b"".join(cd)
        out.write(blob)
        out.write(end_record(len(cd), len(blob), start))
        out.flush()
        os.fsync(out.fileno())
    os.replace(tmp, path)
    return path


def read_directory(path):
    """hash -> (path, data offset, csize, usize, method), from the central
    directory alone."""
    found = {}
    with open(path, "rb") as fh:
        fh.seek(-22, 2)
        sig, _, _, _, count, size, offset, _ = struct.unpack("<IHHHHIIH", fh.read(22))
        fh.seek(offset)
        cd = fh.read(size)
        pos = 0
        for _ in range(count):
            f = struct.unpack("<IHHHHHHIIIHHHHHII", cd[pos : pos + 46])
            nlen, elen, clen = f[10], f[11], f[12]
            name = cd[pos + 46 : pos + 46 + nlen].decode()
            off = f[16]
            found[name] = (path, off + 30 + nlen, f[8], f[9], f[4])
            pos += 46 + nlen + elen + clen
    return found


if "segments" in legs and ZSTD:
    sd = os.path.join(work, "segments")
    os.makedirs(sd, exist_ok=True)
    cap = int(os.environ.get("ARCHBENCH_SEGMENT_MB", "64")) * 1000000
    datas = [z.read(i) for i in infos]
    t = clock()
    members, segs, cur, used = [], [], [], 0
    for b in datas:
        c = zcompress(b)
        m = (hashlib.sha1(b).hexdigest() + ".brp", ZSTD_METHOD, zlib.crc32(b), len(c), len(b), c)
        if used + len(c) > cap and cur:
            segs.append(write_segment(os.path.join(sd, "seg-%d.1" % len(segs)), cur))
            cur, used = [], 0
        cur.append(m)
        used += len(c)
        members.append(m)
    if cur:
        segs.append(write_segment(os.path.join(sd, "seg-%d.1" % len(segs)), cur))
    tc = clock() - t
    zbytes = sum(m[3] for m in members)
    t = clock()
    index = {}
    for p in segs:
        index.update(read_directory(p))
    ti = clock() - t
    keys = [m[0] for m in members]

    def read_segments():
        t = clock()
        total = 0
        handles = {}
        for k in order:
            p, off, csize, usize, method = index[keys[k]]
            fh = handles.get(p) or handles.setdefault(p, open(p, "rb", buffering=0))
            fh.seek(off)
            total += len(zdecompress(fh.read(csize), usize))
        for fh in handles.values():
            fh.close()
        return clock() - t

    tr = read_segments()
    tr2 = read_segments()
    results["E segments zstd"] = (tc, ti, tr, tr2)
    extra.append(
        "E: %d segments, zstd %.1f MB (deflate %.1f MB, raw %.1f MB)"
        % (
            len(segs),
            zbytes / 1e6,
            sum(i.compress_size for i in infos) / 1e6,
            sum(len(b) for b in datas) / 1e6,
        )
    )

    # A generation rewrite of the first segment: half its members dead, 5%
    # of its count appended new, members copied raw (sec 15.7).
    first = [m for m in members if index[m[0]][0] == segs[0]]
    live = first[::2]
    fresh = []
    for m in first[: max(1, len(first) // 20)]:
        b = zdecompress(m[5], m[4]) + b"\n"
        c = zcompress(b)
        fresh.append(
            (hashlib.sha1(b).hexdigest() + ".brp", ZSTD_METHOD, zlib.crc32(b), len(c), len(b), c)
        )
    t = clock()
    write_segment(os.path.join(sd, "seg-0.2"), live + fresh)
    os.remove(segs[0])
    tw = clock() - t
    extra.append(
        "E: rewrite seg-0 (%d members, %.1f MB -> %d live + %d new) %.3f s"
        % (len(first), sum(m[3] for m in first) / 1e6, len(live), len(fresh), tw)
    )
    segs[0] = os.path.join(sd, "seg-0.2")
    if len(segs) > 1:
        second = [m for m in members if index[m[0]][0] == segs[1]]
        t = clock()
        write_segment(os.path.join(sd, "seg-%d.1" % len(segs)), live[::2] + second[::4])
        tm = clock() - t
        extra.append(
            "E: merge a quarter of seg-0 and of seg-1 (%d members) %.3f s"
            % (len(live[::2]) + len(second[::4]), tm)
        )
    t = clock()
    shutil.rmtree(sd)
    deletes["E segments zstd"] = clock() - t

# G. the opened archive split on open (sec 15.10): each blob member copied
# raw -- its deflate bytes, never re-compressed -- into capped segments
# seg-<N>.1, named by content hash. The hash needs the inflated bytes, which
# the restore computes today anyway (sec 14.3), so the split is timed with
# and without it; the single copy is the baseline it replaces.
if "split" in legs:
    sd = os.path.join(work, "split")
    os.makedirs(sd, exist_ok=True)
    cap = int(os.environ.get("ARCHBENCH_SEGMENT_MB", "64")) * 1000000
    dst = os.path.join(work, "split-copy.FCStd")
    t = clock()
    shutil.copyfile(src, dst)
    tcopy = clock() - t
    os.remove(dst)

    def split(named):
        segs, cur, used = [], [], 0
        with open(src, "rb") as fh:
            for i in infos:
                off = local_data_offset(fh, i)
                fh.seek(off)
                raw = fh.read(i.compress_size)
                if named:
                    b = zlib.decompress(raw, -15) if i.compress_type == 8 else raw
                    name = hashlib.sha1(b).hexdigest() + ".brp"
                else:
                    name = i.filename[len("blobs/") :]
                if used + len(raw) > cap and cur:
                    segs.append(write_segment(os.path.join(sd, "seg-%d.1" % (len(segs) + 1)), cur))
                    cur, used = [], 0
                cur.append((name, i.compress_type, i.CRC, len(raw), i.file_size, raw))
                used += len(raw)
        if cur:
            segs.append(write_segment(os.path.join(sd, "seg-%d.1" % (len(segs) + 1)), cur))
        return segs

    t = clock()
    segs = split(False)
    traw = clock() - t
    shutil.rmtree(sd)
    os.makedirs(sd)
    t = clock()
    segs = split(True)
    tc = clock() - t
    t = clock()
    index = {}
    for p in segs:
        index.update(read_directory(p))
    ti = clock() - t
    entries = list(index.values())

    def read_split():
        t = clock()
        handles = {}
        for k in random.Random(1).sample(range(len(entries)), len(entries)):
            p, off, csize, usize, method = entries[k]
            fh = handles.get(p) or handles.setdefault(p, open(p, "rb", buffering=0))
            fh.seek(off)
            b = fh.read(csize)
            if method == 8:
                b = zlib.decompress(b, -15)
        for fh in handles.values():
            fh.close()
        return clock() - t

    tr = read_split()
    tr2 = read_split()
    results["G split on open"] = (tc, ti, tr, tr2)
    extra.append(
        "G: split into %d segments (cap %d MB, %.1f MB): raw copy %.2f s, "
        "with inflate+hash %.2f s; single archive copy %.2f s"
        % (len(segs), cap // 1000000, sum(os.path.getsize(p) for p in segs) / 1e6, traw, tc, tcopy)
    )
    t = clock()
    shutil.rmtree(sd)
    deletes["G split on open"] = clock() - t

# F. the blob half of a save: deflate every blob into the zip (today), against
# copying each zstd member raw (sec 15.7), both with a central directory.
if "save" in legs and ZSTD:
    datas = [z.read(i) for i in infos]
    zmembers = []
    for b in datas:
        c = zcompress(b)
        zmembers.append(
            (
                "blobs/" + hashlib.sha1(b).hexdigest() + ".brp",
                ZSTD_METHOD,
                zlib.crc32(b),
                len(c),
                len(b),
                c,
            )
        )
    dst = os.path.join(work, "save-deflate.zip")
    t = clock()
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=3) as out:
        for i, b in zip(infos, datas):
            out.writestr(i.filename, b)
    td = clock() - t
    dst2 = os.path.join(work, "save-raw.zip")
    t = clock()
    write_segment(dst2, zmembers)
    tz = clock() - t
    extra.append(
        "F save blobs: deflate level 3 %.2f s (%.1f MB); raw zstd copy %.2f s (%.1f MB)"
        % (td, os.path.getsize(dst) / 1e6, tz, os.path.getsize(dst2) / 1e6)
    )
    os.remove(dst)
    os.remove(dst2)

print("%-18s %9s %9s %11s %11s" % ("leg", "create s", "index s", "read-all s", "read-again s"))
for k, (tc, ti, tr, tr2) in results.items():
    print("%-18s %9.2f %9.3f %11.2f %11.2f" % (k, tc, ti, tr, tr2))
for k, v in deletes.items():
    print("%-18s delete the store %.3f s" % (k, v))
for line in extra:
    print(line)
if ("segments" in legs or "save" in legs) and not ZSTD:
    print("no libzstd found: set ARCHBENCH_ZSTD")
