/***************************************************************************
 *   Copyright (c) 2022 Abdullah Tahiri <abdullah.tahiri.yo@gmail.com>     *
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

#ifndef SKETCHERGUI_DrawSketchHandlerExternal_H
#define SKETCHERGUI_DrawSketchHandlerExternal_H

#include <array>

#include <App/Datums.h>
#include <boost/algorithm/string/predicate.hpp>

#include <App/IndexedName.h>
#include <Gui/ViewParams.h>
#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Sketcher/App/ExternalGeometryFacade.h>

#include "Utils.h"
#include <Gui/ViewerContext.h>

#include "ViewProviderSketch.h"


namespace SketcherGui
{

class ExternalSelection : public SketcherSelectionFilterGate
{
public:
    ExternalSelection(App::DocumentObject* obj, bool intersection)
        : SketcherSelectionFilterGate(obj)
        , intersection(intersection)
    {
    }

    bool allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName) override
    {
        Sketcher::SketchObject* sketch = static_cast<Sketcher::SketchObject*>(object);
        static const std::array<const char *, 4> allowedTypes = {"Face", "Wire", "Edge", "Vertex"};

        this->notAllowedReason = "";

        // Set below, for a datum element that has no shape to classify.
        bool shapelessDatum = false;

        bool checkShape = true;
        if (sSubName && sSubName[0]) {
            for (auto type : allowedTypes) {
                if (boost::starts_with(sSubName, type)) {
                    checkShape = false;
                    break;
                }
            }
        }
        else if (!(Gui::ViewerContext::currentKeyboardModifiers() & Qt::AltModifier)) {
            this->notAllowedReason = QT_TR_NOOP("Hold Alt key to enable whole object selection.");
            return false;
        }
        
        if (checkShape) {
            auto shape = Part::TopoShape(Part::Feature::getShape(pObj));
            if (shape.isNull()) {
                // A datum element may legitimately have none. App::Line and
                // App::Plane do get one synthesized by
                // Part::Feature::getTopoShape(), but App::Point does not,
                // and the sketch builds its vertex when it projects
                // (SketchObjectExternal.cpp). Anything else with no shape
                // is simply not referenceable.
                if (!pObj->isDerivedFrom<App::DatumElement>()) {
                    this->notAllowedReason = QT_TR_NOOP("No shape. ");
                    return false;
                }
                shapelessDatum = true;
            }
            else {
                sSubName = nullptr;
                for (auto type : allowedTypes) {
                    auto shapeType = Part::TopoShape::shapeType(type);
                    if (shape.shapeType() == shapeType) {
                        sSubName = type;
                        break;
                    }
                    int count = shape.countSubShapes(shapeType);
                    if (count == 1) {
                        sSubName = type;
                        break;
                    }
                    if (count > 1) {
                        this->notAllowedReason = QT_TR_NOOP("Multiple elements. ");
                        return false;
                    }
                }
                if (!sSubName) {
                    this->notAllowedReason = QT_TR_NOOP("Unknown element. ");
                    return false;
                }
            }
        }

        // sSubName can be null here now: a shapeless datum skips the block
        // above, which is what used to guarantee it had been set.
        if (!ViewProviderSketch::allowFaceExternalPick() && sSubName
                && boost::starts_with(sSubName, "Face")) {
            this->notAllowedReason = QT_TR_NOOP("Face picking disabled in the task panel. ");
            return false;
        }

        Sketcher::SketchObject::eReasonList msg;
        if (!sketch->isExternalAllowed(pDoc, pObj, &msg)){
            switch(msg){
                case Sketcher::SketchObject::rlCircularReference:
                    this->notAllowedReason = QT_TR_NOOP("Linking this will cause circular dependency.");
                    break;

                    // We'll auto create shapebinder in the following cases.
                case Sketcher::SketchObject::rlOtherDoc:
                case Sketcher::SketchObject::rlOtherBody:
                case Sketcher::SketchObject::rlOtherPart:
                    return true;
                default:
                    break;
            }
            return false;
        }

        // Note: its better to search the support of the sketch in case the sketch support is a base
        // plane
        // Part::BodyBase* body = Part::BodyBase::findBodyOf(sketch);
        // if ( body && body->hasFeature ( pObj ) && body->isAfter ( pObj, sketch ) ) {
        // Don't allow selection after the sketch in the same body
        // NOTE: allowness of features in other bodies is handled by
        // SketchObject::isExternalAllowed()
        // TODO may be this should be in SketchObject::isExternalAllowed() (2015-08-07, Fat-Zer)
        // return false;
        //}

        // A shapeless datum is taken whole: there is no sub-element to
        // name. Checked after isExternalAllowed() above, so it gets no
        // pass on the circular-reference and other-body rules.
        if (shapelessDatum) {
            return true;
        }

        std::string element(sSubName ? sSubName : "");
        if (intersection ||
            boost::starts_with(element, "Edge") ||
            boost::starts_with(element, "Vertex") ||
            boost::starts_with(element, "Face") ||
            boost::starts_with(element, "Wire"))
        {
            return true;
        }
        if (pObj->isDerivedFrom<App::Plane>() || pObj->isDerivedFrom<Part::Datum>()) {
            return true;
        }
        return false;
    }

    /// Whether a pick is taken for its intersection with the sketch plane
    void setIntersection(bool on)
    {
        intersection = on;
    }

private:
    bool intersection = false;
};

/** Make what was picked external geometry of the sketch in edit
 *
 * The one way a pick becomes external geometry: the External tool's, and a
 * constraint tool's with outside picking stacked on it. The pick goes
 * through Part.importExternalObject(), which is what carries sub-object
 * paths and mapped element names, and makes a binder for an object of
 * another body or document.
 *
 * No transaction is opened or closed here.
 *
 * @param picked: the selection as it was made (the original one, where the
 * selection was resolved)
 * @param resolved: the same, resolved
 * @return the ids of the external geometry made -- or, where the sketch
 * already refers to that element, of the geometry it has for it. Empty if
 * nothing came of the pick.
 */
