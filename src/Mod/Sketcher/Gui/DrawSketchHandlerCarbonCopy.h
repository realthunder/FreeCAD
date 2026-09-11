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

#ifndef SKETCHERGUI_DrawSketchHandlerCarbonCopy_H
#define SKETCHERGUI_DrawSketchHandlerCarbonCopy_H

#include <QApplication>

#include <Gui/Notifications.h>
#include <Gui/SelectionFilter.h>
#include <Gui/Command.h>
#include <Gui/CommandT.h>
#include <Gui/MDIView.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>

#include <Mod/Sketcher/App/SketchObject.h>

#include "DrawSketchHandler.h"
#include "GeometryCreationMode.h"
#include "Utils.h"
#include <Gui/ViewerContext.h>

#include "ViewProviderSketch.h"


namespace SketcherGui
{

extern GeometryCreationMode geometryCreationMode;  // defined in CommandCreateGeo.cpp

class CarbonCopySelection: public SketcherSelectionFilterGate
{
public:
    explicit CarbonCopySelection(App::DocumentObject* obj)
    : SketcherSelectionFilterGate(obj)
    {}

    bool allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName) override
    {
        Q_UNUSED(sSubName);

        Sketcher::SketchObject* sketch = static_cast<Sketcher::SketchObject*>(object);
        // The modifiers of the view whose click is being resolved: a
        // replayed event carries its own (docs/ThinClient.md 8.11).
        const Qt::KeyboardModifiers modifiers = Gui::ViewerContext::currentKeyboardModifiers();
        sketch->setAllowOtherBody(modifiers == Qt::ControlModifier
                                  || modifiers == (Qt::ControlModifier | Qt::AltModifier));
        sketch->setAllowUnaligned(modifiers == (Qt::ControlModifier | Qt::AltModifier));

        this->notAllowedReason = "";
        Sketcher::SketchObject::eReasonList msg;
        // Reusing code: All good reasons not to allow a carbon copy
        bool xinv = false, yinv = false;
        if (!sketch->isCarbonCopyAllowed(pDoc, pObj, xinv, yinv, &msg)) {
            switch (msg) {
                case Sketcher::SketchObject::rlCircularReference:
                    this->notAllowedReason =
                        QT_TR_NOOP("Carbon copy would cause a circular dependency.");
                    break;
                case Sketcher::SketchObject::rlOtherDoc:
                    this->notAllowedReason = QT_TR_NOOP("This object is in another document.");
                    break;
                case Sketcher::SketchObject::rlOtherBody:
                    this->notAllowedReason = QT_TR_NOOP("This object belongs to another body. Hold "
                                                        "Ctrl to allow cross-references.");
                    break;
                case Sketcher::SketchObject::rlOtherBodyWithLinks:
                    this->notAllowedReason =
                        QT_TR_NOOP("This object belongs to another body and it contains external "
                                   "geometry. Cross-reference not allowed.");
                    break;
                case Sketcher::SketchObject::rlOtherPart:
                    this->notAllowedReason = QT_TR_NOOP("This object belongs to another part.");
                    break;
                case Sketcher::SketchObject::rlNonParallel:
                    this->notAllowedReason =
                        QT_TR_NOOP("The selected sketch is not parallel to this sketch. Hold "
                                   "Ctrl+Alt to allow non-parallel sketches.");
                    break;
                case Sketcher::SketchObject::rlAxesMisaligned:
                    this->notAllowedReason =
                        QT_TR_NOOP("The XY axes of the selected sketch do not have the same "
                                   "direction as this sketch. Hold Ctrl+Alt to disregard it.");
                    break;
                case Sketcher::SketchObject::rlOriginsMisaligned:
                    this->notAllowedReason =
                        QT_TR_NOOP("The origin of the selected sketch is not aligned with the "
                                   "origin of this sketch. Hold Ctrl+Alt to disregard it.");
                    break;
                default:
                    break;
            }
            return false;
        }
        // Carbon copy only works on sketches that are not disallowed (e.g. would produce a circular
        // reference)
        return true;
    }
};

class DrawSketchHandlerCarbonCopy: public DrawSketchHandler
{
public:
    DrawSketchHandlerCarbonCopy() = default;
    ~DrawSketchHandlerCarbonCopy() override
    {
        if (gateOn) {
            gateOn->rmvSelectionGate();
        }
    }

    void mouseMove(Base::Vector2d onSketchPos) override
    {
        Q_UNUSED(onSketchPos);
        if (sketchgui->sessionSelection().getPreselection().pObjectName) {
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

    bool allowExternalPick() const override
    {
        return true;
    }

    bool onSelectionChanged(const Gui::SelectionChanges& msg) override
    {
        if (msg.Type == Gui::SelectionChanges::AddSelection) {
            App::DocumentObject* obj =
                sketchgui->getObject()->getDocument()->getObject(msg.pObjectName);
            if (!obj) {
                THROWM(Base::ValueError, "Sketcher: Carbon Copy: Invalid object in selection")
            }

            if (obj->is<Sketcher::SketchObject>()) {

                try {
                    Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Create a carbon copy"));
                    Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                          "carbonCopy(\"%s\",%s)",
                                          msg.pObjectName,
                                          constructionModeAsBooleanText());

                    Gui::Command::commitCommand();

                    tryAutoRecomputeIfNotSolve(
                        static_cast<Sketcher::SketchObject*>(sketchgui->getObject()));

                    sketchgui->sessionSelection().clearSelection();
                    /* this is ok not to call to purgeHandler
                     * in continuous creation mode because the
                     * handler is destroyed by the quit() method on pressing the
                     * right button of the mouse */
                }
                catch (const Base::Exception&) {
                    Gui::NotifyError(
                        sketchgui,
                        QT_TRANSLATE_NOOP("Notifications", "Error"),
                        QT_TRANSLATE_NOOP("Notifications", "Failed to add carbon copy"));
                    Gui::Command::abortCommand();
                }
                return true;
            }
        }
        return false;
    }

private:
    void activated() override
    {
        setAxisPickStyle(false);
        // The sketch under the pointer is picked by each view's own
        // selection root, which the edit turned off: back on in every
        // view of the session, and the gate on the instance the session
        // selects into (docs/ThinClient.md 8.11 item 3). Not the active
        // window's viewer, which a serving process does not have.
        sketchgui->setSessionSelectionEnabled(true);

        Gui::SelectionSingleton& sel = sketchgui->sessionSelection();
        sel.clearSelection();
        sel.rmvSelectionGate();
        sel.addSelectionGate(new CarbonCopySelection(sketchgui->getObject()));
        gateOn = &sel;
    }

    QString getCrosshairCursorSVGName() const override
    {
        return QStringLiteral("Sketcher_Pointer_CarbonCopy");
    }

    void deactivated() override
    {
        Q_UNUSED(sketchgui);
        setAxisPickStyle(true);
    }

    /// The instance the gate went on (the session's), for the destructor.
    Gui::SelectionSingleton* gateOn = nullptr;
};

}  // namespace SketcherGui


#endif  // SKETCHERGUI_DrawSketchHandlerCarbonCopy_H
