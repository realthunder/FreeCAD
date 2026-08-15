/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QApplication>
# include <QMouseEvent>
# include <QByteArray>
# include <QMenu>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <App/SavedView.h>
#include <App/AutoTransaction.h>
#include <App/GroupExtension.h>
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "Clipping.h"
#include "BitmapFactory.h"
#include "Application.h"
#include "Document.h"
#include "Command.h"
#include "Action.h"
#include "ViewParams.h"
#include "ViewProviderSavedView.h"
#include "ViewProviderSavedViewPy.h"

using namespace Gui;
using namespace Gui::Dialog;

PROPERTY_SOURCE(Gui::ViewProviderSavedView, Gui::ViewProviderDocumentObject)

ViewProviderSavedView::ViewProviderSavedView()
{
    sPixmap = "SavedView";
}

ViewProviderSavedView::~ViewProviderSavedView()
{
}

void ViewProviderSavedView::setupContextMenu(QMenu *menu, QObject*, const char*)
{
    prepareMenu(menu);
}

QString propertyName(const App::Property &prop)
{
    QString name = QString::fromUtf8(prop.getName() ? prop.getName() : "?");
    QString display;
    bool upper = false;
    for (int i=0; i<name.length(); i++) {
        if (name[i].isUpper() && !display.isEmpty()) {
            // if there is a sequence of capital letters do not insert spaces
            if (!upper) {
                QChar last = display.at(display.length()-1);
                if (!last.isSpace())
                    display += QStringLiteral(" ");
            }
        }
        upper = name[i].isUpper();
        display += name[i];
    }
    return display;
}

void ViewProviderSavedView::prepareMenu(QMenu *menu)
{
    auto obj = Base::freecad_dynamic_cast<App::SavedView>(getObject());
    if (!obj)
        return;
    QMenu *submenu;
    menu->addAction(QObject::tr("Capture"), [this]() {capture();});
    submenu = menu->addMenu(QObject::tr("Capture options"));
    submenu->setToolTipsVisible(true);

    auto addOption = [submenu](App::PropertyBool &prop) {
        auto action = Action::addCheckBox(submenu,
                        QApplication::translate("App::Property", propertyName(prop).toUtf8()),
                        QApplication::translate("App::Property", prop.getDocumentation()),
                        QIcon(),
                        prop.getValue());
        QObject::connect(action, &QAction::toggled, [&prop](bool checked){
            prop.setValue(checked);
        });
    };

    QObject::connect(submenu, &QMenu::aboutToShow, []() {
        if (!App::GetApplication().getActiveTransaction())
            App::GetApplication().setActiveTransaction(QT_TRANSLATE_NOOP("Command", "Change saved view"));
    });
    QObject::connect(submenu, &QMenu::aboutToHide, []() {
        App::GetApplication().closeActiveTransaction();
    });

    addOption(obj->SaveClippings);
    addOption(obj->SaveCamera);
    addOption(obj->SaveVisibilities);
    addOption(obj->SaveShowOnTop);
    addOption(obj->SaveDrawStyleSettings);

    submenu = menu->addMenu(QObject::tr("Partial capture"));
    submenu->addAction(QObject::tr("Clipping settings"), [this]() {capture(CaptureOption::Clippings);});
    submenu->addAction(QObject::tr("Camera settings"), [this]() {capture(CaptureOption::Camera);});
    submenu->addAction(QObject::tr("Visibilities"), [this]() {capture(CaptureOption::Visibilities);});
    submenu->addAction(QObject::tr("Show on top"), [this]() {capture(CaptureOption::ShowOnTop);});
    submenu->addAction(QObject::tr("DrawStyle settings"), [this]() {capture(CaptureOption::DrawStyle);});
    submenu->addAction(QObject::tr("Render settings"), [this]() {capture(CaptureOption::RenderSettings);});

    menu->addAction(QObject::tr("Capture all"), [this]() {capture(CaptureOption::All);});

    menu->addSeparator();

    menu->addAction(QObject::tr("Restore"), [this]() {apply();});

    submenu = menu->addMenu(QObject::tr("Partial restore"));
    submenu->addAction(QObject::tr("Clipping settings"), [this]() {apply(CaptureOption::Clippings);});
    submenu->addAction(QObject::tr("Camera settings"), [this]() {apply(CaptureOption::Camera);});
    submenu->addAction(QObject::tr("Visibilities"), [this]() {apply(CaptureOption::Visibilities);});
    submenu->addAction(QObject::tr("Show on top"), [this]() {apply(CaptureOption::ShowOnTop);});
    submenu->addAction(QObject::tr("DrawStyle settings"), [this]() {apply(CaptureOption::DrawStyle);});
    submenu->addAction(QObject::tr("Render settings"), [this]() {apply(CaptureOption::RenderSettings);});

    menu->addAction(QObject::tr("Restore all"), [this]() {apply(CaptureOption::All);});
}

