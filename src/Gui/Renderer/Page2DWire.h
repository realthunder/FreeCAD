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

#ifndef RENDERER_PAGE2D_WIRE_H
#define RENDERER_PAGE2D_WIRE_H

/// The 2D page payload -- milestone M3 of the vg page engine
/// (docs/TechDrawPortAndSection.md sec 24). A served drawing page is
/// its own document group on SceneStreamServer, publishing an FCPD
/// payload whose item list rides the scene root's object-section
/// codec; that shared codec is the entire admission price of the
/// server's per-viewer catch-up path (spliceObjectDelta), which
/// rewrites the list span and copies everything else verbatim.
///
/// The publisher always serializes the FULL form (baseVersion 0, the
/// complete item list); deltas exist only as the server's splice, so
/// the loader reads both forms and tells them apart by the payload's
/// baseVersion field -- the same rule the scene format uses.
///
/// Item content, image pixels and font files travel as content-
/// addressed chunks (sha1 keys, the shared blob store); the root
/// carries the small font/image tables inline, so a spliced delta
/// always states the latest tables -- idempotent to re-apply, like
/// the 3D root's inline volatile state.

#include <cstdint>
#include <string>
#include <vector>

#include "Page2D.h"
#include "SceneDump.h"

namespace Render {

struct PageSnapshot
{
    uint64_t manifestVersion = 0;
    /// 0 = the full form. Nonzero only in a spliced delta.
    uint64_t baseVersion = 0;
    uint64_t sessionId = 0;

    /// The sheet, in Rez units -- what the viewer frames on join.
    float pageWidth = 0.0f;
    float pageHeight = 0.0f;

    struct Font
    {
        std::string name; ///< the registry name text ops refer to
        std::string key;  ///< content key of the font file chunk
        uint32_t size = 0;
    };
    struct Image
    {
        uint64_t id = 0; ///< the registry id image ops refer to
        uint16_t width = 0;
        uint16_t height = 0;
        bool repeat = false;
        std::string key; ///< content key of the raw RGBA8 chunk
        uint32_t size = 0;
    };
    std::vector<Font> fonts;
    std::vector<Image> images;

    /// Save side: the complete item entry list, ordered by objectKey
    /// (= item id) -- diffObjectLists and the splice both assume that
    /// order. entry.key is the item chunk's content key, entry.bbox
    /// the page-space hull with z 0.
    std::vector<SceneSnapshot::ObjectEntry> entries;

    /// Load side: what the payload carried -- every item for a full
    /// root, the changed ones (possibly with inline chunk bytes) plus
    /// the retired ids for a delta.
    std::vector<ObjectSectionEntry> updates;
    std::vector<uint64_t> removed;
};

/// The FCPD format version this build writes and the newest it reads.
RendererExport uint32_t pageDumpVersion();

/// Peek: the payload's format version when the bytes are an FCPD
/// payload (with or without content after the header), else 0. The
/// consumer's routing test -- run it before the scene parse.
RendererExport uint32_t pageSnapshotVersion(const void* data, size_t size);

/// Serialize the full form from the save-side fields. \a spans
/// receives the offsets the server's splice needs; pass it to
/// ScenePublish verbatim.
RendererExport bool savePageSnapshot(std::vector<uint8_t>& out,
                                     const PageSnapshot& snap,
                                     SceneSnapshot::RootSpans& spans);

/// Parse either form into the load-side fields. False on a foreign
/// magic, a newer version, or truncated bytes.
RendererExport bool loadPageSnapshot(const void* data, size_t size,
                                     PageSnapshot& snap);

/// The item chunk: u8 kind, u32 layer, then the op buffer verbatim.
/// Kind or layer changes re-key the chunk -- the entry is the delta
/// unit, and this is what keeps that true.
RendererExport void encodePageItemChunk(Page2D::Kind kind, uint32_t layer,
                                        const std::vector<uint8_t>& ops,
                                        std::vector<uint8_t>& out);
RendererExport bool decodePageItemChunk(const void* data, size_t size,
                                        Page2D::Kind& kind, uint32_t& layer,
                                        std::vector<uint8_t>& ops);

} // namespace Render

#endif // RENDERER_PAGE2D_WIRE_H
