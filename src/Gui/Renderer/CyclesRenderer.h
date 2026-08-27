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

#ifndef RENDER_CYCLES_RENDERER_H
#define RENDER_CYCLES_RENDERER_H

#include <string>
#include <vector>

#include "Renderer.h"

/// The Cycles path tracer (docs/CyclesIntegration.md), vendored at
/// src/3rdParty/cycles and built when BUILD_CYCLES is on.
///
/// Nothing here names a Cycles type: the header is the same with or
/// without the engine, and a build without it answers available()
/// false and fails the rest with a message. Nothing here names a Gui
/// type either -- the rule from the doc's section 7, so that the whole
/// unit can move to a server process later.
namespace Render::Cycles {

/// One compute device Cycles can render on.
struct DeviceInfo {
    std::string type;         ///< "CPU", "CUDA", "OPTIX", "HIP", ...
    std::string description;  ///< the device's own name
};

/// Whether this build carries the engine.
RendererExport bool available();

/// The devices the engine can see, CPU included. Empty without the
/// engine.
RendererExport std::vector<DeviceInfo> devices();

/// Phase 2 of the plan: render a hard-coded scene (a cube on a floor
/// under a uniform sky) to a PNG at \a path, with \a samples per pixel
/// on the device of the given type. Proves that the engine starts a
/// session, runs its threads, hands a frame back and tears down inside
/// this process -- nothing about the document is involved. Blocks
/// until the render is done. On failure returns false with \a error
/// set.
RendererExport bool renderTestScene(const std::string &path,
                                    int width,
                                    int height,
                                    int samples,
                                    const std::string &deviceType,
                                    std::string *error);

}  // namespace Render::Cycles

#endif  // RENDER_CYCLES_RENDERER_H