bool ViewProviderSavedView::doubleClicked()
{
    apply();
    return true;
}

static const QByteArray &_captureIconTag()
{
    static QByteArray _tag("SavedView:capture");
    return _tag;
}

void ViewProviderSavedView::getExtraIcons(std::vector<std::pair<QByteArray, QPixmap> > &icons) const
{
    icons.emplace_back(_captureIconTag(), Gui::BitmapFactory().pixmap("SaveView"));
    inherited::getExtraIcons(icons);
}

bool ViewProviderSavedView::iconMouseEvent(QMouseEvent *ev, const QByteArray &tag)
{
    switch (ev->type()) {
    case QEvent::MouseButtonPress:
        if (ev->button() == Qt::LeftButton) {
            if (tag == _captureIconTag()) {
                capture();
                return true;
            }
        }
        break;
    case QEvent::MouseButtonRelease:
        if (ev->button() == Qt::RightButton) {
            QMenu menu;
            prepareMenu(&menu);
            menu.exec(QCursor::pos());
            return true;
        }
        break;
    default:
        break;
    }
    return inherited::iconMouseEvent(ev, tag);
}

QString ViewProviderSavedView::getToolTip(const QByteArray &tag) const
{
    if (tag == _captureIconTag())
        return QObject::tr("Double click to restore the captured view.\n"
                           "ALT + click this icon to capture current view.\n"
                           "ALT + right click for more actions.");
                           
    return inherited::getToolTip(tag);
}

bool ViewProviderSavedView::setEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default) {
        apply();
        return false;
    }
    return inherited::setEdit(ModNum);
}

// The per-view render settings a saved view deals in: what the view
// itself answers for, and nothing else. A Render_*/Light_*/Section_*
// property exists on a view only because somebody chose it -- the
// preference answers for every knob that has none -- so capturing exactly
// these is the rule that keeps a saved view from carrying, and later
// imposing, settings that were never part of this document.
//
// The machine-local ones are excluded at both ends. They are never saved
// with a view either, so a saved view carrying one could only have come
// from a file written before that, and applying it would hand this
// installation another machine's budget.
static bool isViewRenderProperty(const char *name)
{
    if (!name || Gui::isLocalRenderProperty(name))
        return false;
    return boost::starts_with(name, "Render_")
        || boost::starts_with(name, "Light_")
        || boost::starts_with(name, "Section_");
}

static std::vector<App::Property*> viewRenderProperties(View3DInventor *view)
{
    std::vector<App::Property*> res;
    std::vector<App::Property*> props;
    view->getPropertyList(props);
    for (auto prop : props) {
        if (!isViewRenderProperty(prop->getName()))
            continue;
        // What the view does not persist, a document must not either.
        if (prop->testStatus(App::Property::PropNoPersist))
            continue;
        res.push_back(prop);
    }
    return res;
}

