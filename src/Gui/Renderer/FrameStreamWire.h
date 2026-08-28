/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef RENDER_FRAME_STREAM_WIRE_H
#define RENDER_FRAME_STREAM_WIRE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/// The rendered-frame stream's wire layout (docs/CyclesIntegration.md
/// sec 7.1): a binary WebSocket message that a serving process pushes
/// to one viewer, carrying one encoded frame of the path tracer that
/// renders that viewer's view. Header-only and engine-free, so the
/// server (which has Cycles) and the browser viewer (which never will)
/// spell the same bytes from the same place.
///
/// A scene payload on the same socket starts with a 64-bit publish
/// version; a frame starts with `FCCY`, which no version reaches --
/// isStreamedFrame() is the viewer's first test on a binary message.
namespace Render {

struct StreamedFrameHeader {
    enum Format : uint8_t { JPEG = 1 };
    enum Flags : uint8_t {
        Final = 1u << 0,    ///< the render's last frame (sample budget met)
        Encoded = 1u << 1,  ///< pixels are sRGB-encoded (else display-space as is)
    };
    uint8_t version = 1;
    uint8_t format = JPEG;
    uint8_t flags = 0;
    uint32_t width = 0;      ///< of the encoded image
    uint32_t height = 0;
    uint32_t sequence = 0;   ///< per stream, monotonic
    float progress = 0.0f;   ///< 0..1 of the sample budget
    std::string status;      ///< the engine's status text
};

inline const char *streamedFrameMagic()
{
    return "FCCY";
}

inline bool isStreamedFrame(const void *data, size_t size)
{
    return size >= 4 && std::memcmp(data, streamedFrameMagic(), 4) == 0;
}

/// Append the header for \a h to \a out; the image bytes follow it.
inline void writeStreamedFrameHeader(std::vector<uint8_t> &out,
                                     const StreamedFrameHeader &h)
{
    auto put32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t(v >> (8 * i)));
    };
    out.insert(out.end(), streamedFrameMagic(), streamedFrameMagic() + 4);
    out.push_back(h.version);
    out.push_back(h.format);
    out.push_back(h.flags);
    out.push_back(0);
    put32(h.width);
    put32(h.height);
    put32(h.sequence);
    uint32_t bits;
    std::memcpy(&bits, &h.progress, 4);
    put32(bits);
    const size_t len = h.status.size() > 0xffff ? 0xffff : h.status.size();
    out.push_back(uint8_t(len & 0xff));
    out.push_back(uint8_t(len >> 8));
    out.insert(out.end(), h.status.begin(), h.status.begin() + len);
}

/// Parse a frame message; \a imageOffset is where the image bytes
/// start. False when the message is not a well-formed frame.
inline bool readStreamedFrameHeader(const void *data, size_t size,
                                    StreamedFrameHeader &h, size_t &imageOffset)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    if (!isStreamedFrame(data, size) || size < 26)
        return false;
    auto get32 = [p](size_t at) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(p[at + size_t(i)]) << (8 * i);
        return v;
    };
    h.version = p[4];
    h.format = p[5];
    h.flags = p[6];
    h.width = get32(8);
    h.height = get32(12);
    h.sequence = get32(16);
    const uint32_t bits = get32(20);
    std::memcpy(&h.progress, &bits, 4);
    const size_t len = size_t(p[24]) | (size_t(p[25]) << 8);
    if (size < 26 + len)
        return false;
    h.status.assign(reinterpret_cast<const char *>(p + 26), len);
    imageOffset = 26 + len;
    return h.version == 1;
}

}  // namespace Render

#endif  // RENDER_FRAME_STREAM_WIRE_H
