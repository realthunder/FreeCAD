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

void ImportOCAFGui::applyFaceColors(Part::Feature* part, const std::vector<App::Color>& colors)
{
    auto vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp) {
        return;
    }
    if (colors.empty()) {
        return;
    }

    if (colors.size() == 1) {
        vp->ShapeColor.setValue(colors.front());
        vp->Transparency.setValue(100 * colors.front().a);
    }
    else {
        vp->DiffuseColor.setValues(colors);
    }
}

void ImportOCAFGui::applyFaceMaterials(Part::Feature* part,
                                       const std::vector<App::MaterialAppearance>& mats, bool pbr)
{
    auto vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp || mats.empty()) {
        return;
    }
    // The mode first, so the collapse baselines follow it while the values
    // land. The materials carry the same tag, so the assignment below
    // restates it rather than converting them away.
    vp->ShapeAppearance.setPBR(pbr);
    // Collapse a uniform list to one entry: a single-entry appearance is
    // the whole-object form, whose scalar path every consumer handles.
    if (std::all_of(mats.begin() + 1, mats.end(), [&](const App::MaterialAppearance& m) {
            return m == mats[0];
        })) {
        vp->ShapeAppearance.setValue(mats[0]);
    }
    else {
        vp->ShapeAppearance.setValues(mats);
        // Which of those materials the object IS, decided here where the
        // shape is in hand: an imported list states one per face and
        // nothing about the body colour, and the mirror holds the
        // constructor's grey, which occurs in no imported list
        // (docs/ShapeAppearanceDesign.md 12.4). Area, not count: a green
        // board with five hundred gold pads is decided the wrong way by
        // count.
        std::vector<double> areas;
        if (vp->getFaceWeights(areas)) {
            vp->ShapeAppearance.deriveBase(nullptr, &areas);
        }
        else {
            vp->ShapeAppearance.deriveBase();
        }
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

void ImportOCAFGui::applyEdgeColors(Part::Feature* part, const std::vector<App::Color>& colors)
{
    auto vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp) {
        return;
    }
    if (colors.size() == 1) {
        vp->LineColor.setValue(colors.front());
    }
    else {
        vp->LineColorArray.setValues(colors);
    }
}

void ImportOCAFGui::applyLinkColor(App::DocumentObject* obj, int index, App::Color color)
{
    auto vp =
        dynamic_cast<Gui::ViewProviderLink*>(Gui::Application::Instance->getViewProvider(obj));
    if (!vp) {
        return;
    }
    if (index < 0) {
        vp->OverrideMaterial.setValue(true);
        vp->ShapeAppearance.setDiffuseColor(color);
        return;
    }
    if (vp->OverrideMaterialList.getSize() <= index) {
        vp->OverrideMaterialList.setSize(index + 1);
    }
    vp->OverrideMaterialList.set1Value(index, true);
    App::MaterialAppearance mat(App::MaterialAppearance::DEFAULT);
    if (vp->MaterialList.getSize() <= index) {
        vp->MaterialList.setSize(index + 1, mat);
    }
    mat.diffuseColor = color;
    vp->MaterialList.set1Value(index, mat);
}

void ImportOCAFGui::applyElementColors(App::DocumentObject* obj,
                                       const std::map<std::string, App::Color>& colors)
{
    auto vp = Gui::Application::Instance->getViewProvider(obj);
    if (!vp) {
        return;
    }
    (void)colors;
}

void ImportOCAFGui::applyRenderMaterial(Part::Feature* part,
                                        const Import::RenderMaterial& mat)
{
    // The texture slots become Render_* dynamic properties (see
    // ViewProviderGeometryObject, which builds the render engine scene
    // graph nodes from them); the metalness and roughness factors are
    // material data and land on the appearance instead. The base color
    // factor is already applied through the color labels.
    auto vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp || !mat.valid) {
        return;
    }

    // applyFaceMaterials has normally put the exact per-face factors on
    // the appearance already, and a whole-object value must not flatten
    // them -- so this is the fallback for a shape whose faces did NOT all
    // resolve to a PBR material, where the appearance is still Phong and
    // these two are the only PBR data the file gave us.
    if (!vp->ShapeAppearance.isPBR() && (mat.metallic >= 0.0 || mat.roughness >= 0.0)) {
        // convertPBR rather than setPBR: it carries the Phong reading's
        // look across the mode change instead of re-reading the same
        // slots as PBR values, which would restate colour as metalness.
        vp->ShapeAppearance.convertPBR(true);
        if (mat.metallic >= 0.0) {
            vp->ShapeAppearance.setMetallic(float(mat.metallic));
        }
        if (mat.roughness >= 0.0) {
            vp->ShapeAppearance.setRoughness(float(mat.roughness));
        }
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