// A document written before the settings became per-view states the
// section style and the backlight as global preferences under the clipping
// capture. Give them to the view as its own overrides rather than moving
// the reader's defaults, which is what restoring one used to do.
static void applyLegacyClipSettings(App::SavedView *obj, View3DInventor *view)
{
    struct Legacy {
        const char *stored;      // what the old capture wrote
        const char *property;    // the view property that answers for it now
        Base::Type type;
    };
    const Legacy legacy[] = {
        {"ClipFill", "Section_Fill", App::PropertyBool::getClassTypeId()},
        {"ClipConcave", "Section_Concave", App::PropertyBool::getClassTypeId()},
        {"ClipHatch", "Section_Hatch", App::PropertyBool::getClassTypeId()},
        {"ClipHatchScale", "Section_HatchScale", App::PropertyFloat::getClassTypeId()},
        {"ClipHatchTexture", "Section_HatchTexture", App::PropertyString::getClassTypeId()},
        {"ClipGroup", "Section_FillGroup", App::PropertyBool::getClassTypeId()},
        // Both spellings: capture wrote BackLight*, apply read BackFaceLight*,
        // so the backlight has never actually been restored by a saved view.
        {"BackLight", "Light_EnableBacklight", App::PropertyBool::getClassTypeId()},
        {"BackFaceLight", "Light_EnableBacklight", App::PropertyBool::getClassTypeId()},
        {"BackLightColor", "Light_BacklightColor", App::PropertyColor::getClassTypeId()},
        {"BackFaceLightColor", "Light_BacklightColor", App::PropertyColor::getClassTypeId()},
        {"BackLightIntensity", "Light_BacklightIntensity", App::PropertyFloat::getClassTypeId()},
        {"BackFaceLightIntensity", "Light_BacklightIntensity", App::PropertyFloat::getClassTypeId()},
    };
    for (const auto &v : legacy) {
        auto stored = obj->getPropertyByName(v.stored);
        if (!stored || stored->getTypeId() != v.type)
            continue;
        auto p = view->getPropertyByName(v.property);
        if (!p)
            p = view->addDynamicProperty(v.type.getName(), v.property,
                                         boost::starts_with(v.property, "Light_")
                                             ? "Light" : "Section");
        if (p && p->getTypeId() == v.type)
            p->Paste(*stored);
    }
}

