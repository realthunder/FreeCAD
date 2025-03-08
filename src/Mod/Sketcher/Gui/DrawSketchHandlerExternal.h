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
#include "GeometryCreationMode.h"


namespace SketcherGui {

extern GeometryCreationMode geometryCreationMode; // defined in CommandCreateGeo.cpp

class ExternalSelection : public SketcherSelectionFilterGate
{
public:
    ExternalSelection(App::DocumentObject* obj, bool intersection)
        : SketcherSelectionFilterGate(obj)
        , intersection(intersection)
    {
    }

    bool allow(App::Document *pDoc, App::DocumentObject *pObj, const char *sSubName) override
    {
        Sketcher::SketchObject *sketch = static_cast<Sketcher::SketchObject*>(object);
        static const std::array<const char *, 4> allowedTypes = {"Face", "Wire", "Edge", "Vertex"};

        this->notAllowedReason = "";

        bool checkShape = true;
        if (sSubName && sSubName[0]) {
            for (auto type : allowedTypes) {
                if (boost::starts_with(sSubName, type)) {
                    checkShape = false;
                    break;
                }
            }
        }
        else if (!(QApplication::queryKeyboardModifiers() & Qt::AltModifier)) {
            this->notAllowedReason = QT_TR_NOOP("Hold Alt key to enable whole object selection.");
            return false;
        }
        
        if (checkShape) {
            auto shape = Part::TopoShape(Part::Feature::getShape(pObj));
            if (shape.isNull()) {
                this->notAllowedReason = QT_TR_NOOP("No shape. ");
                return false;
            }
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

        if (!ViewProviderSketch::allowFaceExternalPick() && boost::starts_with(sSubName, "Face")) {
            this->notAllowedReason = QT_TR_NOOP("Face picking disabled in the task panel. ");
            return false;
        }

        Sketcher::SketchObject::eReasonList msg;
        if (!sketch->isExternalAllowed(pDoc, pObj, &msg)){
            switch(msg){
            case Sketcher::SketchObject::rlCircularReference:
                this->notAllowedReason = QT_TR_NOOP("Linking this will cause circular dependency. ");
                break;

                // We'll auto create shapebinder in the following cases.
#if 1
            case Sketcher::SketchObject::rlOtherDoc:
            case Sketcher::SketchObject::rlOtherBody:
            case Sketcher::SketchObject::rlOtherPart:
                return true;
#else
            case Sketcher::SketchObject::rlOtherDoc:
                this->notAllowedReason = QT_TR_NOOP("This object is in another document.");
                break;
            case Sketcher::SketchObject::rlOtherBody:
                this->notAllowedReason = QT_TR_NOOP("This object belongs to another body, can't link.");
                break;
            case Sketcher::SketchObject::rlOtherPart:
                this->notAllowedReason = QT_TR_NOOP("This object belongs to another part, can't link.");
                break;
#endif
            default:
                break;
            }
            return false;
        }

        // Note: its better to search the support of the sketch in case the sketch support is a base plane
        //Part::BodyBase* body = Part::BodyBase::findBodyOf(sketch);
        //if ( body && body->hasFeature ( pObj ) && body->isAfter ( pObj, sketch ) ) {
            // Don't allow selection after the sketch in the same body
            // NOTE: allowness of features in other bodies is handled by SketchObject::isExternalAllowed()
            // TODO may be this should be in SketchObject::isExternalAllowed() (2015-08-07, Fat-Zer)
            //return false;
        //}

        std::string element(sSubName);
        if (intersection ||
            boost::starts_with(element, "Edge") ||
            boost::starts_with(element, "Vertex") ||
            boost::starts_with(element, "Face") ||
            boost::starts_with(element, "Wire"))
        {
            return true;
        }
        if (pObj->getTypeId().isDerivedFrom(App::Plane::getClassTypeId()) ||
            pObj->getTypeId().isDerivedFrom(Part::Datum::getClassTypeId()))
            return true;
        return  false;
    }

private:
    bool intersection = false;
};

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
        Gui::Selection().rmvSelectionGate();
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
        Gui::MDIView *mdi = Gui::Application::Instance->activeDocument()->getActiveView();
        Gui::View3DInventorViewer *viewer;
        viewer = static_cast<Gui::View3DInventor *>(mdi)->getViewer();

        SoNode* root = viewer->getSceneGraph();
        static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionRole.setValue(true);

        Gui::Selection().clearSelection();
        Gui::Selection().rmvSelectionGate();
        Gui::Selection().addSelectionGate(new ExternalSelection(sketchgui->getObject(), intersection));
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

    void mouseMove(Base::Vector2d onSketchPos) override
    {
        Q_UNUSED(onSketchPos);
        if (Gui::Selection().hasPreselection())
            applyCursor();
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

    bool allowExternalDocument() const override {return true;}

    bool onSelectionChanged(const Gui::SelectionChanges& msg) override
    {
        if (msg.Type == Gui::SelectionChanges::AddSelection) {
            App::DocumentObject* obj = msg.Object.getObject();
            if (obj == NULL)
                throw Base::ValueError("Sketcher: External geometry: Invalid object in selection");

            if (msg.Object.getOldElementName().empty()
                    && !(QApplication::queryKeyboardModifiers() & Qt::AltModifier)) {
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
                        std::ostringstream ss;
                        ss << "[";
                        if (defining)
                            ss << "'defining',";
                        if (intersection)
                            ss << "'intersection',";
                        ss << "]";
                        Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                "addExternal(Part.importExternalObject(%s, %s), %s)",
                                msg.pOriginalMsg ?
                                    msg.pOriginalMsg->Object.getSubObjectPython() :
                                    msg.Object.getSubObjectPython(),
                                sketchgui->getEditingContext().getSubObjectPython(false),
                                ss.str());
                    }

                    Gui::Selection().clearSelection();

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
};

} // namespace SketcherGui


#endif // SKETCHERGUI_DrawSketchHandlerExternal_H

