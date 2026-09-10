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

#ifndef RENDERER_IMAGEDECODE_H
#define RENDERER_IMAGEDECODE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Renderer.h"

namespace Render {

/// Whether \a bytes start like a picture this decoder reads: a JPEG or
/// a PNG, by signature. What the producer keeps beside its decoded
/// pixels (TextureImage::encoded) has to be something every tier can
/// open, and this is the test the producer applies before keeping it.
RendererExport bool isEncodedImage(const uint8_t *bytes, size_t size);

/// Decode a JPEG or PNG into tightly packed 8-bit rows, bottom-up like
/// GL, with \a components channels (1, 3 or 4 -- the count the
/// producer's own decode reported, so the two tiers agree on what a
/// texel is). With a positive \a maxSide the picture is halved, box
/// filtered, until neither side exceeds it: the browser tier's memory
/// is what asks for that. Returns false on anything unreadable; the
/// outputs are then untouched.
RendererExport bool decodeImage(const uint8_t *bytes, size_t size,
                                int components, int maxSide, int &width,
                                int &height, std::vector<uint8_t> &pixels);

} // namespace Render

#endif // RENDERER_IMAGEDECODE_H