void ViewProviderSavedView::apply(CaptureOptions options)
{
    try {
        App::AutoTransaction guard("Apply view", false, true);

        auto obj = Base::freecad_dynamic_cast<App::SavedView>(getObject());
        if (!obj)
            THROWM(Base::RuntimeError, "Not object attached")
        auto view = Base::freecad_dynamic_cast<View3DInventor>(getActiveView());
        if (!view)
            THROWM(Base::RuntimeError, "No 3D view")

        checkOptions(obj, options);

        if (options & CaptureOption::Clippings) {
            // What a document written before the per-view settings still
            // carries here: the section style and the backlight, captured
            // then as global preferences. Restoring one used to move the
            // reader's own defaults; it becomes an override on this view
            // instead. ClipShowPlane and ClipPlaneSize are dropped -- they
            // size and show the dragger, which is a tool and not part of
            // how the model is meant to look.
            applyLegacyClipSettings(obj, view);

            Base::Vector3d posX, posY, posZ, pos;
            Base::Rotation rot;
            bool enableX = false, enableY = false, enableZ = false, enable = false;
            if (auto prop = obj->getClippingProperty<App::PropertyBool>("ClipEnableX"))
                enableX = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyVector>("ClipPositionX"))
                posX = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyBool>("ClipEnableY"))
                enableY = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyVector>("ClipPositionY"))
                posY = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyBool>("ClipEnableZ"))
                enableZ = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyVector>("ClipPositionZ"))
                posZ = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyBool>("ClipEnableCustom"))
                enable = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyVector>("ClipPositionCustom"))
                pos = prop->getValue();
            if (auto prop = obj->getClippingProperty<App::PropertyRotation>("ClipRotationCustom"))
                rot = prop->getValue();
            Clipping::restoreClipPlanes(view, posX, enableX, posY, enableY, posZ, enableZ, Base::Placement(pos, rot), enable);
        }

        if (options & CaptureOption::Camera) {
            if (auto prop = obj->getCameraProperty<App::PropertyString>("CameraSettings")) {
                try {
                    if (boost::starts_with(prop->getStrValue(), "SetCamera "))
                        view->setCamera(prop->getValue()+10, 400);
                } catch (Base::Exception &e) {
                    e.ReportException();
                }
            }
        }

        if (options & CaptureOption::DrawStyle) {
            int count = 0;
            for (const auto &name : obj->getDynamicPropertyNames()) {
                if (!boost::equals(name, "DrawStyle")
                        && !boost::starts_with(name, "Shadow_")
                        && !boost::starts_with(name, "HiddenLine_"))
                    continue;
                if (auto prop = obj->getPropertyByName(name.c_str())) {
                    auto p = view->getPropertyByName(name.c_str());
                    if (!p)
                        p = view->addDynamicProperty(prop->getTypeId().getName(),
                                                     prop->getName(),
                                                     prop->getGroup(),
                                                     prop->getDocumentation());
                    if (!p || p->getTypeId() != prop->getTypeId())
                        continue;
                    p->Paste(*prop);
                    ++count;
                }
            }
        }

        if (options & CaptureOption::RenderSettings) {
            for (const auto &name : obj->getDynamicPropertyNames()) {
                if (!isViewRenderProperty(name.c_str()))
                    continue;
                auto prop = obj->getPropertyByName(name.c_str());
                if (!prop)
                    continue;
                auto p = view->getPropertyByName(name.c_str());
                if (!p)
                    p = view->addDynamicProperty(prop->getTypeId().getName(),
                                                 name.c_str(),
                                                 prop->getGroup(),
                                                 prop->getDocumentation());
                if (!p || p->getTypeId() != prop->getTypeId())
                    continue;
                p->Paste(*prop);
            }
        }

        if (options & CaptureOption::Visibilities) {
            if (auto prop = obj->getVisibilityProperty<App::PropertyStringList>("Visibilities")) {
                App::DocumentObject *lastObject = nullptr;
                App::Document *doc = obj->getDocument();
                for (const auto &s : prop->getValues()) {
                    const char *value = s.c_str();
                    bool visible = !boost::starts_with(value, "*");
                    if (!visible)
                        ++value;
                    if (boost::starts_with(value, ".")) {
                        if (lastObject)
                            lastObject->setElementVisible(value+1, visible);
                        continue;
                    }
                    const char *dot = strchr(value, '.');
                    if (dot) {
                        auto parent = doc->getObject(std::string(value, dot-value).c_str());
                        if (parent)
                            parent->setElementVisible(dot+1, visible);
                        continue;
                    }
                    lastObject = doc->getObject(value);
                    if (lastObject)
                        lastObject->Visibility.setValue(visible);
                }
            }
        }

        if (options & CaptureOption::ShowOnTop) {
            if (auto prop = obj->getVisibilityProperty<App::PropertyStringList>("ShowOnTops")) {
                view->getViewer()->clearGroupOnTop(true);
                const char *docName = obj->getDocument()->getName();
                for (const auto &path : prop->getValues()) {
                    size_t pos = path.find('.');
                    if (pos == std::string::npos)
                        continue;
                    view->getViewer()->checkGroupOnTop(
                            SelectionChanges(SelectionChanges::AddSelection,
                                             docName,
                                             path.substr(0, pos).c_str(),
                                             path.c_str() + pos + 1),
                            true);
                }
            }
        }
        Command::updateActive();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void ViewProviderSavedView::checkOptions(App::SavedView *obj, CaptureOptions &options) const
{
    if (options & CaptureOption::Default) {
        if (obj->SaveClippings.getValue())
            options |= CaptureOption::Clippings;
        if (obj->SaveCamera.getValue())
            options |= CaptureOption::Camera;
        if (obj->SaveVisibilities.getValue())
            options |= CaptureOption::Visibilities;
        if (obj->SaveDrawStyleSettings.getValue())
            options |= CaptureOption::DrawStyle;
        if (obj->SaveShowOnTop.getValue())
            options |= CaptureOption::ShowOnTop;
        if (obj->SaveRenderSettings.getValue())
            options |= CaptureOption::RenderSettings;
    }
}

