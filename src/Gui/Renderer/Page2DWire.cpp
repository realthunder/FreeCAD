/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#include <cstring>

#include "Page2DWire.h"

using namespace Render;

namespace {

const uint32_t kPageMagic = 0x46435044; // 'FCPD'
const uint32_t kPageVersion = 1;

// The payload's own little writer/cursor. Deliberately not SceneDump's
// (that one is file-scope there); the shared part -- the item list --
// goes through the exported object-section codec, which is the part
// that must stay byte-identical to the scene root's.
void putRaw(std::vector<uint8_t>& out, const void* p, size_t n)
{
    const uint8_t* b = (const uint8_t*)p;
    out.insert(out.end(), b, b + n);
}
template<typename T>
void putNum(std::vector<uint8_t>& out, T v)
{
    putRaw(out, &v, sizeof(v));
}
void putStr(std::vector<uint8_t>& out, const std::string& s)
{
    putNum<uint32_t>(out, (uint32_t)s.size());
    putRaw(out, s.data(), s.size());
}

struct Cursor
{
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    void raw(void* dst, size_t n)
    {
        if (!ok || (size_t)(end - p) < n) {
            ok = false;
            std::memset(dst, 0, n);
            return;
        }
        std::memcpy(dst, p, n);
        p += n;
    }
    template<typename T>
    T num()
    {
        T v {};
        raw(&v, sizeof(v));
        return v;
    }
    void str(std::string& s, uint32_t maxLen)
    {
        uint32_t len = num<uint32_t>();
        if (!ok || len > maxLen || (size_t)(end - p) < len) {
            ok = false;
            return;
        }
        s.assign((const char*)p, len);
        p += len;
    }
};

} // namespace

uint32_t Render::pageDumpVersion()
{
    return kPageVersion;
}

uint32_t Render::pageSnapshotVersion(const void* data, size_t size)
{
    if (!data || size < 8)
        return 0;
    uint32_t magic, version;
    std::memcpy(&magic, data, 4);
    std::memcpy(&version, (const char*)data + 4, 4);
    return magic == kPageMagic ? version : 0;
}

bool Render::savePageSnapshot(std::vector<uint8_t>& out,
                              const PageSnapshot& snap,
                              SceneSnapshot::RootSpans& spans)
{
    out.clear();
    putNum<uint32_t>(out, kPageMagic);
    putNum<uint32_t>(out, kPageVersion);
    putNum<uint64_t>(out, snap.manifestVersion);
    spans.baseVersionAt = out.size();
    // Always the full form on save; the server's splice rewrites this
    // field when it derives a delta.
    putNum<uint64_t>(out, 0);
    putNum<uint64_t>(out, snap.sessionId);
    putNum<float>(out, snap.pageWidth);
    putNum<float>(out, snap.pageHeight);

    putNum<uint32_t>(out, (uint32_t)snap.fonts.size());
    for (const auto& f : snap.fonts) {
        putStr(out, f.name);
        putStr(out, f.key);
        putNum<uint32_t>(out, f.size);
    }
    putNum<uint32_t>(out, (uint32_t)snap.images.size());
    for (const auto& img : snap.images) {
        putNum<uint64_t>(out, img.id);
        putNum<uint16_t>(out, img.width);
        putNum<uint16_t>(out, img.height);
        putNum<uint8_t>(out, img.repeat ? 1 : 0);
        putStr(out, img.key);
        putNum<uint32_t>(out, img.size);
    }

    spans.listBegin = out.size();
    if (!writeObjectSection(out, {}, snap.entries))
        return false;
    spans.listEnd = out.size();
    // Nothing may follow the list: the splice copies the tail
    // verbatim, so any byte here would have to be base-version
    // independent -- easiest as no bytes at all.
    return true;
}

bool Render::loadPageSnapshot(const void* data, size_t size,
                              PageSnapshot& snap)
{
    if (pageSnapshotVersion(data, size) == 0)
        return false;
    Cursor c {(const uint8_t*)data, (const uint8_t*)data + size};
    uint32_t version;
    c.num<uint32_t>(); // magic, verified above
    version = c.num<uint32_t>();
    if (version > kPageVersion)
        return false;
    snap.manifestVersion = c.num<uint64_t>();
    snap.baseVersion = c.num<uint64_t>();
    snap.sessionId = c.num<uint64_t>();
    snap.pageWidth = c.num<float>();
    snap.pageHeight = c.num<float>();

    uint32_t nfonts = c.num<uint32_t>();
    if (!c.ok || nfonts > 0x1000u)
        return false;
    snap.fonts.clear();
    snap.fonts.reserve(nfonts);
    for (uint32_t i = 0; c.ok && i < nfonts; ++i) {
        PageSnapshot::Font f;
        c.str(f.name, 0x100u);
        c.str(f.key, 128);
        f.size = c.num<uint32_t>();
        snap.fonts.push_back(std::move(f));
    }

    uint32_t nimages = c.num<uint32_t>();
    if (!c.ok || nimages > 0x100000u)
        return false;
    snap.images.clear();
    snap.images.reserve(nimages);
    for (uint32_t i = 0; c.ok && i < nimages; ++i) {
        PageSnapshot::Image img;
        img.id = c.num<uint64_t>();
        img.width = c.num<uint16_t>();
        img.height = c.num<uint16_t>();
        img.repeat = c.num<uint8_t>() != 0;
        c.str(img.key, 128);
        img.size = c.num<uint32_t>();
        snap.images.push_back(std::move(img));
    }
    if (!c.ok)
        return false;

    snap.updates.clear();
    snap.removed.clear();
    return readObjectSection(c.p, c.end, snap.baseVersion != 0,
                             snap.removed, snap.updates);
}

void Render::encodePageItemChunk(Page2D::Kind kind, uint32_t layer,
                                 const std::vector<uint8_t>& ops,
                                 std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(5 + ops.size());
    putNum<uint8_t>(out, (uint8_t)kind);
    putNum<uint32_t>(out, layer);
    putRaw(out, ops.data(), ops.size());
}

bool Render::decodePageItemChunk(const void* data, size_t size,
                                 Page2D::Kind& kind, uint32_t& layer,
                                 std::vector<uint8_t>& ops)
{
    if (!data || size < 5)
        return false;
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t k = p[0];
    if (k > (uint8_t)Page2D::Kind::Annotation)
        return false;
    kind = (Page2D::Kind)k;
    std::memcpy(&layer, p + 1, 4);
    ops.assign(p + 5, p + size);
    return true;
}
