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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BRep_Builder.hxx>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <cmath>
#endif

#include <App/Color.h>
#include <App/GeoFeature.h>
#include <Base/Placement.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Mod/Part/App/ShapeDiff.h>
#include <Mod/Part/App/TopoShape.h>

#include "PartParams.h"
#include "ShapeDiffView.h"
#include "ViewProvider.h"

using namespace PartGui;

namespace
{

/// How much of a kind is seen at the side the position is at, and at the other
constexpr float Full = 0.7f;
constexpr float Ghost = 0.2f;
/// What did not change: thin, wherever the position is
constexpr float Thin = 0.15f;

}  // namespace

const char* ShapeDiffView::name(Kind kind)
{
    switch (kind) {
        case Same:
            return "same";
        case ChangedA:
            return "changedA";
        case ChangedB:
            return "changedB";
        case Removed:
            return "removed";
        case Added:
            return "added";
        default:
            return "";
    }
}

ShapeDiffView::ShapeDiffView(ViewProviderPartExt& owner)
    : _owner(owner)
{
    _root = new Gui::SoFCPathAnnotation;
    _root->priority = -2;
    _root->setName("ShapeDiff");
    _transform = new SoTransform;
    _root->addChild(_transform);
    for (auto& sw : _switch) {
        sw = new SoSwitch;
        sw->whichChild = SO_SWITCH_NONE;
        _root->addChild(sw);
    }
    // Beside what the object shows of itself, so that it is there whether
    // the object is shown or not, and moves with what the object is in.
    _owner.getRoot()->addChild(_root);
}

ShapeDiffView::~ShapeDiffView()
{
    int index = _owner.getRoot()->findChild(_root);
    if (index >= 0)
        _owner.getRoot()->removeChild(index);
    for (auto& view : _view) {
        if (view)
            view->beforeDelete();
    }
}

bool ShapeDiffView::show(const Part::TopoShape& a, const Part::TopoShape& b)
{
    Part::ShapeDiff diff(a, b);
    const auto& faces = diff.elements(TopAbs_FACE);
    if (faces.empty())
        return false;

    BRep_Builder builder;
    TopoDS_Compound shapes[Kinds];
    int counts[Kinds] = {};
    for (auto& shape : shapes)
        builder.MakeCompound(shape);
    auto add = [&](Kind kind, const Part::TopoShape& from, int index) {
        builder.Add(shapes[kind], from.getSubShape(TopAbs_FACE, index));
        ++counts[kind];
    };
    _summary.clear();
    for (const auto& face : faces) {
        ++_summary[Part::ShapeDiff::name(face.what)];
        if (face.by != Part::ShapeDiff::By::None)
            ++_summary[std::string("by.") + Part::ShapeDiff::name(face.by)];
        switch (face.what) {
            case Part::ShapeDiff::What::Same:
                add(Same, b, face.b);
                break;
            case Part::ShapeDiff::What::Moved:
            case Part::ShapeDiff::What::Changed:
                add(ChangedA, a, face.a);
                add(ChangedB, b, face.b);
                break;
            case Part::ShapeDiff::What::Removed:
                add(Removed, a, face.a);
                break;
            case Part::ShapeDiff::What::Added:
                add(Added, b, face.b);
                break;
        }
    }

    const App::Color grey(0.6f, 0.6f, 0.6f);
    const App::Color changed(static_cast<uint32_t>(PartParams::getPreviewDressColor()));
    const App::Color removed(static_cast<uint32_t>(PartParams::getPreviewSubColor()));
    const App::Color added(static_cast<uint32_t>(PartParams::getPreviewAddColor()));
    const App::Color colors[Kinds] = {grey, changed, changed, removed, added};

    for (int kind = 0; kind < Kinds; ++kind) {
        _switch[kind]->removeAllChildren();
        if (_view[kind]) {
            _view[kind]->beforeDelete();
            _view[kind].reset();
        }
        if (!counts[kind])
            continue;
        // As PartDesign's preview of what a feature adds is set up
        // (ViewProviderAddSub): a view of the object that is no view
        // provider of the document, with looks of its own.
        //
        // The shape first, before the view has an object: a view of the
        // object's own shape has the object's looks, and every one set below
        // would be written to the object -- a change of the document.
        auto view = std::make_unique<ViewProviderPart>();
        view->setGivenShape(Part::TopoShape(shapes[kind]));
        view->forceUpdate();
        view->MapFaceColor.setValue(false);
        view->MapLineColor.setValue(false);
        view->MapPointColor.setValue(false);
        view->MapTransparency.setValue(false);
        view->ForceMapColors.setValue(false);
        view->Selectable.setValue(false);
        view->Lighting.setValue(1);
        view->enableFullSelectionHighlight(false, false, false);
        view->setStatus(Gui::SecondaryView, true);
        view->attach(_owner.getObject());
        view->setDefaultMode(0);
        view->show();
        App::Color color = colors[kind];
        color.a = 0.f;
        view->LineColor.setValue(color);
        auto material = view->PointMaterial.getValue();
        material.diffuseColor = color;
        material.transparency = 1.0f;
        view->PointMaterial.setValue(material);
        view->ShapeColor.setValue(color);
        view->updateVisual();
        _switch[kind]->addChild(view->getRoot());
        _view[kind] = std::move(view);
    }
    updatePlacement();
    apply();
    return true;
}

void ShapeDiffView::setOptions(float position, bool differencesOnly)
{
    _position = std::min(1.f, std::max(0.f, position));
    _differencesOnly = differencesOnly;
    apply();
}

void ShapeDiffView::updatePlacement()
{
    // The faces drawn are where each state had them, and this is drawn under
    // the object's own placement: taken out again.
    Base::Matrix4D matrix;
    if (auto geo = Base::freecad_dynamic_cast<App::GeoFeature>(_owner.getObject()))
        matrix = geo->Placement.getValue().inverse().toMatrix();
    _transform->setMatrix(Gui::ViewProvider::convert(matrix));
}

void ShapeDiffView::apply()
{
    auto seen = [](float weight) { return Ghost + (Full - Ghost) * weight; };
    const float opacity[Kinds] = {Thin, seen(1.f - _position), seen(_position),
                                  seen(1.f - _position), seen(_position)};
    _summary["position"] = static_cast<int>(std::lround(_position * 100.f));
    _summary["differencesOnly"] = _differencesOnly ? 1 : 0;
    for (int kind = 0; kind < Kinds; ++kind) {
        const bool shown = _view[kind] && !(kind == Same && _differencesOnly);
        _switch[kind]->whichChild = shown ? SO_SWITCH_ALL : SO_SWITCH_NONE;
        const int transparency = static_cast<int>(std::lround((1.f - opacity[kind]) * 100.f));
        if (_view[kind])
            _view[kind]->Transparency.setValue(transparency);
        _summary[std::string("shown.") + name(static_cast<Kind>(kind))] = shown ? 1 : 0;
        _summary[std::string("transparency.") + name(static_cast<Kind>(kind))] = transparency;
    }
}
