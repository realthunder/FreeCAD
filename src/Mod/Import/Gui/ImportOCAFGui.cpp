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
#include <App/PropertyFile.h>
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
                                       const std::vector<App::Material>& mats, bool pbr)
{
    auto vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp || mats.empty()) {
        return;
    }
    // The mode first: the values land under the reading they were built
    // for (and the collapse baselines follow the mode).
    vp->ShapeAppearance.setPBR(pbr);
    // Collapse a uniform list to one entry: a single-entry appearance is
    // the whole-object form, whose scalar path every consumer handles.
    if (std::all_of(mats.begin() + 1, mats.end(), [&](const App::Material& m) {
            return m == mats[0];
        })) {
        vp->ShapeAppearance.setValue(mats[0]);
    }
    else {
        vp->ShapeAppearance.setValues(mats);
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
    App::Material mat(App::Material::DEFAULT);
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
    // Mirror the imported PBR material into the view provider's Render_*
    // dynamic properties (see ViewProviderGeometryObject, which builds the
    // render engine scene graph nodes from them). The base color factor is
    // already applied through the color labels.
    auto vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
        Gui::Application::Instance->getViewProvider(part));
    if (!vp || !mat.valid) {
        return;
    }

    auto setFloat = [vp](const char* name, double value, const char* doc) {
        if (value < 0.0) {
            return;
        }
        auto prop = Base::freecad_dynamic_cast<App::PropertyFloat>(
            vp->getPropertyByName(name));
        if (!prop) {
            prop = static_cast<App::PropertyFloat*>(vp->addDynamicProperty(
                "App::PropertyFloatConstraint", name, "Render", doc));
        }
        prop->setValue(value);
    };
    setFloat("Render_Metallic", mat.metallic,
             "Metalness of the render engine PBR shading, 0 to 1");
    setFloat("Render_Roughness", mat.roughness,
             "Roughness of the render engine PBR shading, 0 to 1");

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
    setFile("Render_BaseColorTexture", mat.baseColorTexture,
            "Base color texture image of the object");
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
