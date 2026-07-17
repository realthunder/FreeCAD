// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef IMPORT_RENDERMATERIAL_H
#define IMPORT_RENDERMATERIAL_H

#include <string>
#include <App/Color.h>

namespace Import
{

/// Neutral per-object render (PBR metallic-roughness) material carried
/// between the OCAF XCAFDoc_VisMaterial layer and the ViewProvider
/// Render_* dynamic properties (see the renderer plan, Phase 2b).
struct RenderMaterial
{
    bool valid = false;
    /// Metalness / roughness factors, 0..1; < 0 = unset.
    double metallic = -1.0;
    double roughness = -1.0;
    /// Base color factor (import: XCAFDoc_VisMaterialPBR::BaseColor;
    /// export: the view provider's ShapeColor/Transparency).
    bool hasBaseColor = false;
    App::Color baseColor;
    /// Absolute paths of texture image files: extracted embedded images
    /// on import, PropertyFileIncluded storage on export. Empty = none.
    std::string baseColorTexture;
    std::string normalMapTexture;
};

}  // namespace Import

#endif  // IMPORT_RENDERMATERIAL_H