inline std::vector<int> addExternalFromPick(ViewProviderSketch* sketchgui,
                                            const App::SubObjectT& picked,
                                            const App::SubObjectT& resolved,
                                            bool defining,
                                            bool intersection)
{
    auto sketch = sketchgui->getSketchObject();

    // The external geometry by the reference each piece was projected
    // from: "<object>.<element>", the same key the sketch keeps.
    auto idsByRef = [sketch]() {
        std::map<std::string, std::vector<int>> res;
        int geoId = 0;
        for (const auto geo : sketch->getExternalGeometry()) {
            --geoId;
            if (geoId > Sketcher::GeoEnum::RefExt) {
                continue;
            }
            const std::string& ref = Sketcher::ExternalGeometryFacade::getFacade(geo)->getRef();
            if (!ref.empty()) {
                res[ref].push_back(geoId);
            }
        }
        return res;
    };
    const auto had = idsByRef();

    // Already referred to, this way? addExternal() would refuse it with an
    // error. Referred to the other way -- projected, and now picked for its
    // cut, or the reverse -- it is taken both ways, and addExternal() does
    // that.
    if (App::DocumentObject* obj = resolved.getSubObject()) {
        const std::string element = resolved.getOldElementName();
        const auto& objs = sketch->ExternalGeometry.getValues();
        const auto subs = sketch->ExternalGeometry.getSubValues(false);
        const auto names = sketch->ExternalGeometry.getSubValues(true);
        const auto& types = sketch->ExternalTypes.getValues();
        const long kind = static_cast<long>(intersection ? Sketcher::ExtType::Intersection
                                                         : Sketcher::ExtType::Projection);
        for (std::size_t i = 0; i < objs.size() && i < subs.size() && i < names.size(); ++i) {
            if (objs[i] == obj && subs[i] == element) {
                const long has = i < types.size()
                    ? types[i]
                    : static_cast<long>(Sketcher::ExtType::Projection);
                if (has != kind && has != static_cast<long>(Sketcher::ExtType::Both)) {
                    break;
                }
                auto it = had.find(std::string(obj->getNameInDocument()) + "."
                                   + Data::newElementName(names[i].c_str()));
                if (it != had.end()) {
                    return it->second;
                }
            }
        }
    }

    std::ostringstream ss;
    ss << "[";
    if (defining) {
        ss << "'defining',";
    }
    if (intersection) {
        ss << "'intersection',";
    }
    ss << "]";
    Gui::cmdAppObjectArgs(sketch,
                          "addExternal(Part.importExternalObject(%s, %s), %s)",
                          picked.getSubObjectPython(),
                          sketchgui->getEditingContext().getSubObjectPython(false),
                          ss.str());

    // adding external geometry does not require a solve() per se (the DoF is the same),
    // however a solve is required to update the amount of solver geometry, because we only
    // redraw a changed Sketch if the solver geometry amount is the same as the SkethObject
    // geometry amount (as this avoids other issues).
    // This solver is a very low cost one anyway (there is actually nothing to solve).
    tryAutoRecomputeIfNotSolve(sketch);
    // what was not there before: a new reference's geometry, or the half
    // an old one gained
    std::set<int> before;
    for (const auto& [ref, was] : had) {
        before.insert(was.begin(), was.end());
    }
    std::vector<int> ids;
    for (const auto& [ref, made] : idsByRef()) {
        for (int id : made) {
            if (!before.count(id)) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

class DrawSketchHandlerExternal: public DrawSketchHandler
                               , public ParameterGrp::ObserverType
{
public:
    std::vector<int> attaching;
    bool defining = false;
    bool intersection = false;
    bool restoreHighlightPick = false;
    ParameterGrp::handle hGrp;
    ParameterGrp::handle hGrpView;
    bool _activated = false;
    bool _busy = false;
    /// The instance the gate went on (the session's), for the destructor.
    Gui::SelectionSingleton* gateOn = nullptr;

    DrawSketchHandlerExternal(bool defining=false, bool intersection=false)
        :attaching(0)
        ,defining(defining)
        ,intersection(intersection)
    {
        init();
    }

    DrawSketchHandlerExternal(std::vector<int> &&geoIds)
        :attaching(std::move(geoIds))
    {
        init();
    }

    void init() {
        hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Mod/Sketcher/General");
        hGrp->Attach(this);
        hGrpView = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/View");
        hGrpView->Attach(this);
    }

    ~DrawSketchHandlerExternal() override
    {
        hGrp->Detach(this);
        hGrpView->Detach(this);
        if (gateOn)
            gateOn->rmvSelectionGate();
        if (restoreHighlightPick)
            Gui::ViewParams::setAutoTransparentPick(false);
    }

    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if (strcmp(sReason, "SketchAutoTransparentPick") == 0)
            setupTransparentPick();
        else if (strcmp(sReason, "AutoTransparentPick") == 0) {
            if (!_busy)
                hGrp->SetBool("SketchAutoTransparentPick", false);
        }
    }

    bool allowExternalPick() const override
    {
        return true;
    }

    void setupTransparentPick()
    {
        Base::StateLocker guard(_busy);
        bool enabled = hGrp->GetBool("SketchAutoTransparentPick", false);
        if (!_activated || !enabled) {
            if (restoreHighlightPick) {
                restoreHighlightPick = false;
                Gui::ViewParams::setAutoTransparentPick(false);
            }
        }
        else if (_activated) {
            if (!restoreHighlightPick) {
                restoreHighlightPick = true;
                Gui::ViewParams::setAutoTransparentPick(true);
            }
        }
    }

    void activated() override
    {
        _activated = true;
        setupTransparentPick();
        if(attaching.size())
            sketchgui->showGeometry(false);
        sketchgui->setAxisPickStyle(false);
        // The object under the pointer is picked by each view's own
        // selection root, which the edit turned off: back on in every
        // view of the session, and the gate on the instance the session
        // selects into (docs/ThinClient.md 8.11 item 3). Not the active
        // window's viewer, which a serving process does not have.
        sketchgui->setSessionSelectionEnabled(true);

        Gui::SelectionSingleton &sel = sketchgui->sessionSelection();
        sel.clearSelection();
        sel.rmvSelectionGate();
        sel.addSelectionGate(new ExternalSelection(sketchgui->getObject(), intersection));
        gateOn = &sel;
    }

    QString getCrosshairCursorSVGName() const override {
        if (intersection)
            return QStringLiteral("Sketcher_Pointer_Intersection");
        else if(defining)
            return QStringLiteral("Sketcher_Pointer_Defining");
        else if(attaching.size())
            return QStringLiteral("Sketcher_Pointer_Attaching");
        else
            return QStringLiteral("Sketcher_Pointer_External");
    }

    void deactivated() override
    {
        _activated = false;
        setupTransparentPick();
        sketchgui->showGeometry();
        sketchgui->setAxisPickStyle(true);
    }

    void mouseMove(SnapManager::SnapHandle snapHandle) override
    {
        Base::Vector2d onSketchPos = snapHandle.compute();
        Q_UNUSED(onSketchPos);
        if (sketchgui->sessionSelection().hasPreselection()) {
            applyCursor();
        }
    }

    bool pressButton(Base::Vector2d onSketchPos) override
    {
        Q_UNUSED(onSketchPos);
        return true;
    }

    bool releaseButton(Base::Vector2d onSketchPos) override
    {
        Q_UNUSED(onSketchPos);
        /* this is ok not to call to purgeHandler
         * in continuous creation mode because the
         * handler is destroyed by the quit() method on pressing the
         * right button of the mouse */
        return true;
    }

    bool allowExternalDocument() const override
    {
        return true;
    }

    bool onSelectionChanged(const Gui::SelectionChanges& msg) override
    {
        if (msg.Type == Gui::SelectionChanges::AddSelection) {
            App::DocumentObject* obj = msg.Object.getObject();
            if (!obj) {
                THROWM(Base::ValueError, "Sketcher: External geometry: Invalid object in selection")
            }

            if (msg.Object.getOldElementName().empty()
                    && !(Gui::ViewerContext::currentKeyboardModifiers() & Qt::AltModifier)) {
                return false;
            }

            auto indexedName = Data::IndexedName(msg.Object.getOldElementName().c_str());
            if (intersection ||
                obj->getTypeId().isDerivedFrom(App::Plane::getClassTypeId()) ||
                obj->getTypeId().isDerivedFrom(Part::Datum::getClassTypeId()) ||
                msg.Object.getOldElementName().empty() ||
                boost::iends_with(indexedName.getType(), "edge") ||
                boost::iends_with(indexedName.getType(), "vertex") ||
                boost::iends_with(indexedName.getType(), "face") ||
                boost::iends_with(indexedName.getType(), "wire")) {
                try {
                    if(attaching.size()) {
                        Gui::Command::openCommand(
                                QT_TRANSLATE_NOOP("Command", "Attach external geometry"));
                        std::ostringstream ss;
                        ss << '[';
                        for(int geoId : attaching)
                            ss << geoId << ',';
                        ss << ']';
                        Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                "attachExternal(%s, Part.importExternalObject(%s, %s))",
                                ss.str(),
                                msg.pOriginalMsg ?
                                    msg.pOriginalMsg->Object.getSubObjectPython() :
                                    msg.Object.getSubObjectPython(),
                                sketchgui->getEditingContext().getSubObjectPython(false));
                    } else {
                        Gui::Command::openCommand(
                                QT_TRANSLATE_NOOP("Command", "Add external geometry"));
                        addExternalFromPick(sketchgui,
                                            msg.pOriginalMsg ? msg.pOriginalMsg->Object
                                                             : msg.Object,
                                            msg.Object,
                                            defining,
                                            intersection);
                        sketchgui->sessionSelection().clearSelection();
                        Gui::Command::commitCommand();
                        return true;
                    }

                    sketchgui->sessionSelection().clearSelection();

                    // adding external geometry does not require a solve() per se (the DoF is the same),
                    // however a solve is required to update the amount of solver geometry, because we only
                    // redraw a changed Sketch if the solver geometry amount is the same as the SkethObject
                    // geometry amount (as this avoids other issues).
                    // This solver is a very low cost one anyway (there is actually nothing to solve).
                    tryAutoRecomputeIfNotSolve(static_cast<Sketcher::SketchObject *>(sketchgui->getObject()));
                    Gui::Command::commitCommand();
                } catch (Base::Exception &e) {
                    e.ReportException();
                    Base::Console().Error("Failed to add external geometry: %s\n", e.what());
                    Gui::Command::abortCommand();
                }

                if(attaching.size())
                    sketchgui->purgeHandler();
                return true;
            }
        }
        return false;
    }

    /// What the tool waits for, by flavour. The second line is the gate's own
    /// rule: an object as a whole is taken only with Alt held.
    std::list<Gui::InputHint> getToolHints() const override
    {
        using enum Gui::InputHint::UserInput;

        QString pick;
        if (attaching.size()) {
            pick = tr("%1 pick geometry to attach the external geometry to",
                      "Sketcher External: hint");
        }
        else if (intersection && defining) {
            pick = tr("%1 pick geometry to add its intersection with the sketch plane "
                      "as defining geometry",
                      "Sketcher External: hint");
        }
        else if (intersection) {
            pick = tr("%1 pick geometry to add its intersection with the sketch plane",
                      "Sketcher External: hint");
        }
        else if (defining) {
            pick = tr("%1 pick geometry to add its projection as defining geometry",
                      "Sketcher External: hint");
        }
        else {
            pick = tr("%1 pick geometry to add its projection", "Sketcher External: hint");
        }
        return {
            {pick, {MouseLeft}},
            {tr("%1 pick a whole object", "Sketcher External: hint"), {{KeyAlt, MouseLeft}}},
        };
    }
};

} // namespace SketcherGui


#endif  // SKETCHERGUI_DrawSketchHandlerExternal_H
