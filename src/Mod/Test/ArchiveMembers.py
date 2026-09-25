# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD contributors                               *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# ***************************************************************************

"""Read and rewrite document archives whatever their members' methods.

A schema 5 document keeps its blob entries as the pack store holds them,
zstd members included (zip method 93, docs/FileBlobsManager.md sec 15.7).
Python's zipfile reads those natively from 3.14 on; before that, this
decodes them through the libzstd FreeCAD itself links.
"""

import ctypes
import ctypes.util
import glob
import os
import sys
import zipfile

ZSTD_METHOD = 93

_zstd = None


def _library():
    global _zstd
    if _zstd is not None:
        return _zstd
    names = [os.environ.get("FC_TEST_ZSTD", "")]
    names += glob.glob(os.path.join(sys.prefix, "lib", "libzstd.so*"))
    names += glob.glob(os.path.join(sys.prefix, "lib", "libzstd*.dylib"))
    names += glob.glob(os.path.join(sys.prefix, "Library", "bin", "*zstd*.dll"))
    names += ["libzstd.so.1", ctypes.util.find_library("zstd") or ""]
    for name in names:
        if not name:
            continue
        try:
            lib = ctypes.CDLL(name)
            lib.ZSTD_decompress.restype = ctypes.c_size_t
            lib.ZSTD_isError.restype = ctypes.c_uint
            _zstd = lib
            return lib
        except OSError:
            pass
    raise RuntimeError("no libzstd to read a zstd archive member with")


def _native():
    return getattr(zipfile, "ZIP_ZSTANDARD", None) == ZSTD_METHOD


def _raw(archive, info):
    """The member's stored bytes, read past its local header."""
    fp = archive.fp
    fp.seek(info.header_offset)
    header = fp.read(30)
    name_len = int.from_bytes(header[26:28], "little")
    extra_len = int.from_bytes(header[28:30], "little")
    fp.seek(info.header_offset + 30 + name_len + extra_len)
    return fp.read(info.compress_size)


def read(archive, name):
    """The decoded content of one member of an open zipfile.ZipFile."""
    info = archive.getinfo(name)
    if info.compress_type != ZSTD_METHOD or _native():
        return archive.read(name)
    data = _raw(archive, info)
    lib = _library()
    out = ctypes.create_string_buffer(max(info.file_size, 1))
    got = lib.ZSTD_decompress(
        out, ctypes.c_size_t(info.file_size), data, ctypes.c_size_t(len(data))
    )
    if lib.ZSTD_isError(ctypes.c_size_t(got)) or got != info.file_size:
        raise zipfile.BadZipFile("corrupt zstd member %s" % name)
    return out.raw[: info.file_size]


def readFile(path, name):
    with zipfile.ZipFile(path) as archive:
        return read(archive, name)


def members(path):
    """[(ZipInfo, decoded bytes)] of an archive, in archive order. A zstd
    member's info is set to deflate, so writing it back with writestr() works
    where zipfile cannot write zstd."""
    out = []
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            data = read(archive, info.filename)
            if info.compress_type == ZSTD_METHOD and not _native():
                info.compress_type = zipfile.ZIP_DEFLATED
            out.append((info, data))
    return out