void ViewProviderSavedView::capture(CaptureOptions options) 
{
    try {
        App::AutoTransaction guard("Capture view");

        auto obj = Base::freecad_dynamic_cast<App::SavedView>(getObject());
        if (!obj)
            THROWM(Base::RuntimeError, "Not object attached")
        auto view = Base::freecad_dynamic_cast<View3DInventor>(getActiveView());
        if (!view)
            THROWM(Base::RuntimeError, "No 3D view")

        checkOptions(obj, options);

        if (options & CaptureOption::Clippings) {
            obj->SaveClippings.setValue(true);

            Base::Vector3d posX, posY, posZ;
            Base::Placement pla;
            Base::Rotation rotate;
            bool enableX = false, enableY = false, enableZ = false, enableClip = false;
            Clipping::getClipPlanes(view,
                                    posX, enableX,
                                    posY, enableY,
                                    posZ, enableZ,
                                    pla, enableClip);
            obj->getClippingProperty<App::PropertyBool>("ClipEnableX", true)->setValue(enableX);
            obj->getClippingProperty<App::PropertyBool>("ClipEnableY", true)->setValue(enableY);
            obj->getClippingProperty<App::PropertyBool>("ClipEnableZ", true)->setValue(enableZ);
            obj->getClippingProperty<App::PropertyBool>("ClipEnable", true)->setValue(enableClip);
            obj->getClippingProperty<App::PropertyVector>("ClipPositionX", true)->setValue(posX);
            obj->getClippingProperty<App::PropertyVector>("ClipPositionY", true)->setValue(posY);
            obj->getClippingProperty<App::PropertyVector>("ClipPositionZ", true)->setValue(posZ);
            obj->getClippingProperty<App::PropertyVector>("ClipPosition", true)->setValue(pla.getPosition());
            obj->getClippingProperty<App::PropertyRotation>("ClipRotation", true)->setValue(pla.getRotation());
        }

        if (options & CaptureOption::Camera) {
            obj->SaveCamera.setValue(true);
            const char *cameraSettings = nullptr;
            view->onMsg("GetCamera", &cameraSettings);
            if (cameraSettings) {
                std::string s;
                getDocument()->saveCameraSettings(cameraSettings, &s);
                obj->getCameraProperty<App::PropertyString>("CameraSettings", true)->setValue(s);
            }
        }

        if (options & CaptureOption::DrawStyle) {
            obj->SaveDrawStyleSettings.setValue(true);
            std::vector<App::Property*> props;
            view->getPropertyList(props);
            for (auto prop : props) {
                if (!prop->getName())
                    continue;
                if (!boost::equals(prop->getName(), "DrawStyle")
                        && !boost::starts_with(prop->getName(), "Shadow_")
                        && !boost::starts_with(prop->getName(), "HiddenLine_"))
                    continue;
                auto p = obj->getProperty(prop->getTypeId(), prop->getName(), prop->getGroup(), true);
                p->Paste(*prop);
            }
        }

        if (options & CaptureOption::RenderSettings) {
            obj->SaveRenderSettings.setValue(true);
            for (auto prop : viewRenderProperties(view)) {
                auto p = obj->getProperty(prop->getTypeId(), prop->getName(),
                                          "RenderSettings", true);
                p->Paste(*prop);
            }
        }

        if (options & CaptureOption::Visibilities) {
            obj->SaveVisibilities.setValue(true);
            std::vector<std::string> values;
            std::ostringstream ss;
            std::string childName;
            for (auto o : obj->getDocument()->getObjects()) {
                ss.str("");
                if (!o->Visibility.getValue())
                    ss << "*";
                ss << o->getNameInDocument();
                values.push_back(ss.str());
                if (o->hasExtension(App::GroupExtension::getExtensionClassTypeId()))
                    continue;
                for (auto sub : o->getSubObjects(App::DocumentObject::GS_SELECT)) {
                    App::DocumentObject *parent = nullptr;
                    childName.clear();
                    auto sobj = o->resolve(sub.c_str(),&parent,&childName);
                    int vis;
                    if(!sobj || !parent
                            || parent->getDocument() != obj->getDocument()
                            || (vis=parent->isElementVisible(childName.c_str()))<0)
                        continue;
                    ss.str("");
                    if (!vis)
                        ss << "*";
                    if (parent != o)
                        ss << parent->getNameInDocument();
                    ss << "." << childName;
                    values.push_back(ss.str());
                }
            }
            obj->getVisibilityProperty<App::PropertyStringList>("Visibilities", true)->setValues(std::move(values));
        }

        if (options & CaptureOption::ShowOnTop) {
            obj->SaveShowOnTop.setValue(true);
            std::vector<std::string> values;
            for (const auto &objT : view->getViewer()->getObjectsOnTop())
                values.push_back(objT.getSubNameNoElement(true));
            obj->getVisibilityProperty<App::PropertyStringList>("ShowOnTops", true)->setValues(std::move(values));
        }
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

PyObject *ViewProviderSavedView::getPyObject() {
    if (!pyViewObject)
        pyViewObject = new ViewProviderSavedViewPy(this);
    pyViewObject->IncRef();
    return pyViewObject;
}

void ViewProviderSavedView::finishRestoring()
{
    if (auto obj = Base::freecad_dynamic_cast<App::SavedView>(getObject())) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyEnumeration>(
                    obj->getPropertyByName("DrawStyle"))) {
            prop->setEnums(Gui::drawStyleNames());
        }
    }
    inherited::finishRestoring();
}
