// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Zheng Lei <realthunder.dev@gmail.com>              *
 *   Copyright (c) 2023 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#include "PreCompiled.h"

#include "ImportOCAFGui.h"
#include <algorithm>

#include <App/PropertyFile.h>
#include <App/PropertyStandard.h>
#include <Gui/Application.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Gui/ViewProviderLink.h>
#include <Mod/Part/Gui/ViewProvider.h>

using namespace ImportGui;

ImportOCAFGui::ImportOCAFGui(Handle(TDocStd_Document) hDoc,
                             App::Document* pDoc,
                             const std::string& name)
    : ImportOCAF2(hDoc, pDoc, name)
{}

void ImportOCAFGui::applyFaceMaterials(Part::Feature* part,
                                       const std::vector<App::MaterialAppearance>& mats, bool pbr)
{
    // The looks are the object's (docs/ShapeAppearanceDesign.md sec 14.6.7)
    ImportOCAF2::applyFaceMaterials(part, mats, pbr);
    auto vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp || mats.empty()) {
        return;
    }

    // Per-face images arrive UV mapped -- a glTF mesh carries its own
    // texture coordinates and the reader keeps them. The render engine
    // lays a face image out in millimetres of object space by default
    // (a printed decal, on CAD geometry that has no UVs), so say so:
    // Render_FaceTextureScale zero means the mesh's own coordinates.
    if (std::any_of(mats.begin(), mats.end(), [](const App::MaterialAppearance& m) {
            return !m.image.empty() || !m.imagePath.empty();
        })) {
        auto prop = Base::freecad_dynamic_cast<App::PropertyFloat>(
            vp->getPropertyByName("Render_FaceTextureScale"));
        if (!prop) {
            prop = static_cast<App::PropertyFloat*>(vp->addDynamicProperty(
                "App::PropertyFloat", "Render_FaceTextureScale", "Render",
                "Size of one tile of a per-face image, in millimetres of "
                "object space; 0 = the mesh's own texture coordinates"));
        }
        if (prop) {
            prop->setValue(0.0);
        }
    }
}

void ImportOCAFGui::applyRenderMaterial(Part::Feature* part,
                                        const Import::RenderMaterial& mat)
{
    // The texture slots become Render_* dynamic properties (see
    // ViewProviderGeometryObject, which builds the render engine scene
    // graph nodes from them); the metalness and roughness factors are
    // material data and land on the appearance instead. The base color
    // factor is already applied through the color labels.
    // ... which is the object's look, and the object's to take
    ImportOCAF2::applyRenderMaterial(part, mat);
    auto vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp || !mat.valid) {
        return;
    }

    auto setFile = [vp](const char* name, const std::string& path, const char* doc) {
        if (path.empty()) {
            return;
        }
        auto prop = Base::freecad_dynamic_cast<App::PropertyFileIncluded>(
            vp->getPropertyByName(name));
        if (!prop) {
            prop = static_cast<App::PropertyFileIncluded*>(vp->addDynamicProperty(
                "App::PropertyFileIncluded", name, "Render", doc));
        }
        prop->setValue(path.c_str());
    };
    // The base colour image, unless the faces carry their own: those
    // are the more specific statement and the object-wide one would
    // modulate on top of them.
    if (!vp->ShapeAppearance.hasImage()) {
        setFile("Render_BaseColorTexture", mat.baseColorTexture,
                "Base color texture image of the object");
    }
    setFile("Render_NormalMap", mat.normalMapTexture,
            "Tangent space normal map (or grayscale height map) of the object");
    setFile("Render_EmissiveMap", mat.emissiveTexture,
            "Emissive map added to the lit color by the render engine");
    setFile("Render_OcclusionMap", mat.occlusionTexture,
            "Ambient occlusion map multiplying the ambient/environment "
            "light of the render engine");
    setFile("Render_MetallicRoughnessMap", mat.metallicRoughnessTexture,
            "glTF metallic-roughness map of the render engine PBR shading: "
            "green multiplies roughness, blue metallic");
}
