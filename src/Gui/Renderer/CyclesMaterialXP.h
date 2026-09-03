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

#ifndef RENDER_CYCLES_MATERIALX_P_H
#define RENDER_CYCLES_MATERIALX_P_H

#include <string>
#include <vector>

#include "MaterialXSupportP.h"

/// The MaterialX interpreter (docs/CyclesIntegration.md sec 8 item 15
/// phase B step 1): a MaterialX document walked into Cycles' own
/// shader nodes, rather than compiled. Cycles has a node vocabulary of
/// its own, so this is the route engines with one take -- Unreal's
/// Interchange and three.js' MaterialXLoader do the same thing into
/// theirs -- and it needs neither OSL nor a shading-language back end.
///
/// Only translation units built with both Cycles and MaterialX on
/// their include path may include this.

namespace ccl
{
class ShaderGraph;
class ShaderOutput;
}  // namespace ccl

namespace Render::Cycles
{

/// What interpreting a document produced.
struct MaterialXResult
{
    /// The surface closure to connect, or null when the document could
    /// not be interpreted.
    ccl::ShaderOutput *surface = nullptr;
    /// The volume closure, when the surface states an absorbing
    /// interior (OpenPBR's transmission depth); null otherwise.
    ccl::ShaderOutput *volume = nullptr;
    /// Why there is no surface. Empty on success.
    std::string error;
    /// What was interpreted approximately or not at all -- one line
    /// per distinct node category, so a material with fifty
    /// unsupported nodes says one thing about each kind.
    std::vector<std::string> warnings;
    /// Image texture nodes this document built, for the translation
    /// report's `images`. Counted here because the document's maps are
    /// built in this file and nowhere else: without it a scene whose
    /// every map comes from MaterialX reports `images: 0`, which reads
    /// as "the maps did not load" -- the chess set says 43 images and
    /// reported none of them.
    int images = 0;
};

/// Interpret one renderable surface of \a doc onto \a graph: the one
/// \a surface names, or its first when that is empty
/// (docs/MaterialStorage.md sec 17.13). Never throws. A document this
/// build cannot interpret comes back with an error and the caller
/// renders the draw's stock material -- the sandboxed-failure rule: a
/// material that will not translate must not take the frame with it.
MaterialXResult buildMaterialXSurface(ccl::ShaderGraph *graph,
                                      const mx::DocumentPtr &doc,
                                      const std::string &surface = {});

}  // namespace Render::Cycles

#endif  // RENDER_CYCLES_MATERIALX_P_H
