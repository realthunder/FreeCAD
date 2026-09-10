/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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
#include <algorithm>

#ifndef _PreComp_
# include <sstream>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/events/SoMouseButtonEvent.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
# include <QApplication>
# include <QDialog>
# include <QDomDocument>
# include <QDomElement>
# include <QFile>
# include <QFileInfo>
# include <QFont>
# include <QFontMetrics>
# include <QImageReader>
# include <QMessageBox>
# include <QPainter>
# include <QPointer>
# include <QTextStream>
#endif

#include <boost/algorithm/string.hpp>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>
#include <App/ComplexGeoDataPy.h>
#include <App/GeoFeature.h>
#include <App/MeasureDistance.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/DocumentObserver.h>
#include <App/AutoTransaction.h>
#include <App/Part.h>
#include <App/Link.h>
#include <App/MeasureDistance.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Base/Tools.h>
#include <Base/Tools2D.h>

#include "Action.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "CommandT.h"
#include "Control.h"
#include "Clipping.h"
#include "DemoMode.h"
#include "DlgIconBrowser.h"
#include "DlgSettingsImageImp.h"
#include "Document.h"
#include "FileDialog.h"
#include "ImageView.h"
#include "Macro.h"
#include "MainWindow.h"
#include "MouseSelection.h"
#include "NavigationStyle.h"
#include "OverlayParams.h"
#include "OverlayManager.h"
#include "PieMenu.h"
#include "SceneInspector.h"
#include "Selection.h"
#include "SelectionObject.h"
#include "SelectionView.h"
#include "SoAxisCrossKit.h"
#include "ShadingOptions.h"
#include "SoFCOffscreenRenderer.h"
#include "SoFCUnifiedSelection.h"
#include "TaskRenderSettings.h"
#include "TextureMapping.h"
#include "Tools.h"
#include "Tree.h"
#include "TreeParams.h"
#include "Utilities.h"
#include "View.h"
#include "View3DInventor.h"
#include "ViewArea.h"
#include "View3DInventorViewer.h"
#include "ViewParams.h"
#include "ViewProviderMeasureDistance.h"
#include "ViewProviderGeometryObject.h"
#include "WaitCursor.h"


using namespace Gui;
using Gui::Dialog::DlgSettingsImageImp;
namespace sp = std::placeholders;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

DEF_STD_CMD_AC(StdOrthographicCamera)

StdOrthographicCamera::StdOrthographicCamera()
  : Command("Std_OrthographicCamera")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Orthographic view");
    sToolTipText  = QT_TR_NOOP("Switches to orthographic view mode");
    sWhatsThis    = "Std_OrthographicCamera";
    sStatusTip    = QT_TR_NOOP("Switches to orthographic view mode");
    sPixmap       = "view-isometric";
    sAccel        = "V, O";
    eType         = Alter3DView;
}

void StdOrthographicCamera::activated(int iMsg)
{
    if (iMsg == 1) {
        auto view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
        SoCamera* cam = view->getViewer()->getSoRenderManager()->getCamera();
        if (!cam || cam->getTypeId() != SoOrthographicCamera::getClassTypeId())

            doCommand(Command::Gui,"Gui.activeDocument().activeView().setCameraType(\"Orthographic\")");
    }
}

bool StdOrthographicCamera::isActive()
{
    auto view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        // update the action group if needed
        bool check = _pcAction->isChecked();
        SoCamera* cam = view->getViewer()->getSoRenderManager()->getCamera();
        bool mode = cam ? cam->getTypeId() == SoOrthographicCamera::getClassTypeId() : false;

        if (mode != check)
            _pcAction->setChecked(mode);
        return true;
    }

    return false;
}

Action * StdOrthographicCamera::createAction()
{
    Action *pcAction = Command::createAction();
    pcAction->setCheckable(true);
    return pcAction;
}

DEF_STD_CMD_AC(StdPerspectiveCamera)

StdPerspectiveCamera::StdPerspectiveCamera()
  : Command("Std_PerspectiveCamera")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Perspective view");
    sToolTipText  = QT_TR_NOOP("Switches to perspective view mode");
    sWhatsThis    = "Std_PerspectiveCamera";
    sStatusTip    = QT_TR_NOOP("Switches to perspective view mode");
    sPixmap       = "view-perspective";
    sAccel        = "V, P";
    eType         = Alter3DView;
}

void StdPerspectiveCamera::activated(int iMsg)
{
    if (iMsg == 1) {
        View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
        SoCamera* cam = view->getViewer()->getSoRenderManager()->getCamera();
        if (!cam || cam->getTypeId() != SoPerspectiveCamera::getClassTypeId())

            doCommand(Command::Gui,"Gui.activeDocument().activeView().setCameraType(\"Perspective\")");
    }
}

bool StdPerspectiveCamera::isActive()
{
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        // update the action group if needed
        bool check = _pcAction->isChecked();
        SoCamera* cam = view->getViewer()->getSoRenderManager()->getCamera();
        bool mode = cam ? cam->getTypeId() == SoPerspectiveCamera::getClassTypeId() : false;

        if (mode != check)
            _pcAction->setChecked(mode);

        return true;
    }

    return false;
}

Action * StdPerspectiveCamera::createAction()
{
    Action *pcAction = Command::createAction();
    pcAction->setCheckable(true);
    return pcAction;
}

//===========================================================================

// The two commands below are provided for convenience so that they can be bound
// to a button of a space mouse

//===========================================================================
// Std_ViewSaveCamera
//===========================================================================

DEF_3DV_CMD(StdCmdViewSaveCamera)

StdCmdViewSaveCamera::StdCmdViewSaveCamera()
  : Command("Std_ViewSaveCamera")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Save current camera");
    sToolTipText  = QT_TR_NOOP("Save current camera settings");
    sStatusTip    = QT_TR_NOOP("Save current camera settings");
    sWhatsThis    = "Std_ViewSaveCamera";
    eType         = Alter3DView;
}

void StdCmdViewSaveCamera::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    auto view = qobject_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow());
    if (view) {
        view->getViewer()->saveHomePosition();
    }
}

//===========================================================================
// Std_ViewRestoreCamera
//===========================================================================
DEF_3DV_CMD(StdCmdViewRestoreCamera)

StdCmdViewRestoreCamera::StdCmdViewRestoreCamera()
  : Command("Std_ViewRestoreCamera")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Restore saved camera");
    sToolTipText  = QT_TR_NOOP("Restore saved camera settings");
    sStatusTip    = QT_TR_NOOP("Restore saved camera settings");
    sWhatsThis    = "Std_ViewRestoreCamera";
    eType         = Alter3DView;
}

void StdCmdViewRestoreCamera::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    auto view = qobject_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow());
    if (view) {
        view->getViewer()->resetToHomePosition();
    }
}

//===========================================================================
// Std_FreezeViews
//===========================================================================
class StdCmdFreezeViews : public Gui::Command
{
public:
    StdCmdFreezeViews();
    ~StdCmdFreezeViews() override = default;
    const char* className() const override
    { return "StdCmdFreezeViews"; }

    void setShortcut (const QString &) override;
    QString getShortcut() const override;

protected:
    void activated(int iMsg) override;
    bool isActive() override;
    Action * createAction() override;
    void languageChange() override;

private:
    void onSaveViews();
    void onRestoreViews();

private:
    const int maxViews{50};
    int savedViews{0};
    int offset{0};
    QAction* saveView{nullptr};
    QAction* freezeView{nullptr};
    QAction* clearView{nullptr};
    QAction* separator{nullptr};
};

StdCmdFreezeViews::StdCmdFreezeViews()
  : Command("Std_FreezeViews")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Freeze display");
    sToolTipText  = QT_TR_NOOP("Freezes the current view position");
    sWhatsThis    = "Std_FreezeViews";
    sStatusTip    = QT_TR_NOOP("Freezes the current view position");
    sAccel        = "Shift+F";
    eType         = Alter3DView;
}

Action * StdCmdFreezeViews::createAction()
{
    auto pcAction = new ActionGroup(this, getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    // add the action items
    saveView = pcAction->addAction(QObject::tr("Save views..."));
    saveView->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    QAction* loadView = pcAction->addAction(QObject::tr("Load views..."));
    loadView->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    pcAction->addAction(QStringLiteral(""))->setSeparator(true);
    freezeView = pcAction->addAction(QObject::tr("Freeze view"));
    freezeView->setShortcut(QString::fromUtf8(getAccel()));
    freezeView->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    clearView = pcAction->addAction(QObject::tr("Clear views"));
    clearView->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    separator = pcAction->addAction(QStringLiteral(""));
    separator->setSeparator(true);
    offset = pcAction->actions().count();

    // allow up to 50 views
    for (int i=0; i<maxViews; i++)
        pcAction->addAction(QStringLiteral(""))->setVisible(false);

    return pcAction;
}

void StdCmdFreezeViews::setShortcut(const QString &shortcut)
{
    if (freezeView)
        freezeView->setShortcut(shortcut);
}

QString StdCmdFreezeViews::getShortcut() const
{
    if (freezeView)
        return freezeView->shortcut().toString();
    return Command::getShortcut();
}

void StdCmdFreezeViews::activated(int iMsg)
{
    auto pcAction = qobject_cast<ActionGroup*>(_pcAction);

    if (iMsg == 0) {
        onSaveViews();
    }
    else if (iMsg == 1) {
        onRestoreViews();
    }
    else if (iMsg == 3) {
        // Create a new view
        const char* ppReturn=nullptr;
        getGuiApplication()->sendMsgToActiveView("GetCamera",&ppReturn);

        QList<QAction*> acts = pcAction->actions();
        int index = 0;
        for (QList<QAction*>::ConstIterator it = acts.begin()+offset; it != acts.end(); ++it, index++) {
            if (!(*it)->isVisible()) {
                savedViews++;
                QString viewnr = QString(QObject::tr("Restore view &%1")).arg(index+1);
                (*it)->setText(viewnr);
                (*it)->setToolTip(QString::fromUtf8(ppReturn));
                (*it)->setVisible(true);
                if (index < 9) {
                    int accel = Qt::CTRL+Qt::Key_1;
                    (*it)->setShortcut(accel+index);
                }
                break;
            }
        }
    }
    else if (iMsg == 4) {
        savedViews = 0;
        QList<QAction*> acts = pcAction->actions();
        for (QList<QAction*>::ConstIterator it = acts.begin()+offset; it != acts.end(); ++it)
            (*it)->setVisible(false);
    }
    else if (iMsg >= offset) {
        // Activate a view
        QList<QAction*> acts = pcAction->actions();
        QString data = acts[iMsg]->toolTip();
        QString send = QStringLiteral("SetCamera %1").arg(data);
        getGuiApplication()->sendMsgToActiveView(send.toUtf8());
    }
}

void StdCmdFreezeViews::onSaveViews()
{
    // Save the views to an XML file
    QString fn = FileDialog::getSaveFileName(getMainWindow(), QObject::tr("Save frozen views"),
                                             QString(), QStringLiteral("%1 (*.cam)").arg(QObject::tr("Frozen views")));
    if (fn.isEmpty())
        return;
    QFile file(fn);
    if (file.open(QFile::WriteOnly))
    {
        QTextStream str(&file);
        ActionGroup* pcAction = qobject_cast<ActionGroup*>(_pcAction);
        QList<QAction*> acts = pcAction->actions();
        str << "<?xml version='1.0' encoding='utf-8'?>\n"
            << "<FrozenViews SchemaVersion=\"1\">\n";
        str << "  <Views Count=\"" << savedViews <<"\">\n";

        for (QList<QAction*>::ConstIterator it = acts.begin()+offset; it != acts.end(); ++it) {
            if ( !(*it)->isVisible() )
                break;
            QString data = (*it)->toolTip();

            // remove the first line because it's a comment like '#Inventor V2.1 ascii'
            QString viewPos;
            if (!data.isEmpty()) {
                QStringList lines = data.split(QStringLiteral("\n"));
                if (lines.size() > 1) {
                    lines.pop_front();
                }
                viewPos = lines.join(QStringLiteral(" "));
            }

            str << "    <Camera settings=\""
                << Base::Persistence::encodeAttribute(viewPos.toUtf8().constData()).c_str()
                << "\"/>\n";
        }

        str << "  </Views>\n";
        str << "</FrozenViews>\n";
    }
}

void StdCmdFreezeViews::onRestoreViews()
{
    // Should we clear the already saved views
    if (savedViews > 0) {
        int ret = QMessageBox::question(getMainWindow(), QObject::tr("Restore views"),
            QObject::tr("Importing the restored views would clear the already stored views.\n"
                        "Do you want to continue?"), QMessageBox::Yes|QMessageBox::Default,
                                                     QMessageBox::No|QMessageBox::Escape);
        if (ret!=QMessageBox::Yes)
            return;
    }

    // Restore the views from an XML file
    QString fn = FileDialog::getOpenFileName(getMainWindow(), QObject::tr("Restore frozen views"),
                                             QString(), QStringLiteral("%1 (*.cam)").arg(QObject::tr("Frozen views")));
    if (fn.isEmpty())
        return;
    QFile file(fn);
    if (!file.open(QFile::ReadOnly)) {
        QMessageBox::critical(getMainWindow(), QObject::tr("Restore views"),
            QObject::tr("Cannot open file '%1'.").arg(fn));
        return;
    }

    QDomDocument xmlDocument;
    QString errorStr;
    int errorLine;
    int errorColumn;

    // evaluate the XML content
    if (!xmlDocument.setContent(&file, true, &errorStr, &errorLine, &errorColumn)) {
        std::cerr << "Parse error in XML content at line " << errorLine
                  << ", column " << errorColumn << ": "
                  << (const char*)errorStr.toUtf8() << std::endl;
        return;
    }

    // get the root element
    QDomElement root = xmlDocument.documentElement();
    if (root.tagName() != QStringLiteral("FrozenViews")) {
        std::cerr << "Unexpected XML structure" << std::endl;
        return;
    }

    bool ok;
    int scheme = root.attribute(QStringLiteral("SchemaVersion")).toInt(&ok);
    if (!ok) return;
    // SchemeVersion "1"
    if (scheme == 1) {
        // read the views, ignore the attribute 'Count'
        QDomElement child = root.firstChildElement(QStringLiteral("Views"));
        QDomElement views = child.firstChildElement(QStringLiteral("Camera"));
        QStringList cameras;
        while (!views.isNull()) {
            QString setting = views.attribute(QStringLiteral("settings"));
            cameras << setting;
            views = views.nextSiblingElement(QStringLiteral("Camera"));
        }

        // use this rather than the attribute 'Count' because it could be
        // changed from outside
        int ct = cameras.count();
        ActionGroup* pcAction = qobject_cast<ActionGroup*>(_pcAction);
        QList<QAction*> acts = pcAction->actions();

        int numRestoredViews = std::min<int>(ct, acts.size()-offset);
        savedViews = numRestoredViews;

        if (numRestoredViews > 0)
            separator->setVisible(true);
        for(int i=0; i<numRestoredViews; i++) {
            QString setting = cameras[i];
            QString viewnr = QString(QObject::tr("Restore view &%1")).arg(i+1);
            acts[i+offset]->setText(viewnr);
            acts[i+offset]->setToolTip(setting);
            acts[i+offset]->setVisible(true);
            if ( i < 9 ) {
                int accel = Qt::CTRL+Qt::Key_1;
                acts[i+offset]->setShortcut(accel+i);
            }
        }

        // if less views than actions
        for (int index = numRestoredViews+offset; index < acts.count(); index++)
            acts[index]->setVisible(false);
    }
}

bool StdCmdFreezeViews::isActive()
{
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        saveView->setEnabled(savedViews > 0);
        freezeView->setEnabled(savedViews < maxViews);
        clearView->setEnabled(savedViews > 0);
        separator->setVisible(savedViews > 0);
        return true;
    }
    else {
        separator->setVisible(savedViews > 0);
    }

    return false;
}

void StdCmdFreezeViews::languageChange()
{
    Command::languageChange();

    if (!_pcAction)
        return;
    ActionGroup* pcAction = qobject_cast<ActionGroup*>(_pcAction);
    QList<QAction*> acts = pcAction->actions();
    acts[0]->setText(QObject::tr("Save views..."));
    acts[1]->setText(QObject::tr("Load views..."));
    acts[3]->setText(QObject::tr("Freeze view"));
    acts[4]->setText(QObject::tr("Clear views"));
    int index=1;
    for (QList<QAction*>::ConstIterator it = acts.begin()+5; it != acts.end(); ++it, index++) {
        if ((*it)->isVisible()) {
            QString viewnr = QString(QObject::tr("Restore view &%1")).arg(index);
            (*it)->setText(viewnr);
        }
    }
}

//===========================================================================
// Std_ClipPlaneDragger
//===========================================================================

class StdCmdClipPlaneDragger : public  Gui::CheckableCommand
{
public:
    StdCmdClipPlaneDragger();
    virtual const char* className() const
    { return "StdCmdClipPlaneDragger"; }
protected:
    virtual void setOption(bool checked) {
        // Whether the dragger is shown is a property of the view that shows
        // it, so that is where the choice lands. It is also the user's own
        // choice, made here rather than in the Clipping panel, so their
        // default follows it -- the same split the panel uses.
        Dialog::Clipping::setShowPlane(activeView(), checked);
        ViewParams::setShowClipPlane(checked);
    }
    virtual bool getOption() const {
        return Dialog::Clipping::showPlane(activeView());
    }
    static View3DInventor *activeView() {
        return Base::freecad_dynamic_cast<View3DInventor>(
                Application::Instance->activeView());
    }
};
StdCmdClipPlaneDragger::StdCmdClipPlaneDragger()
  : CheckableCommand("Std_ClipPlaneDragger")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Clip plane dragger");
    sToolTipText  = QT_TR_NOOP("Toggles clipping plane dragger");
    sWhatsThis    = "Std_ClipPlaneDragger";
    sStatusTip    = sMenuText;
    sAccel        = "C, D";
    eType         = Alter3DView;
}

//===========================================================================
// Std_ToggleClipPlane
//===========================================================================

DEF_STD_CMD_AC(StdCmdToggleClipPlane)

StdCmdToggleClipPlane::StdCmdToggleClipPlane()
  : Command("Std_ToggleClipPlane")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Clipping plane");
    sToolTipText  = QT_TR_NOOP("Toggles clipping plane for active view");
    sWhatsThis    = "Std_ToggleClipPlane";
    sStatusTip    = QT_TR_NOOP("Toggles clipping plane for active view");
    sPixmap       = "Std_ToggleClipPlane";
    sAccel        = "V, C";
    eType         = Alter3DView;
}

Action * StdCmdToggleClipPlane::createAction()
{
    Action *pcAction = (Action*)Command::createAction();
#if 0
    pcAction->setCheckable(true);
#endif
    return pcAction;
}

void StdCmdToggleClipPlane::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Dialog::Clipping::toggle();
}

bool StdCmdToggleClipPlane::isActive()
{
#if 0
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        Action* action = qobject_cast<Action*>(_pcAction);
        if (action->isChecked() != view->hasClippingPlane())
            action->setChecked(view->hasClippingPlane());
        return true;
    }
    else {
        Action* action = qobject_cast<Action*>(_pcAction);
        if (action->isChecked())
            action->setChecked(false);
        return false;
    }
#else
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    return view ? true : false;
#endif
}

//===========================================================================
// StdCmdDrawStyleBase
//===========================================================================

class StdCmdDrawStyleBase : public Command 
{
public:
    StdCmdDrawStyleBase(int idx, const char *title, const char *doc);

    virtual const char* className() const;

    static const char *cacheString(const char *prefix, const char *text)
    {
        static std::list<std::string> _cache;
        _cache.push_back(prefix);
        _cache.back() += text;
        boost::replace_all(_cache.back(), " ", "");
        return _cache.back().c_str();
    }

protected:
    bool isActive();
    virtual void activated(int iMsg);
    virtual Gui::Action *createAction();

};

StdCmdDrawStyleBase::StdCmdDrawStyleBase(int idx, const char *title, const char *doc)
    :Command(cacheString("Std_DrawStyle", title))
{
    sGroup        = "Standard-View";
    sMenuText     = title;
    sToolTipText  = doc;
    sStatusTip    = sToolTipText;
    sWhatsThis    = getName();
    sPixmap       = cacheString("DrawStyle", title);
    sAccel        = cacheString("V,", std::to_string(idx).c_str());
    eType         = Alter3DView;
}

const char* StdCmdDrawStyleBase::className() const
{
    return "StdCmdDrawStyleBase";
}

bool StdCmdDrawStyleBase::isActive()
{
    return Gui::Application::Instance->activeDocument();
}

void StdCmdDrawStyleBase::activated(int iMsg)
{
    // The style entries are checkable (StdCmdDrawStyle::createAction), so
    // a click arrives as a toggle: iMsg is the new check state, not a
    // sub-command index. Unchecking is not a state a viewer can be in —
    // there is always exactly one override mode — so it is ignored, and
    // the sync in updateIcon() puts the tick back.
    //
    // Only when the toggle is what invoked us, though. A script calling
    // Gui.runCommand("Std_DrawStyleShaded", 0) passes the same 0 with no
    // trigger source, and that has always meant "apply this style";
    // treating it as an untick would silently break every macro that
    // sets a draw style.
    if (iMsg == 0 && triggerSource() != TriggerNone)
        return;

    Gui::Document *doc = this->getActiveGuiDocument();
    if (!doc) return;
    auto activeView = doc->getActiveView();
    bool applyAll = !activeView || QApplication::queryKeyboardModifiers() == Qt::ControlModifier;

    doc->foreachView<View3DInventor>( [=](View3DInventor *view) {
        if(applyAll || view == activeView) {
            View3DInventorViewer *viewer = view->getViewer();
            if (viewer)
                viewer->setOverrideMode(sMenuText);
        }
    });
}

Gui::Action *StdCmdDrawStyleBase::createAction()
{
    Gui::Action *action = Command::createAction();
    if (!action)
        return action;

    // A repeat press of the shortcut -- the style the viewer is already
    // in. A checkable QAction unchecks itself on the way in and then
    // emits triggered, and activated() ignores the uncheck because a
    // viewer is always in exactly one mode. So the tick has to be put
    // back here, or the entry would sit unticked in a style it is in.
    //
    // Menu entries never arrive here: each carries its own widget action
    // (Gui::Action::addWidget) and only forwards toggled.
    QObject::connect(action->action(), &QAction::triggered, action, [this]() {
        auto act = getAction();
        if (!act || !act->action()->isCheckable() || act->isChecked())
            return;
        act->setChecked(true, true);
    });
    return action;
}

//===========================================================================
// StdCmdDrawStyle
//===========================================================================

class StdCmdDrawStyle : public GroupCommand
{
public:
    StdCmdDrawStyle();
    virtual const char* className() const {return "StdCmdDrawStyle";}
    void updateIcon(const MDIView *);
    /// Tick the entry for \a mode and clear the rest. Drives the menus
    /// through Action::actionChecked, and passes no_signal so restoring
    /// the tick does not invoke the command back.
    void syncChecked(const char *mode);

    virtual Action * createAction() {
        Action * action = GroupCommand::createAction();
        action->setCheckable(false);
        // Each style becomes a checkable entry, which ActionGroup::addTo
        // then renders as a radio button in both the tool button's
        // drop-down and the menu bar. Two reasons, and neither is
        // decoration: the menu stays open across a click, so styles can
        // be compared by walking the list with the 3D view visible behind
        // it; and the active one is legible at a glance instead of being
        // whichever entry happens to be highlighted. Gui::Action::
        // setCheckable is what rewires the action from triggered to
        // toggled, so it has to be that rather than QAction::setCheckable.
        for (auto cmd : getCommands()) {
            if (auto sub = cmd->getAction())
                sub->setCheckable(true);
        }
        // Pressing the active style's shortcut has to reach the command.
        // Under the strict policy QAction::activate() drops that press
        // before any signal, so the tick repair after it would never
        // run.
        if (auto group = qobject_cast<Gui::ActionGroup*>(action))
            group->setExclusiveOptional(true);
        // The renderer's shading options ride under the style list as a
        // popover section: they are not exclusive with each other, so
        // they cannot be entries in it (Gui/ShadingOptions.h). The menu
        // is built by ActionGroup::addTo -- for the tool button and for
        // the menu bar, and again whenever the toolbar is rebuilt -- so
        // the section is installed on first show of each one.
        if (auto group = qobject_cast<Gui::ActionGroup*>(action)) {
            QObject::connect(group, &Gui::ActionGroup::aboutToShow,
                             ShadingOptionsWidget::install);
        }
        return action;
    }
};

StdCmdDrawStyle::StdCmdDrawStyle()
  : GroupCommand("Std_DrawStyle")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Display style");
    sToolTipText  = QT_TR_NOOP("Change the display style and shading of the objects");
    sStatusTip    = QT_TR_NOOP("Change the display style and shading of the objects");
    sWhatsThis    = "Std_DrawStyle";
    eType         = 0;
    bCanLog       = false;
    // One override mode per viewer, so the list is a pick-one: this is
    // what makes ActionGroup render it as radio buttons rather than as
    // checkboxes, which would advertise combinations that do not exist.
    setExclusive(true);

    int i = 0;
    while(const char *title = drawStyleNameFromIndex(i++)) {
        addCommand(new StdCmdDrawStyleBase(i, title, drawStyleDocumentation(i-1)));
    }

    this->getGuiApplication()->signalActivateView.connect(std::bind(&StdCmdDrawStyle::updateIcon, this, sp::_1));
    this->getGuiApplication()->signalViewModeChanged.connect(
        [this](const MDIView *view) {
            if (view == Application::Instance->activeView())
                updateIcon(view);
        });
}

void StdCmdDrawStyle::syncChecked(const char *mode)
{
    const int active = drawStyleIndexFromName(mode);
    int i = -1;
    for (auto cmd : getCommands()) {
        ++i;
        if (auto sub = cmd->getAction())
            sub->setChecked(i == active, true);
    }
}

void StdCmdDrawStyle::updateIcon(const MDIView *view)
{
    if (!_pcAction)
        return;
    if (auto view3d = Base::freecad_dynamic_cast<const Gui::View3DInventor>(view)) {
        Gui::View3DInventorViewer *viewer = view3d->getViewer();
        if (!viewer)
            return;
        std::string mode(viewer->getOverrideMode());
        if (dynamic_cast<Gui::ActionGroup *>(_pcAction)) {
            int index = drawStyleIndexFromName(mode.c_str());;
            if (index >= 0) {
                _pcAction->setProperty("defaultAction", QVariant(index));
                setup(_pcAction);
            }
        }
        // The tick follows the viewer, not the click: the style also
        // changes from the Shading section's shadow switch, from a
        // restored document and from the console.
        syncChecked(mode.c_str());
    }
}

//===========================================================================
// Std_ToggleVisibility
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleVisibility)

StdCmdToggleVisibility::StdCmdToggleVisibility()
  : Command("Std_ToggleVisibility")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle visibility");
    sToolTipText  = QT_TR_NOOP("Toggles visibility");
    sStatusTip    = QT_TR_NOOP("Toggles visibility");
    sWhatsThis    = "Std_ToggleVisibility";
    sPixmap       = "Std_ToggleVisibility";
    sAccel        = "Space";
    eType         = Alter3DView | ViewTransaction;
}


void StdCmdToggleVisibility::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Selection().setVisible(SelectionSingleton::VisToggle);
}

bool StdCmdToggleVisibility::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_ToggleGroupVisibility
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleGroupVisibility)

StdCmdToggleGroupVisibility::StdCmdToggleGroupVisibility()
  : Command("Std_ToggleGroupVisibility")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle group visibility");
    sToolTipText  = QT_TR_NOOP("Toggles visibility of a group and all its nested children");
    sStatusTip    = sToolTipText;
    sWhatsThis    = "Std_ToggleGroupVisibility";
    eType         = Alter3DView;
}


void StdCmdToggleGroupVisibility::activated(int iMsg)
{
    Q_UNUSED(iMsg); 

    auto sels = Gui::Selection().getSelectionT(nullptr, ResolveMode::NoResolve);
    std::set<App::DocumentObject*> groups;
    for(auto &sel : sels) {
        auto sobj = sel.getSubObject();
        if(!sobj)
            continue;
        if(App::GeoFeatureGroupExtension::isNonGeoGroup(sobj))
            groups.insert(sobj);
    }

    for(auto it=sels.begin();it!=sels.end();) {
        auto &sel = *it;
        auto sobj = sel.getSubObject();
        if(!sobj || groups.count(App::GroupExtension::getGroupOfObject(sobj)))
            it = sels.erase(it);
        else
            ++it;
    }

    if(sels.empty())
        return;

    App::GroupExtension::ToggleNestedVisibility guard;
    Gui::Selection().setVisible(SelectionSingleton::VisToggle,sels);
}

bool StdCmdToggleGroupVisibility::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_ToggleVisibility
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleShowOnTop)

StdCmdToggleShowOnTop::StdCmdToggleShowOnTop()
  : Command("Std_ToggleShowOnTop")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle show on top");
    sToolTipText  = QT_TR_NOOP("Toggles whether to show the object on top");
    sStatusTip    = sToolTipText;
    sWhatsThis    = "Std_ToggleShowOnTop";
    sAccel        = "Ctrl+Shift+Space";
    eType         = Alter3DView;
}

void StdCmdToggleShowOnTop::activated(int iMsg)
{
    Q_UNUSED(iMsg); 

    auto gdoc = Application::Instance->activeDocument();
    if(!gdoc)
        return;
    auto view = Base::freecad_dynamic_cast<View3DInventor>(gdoc->getActiveView());
    if(!view)
        return;
    auto viewer = view->getViewer();

    std::set<App::SubObjectT> objs;

    std::vector<App::SubObjectT> sels;
    for(auto sel : Selection().getSelectionT(gdoc->getDocument()->getName(),ResolveMode::NoResolve)) {
        objs.insert(sel.normalized(App::SubObjectT::NormalizeOption::NoElement)).second;
    }

    if (Selection().hasPreselection()) {
        auto presel = Selection().getPreselection().Object.normalized(
                App::SubObjectT::NormalizeOption::NoElement);
        if (!objs.count(presel)) {
            objs.clear();
            objs.insert(presel);
        }
    }

    for (auto &sel : objs) {
        bool selected = viewer->isInGroupOnTop(sel);
        viewer->checkGroupOnTop(SelectionChanges(
                    selected?SelectionChanges::RmvSelection:SelectionChanges::AddSelection, sel),true);
    }
    if(objs.empty())
        viewer->clearGroupOnTop(true);
}

bool StdCmdToggleShowOnTop::isActive()
{
    auto gdoc = Application::Instance->activeDocument();
    return gdoc && Base::freecad_dynamic_cast<View3DInventor>(gdoc->getActiveView());
}

//===========================================================================
// Std_DisplayModeInView
//===========================================================================

/// One entry of the "Display mode in this view" group
/// (docs/CoinRetirement.md 5.9): writes a PATH-form entry -- this
/// occurrence and its subtree, the same selection set show-on-top acts
/// on -- into the active 3D view's ObjectDisplayModes property. A null
/// mode is the "clear all" entry. The four Class-A styles and "As Is"
/// cover the common gestures; an object's non-standard modes are
/// reachable through its DisplayModeInView property row, which lists
/// the provider's own enum.
class StdCmdObjDisplayModeBase : public Command
{
public:
    StdCmdObjDisplayModeBase(const char *name, const char *mode,
                             const char *title, const char *doc)
        : Command(name), modeName(mode)
    {
        sGroup        = "Standard-View";
        sMenuText     = title;
        sToolTipText  = doc;
        sStatusTip    = sToolTipText;
        sWhatsThis    = getName();
        eType         = Alter3DView;
    }

    virtual const char *className() const { return "StdCmdObjDisplayModeBase"; }

protected:
    virtual bool isActive()
    {
        auto gdoc = Application::Instance->activeDocument();
        return gdoc && Base::freecad_dynamic_cast<View3DInventor>(
                gdoc->getActiveView());
    }

    virtual void activated(int iMsg)
    {
        Q_UNUSED(iMsg);
        auto gdoc = Application::Instance->activeDocument();
        if (!gdoc)
            return;
        auto view = Base::freecad_dynamic_cast<View3DInventor>(
                gdoc->getActiveView());
        if (!view)
            return;

        if (!modeName) {
            if (view->ObjectDisplayModes.getSize())
                view->ObjectDisplayModes.setValues(
                        std::map<std::string, std::string>());
            return;
        }

        // The selection's full paths (NoResolve), preselection
        // standing in when it points elsewhere -- the same set
        // StdCmdToggleShowOnTop acts on.
        std::set<App::SubObjectT> objs;
        for (auto &sel : Selection().getSelectionT(
                    gdoc->getDocument()->getName(), ResolveMode::NoResolve))
            objs.insert(sel.normalized(
                        App::SubObjectT::NormalizeOption::NoElement));
        if (Selection().hasPreselection()) {
            auto presel = Selection().getPreselection().Object.normalized(
                    App::SubObjectT::NormalizeOption::NoElement);
            if (!objs.count(presel)) {
                objs.clear();
                objs.insert(presel);
            }
        }
        if (objs.empty())
            return;

        auto values = view->ObjectDisplayModes.getValues();
        bool changed = false;
        for (auto &objT : objs) {
            std::string key = objT.getSubNameNoElement(true);
            if (key.empty())
                continue;
            // Rooted path form even for a top-level object: the
            // trailing dot tells "Box." (this occurrence and its
            // subtree) from the bare "Box" (anywhere in the view) the
            // property row writes.
            if (key.find('.') == std::string::npos)
                key += '.';
            if (strcmp(modeName, "Use View Mode") == 0) {
                changed = values.erase(key) > 0 || changed;
            }
            else {
                auto &v = values[key];
                changed = changed || v != modeName;
                v = modeName;
            }
        }
        if (changed)
            view->ObjectDisplayModes.setValues(std::move(values));
    }

    const char *modeName;
};

class StdCmdDisplayModeInView : public GroupCommand
{
public:
    StdCmdDisplayModeInView()
        : GroupCommand("Std_DisplayModeInView")
    {
        sGroup        = "Standard-View";
        sMenuText     = QT_TR_NOOP("Display mode in this view");
        sToolTipText  = QT_TR_NOOP(
                "Override the display mode of the selected objects in the\n"
                "active 3D view only; other views keep their own look");
        sStatusTip    = sToolTipText;
        sWhatsThis    = "Std_DisplayModeInView";
        eType         = Alter3DView;
        bCanLog       = false;

        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModeUseView", "Use View Mode",
                QT_TR_NOOP("Use view mode"),
                QT_TR_NOOP("Remove the override of the selected objects in this view")));
        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModeAsIs", "As Is",
                QT_TR_NOOP("As is"),
                QT_TR_NOOP("Pin the selected objects to their own display mode,\n"
                           "escaping this view's draw style")));
        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModeFlatLines", "Flat Lines",
                QT_TR_NOOP("Flat lines"),
                QT_TR_NOOP("Show the selected objects shaded with their edges,\n"
                           "in this view only")));
        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModeShaded", "Shaded",
                QT_TR_NOOP("Shaded"),
                QT_TR_NOOP("Show the selected objects shaded without edges,\n"
                           "in this view only")));
        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModeWireframe", "Wireframe",
                QT_TR_NOOP("Wireframe"),
                QT_TR_NOOP("Show the selected objects as wireframe,\n"
                           "in this view only")));
        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModePoints", "Points",
                QT_TR_NOOP("Points"),
                QT_TR_NOOP("Show the selected objects as points,\n"
                           "in this view only")));
        addCommand(new StdCmdObjDisplayModeBase(
                "Std_ObjDisplayModeClearAll", nullptr,
                QT_TR_NOOP("Clear all in this view"),
                QT_TR_NOOP("Remove every per-object display mode override\n"
                           "of the active 3D view")));
    }

    virtual const char *className() const { return "StdCmdDisplayModeInView"; }
};

//===========================================================================
// Std_ToggleTransparency
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleTransparency)

StdCmdToggleTransparency::StdCmdToggleTransparency()
    : Command("Std_ToggleTransparency")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Toggle transparency");
    static std::string toolTip = std::string("<p>")
        + QT_TR_NOOP("Toggles transparency of the selected objects. You can also fine tune transparency "
            "value in the Appearance taskbox (right click an object in the tree, Appearance).")
        + "</p>";
    sToolTipText = toolTip.c_str();
    sStatusTip = sToolTipText;
    sWhatsThis = "Std_ToggleTransparency";
    sPixmap = "Std_ToggleTransparency";
    sAccel = "V,T";
    eType = Alter3DView | ViewTransaction;
}

void StdCmdToggleTransparency::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    getActiveGuiDocument()->openCommand(QT_TRANSLATE_NOOP("Command", "Toggle transparency"));

    std::vector<Gui::SelectionSingleton::SelObj> sels = Gui::Selection().getCompleteSelection();

    std::vector<Gui::ViewProvider*> viewsToToggle = {};

    for (Gui::SelectionSingleton::SelObj& sel : sels) {
        App::DocumentObject* obj = sel.pObject;
        if (!obj)
            continue;

        if (!dynamic_cast<App::Part*>(obj) && !dynamic_cast<App::LinkGroup*>(obj)) {
            Gui::ViewProvider* view = Application::Instance->getDocument(sel.pDoc)->getViewProvider(obj);
            App::Property* prop = view->getPropertyByName("Transparency");
            if (prop && prop->getTypeId().isDerivedFrom(App::PropertyInteger::getClassTypeId())) {
                viewsToToggle.push_back(view);
            }
        }
        else {
            std::function<void(App::DocumentObject*, std::vector<Gui::ViewProvider*>&)> addSubObjects =
                [&addSubObjects](App::DocumentObject* obj, std::vector<Gui::ViewProvider*>& viewsToToggle) {
                if (!dynamic_cast<App::Part*>(obj) && !dynamic_cast<App::LinkGroup*>(obj)) {
                    App::Document* doc = obj->getDocument();
                    Gui::ViewProvider* view = Application::Instance->getDocument(doc)->getViewProvider(obj);
                    App::Property* prop = view->getPropertyByName("Transparency");
                    if (prop && prop->getTypeId().isDerivedFrom(App::PropertyInteger::getClassTypeId())
                        && std::find(viewsToToggle.begin(), viewsToToggle.end(), view) == viewsToToggle.end()) {
                        viewsToToggle.push_back(view);
                    }
                }
                else {
                    for (App::DocumentObject* subobj : obj->getOutList()) {
                        addSubObjects(subobj, viewsToToggle);
                    }
                }
            };

            addSubObjects(obj, viewsToToggle);
        }
    }

    bool oneTransparent = false;
    for (auto* view : viewsToToggle) {
        App::Property* prop = view->getPropertyByName("Transparency");
        if (prop && prop->getTypeId().isDerivedFrom(App::PropertyInteger::getClassTypeId())) {
            auto* transparencyProp = dynamic_cast<App::PropertyInteger*>(prop);
            int transparency = transparencyProp->getValue();
            if (transparency != 0) {
                oneTransparent = true;
            }
        }
    }

    int transparency = oneTransparent ? 0 : 70;

    for (auto* view : viewsToToggle) {
        App::Property* prop = view->getPropertyByName("Transparency");
        if (prop && prop->getTypeId().isDerivedFrom(App::PropertyInteger::getClassTypeId())) {
            auto* transparencyProp = dynamic_cast<App::PropertyInteger*>(prop);
            transparencyProp->setValue(transparency);
        }
    }

    getActiveGuiDocument()->commitCommand();
}

bool StdCmdToggleTransparency::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_ToggleSelectability
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleSelectability)

StdCmdToggleSelectability::StdCmdToggleSelectability()
  : Command("Std_ToggleSelectability")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle selectability");
    sToolTipText  = QT_TR_NOOP("Toggles the property of the objects to get selected in the 3D-View");
    sStatusTip    = QT_TR_NOOP("Toggles the property of the objects to get selected in the 3D-View");
    sWhatsThis    = "Std_ToggleSelectability";
    sPixmap       = "view-unselectable";
    eType         = Alter3DView | ViewTransaction;
}

void StdCmdToggleSelectability::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // go through all documents
    const std::vector<App::Document*> docs = App::GetApplication().getDocuments();
    for (std::vector<App::Document*>::const_iterator it = docs.begin(); it != docs.end(); ++it) {
        Document *pcDoc = Application::Instance->getDocument(*it);
        std::vector<App::DocumentObject*> sel = Selection().getObjectsOfType
            (App::DocumentObject::getClassTypeId(), (*it)->getName());


        for (std::vector<App::DocumentObject*>::const_iterator ft=sel.begin();ft!=sel.end();++ft) {
            ViewProvider *pr = pcDoc->getViewProviderByName((*ft)->getNameInDocument());
            if (pr && pr->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())){
                if (static_cast<ViewProviderGeometryObject*>(pr)->Selectable.getValue())
                    doCommand(Gui,"Gui.getDocument(\"%s\").getObject(\"%s\").Selectable=False"
                                 , (*it)->getName(), (*ft)->getNameInDocument());
                else
                    doCommand(Gui,"Gui.getDocument(\"%s\").getObject(\"%s\").Selectable=True"
                                 , (*it)->getName(), (*ft)->getNameInDocument());
            }
        }
    }
}

bool StdCmdToggleSelectability::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_ShowSelection
//===========================================================================
DEF_STD_CMD_A(StdCmdShowSelection)

StdCmdShowSelection::StdCmdShowSelection()
  : Command("Std_ShowSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Show selection");
    sToolTipText  = QT_TR_NOOP("Show all selected objects");
    sStatusTip    = QT_TR_NOOP("Show all selected objects");
    sWhatsThis    = "Std_ShowSelection";
    sPixmap       = "Std_ShowSelection";
    eType         = Alter3DView;
}

void StdCmdShowSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Selection().setVisible(SelectionSingleton::VisShow);
}

bool StdCmdShowSelection::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_HideSelection
//===========================================================================
DEF_STD_CMD_A(StdCmdHideSelection)

StdCmdHideSelection::StdCmdHideSelection()
  : Command("Std_HideSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Hide selection");
    sToolTipText  = QT_TR_NOOP("Hide all selected objects");
    sStatusTip    = QT_TR_NOOP("Hide all selected objects");
    sWhatsThis    = "Std_HideSelection";
    sPixmap       = "Std_HideSelection";
    eType         = Alter3DView;
}

void StdCmdHideSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Selection().setVisible(SelectionSingleton::VisHide);
}

bool StdCmdHideSelection::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_SelectVisibleObjects
//===========================================================================
DEF_STD_CMD_A(StdCmdSelectVisibleObjects)

StdCmdSelectVisibleObjects::StdCmdSelectVisibleObjects()
  : Command("Std_SelectVisibleObjects")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Select visible objects");
    sToolTipText  = QT_TR_NOOP("Select visible objects in the active document");
    sStatusTip    = QT_TR_NOOP("Select visible objects in the active document");
    sWhatsThis    = "Std_SelectVisibleObjects";
    sPixmap       = "Std_SelectVisibleObjects";
    eType         = Alter3DView;
}

void StdCmdSelectVisibleObjects::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // go through active document
    Gui::Document* doc = Application::Instance->activeDocument();
    App::Document* app = doc->getDocument();
    const std::vector<App::DocumentObject*> obj = app->getObjectsOfType
        (App::DocumentObject::getClassTypeId());

    std::vector<App::DocumentObject*> visible;
    visible.reserve(obj.size());
    for (std::vector<App::DocumentObject*>::const_iterator it=obj.begin();it!=obj.end();++it) {
        if (doc->isShow((*it)->getNameInDocument()))
            visible.push_back(*it);
    }

    SelectionSingleton& rSel = Selection();
    rSel.setSelection(visible);
}

bool StdCmdSelectVisibleObjects::isActive()
{
    return App::GetApplication().getActiveDocument();
}

//===========================================================================
// Std_ToggleObjects
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleObjects)

StdCmdToggleObjects::StdCmdToggleObjects()
  : Command("Std_ToggleObjects")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle all objects");
    sToolTipText  = QT_TR_NOOP("Toggles visibility of all objects in the active document");
    sStatusTip    = QT_TR_NOOP("Toggles visibility of all objects in the active document");
    sWhatsThis    = "Std_ToggleObjects";
    sPixmap       = "Std_ToggleObjects";
    eType         = Alter3DView;
}

void StdCmdToggleObjects::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // go through active document
    Gui::Document* doc = Application::Instance->activeDocument();
    App::Document* app = doc->getDocument();
    const std::vector<App::DocumentObject*> obj = app->getObjectsOfType
        (App::DocumentObject::getClassTypeId());

    for (std::vector<App::DocumentObject*>::const_iterator it=obj.begin();it!=obj.end();++it) {
        if (doc->isShow((*it)->getNameInDocument()))
            doCommand(Gui,"Gui.getDocument(\"%s\").getObject(\"%s\").Visibility=False"
                         , app->getName(), (*it)->getNameInDocument());
        else
            doCommand(Gui,"Gui.getDocument(\"%s\").getObject(\"%s\").Visibility=True"
                         , app->getName(), (*it)->getNameInDocument());
    }
}

bool StdCmdToggleObjects::isActive()
{
    return App::GetApplication().getActiveDocument();
}

//===========================================================================
// Std_ShowObjects
//===========================================================================
DEF_STD_CMD_A(StdCmdShowObjects)

StdCmdShowObjects::StdCmdShowObjects()
  : Command("Std_ShowObjects")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Show all objects");
    sToolTipText  = QT_TR_NOOP("Show all objects in the document");
    sStatusTip    = QT_TR_NOOP("Show all objects in the document");
    sWhatsThis    = "Std_ShowObjects";
    sPixmap       = "Std_ShowObjects";
    eType         = Alter3DView;
}

void StdCmdShowObjects::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // go through active document
    Gui::Document* doc = Application::Instance->activeDocument();
    App::Document* app = doc->getDocument();
    const std::vector<App::DocumentObject*> obj = app->getObjectsOfType
        (App::DocumentObject::getClassTypeId());

    for (std::vector<App::DocumentObject*>::const_iterator it=obj.begin();it!=obj.end();++it) {
        doCommand(Gui,"Gui.getDocument(\"%s\").getObject(\"%s\").Visibility=True"
                     , app->getName(), (*it)->getNameInDocument());
    }
}

bool StdCmdShowObjects::isActive()
{
    return App::GetApplication().getActiveDocument();
}

//===========================================================================
// Std_HideObjects
//===========================================================================
DEF_STD_CMD_A(StdCmdHideObjects)

StdCmdHideObjects::StdCmdHideObjects()
  : Command("Std_HideObjects")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Hide all objects");
    sToolTipText  = QT_TR_NOOP("Hide all objects in the document");
    sStatusTip    = QT_TR_NOOP("Hide all objects in the document");
    sWhatsThis    = "Std_HideObjects";
    sPixmap       = "Std_HideObjects";
    eType         = Alter3DView;
}

void StdCmdHideObjects::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // go through active document
    Gui::Document* doc = Application::Instance->activeDocument();
    App::Document* app = doc->getDocument();
    const std::vector<App::DocumentObject*> obj = app->getObjectsOfType
        (App::DocumentObject::getClassTypeId());

    for (std::vector<App::DocumentObject*>::const_iterator it=obj.begin();it!=obj.end();++it) {
        doCommand(Gui,"Gui.getDocument(\"%s\").getObject(\"%s\").Visibility=False"
                     , app->getName(), (*it)->getNameInDocument());
    }
}

bool StdCmdHideObjects::isActive()
{
    return App::GetApplication().getActiveDocument();
}

//===========================================================================
// Std_SelectDependents
//===========================================================================

DEF_STD_CMD_A(StdCmdSelectDependents)

StdCmdSelectDependents::StdCmdSelectDependents()
    : Command("Std_SelectDependents")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Select direct dependent objects");
    sToolTipText  = QT_TR_NOOP("Select objects that are directly depended on the current selection");
    sWhatsThis    = "Std_SelectDependents";
    sStatusTip    = sMenuText;
    eType         = Alter3DView;
}

void StdCmdSelectDependents::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> objs;
    auto sels = Gui::Selection().getCompleteSelection();
    for (const auto &sel : sels) {
        for (auto obj : sel.pObject->getOutList()) {
            if (objs.empty()) {
                objs.push_back(obj);
                continue;
            }
            auto it = std::upper_bound(objs.begin(), objs.end(), obj);
            if (*(it - 1) == obj)
                continue;
            objs.insert(it, obj);
        }
    }
    if (QApplication::queryKeyboardModifiers() != Qt::ControlModifier)
        Selection().clearCompleteSelection();
    Selection().setSelection(objs);
}

bool StdCmdSelectDependents::isActive()
{
    return Gui::Selection().hasSelection();
}

//===========================================================================
// Std_SelectDependentsRecursive
//===========================================================================

DEF_STD_CMD_A(StdCmdSelectDependentsRecursive)

StdCmdSelectDependentsRecursive::StdCmdSelectDependentsRecursive()
    : Command("Std_SelectDependentsRecursive")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Select all dependent objects");
    sToolTipText  = QT_TR_NOOP("Select all dependent objects of the current selection");
    sWhatsThis    = "Std_SelectDependentsRecursive";
    sStatusTip    = sMenuText;
    eType         = Alter3DView;
}

void StdCmdSelectDependentsRecursive::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> input;
    auto sels = Gui::Selection().getCompleteSelection();
    for (const auto &sel : sels)
        input.push_back(sel.pObject);

    std::vector<App::DocumentObject*> objs;
    for (auto obj : App::Document::getDependencyList(input)) {
        if (objs.empty()) {
            objs.push_back(obj);
            continue;
        }
        auto it = std::upper_bound(objs.begin(), objs.end(), obj);
        if (*(it - 1) == obj)
            continue;
        objs.insert(it, obj);
    }
    if (QApplication::queryKeyboardModifiers() != Qt::ControlModifier)
        Selection().clearCompleteSelection();
    Selection().setSelection(objs);
}

bool StdCmdSelectDependentsRecursive::isActive()
{
    return Gui::Selection().hasSelection();
}

//===========================================================================
// Std_RenderSettings
//===========================================================================
DEF_STD_CMD_A(StdCmdRenderSettings)

StdCmdRenderSettings::StdCmdRenderSettings()
  : Command("Std_RenderSettings")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Render settings...");
    sToolTipText  = QT_TR_NOOP("Edit the per-object render engine settings "
                               "(PBR material, textures, shadow flags) of "
                               "the selected objects");
    sWhatsThis    = "Std_RenderSettings";
    sStatusTip    = sToolTipText;
    eType         = Alter3DView;
}

void StdCmdRenderSettings::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new Gui::TaskRenderSettings());
}

bool StdCmdRenderSettings::isActive()
{
    return (Gui::Control().activeDialog() == nullptr)
        && !Gui::TaskRenderSettings::selectedViewProviders().empty();
}

//===========================================================================
// Std_ViewHome
//===========================================================================
DEF_3DV_CMD(StdCmdViewHome)

StdCmdViewHome::StdCmdViewHome()
  : Command("Std_ViewHome")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Home");
    sToolTipText  = QT_TR_NOOP("Set to default home view");
    sWhatsThis    = "Std_ViewHome";
    sStatusTip    = QT_TR_NOOP("Set to default home view");
    sPixmap       = "Std_ViewHome";
    sAccel        = "Home";
    eType         = Alter3DView;
}

void StdCmdViewHome::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    auto hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    std::string default_view = hGrp->GetASCII("NewDocumentCameraOrientation","Top");
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewDefaultOrientation('%s',0)",default_view.c_str());
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"ViewFit\")");
}

//===========================================================================
// Std_ViewBottom
//===========================================================================
DEF_3DV_CMD(StdCmdViewBottom)

StdCmdViewBottom::StdCmdViewBottom()
  : Command("Std_ViewBottom")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Bottom");
    sToolTipText  = QT_TR_NOOP("Set to bottom view");
    sWhatsThis    = "Std_ViewBottom";
    sStatusTip    = QT_TR_NOOP("Set to bottom view");
    sPixmap       = "view-bottom";
    sAccel        = "5";
    eType         = Alter3DView;
}

void StdCmdViewBottom::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewBottom()");
}

//===========================================================================
// Std_ViewFront
//===========================================================================
DEF_3DV_CMD(StdCmdViewFront)

StdCmdViewFront::StdCmdViewFront()
  : Command("Std_ViewFront")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Front");
    sToolTipText  = QT_TR_NOOP("Set to front view");
    sWhatsThis    = "Std_ViewFront";
    sStatusTip    = QT_TR_NOOP("Set to front view");
    sPixmap       = "view-front";
    sAccel        = "1";
    eType         = Alter3DView;
}

void StdCmdViewFront::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewFront()");
}

//===========================================================================
// Std_ViewLeft
//===========================================================================
DEF_3DV_CMD(StdCmdViewLeft)

StdCmdViewLeft::StdCmdViewLeft()
  : Command("Std_ViewLeft")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Left");
    sToolTipText  = QT_TR_NOOP("Set to left view");
    sWhatsThis    = "Std_ViewLeft";
    sStatusTip    = QT_TR_NOOP("Set to left view");
    sPixmap       = "view-left";
    sAccel        = "6";
    eType         = Alter3DView;
}

void StdCmdViewLeft::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewLeft()");
}

//===========================================================================
// Std_ViewRear
//===========================================================================
DEF_3DV_CMD(StdCmdViewRear)

StdCmdViewRear::StdCmdViewRear()
  : Command("Std_ViewRear")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Rear");
    sToolTipText  = QT_TR_NOOP("Set to rear view");
    sWhatsThis    = "Std_ViewRear";
    sStatusTip    = QT_TR_NOOP("Set to rear view");
    sPixmap       = "view-rear";
    sAccel        = "4";
    eType         = Alter3DView;
}

void StdCmdViewRear::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewRear()");
}

//===========================================================================
// Std_ViewRight
//===========================================================================
DEF_3DV_CMD(StdCmdViewRight)

StdCmdViewRight::StdCmdViewRight()
  : Command("Std_ViewRight")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Right");
    sToolTipText  = QT_TR_NOOP("Set to right view");
    sWhatsThis    = "Std_ViewRight";
    sStatusTip    = QT_TR_NOOP("Set to right view");
    sPixmap       = "view-right";
    sAccel        = "3";
    eType         = Alter3DView;
}

void StdCmdViewRight::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewRight()");
}

//===========================================================================
// Std_ViewTop
//===========================================================================
DEF_3DV_CMD(StdCmdViewTop)

StdCmdViewTop::StdCmdViewTop()
  : Command("Std_ViewTop")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Top");
    sToolTipText  = QT_TR_NOOP("Set to top view");
    sWhatsThis    = "Std_ViewTop";
    sStatusTip    = QT_TR_NOOP("Set to top view");
    sPixmap       = "view-top";
    sAccel        = "2";
    eType         = Alter3DView;
}

void StdCmdViewTop::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewTop()");
}


//===========================================================================
// Std_ViewIsometric
//===========================================================================
DEF_3DV_CMD(StdCmdViewIsometric)

StdCmdViewIsometric::StdCmdViewIsometric()
  : Command("Std_ViewIsometric")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Isometric");
    sToolTipText= QT_TR_NOOP("Set to isometric view");
    sWhatsThis  = "Std_ViewIsometric";
    sStatusTip  = QT_TR_NOOP("Set to isometric view");
    sPixmap     = "view-axonometric";
    sAccel      = "0";
    eType         = Alter3DView;
}

void StdCmdViewIsometric::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewIsometric()");
}

//===========================================================================
// Std_ViewDimetric
//===========================================================================
DEF_3DV_CMD(StdCmdViewDimetric)

StdCmdViewDimetric::StdCmdViewDimetric()
  : Command("Std_ViewDimetric")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Dimetric");
    sToolTipText= QT_TR_NOOP("Set to dimetric view");
    sWhatsThis  = "Std_ViewDimetric";
    sStatusTip  = QT_TR_NOOP("Set to dimetric view");
    sPixmap       = "Std_ViewDimetric";
    eType         = Alter3DView;
}

void StdCmdViewDimetric::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewDimetric()");
}

//===========================================================================
// Std_ViewTrimetric
//===========================================================================
DEF_3DV_CMD(StdCmdViewTrimetric)

StdCmdViewTrimetric::StdCmdViewTrimetric()
  : Command("Std_ViewTrimetric")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Trimetric");
    sToolTipText= QT_TR_NOOP("Set to trimetric view");
    sWhatsThis  = "Std_ViewTrimetric";
    sStatusTip  = QT_TR_NOOP("Set to trimetric view");
    sPixmap       = "Std_ViewTrimetric";
    eType         = Alter3DView;
}

void StdCmdViewTrimetric::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewTrimetric()");
}

//===========================================================================
// Std_ViewRotateLeft
//===========================================================================
DEF_3DV_CMD(StdCmdViewRotateLeft)

StdCmdViewRotateLeft::StdCmdViewRotateLeft()
  : Command("Std_ViewRotateLeft")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Rotate Left");
    sToolTipText  = QT_TR_NOOP("Rotate the view by 90\xc2\xb0 counter-clockwise");
    sWhatsThis    = "Std_ViewRotateLeft";
    sStatusTip    = QT_TR_NOOP("Rotate the view by 90\xc2\xb0 counter-clockwise");
    sPixmap       = "view-rotate-left";
    sAccel        = "Shift+Left";
    eType         = Alter3DView;
}

void StdCmdViewRotateLeft::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewRotateLeft()");
}


//===========================================================================
// Std_ViewRotateRight
//===========================================================================
DEF_3DV_CMD(StdCmdViewRotateRight)

StdCmdViewRotateRight::StdCmdViewRotateRight()
  : Command("Std_ViewRotateRight")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Rotate Right");
    sToolTipText  = QT_TR_NOOP("Rotate the view by 90\xc2\xb0 clockwise");
    sWhatsThis    = "Std_ViewRotateRight";
    sStatusTip    = QT_TR_NOOP("Rotate the view by 90\xc2\xb0 clockwise");
    sPixmap       = "view-rotate-right";
    sAccel        = "Shift+Right";
    eType         = Alter3DView;
}

void StdCmdViewRotateRight::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().viewRotateRight()");
}


//===========================================================================
// Std_ViewFitAll
//===========================================================================
DEF_STD_CMD_A(StdCmdViewFitAll)

StdCmdViewFitAll::StdCmdViewFitAll()
  : Command("Std_ViewFitAll")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Fit all");
    sToolTipText  = QT_TR_NOOP("Fits the whole content on the screen");
    sWhatsThis    = "Std_ViewFitAll";
    sStatusTip    = QT_TR_NOOP("Fits the whole content on the screen");
    sPixmap       = "zoom-all";
    sAccel        = "V, F";
    eType         = Alter3DView;
}

void StdCmdViewFitAll::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    //doCommand(Command::Gui,"Gui.activeDocument().activeView().fitAll()");
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"ViewFit\")");
}

bool StdCmdViewFitAll::isActive()
{
    //return isViewOfType(Gui::View3DInventor::getClassTypeId());
    return getGuiApplication()->sendHasMsgToActiveView("ViewFit");
}

//===========================================================================
// Std_ViewFitSelection
//===========================================================================
DEF_STD_CMD_A(StdCmdViewFitSelection)

StdCmdViewFitSelection::StdCmdViewFitSelection()
  : Command("Std_ViewFitSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Fit selection");
    sToolTipText  = QT_TR_NOOP("Fits the selected content on the screen");
    sWhatsThis    = "Std_ViewFitSelection";
    sStatusTip    = QT_TR_NOOP("Fits the selected content on the screen");
    sAccel        = "V, S";
    sPixmap       = "zoom-selection";
    eType         = Alter3DView;
}

void StdCmdViewFitSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    //doCommand(Command::Gui,"Gui.activeDocument().activeView().fitAll()");
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"ViewSelection\")");
}

bool StdCmdViewFitSelection::isActive()
{
  //return isViewOfType(Gui::View3DInventor::getClassTypeId());
  return getGuiApplication()->sendHasMsgToActiveView("ViewSelection");
}

//===========================================================================
// Std_ViewSelectionExtend
//===========================================================================
DEF_STD_CMD_A(StdCmdViewSelectionExtend)

StdCmdViewSelectionExtend::StdCmdViewSelectionExtend()
  : Command("Std_ViewSelectionExtend")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("View selection");
    sToolTipText  = QT_TR_NOOP("Adjust the camera view to include the selected contents on the screen.");
    sWhatsThis    = "Std_ViewSelectionExtend";
    sStatusTip    = sToolTipText;
    sAccel        = "V, E";
    sPixmap       = "view-selection-extend";
    eType         = Alter3DView;
}

void StdCmdViewSelectionExtend::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"ViewSelectionExtend\")");
}

bool StdCmdViewSelectionExtend::isActive()
{
  return getGuiApplication()->sendHasMsgToActiveView("ViewSelectionExtend");
}

//===========================================================================
// Std_ViewSelectionFront
//===========================================================================

DEF_STD_CMD_A(StdCmdViewSelectionFront)

StdCmdViewSelectionFront::StdCmdViewSelectionFront()
  : Command("Std_ViewSelectionFront")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("View front face");
    sToolTipText  = QT_TR_NOOP("Rotate the camera view to look in the front facing\n"
                               "normal direction of the (pre)selected element");
    sWhatsThis    = "Std_ViewSelectionFront";
    sStatusTip    = sMenuText;
    sAccel        = "V, N";
    eType         = Alter3DView | NoDefaultAction;
}

void StdCmdViewSelectionFront::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView('ViewSelectionFront')");
}

bool StdCmdViewSelectionFront::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("ViewSelectionFront");
}

DEF_STD_CMD_A(StdCmdViewSelectionBack)

StdCmdViewSelectionBack::StdCmdViewSelectionBack()
  : Command("Std_ViewSelectionBack")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("View back face");
    sToolTipText  = QT_TR_NOOP("Rotate the camera view to look in the back facing\n"
                               "normal direction of the (pre)selected element");
    sWhatsThis    = "Std_ViewSelectionBack";
    sStatusTip    = sMenuText;
    sAccel        = "V, B";
    eType         = Alter3DView | NoDefaultAction;
}

void StdCmdViewSelectionBack::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView('ViewSelectionBack')");
}

bool StdCmdViewSelectionBack::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("ViewSelectionBack");
}

//===========================================================================
// Std_RotationCenterSelection
//===========================================================================
DEF_STD_CMD_A(StdCmdRotationCenterSelection)

StdCmdRotationCenterSelection::StdCmdRotationCenterSelection()
  : Command("Std_RotationCenterSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Rotation center at selection");
    sToolTipText  = QT_TR_NOOP("Set rotation center at the center of current selection");
    sWhatsThis    = "Std_RotationCenterSelection";
    sStatusTip    = sToolTipText;
    sAccel        = "V, R";
    eType         = Alter3DView | NoDefaultAction;
}

void StdCmdRotationCenterSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"RotationCenterSelection\")");
}

bool StdCmdRotationCenterSelection::isActive()
{
  return getGuiApplication()->sendHasMsgToActiveView("RotationCenterSelection");
}


//===========================================================================
// Std_ViewSelection
//===========================================================================

class StdCmdViewSelection : public GroupCommand
{
public:
    StdCmdViewSelection()
        :GroupCommand("Std_ViewSelection")
    {
        sGroup        = "View";
        sMenuText     = QT_TR_NOOP("View selection");
        sToolTipText  = QT_TR_NOOP("Adjust camera to view selected contents on screen");
        sWhatsThis    = "Std_ViewSelection";
        sStatusTip    = sToolTipText;
        eType         = 0;
        bCanLog       = false;

        addCommand(new StdCmdViewFitSelection());
        addCommand(new StdCmdViewSelectionExtend());
        addCommand(new StdCmdRotationCenterSelection());
        addCommand(new StdCmdViewSelectionFront());
        addCommand(new StdCmdViewSelectionBack());
        addCommand();
        addCommand(new StdCmdViewIsometric());
        addCommand(new StdCmdViewDimetric());
        addCommand(new StdCmdViewTrimetric());
        addCommand(new StdCmdViewHome());
        addCommand();
        addCommand(new StdCmdViewFront());
        addCommand(new StdCmdViewTop());
        addCommand(new StdCmdViewRight());
        addCommand(new StdCmdViewRear());
        addCommand(new StdCmdViewBottom());
        addCommand(new StdCmdViewLeft());
    };
    virtual const char* className() const {return "StdCmdViewSelection";}
};

//===========================================================================
// Std_ViewDock
//===========================================================================
DEF_STD_CMD_A(StdViewDock)

StdViewDock::StdViewDock()
  : Command("Std_ViewDock")
{
    sGroup       = "Standard-View";
    sMenuText    = QT_TR_NOOP("Docked");
    sToolTipText = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sWhatsThis   = "Std_ViewDock";
    sStatusTip   = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sAccel       = "V, D";
    eType        = Alter3DView;
    bCanLog       = false;
}

void StdViewDock::activated(int iMsg)
{
    Q_UNUSED(iMsg);
}

bool StdViewDock::isActive()
{
    MDIView* view = getMainWindow()->activeWindow();
    return (qobject_cast<MDIView*>(view) ? true : false);
}

//===========================================================================
// Std_ViewUndock
//===========================================================================
DEF_STD_CMD_A(StdViewUndock)

StdViewUndock::StdViewUndock()
  : Command("Std_ViewUndock")
{
    sGroup       = "Standard-View";
    sMenuText    = QT_TR_NOOP("Undocked");
    sToolTipText = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sWhatsThis   = "Std_ViewUndock";
    sStatusTip   = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sAccel       = "V, U";
    eType        = Alter3DView;
    bCanLog       = false;
}

void StdViewUndock::activated(int iMsg)
{
    Q_UNUSED(iMsg);
}

bool StdViewUndock::isActive()
{
    MDIView* view = getMainWindow()->activeWindow();
    return (qobject_cast<MDIView*>(view) ? true : false);
}

//===========================================================================
// Std_MainFullscreen
//===========================================================================
DEF_STD_CMD(StdMainFullscreen)

StdMainFullscreen::StdMainFullscreen()
  : Command("Std_MainFullscreen")
{
    sGroup       = "Standard-View";
    sMenuText    = QT_TR_NOOP("Fullscreen");
    sToolTipText = QT_TR_NOOP("Display the main window in fullscreen mode");
    sWhatsThis   = "Std_MainFullscreen";
    sStatusTip   = QT_TR_NOOP("Display the main window in fullscreen mode");
    sPixmap      = "view-fullscreen";
    sAccel       = "Alt+F11";
    eType        = Alter3DView;
}

void StdMainFullscreen::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    MDIView* view = getMainWindow()->activeWindow();

    if (view)
        view->setCurrentViewMode(MDIView::Child);

    if (getMainWindow()->isFullScreen())
        getMainWindow()->showNormal();
    else
        getMainWindow()->showFullScreen();
}

//===========================================================================
// Std_ViewFullscreen
//===========================================================================
DEF_STD_CMD_A(StdViewFullscreen)

StdViewFullscreen::StdViewFullscreen()
  : Command("Std_ViewFullscreen")
{
    sGroup       = "Standard-View";
    sMenuText    = QT_TR_NOOP("Fullscreen");
    sToolTipText = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sWhatsThis   = "Std_ViewFullscreen";
    sStatusTip   = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sPixmap      = "view-fullscreen";
    sAccel       = "F11";
    eType        = Alter3DView;
    bCanLog       = false;
}

void StdViewFullscreen::activated(int iMsg)
{
    Q_UNUSED(iMsg);
}

bool StdViewFullscreen::isActive()
{
    MDIView* view = getMainWindow()->activeWindow();
    return (qobject_cast<MDIView*>(view) ? true : false);
}

//===========================================================================
// Std_ViewDockUndockFullscreen
//===========================================================================
DEF_STD_CMD_AC(StdViewDockUndockFullscreen)

StdViewDockUndockFullscreen::StdViewDockUndockFullscreen()
  : Command("Std_ViewDockUndockFullscreen")
{
    sGroup       = "Standard-View";
    sMenuText    = QT_TR_NOOP("Document window");
    sToolTipText = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    sWhatsThis   = "Std_ViewDockUndockFullscreen";
    sStatusTip   = QT_TR_NOOP("Display the active view either in fullscreen, in undocked or docked mode");
    eType        = Alter3DView;

    CommandManager &rcCmdMgr = Application::Instance->commandManager();
    rcCmdMgr.addCommand(new StdViewDock());
    rcCmdMgr.addCommand(new StdViewUndock());
    rcCmdMgr.addCommand(new StdViewFullscreen());
}

Action * StdViewDockUndockFullscreen::createAction()
{
    ActionGroup* pcAction = new ActionGroup(this, getMainWindow());
    pcAction->setDropDownMenu(true);
    pcAction->setText(QCoreApplication::translate(
        this->className(), getMenuText()));

    CommandManager &rcCmdMgr = Application::Instance->commandManager();
    Command* cmdD = rcCmdMgr.getCommandByName("Std_ViewDock");
    Command* cmdU = rcCmdMgr.getCommandByName("Std_ViewUndock");
    Command* cmdF = rcCmdMgr.getCommandByName("Std_ViewFullscreen");
    cmdD->addToGroup(pcAction, true);
    cmdU->addToGroup(pcAction, true);
    cmdF->addToGroup(pcAction, true);

    return pcAction;
}

void StdViewDockUndockFullscreen::activated(int iMsg)
{
    // Check if main window is in fullscreen mode.
    if (getMainWindow()->isFullScreen())
        getMainWindow()->showNormal();

    MDIView* view = getMainWindow()->activeWindow();
    if (!view) return; // no active view

#if defined(HAVE_QT5_OPENGL)
    // nothing to do when the view is docked and 'Docked' is pressed
    if (iMsg == 0 && view->currentViewMode() == MDIView::Child)
        return;
    // Change the view mode after an mdi view was already visible doesn't
    // work well with Qt5 any more because of some strange OpenGL behaviour.
    // A workaround is to clone the mdi view, set its view mode and delete
    // the original view.
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (doc) {
        Gui::MDIView* clone = view;
        if (view->isDerivedFrom(View3DInventor::getClassTypeId())) {
            clone = doc->cloneView(view);
            if (!clone)
                return;

            const char* ppReturn = 0;
            if (view->onMsg("GetCamera", &ppReturn)) {
                std::string sMsg = "SetCamera ";
                sMsg += ppReturn;

                const char** pReturnIgnore=0;
                clone->onMsg(sMsg.c_str(), pReturnIgnore);
            }
        }

        if (iMsg==0) {
            getMainWindow()->addWindow(clone);
        }
        else if (iMsg==1) {
            if (view->currentViewMode() == MDIView::TopLevel)
                getMainWindow()->addWindow(clone);
            else
                clone->setCurrentViewMode(MDIView::TopLevel);
        }
        else if (iMsg==2) {
            if (view->currentViewMode() == MDIView::FullScreen)
                getMainWindow()->addWindow(clone);
            else
                clone->setCurrentViewMode(MDIView::FullScreen);
        }

        // destroy the old view
        if (clone != view)
            view->deleteSelf();
    }
#else
    if (iMsg==0) {
        view->setCurrentViewMode(MDIView::Child);
    }
    else if (iMsg==1) {
        if (view->currentViewMode() == MDIView::TopLevel)
            view->setCurrentViewMode(MDIView::Child);
        else
            view->setCurrentViewMode(MDIView::TopLevel);
    }
    else if (iMsg==2) {
        if (view->currentViewMode() == MDIView::FullScreen)
            view->setCurrentViewMode(MDIView::Child);
        else
            view->setCurrentViewMode(MDIView::FullScreen);
    }
#endif
}

bool StdViewDockUndockFullscreen::isActive()
{
    MDIView* view = getMainWindow()->activeWindow();
    if (qobject_cast<MDIView*>(view)) {
        // update the action group if needed
        ActionGroup* pActGrp = qobject_cast<ActionGroup*>(_pcAction);
        if (pActGrp) {
            int index = pActGrp->checkedAction();
            int mode = (int)(view->currentViewMode());
            if (index != mode) {
                // active window has changed with another view mode
                pActGrp->setCheckedAction(mode);
            }
        }

        return true;
    }

    return false;
}


//===========================================================================
// Std_ViewVR
//===========================================================================
DEF_STD_CMD_A(StdCmdViewVR)

StdCmdViewVR::StdCmdViewVR()
  : Command("Std_ViewVR")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("FreeCAD-VR");
    sToolTipText  = QT_TR_NOOP("Extend the FreeCAD 3D Window to a Oculus Rift");
    sWhatsThis    = "Std_ViewVR";
    sStatusTip    = QT_TR_NOOP("Extend the FreeCAD 3D Window to a Oculus Rift");
    eType         = Alter3DView;
}

void StdCmdViewVR::activated(int iMsg)
{
    Q_UNUSED(iMsg);
  //doCommand(Command::Gui,"Gui.activeDocument().activeView().fitAll()");
   doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"ViewVR\")");
}

bool StdCmdViewVR::isActive()
{
   return getGuiApplication()->sendHasMsgToActiveView("ViewVR");
}

//===========================================================================
// Std_SaveView
//===========================================================================
DEF_STD_CMD_A(StdCmdSaveView)

StdCmdSaveView::StdCmdSaveView()
  : Command("Std_SaveView")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Save view");
    sToolTipText= QT_TR_NOOP("Save the current view into an object");
    sWhatsThis  = "Std_SaveView";
    sStatusTip  = sMenuText;
    sPixmap     = "SavedView";
    eType       = AlterDoc;
    sAccel      = "Ctrl+Shift+V";
}

void StdCmdSaveView::activated(int)
{
    auto doc = App::GetApplication().getActiveDocument();
    if(!doc)
        return;
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Save view"));
    try {
        std::string name = doc->getUniqueObjectName("SavedView");
        std::ostringstream ss;
        cmdAppDocument(doc, ss << "addObject('App::SavedView','" << name << "')");
        auto obj = doc->getObject(name.c_str());
        cmdGuiObject(obj, "capture()");
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

bool StdCmdSaveView::isActive()
{
    return App::GetApplication().getActiveDocument() != nullptr;
}

//===========================================================================
// Std_ViewScreenShot
//===========================================================================
DEF_STD_CMD_A(StdViewScreenShot)

StdViewScreenShot::StdViewScreenShot()
  : Command("Std_ViewScreenShot")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Save picture...");
    sToolTipText= QT_TR_NOOP("Creates a screenshot of the active view");
    sWhatsThis  = "Std_ViewScreenShot";
    sStatusTip  = QT_TR_NOOP("Creates a screenshot of the active view");
    sPixmap     = "camera-photo";
    eType         = Alter3DView;
}

void StdViewScreenShot::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        QStringList formats;
        SbViewportRegion vp(view->getViewer()->getSoRenderManager()->getViewportRegion());
        {
            SoQtOffscreenRenderer rd(vp);
            formats = rd.getWriteImageFiletypeInfo();
        }

        Base::Reference<ParameterGrp> hExt = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                                   ->GetGroup("Preferences")->GetGroup("General");
        QString ext = QString::fromUtf8(hExt->GetASCII("OffscreenImageFormat").c_str());
        int backtype = hExt->GetInt("OffscreenImageBackground",0);

        Base::Reference<ParameterGrp> methodGrp = App::GetApplication().GetParameterGroupByPath
            ("User parameter:BaseApp/Preferences/View");
        QByteArray method = methodGrp->GetASCII("SavePicture").c_str();

        QStringList filter;
        QString selFilter;
        for (QStringList::Iterator it = formats.begin(); it != formats.end(); ++it) {
            filter << QStringLiteral("%1 %2 (*.%3)").arg((*it).toUpper(),
                QObject::tr("files"), (*it).toLower());
            if (ext == *it)
                selFilter = filter.last();
        }

        FileOptionsDialog fd(getMainWindow(), Qt::WindowFlags());
        fd.setFileMode(QFileDialog::AnyFile);
        fd.setAcceptMode(QFileDialog::AcceptSave);
        fd.setWindowTitle(QObject::tr("Save picture"));
        fd.setNameFilters(filter);
        if (!selFilter.isEmpty())
            fd.selectNameFilter(selFilter);

        // create the image options widget
        DlgSettingsImageImp* opt = new DlgSettingsImageImp(&fd);
        SbVec2s sz = vp.getWindowSize();
        opt->setImageSize((int)sz[0], (int)sz[1]);
        opt->setBackgroundType(backtype);
        opt->setMethod(method);

        fd.setOptionsWidget(FileOptionsDialog::ExtensionRight, opt);
        fd.setOption(QFileDialog::DontConfirmOverwrite, false);
        opt->onSelectedFilter(fd.selectedNameFilter());
        QObject::connect(&fd, SIGNAL(filterSelected(const QString&)),
                         opt, SLOT(onSelectedFilter(const QString&)));

        if (fd.exec() == QDialog::Accepted) {
            selFilter = fd.selectedNameFilter();
            // The raw path: QFileInfo and QPixmap below want the real name, and
            // used to be handed the escaped one.
            QString fn = fd.selectedFiles().front();
            std::string pyfn = Base::Tools::pythonLiteral(fn);

            Gui::WaitCursor wc;

            // get the defined values
            int w = opt->imageWidth();
            int h = opt->imageHeight();

            // search for the matching format
            QString format = formats.front(); // take the first as default
            for (QStringList::Iterator it = formats.begin(); it != formats.end(); ++it) {
                if (selFilter.startsWith((*it).toUpper())) {
                    format = *it;
                    break;
                }
            }

            hExt->SetASCII("OffscreenImageFormat", (const char*)format.toUtf8());

            method = opt->method();
            methodGrp->SetASCII("SavePicture", method.constData());

            // which background chosen
            const char* background;
            switch(opt->backgroundType()){
                case 0:  background="Current"; break;
                case 1:  background="White"; break;
                case 2:  background="Black"; break;
                case 3:  background="Transparent"; break;
                default: background="Current"; break;
            }
            hExt->SetInt("OffscreenImageBackground",opt->backgroundType());

            QString comment = opt->comment();
            if (!comment.isEmpty()) {
                // Replace newline escape sequence through '\\n' string to build one big string,
                // otherwise Python would interpret it as an invalid command.
                // Python does the decoding for us.
#if QT_VERSION >= QT_VERSION_CHECK(5,15,0)
                QStringList lines = comment.split(QStringLiteral("\n"), Qt::KeepEmptyParts );
#else
                QStringList lines = comment.split(QStringLiteral("\n"), QString::KeepEmptyParts );
#endif
                comment = lines.join(QStringLiteral("\\n"));
                doCommand(Gui,"Gui.activeDocument().activeView().saveImage(%s,%d,%d,'%s','%s')",
                            pyfn.c_str(),w,h,background,comment.toUtf8().constData());
            }
            else {
                doCommand(Gui,"Gui.activeDocument().activeView().saveImage(%s,%d,%d,'%s')",
                            pyfn.c_str(),w,h,background);
            }

            // When adding a watermark check if the image could be created
            if (opt->addWatermark()) {
                QFileInfo fi(fn);
                QPixmap pixmap;
                if (fi.exists() && pixmap.load(fn)) {
                    QString name = qApp->applicationName();
                    std::map<std::string, std::string>& config = App::Application::Config();
                    QString url  = QString::fromUtf8(config["MaintainerUrl"].c_str());
                    url = QUrl(url).host();

                    QPixmap appicon = Gui::BitmapFactory().pixmap(config["AppIcon"].c_str());

                    QPainter painter;
                    painter.begin(&pixmap);

                    painter.drawPixmap(8, h-15-appicon.height(), appicon);

                    QFont font = painter.font();
                    font.setPointSize(20);

                    QFontMetrics fm(font);
                    int n = QtTools::horizontalAdvance(fm, name);
                    int h = pixmap.height();

                    painter.setFont(font);
                    painter.drawText(8+appicon.width(), h-24, name);

                    font.setPointSize(12);
                    int u = QtTools::horizontalAdvance(fm, url);
                    painter.setFont(font);
                    painter.drawText(8+appicon.width()+n-u, h-9, url);

                    painter.end();
                    pixmap.save(fn);
                }
            }
        }
    }
}

bool StdViewScreenShot::isActive()
{
    return isViewOfType(Gui::View3DInventor::getClassTypeId());
}

//===========================================================================
// Std_ViewLoadImage
//===========================================================================
DEF_STD_CMD(StdViewLoadImage)

StdViewLoadImage::StdViewLoadImage()
  : Command("Std_ViewLoadImage")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Load image...");
    sToolTipText= QT_TR_NOOP("Loads an image");
    sWhatsThis  = "Std_ViewLoadImage";
    sStatusTip  = QT_TR_NOOP("Loads an image");
    sPixmap     = "image-open";
    eType       = 0;
}

void StdViewLoadImage::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // add all supported QImage formats
    QStringList mimeTypeFilters;
    QList<QByteArray> supportedMimeTypes = QImageReader::supportedMimeTypes();
    for (const auto& mimeTypeName : supportedMimeTypes) {
        mimeTypeFilters.append(QString::fromUtf8(mimeTypeName));
    }

    // Reading an image
    QFileDialog dialog(Gui::getMainWindow());
    dialog.setWindowTitle(QObject::tr("Choose an image file to open"));
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter(QStringLiteral("image/png"));
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setOption(QFileDialog::DontUseNativeDialog);

    if (dialog.exec()) {
        QString fileName = dialog.selectedFiles().constFirst();
        ImageView* view = new ImageView(Gui::getMainWindow());
        view->loadFile(fileName);
        view->resize(400, 300);
        Gui::getMainWindow()->addWindow(view);
    }
}

//===========================================================================
// Std_ViewCreate
//===========================================================================
DEF_STD_CMD_A(StdCmdViewCreate)

StdCmdViewCreate::StdCmdViewCreate()
  : Command("Std_ViewCreate")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Create new view");
    sToolTipText= QT_TR_NOOP("Creates a new view window for the active document");
    sWhatsThis  = "Std_ViewCreate";
    sStatusTip  = QT_TR_NOOP("Creates a new view window for the active document");
    sPixmap     = "window-new";
    eType         = Alter3DView;
}

void StdCmdViewCreate::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    getActiveGuiDocument()->createView(View3DInventor::getClassTypeId());
    getActiveGuiDocument()->getActiveView()->viewAll();
}

bool StdCmdViewCreate::isActive()
{
    return (getActiveGuiDocument()!=NULL);
}

//===========================================================================
// Std_ViewSplitRight / Std_ViewSplitDown / Std_ViewSplitClose
//
// Blender-style split view areas (docs/SplitViews.md). Splitting a view
// that still lives in a plain MDI tab first wraps it into a ViewArea.
//===========================================================================

static void splitActiveView(Qt::Orientation orientation)
{
    auto view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (!view)
        return;
    ViewArea *area = ViewArea::areaOf(view);
    if (!area)
        area = ViewArea::wrap(view);
    if (!area)
        return;
    if (auto cell = area->cellOf(view))
        area->splitCell(cell, orientation);
}

DEF_STD_CMD_A(StdCmdViewSplitRight)

StdCmdViewSplitRight::StdCmdViewSplitRight()
  : Command("Std_ViewSplitRight")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Split view right");
    sToolTipText= QT_TR_NOOP("Splits the active view area side by side, with the new view on the right");
    sWhatsThis  = "Std_ViewSplitRight";
    sStatusTip  = sToolTipText;
    eType       = Alter3DView;
}

void StdCmdViewSplitRight::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    splitActiveView(Qt::Horizontal);
}

bool StdCmdViewSplitRight::isActive()
{
    return qobject_cast<View3DInventor*>(getMainWindow()->activeWindow()) != nullptr;
}

DEF_STD_CMD_A(StdCmdViewSplitDown)

StdCmdViewSplitDown::StdCmdViewSplitDown()
  : Command("Std_ViewSplitDown")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Split view down");
    sToolTipText= QT_TR_NOOP("Splits the active view area top and bottom, with the new view below");
    sWhatsThis  = "Std_ViewSplitDown";
    sStatusTip  = sToolTipText;
    eType       = Alter3DView;
}

void StdCmdViewSplitDown::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    splitActiveView(Qt::Vertical);
}

bool StdCmdViewSplitDown::isActive()
{
    return qobject_cast<View3DInventor*>(getMainWindow()->activeWindow()) != nullptr;
}

DEF_STD_CMD_A(StdCmdViewSplitClose)

StdCmdViewSplitClose::StdCmdViewSplitClose()
  : Command("Std_ViewSplitClose")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Close split view");
    sToolTipText= QT_TR_NOOP("Closes the active view area cell, its space goes to the neighbors");
    sWhatsThis  = "Std_ViewSplitClose";
    sStatusTip  = sToolTipText;
    eType       = Alter3DView;
}

void StdCmdViewSplitClose::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    auto view = getMainWindow()->activeWindow();
    ViewArea *area = ViewArea::areaOf(view);
    if (!area)
        return;
    if (auto cell = area->cellOf(view))
        area->closeCell(cell);
}

bool StdCmdViewSplitClose::isActive()
{
    auto view = getMainWindow()->activeWindow();
    ViewArea *area = ViewArea::areaOf(view);
    return area && area->cellCount() > 1;
}

//===========================================================================
// Std_ViewSplitMaximize
//===========================================================================
DEF_STD_CMD_A(StdCmdViewSplitMaximize)

StdCmdViewSplitMaximize::StdCmdViewSplitMaximize()
  : Command("Std_ViewSplitMaximize")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Maximize view cell");
    sToolTipText= QT_TR_NOOP("Temporarily gives the active view area cell the whole window; invoke again to restore the layout");
    sWhatsThis  = "Std_ViewSplitMaximize";
    sStatusTip  = sToolTipText;
    eType       = Alter3DView;
}

void StdCmdViewSplitMaximize::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    auto view = getMainWindow()->activeWindow();
    ViewArea *area = ViewArea::areaOf(view);
    if (!area)
        return;
    if (area->maximizedCell())
        area->toggleMaximizeCell(area->maximizedCell());
    else if (auto cell = area->cellOf(area->activeSubView()))
        area->toggleMaximizeCell(cell);
}

bool StdCmdViewSplitMaximize::isActive()
{
    auto view = getMainWindow()->activeWindow();
    ViewArea *area = ViewArea::areaOf(view);
    return area && (area->cellCount() > 1 || area->maximizedCell());
}

//===========================================================================
// Std_ViewCellShowObject
//
// The Blender "switch the area's editor" operation: host the selected
// object's own view (a TechDraw page, a spreadsheet, ...) in the active
// view area cell. Works for any object whose view provider offers an
// MDIView; show() is used to materialize it, which for such objects is
// plain visibility semantics.
//===========================================================================
DEF_STD_CMD_A(StdCmdViewCellShowObject)

StdCmdViewCellShowObject::StdCmdViewCellShowObject()
  : Command("Std_ViewCellShowObject")
{
    sGroup      = "Standard-View";
    sMenuText   = QT_TR_NOOP("Show object in view cell");
    sToolTipText= QT_TR_NOOP("Shows the selected object's view (e.g. a TechDraw page) in the active view area cell");
    sWhatsThis  = "Std_ViewCellShowObject";
    sStatusTip  = sToolTipText;
    eType       = Alter3DView;
}

static Gui::ViewProviderDocumentObject *selectedViewBearingProvider()
{
    auto objs = Gui::Selection().getObjectsOfType(App::DocumentObject::getClassTypeId());
    if (objs.size() != 1)
        return nullptr;
    return dynamic_cast<ViewProviderDocumentObject*>(
            Application::Instance->getViewProvider(objs.front()));
}

void StdCmdViewCellShowObject::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    auto vp = selectedViewBearingProvider();
    if (!vp)
        return;

    auto active = getMainWindow()->activeWindow();
    ViewArea *area = ViewArea::areaOf(active);
    if (!area) {
        area = ViewArea::wrap(active);
        if (!area)
            return;
    }
    auto cell = area->cellOf(area->activeSubView());
    if (!cell)
        return;

    MDIView *view = vp->getMDIView();
    if (!view) {
        vp->show();
        view = vp->getMDIView();
    }
    if (!view || view == active)
        return;
    area->setCellView(cell, view);
}

bool StdCmdViewCellShowObject::isActive()
{
    if (!selectedViewBearingProvider())
        return false;
    auto view = getMainWindow()->activeWindow();
    return ViewArea::areaOf(view)
        || qobject_cast<View3DInventor*>(view) != nullptr;
}

//===========================================================================
// Std_ToggleNavigation
//===========================================================================
DEF_STD_CMD_A(StdCmdToggleNavigation)

StdCmdToggleNavigation::StdCmdToggleNavigation()
  : Command("Std_ToggleNavigation")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle navigation/Edit mode");
    sToolTipText  = QT_TR_NOOP("Toggle between navigation and edit mode");
    sStatusTip    = QT_TR_NOOP("Toggle between navigation and edit mode");
    sWhatsThis    = "Std_ToggleNavigation";
  //iAccel        = Qt::SHIFT+Qt::Key_Space;
    sAccel        = "Esc";
    sPixmap       = "Std_ToggleNavigation";
    eType         = Alter3DView;
}

void StdCmdToggleNavigation::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        Gui::View3DInventorViewer* viewer = static_cast<Gui::View3DInventor*>(view)->getViewer();
        SbBool toggle = viewer->isRedirectedToSceneGraph();
        viewer->setRedirectToSceneGraph(!toggle);
    }
}

bool StdCmdToggleNavigation::isActive()
{
    //#0001087: Inventor Navigation continues with released Mouse Button
    //This happens because 'Esc' is also used to close the task dialog.
    //Add also new method 'isRedirectToSceneGraphEnabled' to explicitly
    //check if this is allowed.
    if (Gui::Control().activeDialog())
        return false;
    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        Gui::View3DInventorViewer* viewer = static_cast<Gui::View3DInventor*>(view)->getViewer();
        return viewer->isEditing() && viewer->isRedirectToSceneGraphEnabled();
    }
    return false;
}



#if 0 // old Axis command
// Command to show/hide axis cross
class StdCmdAxisCross : public Gui::Command
{
private:
    SoShapeScale* axisCross;
    SoGroup* axisGroup;
public:
    StdCmdAxisCross() : Command("Std_AxisCross"), axisCross(0), axisGroup(0)
    {
        sGroup        = "Standard-View";
        sMenuText     = QT_TR_NOOP("Toggle axis cross");
        sToolTipText  = QT_TR_NOOP("Toggle axis cross");
        sStatusTip    = QT_TR_NOOP("Toggle axis cross");
        sWhatsThis    = "Std_AxisCross";
        sPixmap       = "Std_AxisCross";
    }
    ~StdCmdAxisCross()
    {
        if (axisGroup)
            axisGroup->unref();
        if (axisCross)
            axisCross->unref();
    }
    const char* className() const
    { return "StdCmdAxisCross"; }

    Action * createAction()
    {
        axisCross = new Gui::SoShapeScale;
        axisCross->ref();
        Gui::SoAxisCrossKit* axisKit = new Gui::SoAxisCrossKit();
        axisKit->set("xAxis.appearance.drawStyle", "lineWidth 2");
        axisKit->set("yAxis.appearance.drawStyle", "lineWidth 2");
        axisKit->set("zAxis.appearance.drawStyle", "lineWidth 2");
        axisCross->setPart("shape", axisKit);
        axisGroup = new SoSkipBoundingGroup;
        axisGroup->ref();
        axisGroup->addChild(axisCross);

        Action *pcAction = Gui::Command::createAction();
        pcAction->setCheckable(true);
        return pcAction;
    }

protected:
    void activated(int iMsg)
    {
        float scale = 1.0f;

        Gui::View3DInventor* view = qobject_cast<Gui::View3DInventor*>
            (getMainWindow()->activeWindow());
        if (view) {
            SoNode* scene = view->getViewer()->getSceneGraph();
            SoSeparator* sep = static_cast<SoSeparator*>(scene);
            bool hasaxis = (sep->findChild(axisGroup) != -1);
            if (iMsg > 0 && !hasaxis) {
                axisCross->scaleFactor = scale;
                sep->addChild(axisGroup);
            }
            else if (iMsg == 0 && hasaxis) {
                sep->removeChild(axisGroup);
            }
        }
    }

    bool isActive()
    {
        Gui::View3DInventor* view = qobject_cast<View3DInventor*>(Gui::getMainWindow()->activeWindow());
        if (view) {
            Gui::View3DInventorViewer* viewer = view->getViewer();
            if (!viewer)
                return false; // no active viewer
            SoGroup* group = dynamic_cast<SoGroup*>(viewer->getSceneGraph());
            if (!group)
                return false; // empty scene graph
            bool hasaxis = group->findChild(axisGroup) != -1;
            if (_pcAction->isChecked() != hasaxis)
                _pcAction->setChecked(hasaxis);
            return true;
        }
        else {
            if (_pcAction->isChecked())
                _pcAction->setChecked(false);
            return false;
        }
    }
};
#else
//===========================================================================
// Std_ViewExample1
//===========================================================================
DEF_STD_CMD_A(StdCmdAxisCross)

StdCmdAxisCross::StdCmdAxisCross()
  : Command("Std_AxisCross")
{
        sGroup        = "Standard-View";
        sMenuText     = QT_TR_NOOP("Toggle axis cross");
        sToolTipText  = QT_TR_NOOP("Toggle axis cross");
        sStatusTip    = QT_TR_NOOP("Toggle axis cross");
        sWhatsThis    = "Std_AxisCross";
        sPixmap       = "Std_AxisCross";
        sAccel        = "A,C";
}

void StdCmdAxisCross::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::View3DInventor* view = qobject_cast<View3DInventor*>(Gui::getMainWindow()->activeWindow());
    if (view) {
        if (view->getViewer()->hasAxisCross() == false)
            doCommand(Command::Gui,"Gui.ActiveDocument.ActiveView.setAxisCross(True)");
        else
            doCommand(Command::Gui,"Gui.ActiveDocument.ActiveView.setAxisCross(False)");
    }
}

bool StdCmdAxisCross::isActive()
{
    Gui::View3DInventor* view = qobject_cast<View3DInventor*>(Gui::getMainWindow()->activeWindow());
    if (view && view->getViewer()->hasAxisCross()) {
        if (!_pcAction->isChecked())
            _pcAction->setChecked(true);
    }
    else {
        if (_pcAction->isChecked())
            _pcAction->setChecked(false);
    }
    if (view ) return true;
    return false;

}

#endif

//===========================================================================
// Std_ViewExample1
//===========================================================================
DEF_STD_CMD_A(StdCmdViewExample1)

StdCmdViewExample1::StdCmdViewExample1()
  : Command("Std_ViewExample1")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Inventor example #1");
    sToolTipText  = QT_TR_NOOP("Shows a 3D texture with manipulator");
    sWhatsThis    = "Std_ViewExample1";
    sStatusTip    = QT_TR_NOOP("Shows a 3D texture with manipulator");
    sPixmap       = "Std_Tool1";
    eType         = Alter3DView;
}

void StdCmdViewExample1::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"Example1\")");
}

bool StdCmdViewExample1::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("Example1");
}

//===========================================================================
// Std_ViewExample2
//===========================================================================
DEF_STD_CMD_A(StdCmdViewExample2)

StdCmdViewExample2::StdCmdViewExample2()
  : Command("Std_ViewExample2")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Inventor example #2");
    sToolTipText  = QT_TR_NOOP("Shows spheres and drag-lights");
    sWhatsThis    = "Std_ViewExample2";
    sStatusTip    = QT_TR_NOOP("Shows spheres and drag-lights");
    sPixmap       = "Std_Tool2";
    eType         = Alter3DView;
}

void StdCmdViewExample2::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"Example2\")");
}

bool StdCmdViewExample2::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("Example2");
}

//===========================================================================
// Std_ViewExample3
//===========================================================================
DEF_STD_CMD_A(StdCmdViewExample3)

StdCmdViewExample3::StdCmdViewExample3()
  : Command("Std_ViewExample3")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Inventor example #3");
    sToolTipText  = QT_TR_NOOP("Shows a animated texture");
    sWhatsThis    = "Std_ViewExample3";
    sStatusTip    = QT_TR_NOOP("Shows a animated texture");
    sPixmap       = "Std_Tool3";
    eType         = Alter3DView;
}

void StdCmdViewExample3::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.SendMsgToActiveView(\"Example3\")");
}

bool StdCmdViewExample3::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("Example3");
}


//===========================================================================
// Std_ViewIvStereoOff
//===========================================================================
DEF_STD_CMD_A(StdCmdViewIvStereoOff)

StdCmdViewIvStereoOff::StdCmdViewIvStereoOff()
  : Command("Std_ViewIvStereoOff")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Stereo Off");
    sToolTipText  = QT_TR_NOOP("Switch stereo viewing off");
    sWhatsThis    = "Std_ViewIvStereoOff";
    sStatusTip    = QT_TR_NOOP("Switch stereo viewing off");
    sPixmap       = "Std_ViewIvStereoOff";
    eType         = Alter3DView;
}

void StdCmdViewIvStereoOff::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().setStereoType(\"None\")");
}

bool StdCmdViewIvStereoOff::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("SetStereoOff");
}


//===========================================================================
// Std_ViewIvStereoRedGreen
//===========================================================================
DEF_STD_CMD_A(StdCmdViewIvStereoRedGreen)

StdCmdViewIvStereoRedGreen::StdCmdViewIvStereoRedGreen()
  : Command("Std_ViewIvStereoRedGreen")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Stereo red/cyan");
    sToolTipText  = QT_TR_NOOP("Switch stereo viewing to red/cyan");
    sWhatsThis    = "Std_ViewIvStereoRedGreen";
    sStatusTip    = QT_TR_NOOP("Switch stereo viewing to red/cyan");
    sPixmap       = "Std_ViewIvStereoRedGreen";
    eType         = Alter3DView;
}

void StdCmdViewIvStereoRedGreen::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().setStereoType(\"Anaglyph\")");
}

bool StdCmdViewIvStereoRedGreen::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("SetStereoRedGreen");
}

//===========================================================================
// Std_ViewIvStereoQuadBuff
//===========================================================================
DEF_STD_CMD_A(StdCmdViewIvStereoQuadBuff)

StdCmdViewIvStereoQuadBuff::StdCmdViewIvStereoQuadBuff()
  : Command("Std_ViewIvStereoQuadBuff")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Stereo quad buffer");
    sToolTipText  = QT_TR_NOOP("Switch stereo viewing to quad buffer");
    sWhatsThis    = "Std_ViewIvStereoQuadBuff";
    sStatusTip    = QT_TR_NOOP("Switch stereo viewing to quad buffer");
    sPixmap       = "Std_ViewIvStereoQuadBuff";
    eType         = Alter3DView;
}

void StdCmdViewIvStereoQuadBuff::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().setStereoType(\"QuadBuffer\")");
}

bool StdCmdViewIvStereoQuadBuff::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("SetStereoQuadBuff");
}

//===========================================================================
// Std_ViewIvStereoInterleavedRows
//===========================================================================
DEF_STD_CMD_A(StdCmdViewIvStereoInterleavedRows)

StdCmdViewIvStereoInterleavedRows::StdCmdViewIvStereoInterleavedRows()
  : Command("Std_ViewIvStereoInterleavedRows")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Stereo Interleaved Rows");
    sToolTipText  = QT_TR_NOOP("Switch stereo viewing to Interleaved Rows");
    sWhatsThis    = "Std_ViewIvStereoInterleavedRows";
    sStatusTip    = QT_TR_NOOP("Switch stereo viewing to Interleaved Rows");
    sPixmap       = "Std_ViewIvStereoInterleavedRows";
    eType         = Alter3DView;
}

void StdCmdViewIvStereoInterleavedRows::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().setStereoType(\"InterleavedRows\")");
}

bool StdCmdViewIvStereoInterleavedRows::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("SetStereoInterleavedRows");
}

//===========================================================================
// Std_ViewIvStereoInterleavedColumns
//===========================================================================
DEF_STD_CMD_A(StdCmdViewIvStereoInterleavedColumns)

StdCmdViewIvStereoInterleavedColumns::StdCmdViewIvStereoInterleavedColumns()
  : Command("Std_ViewIvStereoInterleavedColumns")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Stereo Interleaved Columns");
    sToolTipText  = QT_TR_NOOP("Switch stereo viewing to Interleaved Columns");
    sWhatsThis    = "Std_ViewIvStereoInterleavedColumns";
    sStatusTip    = QT_TR_NOOP("Switch stereo viewing to Interleaved Columns");
    sPixmap       = "Std_ViewIvStereoInterleavedColumns";
    eType         = Alter3DView;
}

void StdCmdViewIvStereoInterleavedColumns::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Command::Gui,"Gui.activeDocument().activeView().setStereoType(\"InterleavedColumns\")");
}

bool StdCmdViewIvStereoInterleavedColumns::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("SetStereoInterleavedColumns");
}


//===========================================================================
// Std_ViewIvIssueCamPos
//===========================================================================
DEF_STD_CMD_A(StdCmdViewIvIssueCamPos)

StdCmdViewIvIssueCamPos::StdCmdViewIvIssueCamPos()
  : Command("Std_ViewIvIssueCamPos")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Issue camera position");
    sToolTipText  = QT_TR_NOOP("Issue the camera position to the console and to a macro, to easily recall this position");
    sWhatsThis    = "Std_ViewIvIssueCamPos";
    sStatusTip    = QT_TR_NOOP("Issue the camera position to the console and to a macro, to easily recall this position");
    sPixmap       = "Std_ViewIvIssueCamPos";
    eType         = Alter3DView;
}

void StdCmdViewIvIssueCamPos::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::string Temp,Temp2;
    std::string::size_type pos;

    const char* ppReturn=0;
    getGuiApplication()->sendMsgToActiveView("GetCamera",&ppReturn);

    // remove the #inventor line...
    Temp2 = ppReturn;
    pos = Temp2.find_first_of("\n");
    Temp2.erase(0,pos);

    // remove all returns
    while((pos=Temp2.find('\n')) != std::string::npos)
        Temp2.replace(pos,1," ");

    // build up the command string
    Temp += "Gui.SendMsgToActiveView(\"SetCamera ";
    Temp += Temp2;
    Temp += "\")";

    Base::Console().Message("%s\n",Temp2.c_str());
    getGuiApplication()->macroManager()->addLine(MacroManager::Gui,Temp.c_str());
}

bool StdCmdViewIvIssueCamPos::isActive()
{
    return getGuiApplication()->sendHasMsgToActiveView("GetCamera");
}


//===========================================================================
// Std_ViewZoomIn
//===========================================================================
DEF_STD_CMD_A(StdViewZoomIn)

StdViewZoomIn::StdViewZoomIn()
  : Command("Std_ViewZoomIn")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Zoom In");
    sToolTipText  = QT_TR_NOOP("Zoom In");
    sWhatsThis    = "Std_ViewZoomIn";
    sStatusTip    = QT_TR_NOOP("Zoom In");
    sPixmap       = "zoom-in";
    sAccel        = keySequenceToAccel(QKeySequence::ZoomIn);
    eType         = Alter3DView;
}

void StdViewZoomIn::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if ( view ) {
        View3DInventorViewer* viewer = view->getViewer();
        viewer->navigationStyle()->zoomIn();
    }
}

bool StdViewZoomIn::isActive()
{
    return (qobject_cast<View3DInventor*>(getMainWindow()->activeWindow()));
}

//===========================================================================
// Std_ViewZoomOut
//===========================================================================
DEF_STD_CMD_A(StdViewZoomOut)

StdViewZoomOut::StdViewZoomOut()
  : Command("Std_ViewZoomOut")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Zoom Out");
    sToolTipText  = QT_TR_NOOP("Zoom Out");
    sWhatsThis    = "Std_ViewZoomOut";
    sStatusTip    = QT_TR_NOOP("Zoom Out");
    sPixmap       = "zoom-out";
    sAccel        = keySequenceToAccel(QKeySequence::ZoomOut);
    eType         = Alter3DView;
}

void StdViewZoomOut::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        View3DInventorViewer* viewer = view->getViewer();
        viewer->navigationStyle()->zoomOut();
    }
}

bool StdViewZoomOut::isActive()
{
    return (qobject_cast<View3DInventor*>(getMainWindow()->activeWindow()));
}
class SelectionCallbackHandler {    

private:
    static std::unique_ptr<SelectionCallbackHandler> currentSelectionHandler;
    QCursor* prevSelectionCursor;
    typedef void (*FnCb)(void * userdata, SoEventCallback * node);
    FnCb fnCb;
    void* userData;
    bool prevSelectionEn;

public:
    // Creates a selection handler used to implement the common behaviour of BoxZoom, BoxSelection and BoxElementSelection. 
    // Takes the viewer, a selection mode, a cursor, a function pointer to be called on success and a void pointer for user data to be passed to the given function.
    // The selection handler class stores all necessary previous states, registers a event callback and starts the selection in the given mode.    
    // If there is still a selection handler active, this call will generate a message and returns.
    static void Create(View3DInventorViewer* viewer, View3DInventorViewer::SelectionMode selectionMode, const QCursor& cursor, FnCb doFunction= NULL, void* ud=NULL)
    {
        if (currentSelectionHandler)
        {
            Base::Console().Message("SelectionCallbackHandler: A selection handler already active.");
            return;
        }

        currentSelectionHandler = std::make_unique<SelectionCallbackHandler>();
        if (viewer)
        {
            currentSelectionHandler->userData = ud;
            currentSelectionHandler->fnCb = doFunction;
            currentSelectionHandler->prevSelectionCursor = new QCursor(viewer->cursor());
            viewer->setEditingCursor(cursor);
            viewer->addEventCallback(SoEvent::getClassTypeId(),
                SelectionCallbackHandler::selectionCallback, currentSelectionHandler.get());
            currentSelectionHandler->prevSelectionEn = viewer->isSelectionEnabled();
            viewer->setSelectionEnabled(false);
            viewer->startSelection(selectionMode);
        }
    };

    void* getUserData() { return userData; };

    // Implements the event handler. In the normal case the provided function is called. 
    // Also supports aborting the selection mode by pressing (releasing) the Escape key. 
    static void selectionCallback(void * ud, SoEventCallback * n)
    {
        SelectionCallbackHandler* selectionHandler = reinterpret_cast<SelectionCallbackHandler*>(ud);
        Gui::View3DInventorViewer* view = Gui::View3DInventorViewer::fromEventCallback(n);
        const SoEvent* ev = n->getEvent();
        if (ev->isOfType(SoKeyboardEvent::getClassTypeId())) {

            n->setHandled();
            n->getAction()->setHandled();

            const SoKeyboardEvent * ke = static_cast<const SoKeyboardEvent*>(ev);
            const SbBool press = ke->getState() == SoButtonEvent::DOWN ? true : false;
            if (ke->getKey() == SoKeyboardEvent::ESCAPE) {
                              
                if (!press) {                    
                    view->abortSelection();
                    restoreState(selectionHandler, view);
                }                
            }
        }
        else if (ev->isOfType(SoMouseButtonEvent::getClassTypeId())) {
            const SoMouseButtonEvent * mbe = static_cast<const SoMouseButtonEvent*>(ev);

            // Mark all incoming mouse button events as handled, especially, to deactivate the selection node
            n->getAction()->setHandled();

            if (mbe->getButton() == SoMouseButtonEvent::BUTTON1 && mbe->getState() == SoButtonEvent::UP)
            {
                if (selectionHandler && selectionHandler->fnCb) selectionHandler->fnCb(selectionHandler->getUserData(), n);
                restoreState(selectionHandler, view);
            }
            // No other mouse events available from Coin3D to implement right mouse up abort
        }
    }

    static void restoreState(SelectionCallbackHandler * selectionHandler, View3DInventorViewer* view)
    {
        if(selectionHandler) selectionHandler->fnCb = NULL;
        view->setEditingCursor(*selectionHandler->prevSelectionCursor);
        view->removeEventCallback(SoEvent::getClassTypeId(), SelectionCallbackHandler::selectionCallback, selectionHandler);
        view->setSelectionEnabled(selectionHandler->prevSelectionEn);
        Application::Instance->commandManager().testActive();
        currentSelectionHandler = NULL;
    }
};

std::unique_ptr<SelectionCallbackHandler> SelectionCallbackHandler::currentSelectionHandler = std::unique_ptr<SelectionCallbackHandler>();
//===========================================================================
// Std_ViewBoxZoom
//===========================================================================
/* XPM */
static const char * cursor_box_zoom[] = {
"32 32 3 1",
" 	c None",
".	c #FFFFFF",
"@	c #FF0000",
"      .                         ",
"      .                         ",
"      .                         ",
"      .                         ",
"      .                         ",
"                                ",
".....   .....                   ",
"                                ",
"      .      @@@@@@@            ",
"      .    @@@@@@@@@@@          ",
"      .   @@         @@         ",
"      .  @@. . . . . .@@        ",
"      .  @             @        ",
"        @@ .         . @@       ",
"        @@             @@       ",
"        @@ .         . @@       ",
"        @@             @@       ",
"        @@ .         . @@       ",
"        @@             @@       ",
"        @@ .         . @@       ",
"         @             @        ",
"         @@. . . . . .@@@       ",
"          @@          @@@@      ",
"           @@@@@@@@@@@@  @@     ",
"             @@@@@@@ @@   @@    ",
"                      @@   @@   ",
"                       @@   @@  ",
"                        @@   @@ ",
"                         @@  @@ ",
"                          @@@@  ",
"                           @@   ",
"                                " };

DEF_3DV_CMD(StdViewBoxZoom)

StdViewBoxZoom::StdViewBoxZoom()
  : Command("Std_ViewBoxZoom")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Box zoom");
    sToolTipText  = QT_TR_NOOP("Box zoom");
    sWhatsThis    = "Std_ViewBoxZoom";
    sStatusTip    = QT_TR_NOOP("Box zoom");
    sPixmap       = "zoom-border";
    sAccel        = "Ctrl+B";
    eType         = Alter3DView;
}

void StdViewBoxZoom::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if ( view ) {
        View3DInventorViewer* viewer = view->getViewer();
        if (!viewer->isSelecting()) {
            SelectionCallbackHandler::Create(viewer, View3DInventorViewer::BoxZoom, QCursor(QPixmap(cursor_box_zoom), 7, 7));
        }
    }
}

//===========================================================================
// Std_BoxSelection
//===========================================================================
DEF_3DV_CMD(StdBoxSelection)

StdBoxSelection::StdBoxSelection()
  : Command("Std_BoxSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Box selection");
    sToolTipText  = QT_TR_NOOP("Box selection");
    sWhatsThis    = "Std_BoxSelection";
    sStatusTip    = QT_TR_NOOP("Box selection");
    sPixmap       = "edit-select-box";
    sAccel        = "Shift+B";
    eType         = AlterSelection;
}

static void selectionCallback(void * ud, SoEventCallback * cb)
{
    const SoEvent* ev = cb->getEvent();
    cb->setHandled();
    auto view  = Gui::View3DInventorViewer::fromEventCallback(cb);
    bool unselect = false;
    bool backFaceCull = true;
    bool singleSelect = true;

    if(ev) {
        if(ev->isOfType(SoKeyboardEvent::getClassTypeId())) {
            if(static_cast<const SoKeyboardEvent*>(ev)->getKey() == SoKeyboardEvent::ESCAPE) {
                view->stopSelection();
                view->removeEventCallback(SoEvent::getClassTypeId(), selectionCallback, ud);
                view->setEditing(false);
                view->setSelectionEnabled(true);
            }
            return;
        } else if (!ev->isOfType(SoMouseButtonEvent::getClassTypeId()))
            return;

        if(ev->wasShiftDown())  {
            unselect = true;
            singleSelect = false;
        } else if(ev->wasCtrlDown())
            singleSelect = false;
        if(ev->wasAltDown())
            backFaceCull = false;
    }

    bool selectElement = ud?true:false;

    bool center = false;
    auto points = view->getGLPolygon();
    bool doSelect = true;
    if (points.size() <= 1) {
        doSelect = false;
        singleSelect = true;
    } else if (points.size() == 2) {
        center = true;
        if (points[0] == points[1]) {
            doSelect = false;
            singleSelect = true;
        }
        // when selecting from right to left then select by intersection
        // otherwise if the center is inside the rectangle
        else if (points[0][0] > points[1][0])
            center = false;
    } else {
        double sum = 0;
        size_t i=0;
        for(size_t c=points.size()-1; i<c; ++i)
            sum += ((double)points[i+1][0] - points[i][0]) * ((double)points[i+1][1] + points[i][1]);
        sum += ((double)points[0][0] - points[i][0]) * (points[0][1] + points[i][1]);
        // use polygon windings to choose intersection or center inclusion
        center = sum>0;
    }

    if (doSelect) {
        if (singleSelect) {
            App::Document *doc = App::GetApplication().getActiveDocument();
            if (doc)
                Gui::Selection().clearSelection(doc->getName());
        }

        bool currentSelection = (ViewParams::getShowSelectionOnTop() 
                                 && ViewParams::getSelectElementOnTop()
                                 && selectElement);

        auto picked = view->getPickedList(points, center, selectElement, backFaceCull,
                                        currentSelection, unselect, false);
        for (auto &objT : picked) {
            if (unselect)
                Selection().rmvSelection(objT);
            else
                Selection().addSelection(objT);
        }
    }

    if (singleSelect) {
        view->removeEventCallback(SoEvent::getClassTypeId(), selectionCallback, ud);
        view->setEditing(false);
        view->setSelectionEnabled(true);
    } else {
        Command *cmd = (Command*)ud;
        AbstractMouseSelection *sel;
        if (cmd && Base::streq(cmd->getName(), "Std_LassoElementSelection"))
            sel = view->startSelection(View3DInventorViewer::Lasso);
        else
            sel = view->startSelection(View3DInventorViewer::Rubberband);
        if (sel) sel->changeCursorOnKeyPress(2);
    }
}

void StdBoxSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        View3DInventorViewer* viewer = view->getViewer();
        if (!viewer->isSelecting()) {
            // #0002931: Box select misbehaves with touchpad navigation style
            // Notify the navigation style to cleanup internal states
            int mode = viewer->navigationStyle()->getViewingMode();
            if (mode != Gui::NavigationStyle::IDLE) {
                SoKeyboardEvent ev;
                viewer->navigationStyle()->processEvent(&ev);
            }
            viewer->setEditing(true);
            AbstractMouseSelection *sel = viewer->startSelection(View3DInventorViewer::Rubberband);
            if (sel) sel->changeCursorOnKeyPress(1);
            viewer->addEventCallback(SoEvent::getClassTypeId(), selectionCallback);
            viewer->setSelectionEnabled(false);
        }
    }
}

//===========================================================================
// Std_BoxElementSelection
//===========================================================================

DEF_3DV_CMD(StdBoxElementSelection)

StdBoxElementSelection::StdBoxElementSelection()
  : Command("Std_BoxElementSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Box element selection");
    sToolTipText  = QT_TR_NOOP("Box element selection");
    sWhatsThis    = "Std_BoxElementSelection";
    sStatusTip    = QT_TR_NOOP("Box element selection");
    sPixmap       = "edit-element-select-box";
    sAccel        = "Ctrl+Shift+E";
    eType         = AlterSelection;
}

void StdBoxElementSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        View3DInventorViewer* viewer = view->getViewer();
        if (!viewer->isSelecting()) {
            // #0002931: Box select misbehaves with touchpad navigation style
            // Notify the navigation style to cleanup internal states
            int mode = viewer->navigationStyle()->getViewingMode();
            if (mode != Gui::NavigationStyle::IDLE) {
                SoKeyboardEvent ev;
                viewer->navigationStyle()->processEvent(&ev);
            }
            viewer->setEditing(true);
            AbstractMouseSelection *sel = viewer->startSelection(View3DInventorViewer::Rubberband);
            if (sel) sel->changeCursorOnKeyPress(1);
            viewer->addEventCallback(SoEvent::getClassTypeId(), selectionCallback, this);
            viewer->setSelectionEnabled(false);
        }
    }
}

//===========================================================================
// Std_LassoElementSelection
//===========================================================================
DEF_3DV_CMD(StdLassoElementSelection)

StdLassoElementSelection::StdLassoElementSelection()
  : Command("Std_LassoElementSelection")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Lasso element selection");
    sToolTipText  = QT_TR_NOOP("Lasso element selection");
    sWhatsThis    = "Std_LassoElementSelection";
    sStatusTip    = QT_TR_NOOP("Lasso element selection");
#if QT_VERSION >= 0x040200
    sPixmap       = "edit-element-select-lasso";
#endif
    sAccel        = "Shift+S";
    eType         = AlterSelection;
}

void StdLassoElementSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    View3DInventor* view = qobject_cast<View3DInventor*>(getMainWindow()->activeWindow());
    if (view) {
        View3DInventorViewer* viewer = view->getViewer();
        if (!viewer->isSelecting()) {
            // #0002931: Box select misbehaves with touchpad navigation style
            // Notify the navigation style to cleanup internal states
            int mode = viewer->navigationStyle()->getViewingMode();
            if (mode != Gui::NavigationStyle::IDLE) {
                SoKeyboardEvent ev;
                viewer->navigationStyle()->processEvent(&ev);
            }
            viewer->setEditing(true);
            AbstractMouseSelection *sel = viewer->startSelection(View3DInventorViewer::Lasso);
            if (sel) sel->changeCursorOnKeyPress(1);
            viewer->addEventCallback(SoEvent::getClassTypeId(), selectionCallback, this);
            viewer->setSelectionEnabled(false);
        }
    }
}

//===========================================================================
// Std_TreeSelection
//===========================================================================

DEF_STD_CMD(StdTreeSelection)

StdTreeSelection::StdTreeSelection()
  : Command("Std_TreeSelection")
{
    sGroup        = "TreeView";
    sMenuText     = QT_TR_NOOP("Show selection in tree view");
    sToolTipText  = QT_TR_NOOP("Scroll the tree view to the first selected item");
    sWhatsThis    = "Std_TreeSelection";
    sStatusTip    = QT_TR_NOOP("Scroll to first selected item");
    eType         = Alter3DView;
    sPixmap       = "tree-goto-sel";
    sAccel        = "T,G";
}

void StdTreeSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    TreeWidget::scrollItemToTop();
}

//===========================================================================
// Std_TreeCollapse
//===========================================================================

DEF_STD_CMD(StdCmdTreeCollapse)

StdCmdTreeCollapse::StdCmdTreeCollapse()
  : Command("Std_TreeCollapse")
{
    sGroup        = "View";
    sMenuText     = QT_TR_NOOP("Collapse selected item");
    sToolTipText  = QT_TR_NOOP("Collapse currently selected tree items");
    sWhatsThis    = "Std_TreeCollapse";
    sStatusTip    = QT_TR_NOOP("Collapse currently selected tree items");
    eType         = Alter3DView;
}

void StdCmdTreeCollapse::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    TreeWidget::expandSelectedItems(TreeItemMode::CollapseItem);
}

//===========================================================================
// Std_TreeExpand
//===========================================================================

DEF_STD_CMD(StdCmdTreeExpand)

StdCmdTreeExpand::StdCmdTreeExpand()
  : Command("Std_TreeExpand")
{
    sGroup        = "View";
    sMenuText     = QT_TR_NOOP("Expand selected item");
    sToolTipText  = QT_TR_NOOP("Expand currently selected tree items");
    sWhatsThis    = "Std_TreeExpand";
    sStatusTip    = QT_TR_NOOP("Expand currently selected tree items");
    eType         = Alter3DView;
}

void StdCmdTreeExpand::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    TreeWidget::expandSelectedItems(TreeItemMode::ExpandItem);
}

//===========================================================================
// Std_TreeSelectAllInstance
//===========================================================================

DEF_STD_CMD_A(StdCmdTreeSelectAllInstances)

StdCmdTreeSelectAllInstances::StdCmdTreeSelectAllInstances()
  : Command("Std_TreeSelectAllInstances")
{
    sGroup        = "View";
    sMenuText     = QT_TR_NOOP("Select all instances");
    sToolTipText  = QT_TR_NOOP("Select all instances of the current selected object");
    sWhatsThis    = "Std_TreeSelectAllInstances";
    sStatusTip    = QT_TR_NOOP("Select all instances of the current selected object");
    sPixmap       = "sel-instance";
    eType         = AlterSelection;
}

bool StdCmdTreeSelectAllInstances::isActive()
{
    SelectionObject sel;
    if (!Selection().getSingleSelection(sel))
        return false;
    auto obj = sel.getObject();
    if(!obj || !obj->isAttachedToDocument())
        return false;
    return dynamic_cast<ViewProviderDocumentObject*>(
            Application::Instance->getViewProvider(obj)) != nullptr;
}

void StdCmdTreeSelectAllInstances::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    SelectionObject sel;
    if (!Selection().getSingleSelection(sel))
        return;
    auto obj = sel.getObject();
    if(!obj || !obj->isAttachedToDocument())
        return;
    auto vpd = dynamic_cast<ViewProviderDocumentObject*>(
            Application::Instance->getViewProvider(obj));
    if(!vpd)
        return;
    Selection().selStackPush();
    Selection().clearCompleteSelection();
    TreeWidget::selectAllInstances(*vpd);
    Selection().selStackPush();
}

//===========================================================================
// Std_MeasureDistance
//===========================================================================

DEF_STD_CMD_A(StdCmdMeasureDistance)

StdCmdMeasureDistance::StdCmdMeasureDistance()
  : Command("Std_MeasureDistance")
{
    sGroup        = "View";
    sMenuText     = QT_TR_NOOP("Measure distance");
    sToolTipText  = QT_TR_NOOP("Measure distance");
    sWhatsThis    = "Std_MeasureDistance";
    sStatusTip    = QT_TR_NOOP("Measure distance");
    sPixmap       = "view-measurement";
    eType         = Alter3DView;
}

// Yay for cheezy drawings!
/* XPM */
static const char * cursor_ruler[] = {
"32 32 3 1",
" 	c None",
".	c #FFFFFF",
"+	c #FF0000",
"      .                         ",
"      .                         ",
"      .                         ",
"      .                         ",
"      .                         ",
"                                ",
".....   .....                   ",
"                                ",
"      .                         ",
"      .                         ",
"      .        ++               ",
"      .       +  +              ",
"      .      +   ++             ",
"            +   +  +            ",
"           +   +    +           ",
"          +   +     ++          ",
"          +        +  +         ",
"           +           +        ",
"            +         + +       ",
"             +       +   +      ",
"              +           +     ",
"               +         + +    ",
"                +       +   +   ",
"                 +           +  ",
"                  +         + + ",
"                   +       +  ++",
"                    +     +   + ",
"                     +       +  ",
"                      +     +   ",
"                       +   +    ",
"                        + +     ",
"                         +      "};
void StdCmdMeasureDistance::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    Gui::View3DInventor* view = static_cast<Gui::View3DInventor*>(doc->getActiveView());
    if (view) {
        Gui::View3DInventorViewer* viewer = view->getViewer();
        viewer->setEditing(true);
        viewer->setEditingCursor(QCursor(QPixmap(cursor_ruler), 7, 7));

        // Derives from QObject and we have a parent object, so we don't
        // require a delete.
        PointMarker* marker = new PointMarker(viewer);
        viewer->addEventCallback(SoEvent::getClassTypeId(),
            ViewProviderMeasureDistance::measureDistanceCallback, marker);
     }
}

bool StdCmdMeasureDistance::isActive()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc || doc->countObjectsOfType(App::GeoFeature::getClassTypeId()) == 0)
        return false;

    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        Gui::View3DInventorViewer* viewer = static_cast<Gui::View3DInventor*>(view)->getViewer();
        return !viewer->isEditing();
    }

    return false;
}

//===========================================================================
// Std_SceneInspector
//===========================================================================

DEF_3DV_CMD(StdCmdSceneInspector)

StdCmdSceneInspector::StdCmdSceneInspector()
  : Command("Std_SceneInspector")
{
    // setting the
    sGroup        = "Tools";
    sMenuText     = QT_TR_NOOP("Scene inspector...");
    sToolTipText  = QT_TR_NOOP("Scene inspector");
    sWhatsThis    = "Std_SceneInspector";
    sStatusTip    = QT_TR_NOOP("Scene inspector");
    eType         = 0;
    sAccel        = "T, I";
    sPixmap       = "Std_SceneInspector";
}

void StdCmdSceneInspector::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Document* doc = Application::Instance->activeDocument();
    if (doc) {
        static QPointer<Gui::Dialog::DlgInspector> dlg = 0;
        if (!dlg)
            dlg = new Gui::Dialog::DlgInspector(getMainWindow());
        dlg->setDocument(doc);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

//===========================================================================
// Std_TextureMapping
//===========================================================================

DEF_STD_CMD_A(StdCmdTextureMapping)

StdCmdTextureMapping::StdCmdTextureMapping()
  : Command("Std_TextureMapping")
{
    // setting the
    sGroup        = "Tools";
    sMenuText     = QT_TR_NOOP("Texture mapping...");
    sToolTipText  = QT_TR_NOOP("Texture mapping");
    sWhatsThis    = "Std_TextureMapping";
    sStatusTip    = QT_TR_NOOP("Texture mapping");
    sPixmap       = "Std_TextureMapping";
    eType         = Alter3DView;
}

void StdCmdTextureMapping::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new Gui::Dialog::TaskTextureMapping);
}

bool StdCmdTextureMapping::isActive()
{
    Gui::MDIView* view = getMainWindow()->activeWindow();
    return view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())
                && (Gui::Control().activeDialog()==0);
}

DEF_STD_CMD(StdCmdDemoMode)

StdCmdDemoMode::StdCmdDemoMode()
  : Command("Std_DemoMode")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("View turntable...");
    sToolTipText  = QT_TR_NOOP("View turntable");
    sWhatsThis    = "Std_DemoMode";
    sStatusTip    = QT_TR_NOOP("View turntable");
    eType         = Alter3DView;
    sPixmap       = "Std_DemoMode";
}

void StdCmdDemoMode::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    static QPointer<QDialog> dlg = 0;
    if (!dlg)
        dlg = new Gui::Dialog::DemoMode(getMainWindow());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

//===========================================================================
// Part_Measure_Clear_All
//===========================================================================

DEF_STD_CMD(CmdViewMeasureClearAll)

CmdViewMeasureClearAll::CmdViewMeasureClearAll()
  : Command("View_Measure_Clear_All")
{
    sGroup        = "Measure";
    sMenuText     = QT_TR_NOOP("Clear measurement");
    sToolTipText  = QT_TR_NOOP("Clear measurement");
    sWhatsThis    = "View_Measure_Clear_All";
    sStatusTip    = sToolTipText;
    sPixmap       = "Part_Measure_Clear_All";
}

void CmdViewMeasureClearAll::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::View3DInventor *view = dynamic_cast<Gui::View3DInventor*>(Gui::Application::Instance->
        activeDocument()->getActiveView());
    if (!view)
        return;
    Gui::View3DInventorViewer *viewer = view->getViewer();
    if (!viewer)
        return;
    viewer->eraseAllDimensions();
}

//===========================================================================
// Part_Measure_Toggle_All
//===========================================================================

DEF_STD_CMD(CmdViewMeasureToggleAll)

CmdViewMeasureToggleAll::CmdViewMeasureToggleAll()
  : Command("View_Measure_Toggle_All")
{
    sGroup        = "Measure";
    sMenuText     = QT_TR_NOOP("Toggle measurement");
    sToolTipText  = QT_TR_NOOP("Toggle measurement");
    sWhatsThis    = "View_Measure_Toggle_All";
    sStatusTip    = sToolTipText;
    sPixmap       = "Part_Measure_Toggle_All";
}

void CmdViewMeasureToggleAll::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ParameterGrp::handle group = App::GetApplication().GetUserParameter().
    GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("View");
    bool visibility = group->GetBool("DimensionsVisible", true);
    if (visibility)
        group->SetBool("DimensionsVisible", false);
    else
      group->SetBool("DimensionsVisible", true);
}

//===========================================================================
// Std_SelUp
//===========================================================================

DEF_STD_CMD_AC(StdCmdSelUp)

StdCmdSelUp::StdCmdSelUp()
  :Command("Std_SelUp")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Up hierarchy");
  sToolTipText  = QT_TR_NOOP("Go up object hierarchy of the current selection");
  sWhatsThis    = "Std_SelUp";
  sStatusTip    = sToolTipText;
  sPixmap       = "sel-up";
  sAccel        = "U, U";
  eType         = NoTransaction | AlterSelection | NoHistory;
}

bool StdCmdSelUp::isActive()
{
    return App::GetApplication().getActiveDocument();
}

void StdCmdSelUp::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    if (_pcAction)
        static_cast<SelUpAction*>(_pcAction)->popup(QCursor::pos());
}

Action * StdCmdSelUp::createAction()
{
    Action *pcAction;
    pcAction = new SelUpAction(this, getMainWindow());
    pcAction->setIcon(BitmapFactory().iconFromTheme(sPixmap));
    pcAction->setShortcut(QString::fromUtf8(sAccel));
    applyCommandData(this->className(), pcAction);
    return pcAction;
}

//===========================================================================
// Std_SelBack
//===========================================================================

DEF_STD_CMD_AC(StdCmdSelBack)

StdCmdSelBack::StdCmdSelBack()
  :Command("Std_SelBack")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Selection back");
  static std::string toolTip = std::string("<p>")
      + QT_TR_NOOP("Restore the previous Tree view selection. "
      "Only works if Tree RecordSelection mode is switched on.")
      + "</p>";
  sToolTipText = toolTip.c_str();
  sWhatsThis    = "Std_SelBack";
  sStatusTip    = sToolTipText;
  sPixmap       = "sel-back";
  sAccel        = "S, B";
  eType         = AlterSelection;
}

void StdCmdSelBack::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Selection().selStackGoBack();
}

bool StdCmdSelBack::isActive()
{
    return Selection().selStackBackSize()>0;
}

Action * StdCmdSelBack::createAction()
{
    Action *pcAction;
    pcAction = new SelStackAction(this, SelStackAction::Type::Backward, getMainWindow());
    pcAction->setIcon(BitmapFactory().iconFromTheme(sPixmap));
    pcAction->setShortcut(QString::fromUtf8(sAccel));
    applyCommandData(this->className(), pcAction);
    return pcAction;
}

//===========================================================================
// Std_SelForward
//===========================================================================

DEF_STD_CMD_AC(StdCmdSelForward)

StdCmdSelForward::StdCmdSelForward()
  :Command("Std_SelForward")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Selection forward");
  static std::string toolTip = std::string("<p>")
      + QT_TR_NOOP("Restore the next Tree view selection. "
      "Only works if Tree RecordSelection mode is switched on.")
      + "</p>";
  sToolTipText = toolTip.c_str();
  sWhatsThis    = "Std_SelForward";
  sStatusTip    = sToolTipText;
  sPixmap       = "sel-forward";
  sAccel        = "S, F";
  eType         = AlterSelection;
}

void StdCmdSelForward::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Selection().selStackGoForward();
}

bool StdCmdSelForward::isActive()
{
  return !!Selection().selStackForwardSize();
}

Action * StdCmdSelForward::createAction()
{
    Action *pcAction;
    pcAction = new SelStackAction(this, SelStackAction::Type::Forward, getMainWindow());
    pcAction->setIcon(BitmapFactory().iconFromTheme(sPixmap));
    pcAction->setShortcut(QString::fromUtf8(sAccel));
    applyCommandData(this->className(), pcAction);
    return pcAction;
}

//===========================================================================
// Std_SelGeometry
//===========================================================================

DEF_STD_CMD_A(StdCmdPickGeometry)

StdCmdPickGeometry::StdCmdPickGeometry()
  :Command("Std_PickGeometry")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Pick geometry");
  sToolTipText  = QT_TR_NOOP("Pick hidden geometires under the mouse cursor in 3D view.\n"
                             "This command is supposed to be activated by keyboard shortcut.");
  sWhatsThis    = "Std_PickGeometry";
  sStatusTip    = sToolTipText;
  // sPixmap       = "sel-geo";
  sAccel        = "G, G";
  eType         = NoTransaction | AlterSelection | NoDefaultAction | NoHistory;
}

void StdCmdPickGeometry::activated(int iMsg)
{
    Q_UNUSED(iMsg); 

    auto widget = OverlayManager::instance()->getLastMouseInterceptWidget();
    if (!widget) {
        QPoint pos = QCursor::pos();
        widget = qApp->widgetAt(pos);
    }
    if (widget)
        widget = widget->parentWidget();
    auto viewer = qobject_cast<View3DInventorViewer*>(widget);
    if (!viewer)
        return;

    auto sels = viewer->getPickedList(false);
    if (sels.empty())
        return;

    SelectionMenu menu;
    menu.doPick(sels);
}

bool StdCmdPickGeometry::isActive()
{
    auto view = Application::Instance->activeView();
    return view && view->isDerivedFrom(View3DInventor::getClassTypeId());
}

//===========================================================================
// Std_ItemMenu
//===========================================================================

DEF_STD_CMD_A(StdCmdItemMenu)

StdCmdItemMenu::StdCmdItemMenu()
  :Command("Std_ItemMenu")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Item menu");
  sToolTipText  = QT_TR_NOOP("Bring up the context menu of either a document or\n"
                             "an object item under the mouse cursor.");
  sWhatsThis    = "Std_ItemMenu";
  sStatusTip    = sToolTipText;
  sAccel        = "M, M";
  eType         = NoTransaction | AlterSelection | NoHistory;
}

bool StdCmdItemMenu::isActive()
{
    return App::GetApplication().getActiveDocument();
}

void StdCmdItemMenu::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    QMenu menu;
    App::SubObjectT ctxobj;
    SelectionContext sctx;
    TreeWidget::setupObjectMenu(menu, nullptr, &ctxobj);
    Selection().setContext(ctxobj);
    PieMenu::exec(&menu, QCursor::pos(), getName());
}

//=======================================================================
// Std_TreeSingleDocument
//===========================================================================
#define TREEVIEW_DOC_CMD_DEF(_name,_v) \
DEF_STD_CMD_AC(StdTree##_name) \
void StdTree##_name::activated(int){ \
    TreeParams::setDocumentMode(_v);\
    if(_pcAction) _pcAction->setChecked(true,true);\
}\
Action * StdTree##_name::createAction() {\
    Action *pcAction = Command::createAction();\
    pcAction->setCheckable(true);\
    pcAction->setIcon(QIcon());\
    _pcAction = pcAction;\
    isActive();\
    return pcAction;\
}\
bool StdTree##_name::isActive() {\
    bool checked = TreeParams::getDocumentMode()==_v;\
    if(_pcAction && _pcAction->isChecked()!=checked)\
        _pcAction->setChecked(checked,true);\
    return true;\
}

TREEVIEW_DOC_CMD_DEF(SingleDocument,0)

StdTreeSingleDocument::StdTreeSingleDocument()
  : Command("Std_TreeSingleDocument")
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Single document");
    sToolTipText = QT_TR_NOOP("Only display the active document in the tree view");
    sWhatsThis   = "Std_TreeSingleDocument";
    sStatusTip   = QT_TR_NOOP("Only display the active document in the tree view");
    sPixmap      = "tree-doc-single";
    eType        = 0;
}

//===========================================================================
// Std_TreeMultiDocument
//===========================================================================
TREEVIEW_DOC_CMD_DEF(MultiDocument,1)

StdTreeMultiDocument::StdTreeMultiDocument()
  : Command("Std_TreeMultiDocument")
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Multi document");
    sToolTipText = QT_TR_NOOP("Display all documents in the tree view");
    sWhatsThis   = "Std_TreeMultiDocument";
    sStatusTip   = QT_TR_NOOP("Display all documents in the tree view");
    sPixmap      = "tree-doc-multi";
    eType        = 0;
}

//===========================================================================
// Std_TreeCollapseDocument
//===========================================================================
TREEVIEW_DOC_CMD_DEF(CollapseDocument,2)

StdTreeCollapseDocument::StdTreeCollapseDocument()
  : Command("Std_TreeCollapseDocument")
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Collapse/Expand");
    sToolTipText = QT_TR_NOOP("Expand active document and collapse all others");
    sWhatsThis   = "Std_TreeCollapseDocument";
    sStatusTip   = QT_TR_NOOP("Expand active document and collapse all others");
    sPixmap      = "tree-doc-collapse";
    eType        = 0;
}

typedef Gui::CheckableCommand StdCmdCheckableOption;

#define TREEVIEW_CMD_DEF(_name) \
class StdTree##_name : public StdCmdCheckableOption \
{\
public:\
    StdTree##_name();\
    virtual const char* className() const\
    { return "StdTree" #_name; }\
protected: \
    virtual void setOption(bool checked) {\
        TreeParams::set##_name(checked);\
    }\
    virtual bool getOption() const {\
        return TreeParams::get##_name();\
    }\
};\
StdTree##_name::StdTree##_name():StdCmdCheckableOption("Std_Tree" #_name)

//===========================================================================
// Std_TreeSyncView
//===========================================================================

TREEVIEW_CMD_DEF(SyncView)
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Sync view");
    sToolTipText = QT_TR_NOOP("Auto switch to the 3D view containing the selected item");
    sStatusTip   = sToolTipText;
    sWhatsThis   = "Std_TreeSyncView";
    sPixmap      = "tree-sync-view";
    sAccel       = "T,1";
    eType        = 0;
}

//===========================================================================
// Std_TreeSyncSelection
//===========================================================================
TREEVIEW_CMD_DEF(SyncSelection)
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Sync selection");
    sToolTipText = QT_TR_NOOP("Auto expand tree item when the corresponding object is selected in 3D view");
    sStatusTip   = sToolTipText;
    sWhatsThis   = "Std_TreeSyncSelection";
    sPixmap      = "tree-sync-sel";
    sAccel       = "T,2";
    eType        = 0;
}

//===========================================================================
// Std_TreeSyncPlacement
//===========================================================================
TREEVIEW_CMD_DEF(SyncPlacement)
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Sync placement");
    sToolTipText = QT_TR_NOOP("Auto adjust placement on drag and drop objects across coordinate systems");
    sStatusTip   = sToolTipText;
    sWhatsThis   = "Std_TreeSyncPlacement";
    sPixmap      = "tree-sync-pla";
    sAccel       = "T,3";
    eType        = 0;
}

//===========================================================================
// Std_TreePreSelection
//===========================================================================
TREEVIEW_CMD_DEF(PreSelection)
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Tree view pre-selection");
    sToolTipText = QT_TR_NOOP("Preselect the object in 3D view when mouse over the tree item");
    sStatusTip   = sToolTipText;
    sWhatsThis   = "Std_TreePreSelection";
    sPixmap      = "tree-pre-sel";
    sAccel       = "T,4";
    eType        = 0;
}

//===========================================================================
// Std_TreeRecordSelection
//===========================================================================
TREEVIEW_CMD_DEF(RecordSelection)
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Record selection");
    sToolTipText = QT_TR_NOOP("Record selection in tree view in order to go back/forward using navigation button");
    sStatusTip   = sToolTipText;
    sWhatsThis   = "Std_TreeRecordSelection";
    sPixmap      = "tree-rec-sel";
    sAccel       = "T,5";
    eType        = 0;
}

//===========================================================================
// Std_TreeDrag
//===========================================================================
DEF_STD_CMD(StdTreeDrag)

StdTreeDrag::StdTreeDrag()
  : Command("Std_TreeDrag")
{
    sGroup       = "TreeView";
    sMenuText    = QT_TR_NOOP("Initiate dragging");
    sToolTipText = QT_TR_NOOP("Initiate dragging of current selected tree items");
    sStatusTip   = sToolTipText;
    sWhatsThis   = "Std_TreeDrag";
    sPixmap      = "tree-item-drag";
    sAccel       = "T,D";
    eType        = 0;
}

void StdTreeDrag::activated(int)
{
    if(Gui::Selection().hasSelection()) {
        for(auto tree : getMainWindow()->findChildren<TreeWidget*>()) {
            if(tree->isVisible()) {
                tree->startDragging();
                break;
            }
        }
    }
}

//======================================================================
// Std_TreeHideSelection
//======================================================================
DEF_STD_CMD(StdTreeHideSelection)

StdTreeHideSelection::StdTreeHideSelection()
    : Command("Std_TreeHideSelection")
{
    sGroup          = "TreeView";
    sMenuText       = QT_TR_NOOP("Hide item in tree");
    sToolTipText    = QT_TR_NOOP("Hides the selected item in the tree view");
    sStatusTip      = sToolTipText;
    sWhatsThis      = "Std_TreeHideSelection";
    sPixmap         = "tree-item-hide";
    sAccel          = "Ctrl+Shift+H";
    eType           = 0;
}

void StdTreeHideSelection::activated(int)
{
    auto sels = Gui::Selection().getSelectionT();
    std::set<App::SubObjectT> objs(sels.begin(), sels.end());
    std::ostringstream ss;
    App::AutoTransaction guard(QT_TRANSLATE_NOOP("Command", "Toggle tree item"),
                               false, /*not a temporary transaction*/
                               true /*enable view object undo*/);
    for (auto &objT : objs) {
        ss.str("");
        ss << objT.getObjectPython() << ".ViewObject.ShowInTree = not "
           << objT.getObjectPython() << ".ViewObject.ShowInTree";
        runCommand(Command::Gui, ss.str().c_str());
    }
}

//======================================================================
// Std_TreeToggleShowHidden
//======================================================================
DEF_STD_CMD_AC(StdTreeToggleShowHidden)

StdTreeToggleShowHidden::StdTreeToggleShowHidden()
	: Command("Std_TreeToggleShowHidden")
{
	sGroup          = "TreeView";
	sMenuText       = QT_TR_NOOP("Toggle showing hidden items");
	sToolTipText    = QT_TR_NOOP("Toggles whether or not hidden items are visible in the tree");
	sStatusTip      = sToolTipText;
	sWhatsThis      = "Std_TreeToggleShowHidden";
	// sPixmap         = "tree-show-hidden";
	sAccel          = "T, S";
	eType           = 0;
}

static App::Document *getShowHiddenDocument()
{
    auto sels = Gui::Selection().getSelectionT(nullptr, ResolveMode::NoResolve, /*single*/true);
    App::Document *doc = nullptr;
    if (!sels.empty())
        doc = sels[0].getDocument();
    if (!doc)
        doc = App::GetApplication().getActiveDocument();
    return doc;
}

void StdTreeToggleShowHidden::activated(int)
{
    if (auto doc = getShowHiddenDocument()) {
        std::ostringstream ss;
        ss << "App.getDocument('" << doc->getName() << "').ShowHidden = not "
              "App.getDocument('" << doc->getName() << "').ShowHidden";
        runCommand(Command::Gui, ss.str().c_str());
        _pcAction->setChecked(doc->ShowHidden.getValue(), true);
    }
}

Action * StdTreeToggleShowHidden::createAction() {
    Action *pcAction = Command::createAction();
    pcAction->setCheckable(true);
    _pcAction = pcAction;
    isActive();
    return pcAction;
}

bool StdTreeToggleShowHidden::isActive() {
    if (auto doc = getShowHiddenDocument()) {
        bool checked = doc->ShowHidden.getValue();
        if(_pcAction && _pcAction->isChecked()!=checked)
            _pcAction->setChecked(checked, true);
        return true;
    }
    return false;
}

//======================================================================
// Std_TreeRelabelObject
//===========================================================================
//
DEF_STD_CMD_A(StdCmdTreeRelabelObject)

StdCmdTreeRelabelObject::StdCmdTreeRelabelObject()
  :Command("Std_TreeRelabelObject")
{
    // setting the
    sGroup        = "Edit";
    sMenuText     = QT_TR_NOOP("Rename object");
    sToolTipText  = QT_TR_NOOP("Rename the selected object");
    sWhatsThis    = "Std_TreeRelabelObject";
    sStatusTip    = sToolTipText;
    sAccel        = "F2";
}

bool StdCmdTreeRelabelObject::isActive(void)
{
    if (auto tree = TreeWidget::instance()) {
        return tree->isVisible() && tree->currentItem() != nullptr;
    }
    return false;
}

void StdCmdTreeRelabelObject::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    if (auto tree = TreeWidget::instance()) {
        if (tree->isVisible()) {
            tree->relabelObject();
        }
    }
}

//======================================================================
// Std_TreeViewActions
//===========================================================================
//
class StdCmdTreeViewActions : public GroupCommand
{
public:
    StdCmdTreeViewActions()
        :GroupCommand("Std_TreeViewActions")
    {
        sGroup        = "TreeView";
        sMenuText     = QT_TR_NOOP("TreeView actions");
        sToolTipText  = QT_TR_NOOP("TreeView behavior options and actions");
        sWhatsThis    = "Std_TreeViewActions";
        sStatusTip    = QT_TR_NOOP("TreeView behavior options and actions");
        eType         = 0;
        bCanLog       = false;

        addCommand(new StdTreeSyncView());
        addCommand(new StdTreeSyncSelection());
        addCommand(new StdTreeSyncPlacement());
        addCommand("Std_TreePreSelection");
        addCommand(new StdTreeRecordSelection());

        addCommand();

        addCommand(new StdTreeSingleDocument());
        addCommand(new StdTreeMultiDocument());
        addCommand(new StdTreeCollapseDocument());

        addCommand();

        addCommand(new StdTreeDrag(),cmds.size());
        addCommand(new StdTreeSelection(),cmds.size());
        addCommand(new StdTreeHideSelection());
        addCommand(new StdTreeToggleShowHidden());
    };

    const char* className() const override
    {
        return "StdCmdTreeViewActions";
    }
};


#define VIEW_CMD_DEF(_name,_option) \
class StdCmd##_name : public StdCmdCheckableOption \
{\
public:\
    StdCmd##_name();\
    virtual const char* className() const\
    { return "StdCmd" #_name; }\
protected: \
    virtual void setOption(bool checked) {\
        ViewParams::set##_option(checked);\
    }\
    virtual bool getOption() const {\
        return ViewParams::get##_option();\
    }\
};\
StdCmd##_name::StdCmd##_name():StdCmdCheckableOption("Std_" #_name)

//======================================================================
// Std_SelBoundingBox
//======================================================================
VIEW_CMD_DEF(SelBoundingBox, ShowSelectionBoundingBox)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Bounding box");
  sToolTipText  = ViewParams::docShowSelectionBoundingBox();
  sWhatsThis    = "Std_SelBoundingBox";
  sStatusTip    = sToolTipText;
  sPixmap       = "sel-bbox";
  eType         = Alter3DView;
}

//======================================================================
// Std_TightBoundingBox
//======================================================================
VIEW_CMD_DEF(TightBoundingBox, UseTightBoundingBox)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Tighten bounding box");
  sToolTipText  = ViewParams::docUseTightBoundingBox();
  sWhatsThis    = "Std_TightBoundingBox";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_ProjectBoundingBox
//======================================================================
VIEW_CMD_DEF(ProjectBoundingBox, RenderProjectedBBox)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Project bounding box");
  sToolTipText  = ViewParams::docRenderProjectedBBox();
  sWhatsThis    = "Std_ProjectBoundingBox";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_SelectionFaceWire
//======================================================================
VIEW_CMD_DEF(SelectionFaceWire, SelectionFaceWire)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Show selected face wires");
  sToolTipText  = ViewParams::docSelectionFaceWire();
  sWhatsThis    = "Std_SelectionFaceWire";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_AutoTransparentPick
//======================================================================
VIEW_CMD_DEF(AutoTransparentPick, AutoTransparentPick)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Auto transparent pick");
  sToolTipText  = ViewParams::docAutoTransparentPick();
  sWhatsThis    = "Std_AutoTransparentPick";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_SelOnTop
//======================================================================
VIEW_CMD_DEF(SelOnTop, ShowSelectionOnTop)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Selection on top");
  sToolTipText  = ViewParams::docShowSelectionOnTop();
  sWhatsThis    = "Std_SelOnTop";
  sStatusTip    = sToolTipText;
  sPixmap       = "sel-on-top";
  sAccel        = "V, T";
  eType         = 0;
}

//======================================================================
// Std_PreSelFaceOnTop
//======================================================================
VIEW_CMD_DEF(PreSelFaceOnTop, ShowPreSelectedFaceOnTop)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Pre-selection on top");
  sToolTipText  = ViewParams::docShowPreSelectedFaceOnTop();
  sWhatsThis    = "Std_PreSelFaceOnTop";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_PreSelFaceOutline
//======================================================================
VIEW_CMD_DEF(PreSelFaceOutline, ShowPreSelectedFaceOutline)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Draw pre-selection &outline");
  sToolTipText  = ViewParams::docShowPreSelectedFaceOutline();
  sWhatsThis    = "Std_PreSelFaceOutline";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_SelFaceOutline
//======================================================================
VIEW_CMD_DEF(SelFaceOutline, ShowSelectedFaceOutline)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Draw selection outline");
  sToolTipText  = ViewParams::docShowSelectedFaceOutline();
  sWhatsThis    = "Std_SelFaceOutline";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_NoSelFaceHighlightWithOutline
//======================================================================
VIEW_CMD_DEF(NoSelFaceHighlightWithOutline, NoSelFaceHighlightWithOutline)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("No selection face highlight with outline");
  sToolTipText  = ViewParams::docNoSelFaceHighlightWithOutline();
  sWhatsThis    = "Std_NoSelFaceHighlightWithOutline";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_NoPreSelFaceHighlightWithOutline
//======================================================================
VIEW_CMD_DEF(NoPreSelFaceHighlightWithOutline, NoPreSelFaceHighlightWithOutline)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("No pre-selection face highlight with outline");
  sToolTipText  = ViewParams::docNoPreSelFaceHighlightWithOutline();
  sWhatsThis    = "Std_NoPreSelFaceHighlightWithOutline";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_SelHierarchyAscend
//======================================================================
VIEW_CMD_DEF(SelHierarchyAscend, HierarchyAscend)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Hierarchy selection");
  sToolTipText  = ViewParams::docHierarchyAscend();
  sWhatsThis    = "Std_SelHierarhcyAscend";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_PartialHighlightOnFullSelect
//======================================================================
VIEW_CMD_DEF(PartialHighlightOnFullSelect, PartialHighlightOnFullSelect)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Partial highlight");
  sToolTipText  = ViewParams::docPartialHighlightOnFullSelect();
  sWhatsThis    = "Std_PartialHighlightOnFullSelect";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_PreselEdgeOnly
//======================================================================
VIEW_CMD_DEF(PreselEdgeOnly, ShowHighlightEdgeOnly)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("&Highlight edge only");
  sToolTipText  = ViewParams::docShowHighlightEdgeOnly();
  sWhatsThis    = "Std_PreselEdgeOnly";
  sStatusTip    = sToolTipText;
  sPixmap       = "presel-edge-only";
  eType         = Alter3DView;
}

//======================================================================
// Std_HiddenLineSelectionOnTop
//======================================================================
VIEW_CMD_DEF(HiddenLineSelectionOnTop, HiddenLineSelectionOnTop)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Hidden line select on top");
  sToolTipText  = ViewParams::docHiddenLineSelectionOnTop();
  sWhatsThis    = "Std_HiddenLineSelectionOnTop";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_OverrideSelectability
//======================================================================
VIEW_CMD_DEF(OverrideSelectability, OverrideSelectability)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Override selectability");
  sToolTipText  = ViewParams::docOverrideSelectability();
  sWhatsThis    = "Std_OverrideSelectability";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_MapChildrenPlacement
//======================================================================
VIEW_CMD_DEF(MapChildrenPlacement, MapChildrenPlacement)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Map children (Experimental!)");
  sToolTipText  = ViewParams::docMapChildrenPlacement();
  sWhatsThis    = "Std_MapChildrenPlacement";
  sStatusTip    = sToolTipText;
  eType         = NoDefaultAction;
}

//======================================================================
// Std_MapChildrenPlacement
//======================================================================
VIEW_CMD_DEF(3DViewPreselection, EnablePreselection)
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("3D view pre-selection");
  sToolTipText  = QT_TR_NOOP("Enable pre-selection in 3D view");
  sWhatsThis    = "Std_3DViewPreselection";
  sStatusTip    = sToolTipText;
  sAccel        = "V, Q";
  eType         = NoDefaultAction;
}


//////////////////////////////////////////////////////////

class StdCmdSelOptions : public GroupCommand
{
public:
    StdCmdSelOptions()
        :GroupCommand("Std_SelOptions")
    {
        sGroup        = "View";
        sMenuText     = QT_TR_NOOP("Selection options");
        sToolTipText  = QT_TR_NOOP("Selection behavior options");
        sWhatsThis    = "Std_SelOptions";
        sStatusTip    = sToolTipText;
        eType         = 0;
        bCanLog       = false;

        addCommand(new StdCmdSelBoundingBox());
        addCommand(new StdCmdTightBoundingBox());
        addCommand(new StdCmdProjectBoundingBox());
        addCommand();
        addCommand(new StdCmdSelOnTop());
        addCommand(new StdCmdPreSelFaceOnTop());
        addCommand(new StdCmdPreSelFaceOutline());
        addCommand(new StdCmdSelFaceOutline());
        addCommand(new StdCmdNoSelFaceHighlightWithOutline());
        addCommand(new StdCmdNoPreSelFaceHighlightWithOutline());
        addCommand(new StdCmdPartialHighlightOnFullSelect());
        // addCommand(new StdCmdSelectionFaceWire());
        addCommand(new StdCmdAutoTransparentPick());
        // addCommand(new StdCmdPreselEdgeOnly());
        addCommand(new StdCmdHiddenLineSelectionOnTop()); 
        addCommand(new StdCmdOverrideSelectability()); 
        addCommand("Std_TreePreSelection");
        addCommand(new StdCmd3DViewPreselection());
        addCommand(new StdCmdSelHierarchyAscend());
        addCommand();
        addCommand(new StdCmdMapChildrenPlacement());
    };
    virtual const char* className() const {return "StdCmdSelOptions";}
};

//===========================================================================
// Std_DockOverlayAll
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayAll)

StdCmdDockOverlayAll::StdCmdDockOverlayAll()
  :Command("Std_DockOverlayAll")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Toggle overlay for all");
  sToolTipText  = QT_TR_NOOP("Toggle overlay mode for all docked windows");
  sWhatsThis    = "Std_DockOverlayAll";
  sStatusTip    = sToolTipText;
  sAccel        = "F4";
  eType         = 0;
}

void StdCmdDockOverlayAll::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleAll);
}

//===========================================================================
// Std_DockOverlayTransparentAll
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayTransparentAll)

StdCmdDockOverlayTransparentAll::StdCmdDockOverlayTransparentAll()
  :Command("Std_DockOverlayTransparentAll")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Toggle transparent for all");
  sToolTipText  = QT_TR_NOOP("Toggle transparent for all overlay docked window.\n"
                             "This makes the docked widget stay transparent at all times.");
  sWhatsThis    = "Std_DockOverlayTransparentAll";
  sStatusTip    = sToolTipText;
  sAccel        = "SHIFT+F4";
  eType         = 0;
}

void StdCmdDockOverlayTransparentAll::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleTransparentAll);
}

//===========================================================================
// Std_DockOverlayToggle
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayToggle)

StdCmdDockOverlayToggle::StdCmdDockOverlayToggle()
  :Command("Std_DockOverlayToggle")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Toggle overlay");
  sToolTipText  = QT_TR_NOOP("Toggle overlay mode of the docked window under cursor");
  sWhatsThis    = "Std_DockOverlayToggle";
  sStatusTip    = sToolTipText;
  sAccel        = "F3";
  eType         = 0;
}

void StdCmdDockOverlayToggle::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleActive);
}

//===========================================================================
// Std_DockOverlayToggleTransparent
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayToggleTransparent)

StdCmdDockOverlayToggleTransparent::StdCmdDockOverlayToggleTransparent()
  :Command("Std_DockOverlayToggleTransparent")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle transparent");
    sToolTipText  = QT_TR_NOOP("Toggle transparent mode for the docked widget under cursor.\n"
                               "This makes the docked widget stay transparent at all times.");
    sWhatsThis    = "Std_DockOverlayToggleTransparent";
    sStatusTip    = sToolTipText;
    sAccel        = "SHIFT+F3";
    eType         = 0;
}

void StdCmdDockOverlayToggleTransparent::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleTransparent);
}

//===========================================================================
// Std_DockOverlayToggleLeft
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayToggleLeft)

StdCmdDockOverlayToggleLeft::StdCmdDockOverlayToggleLeft()
  :Command("Std_DockOverlayToggleLeft")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle left");
    sToolTipText  = QT_TR_NOOP("Show/hide left overlay panel");
    sWhatsThis    = "Std_DockOverlayToggleLeft";
    sStatusTip    = sToolTipText;
    sAccel        = "Ctrl+Shift+Left";
    sPixmap       = "qss:overlay/close.svg";
    eType         = 0;
}

void StdCmdDockOverlayToggleLeft::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleLeft);
}

//===========================================================================
// Std_DockOverlayToggleRight
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayToggleRight)

StdCmdDockOverlayToggleRight::StdCmdDockOverlayToggleRight()
  :Command("Std_DockOverlayToggleRight")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle right");
    sToolTipText  = QT_TR_NOOP("Show/hide right overlay panel");
    sWhatsThis    = "Std_DockOverlayToggleRight";
    sStatusTip    = sToolTipText;
    sAccel        = "Ctrl+Shift+Right";
    sPixmap       = "qss:overlay/close.svg";
    eType         = 0;
}

void StdCmdDockOverlayToggleRight::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleRight);
}

//===========================================================================
// Std_DockOverlayToggleTop
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayToggleTop)

StdCmdDockOverlayToggleTop::StdCmdDockOverlayToggleTop()
  :Command("Std_DockOverlayToggleTop")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle top");
    sToolTipText  = QT_TR_NOOP("Show/hide top overlay panel");
    sWhatsThis    = "Std_DockOverlayToggleTop";
    sStatusTip    = sToolTipText;
    sAccel        = "Ctrl+Shift+Up";
    sPixmap       = "qss:overlay/close.svg";
    eType         = 0;
}

void StdCmdDockOverlayToggleTop::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleTop);
}

//===========================================================================
// Std_DockOverlayToggleBottom
//===========================================================================

DEF_STD_CMD(StdCmdDockOverlayToggleBottom)

StdCmdDockOverlayToggleBottom::StdCmdDockOverlayToggleBottom()
  :Command("Std_DockOverlayToggleBottom")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Toggle bottom");
    sToolTipText  = QT_TR_NOOP("Show/hide bottom overlay panel");
    sWhatsThis    = "Std_DockOverlayToggleBottom";
    sStatusTip    = sToolTipText;
    sAccel        = "Ctrl+Shift+Down";
    sPixmap       = "qss:overlay/close.svg";
    eType         = 0;
}

void StdCmdDockOverlayToggleBottom::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    OverlayManager::instance()->setOverlayMode(OverlayManager::OverlayMode::ToggleBottom);
}

//===========================================================================
// Std_DockOverlayMouseTransparent
//===========================================================================

DEF_STD_CMD_AC(StdCmdDockOverlayMouseTransparent)

StdCmdDockOverlayMouseTransparent::StdCmdDockOverlayMouseTransparent()
  :Command("Std_DockOverlayMouseTransparent")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Bypass mouse event in dock overlay");
  sToolTipText  = QT_TR_NOOP("Bypass all mouse event in dock overlay");
  sWhatsThis    = "Std_DockOverlayMouseTransparent";
  sStatusTip    = sToolTipText;
  sAccel        = "T, T";
  eType         = NoTransaction;
}

void StdCmdDockOverlayMouseTransparent::activated(int iMsg)
{
    (void)iMsg;
    bool checked = !OverlayManager::instance()->isMouseTransparent();
    OverlayManager::instance()->setMouseTransparent(checked);
    if(_pcAction)
        _pcAction->setChecked(checked,true);
}

Action * StdCmdDockOverlayMouseTransparent::createAction() {
    Action *pcAction = Command::createAction();
    pcAction->setCheckable(true);
    pcAction->setIcon(QIcon());
    _pcAction = pcAction;
    isActive();
    return pcAction;
}

bool StdCmdDockOverlayMouseTransparent::isActive() {
    bool checked = OverlayManager::instance()->isMouseTransparent();
    if(_pcAction && _pcAction->isChecked()!=checked)
        _pcAction->setChecked(checked,true);
    return true;
}


// ============================================================================

class StdCmdDockOverlay : public GroupCommand
{
public:
    StdCmdDockOverlay()
        :GroupCommand("Std_DockOverlay")
    {
        sGroup        = "View";
        sMenuText     = QT_TR_NOOP("Dock window overlay");
        sToolTipText  = QT_TR_NOOP("Setting docked window overlay mode");
        sWhatsThis    = "Std_DockOverlay";
        sStatusTip    = sToolTipText;
        eType         = 0;
        bCanLog       = false;

        addCommand(new StdCmdDockOverlayAll());
        addCommand(new StdCmdDockOverlayTransparentAll());
        addCommand();
        addCommand(new StdCmdDockOverlayToggle());
        addCommand(new StdCmdDockOverlayToggleTransparent());
        addCommand();
        addCommand(new StdCmdDockOverlayMouseTransparent());
        addCommand();
        addCommand(new StdCmdDockOverlayToggleLeft());
        addCommand(new StdCmdDockOverlayToggleRight());
        addCommand(new StdCmdDockOverlayToggleTop());
        addCommand(new StdCmdDockOverlayToggleBottom());
    };
    virtual const char* className() const {return "StdCmdDockOverlay";}
};

//===========================================================================
// Std_BindViewCamera
//===========================================================================

DEF_STD_CMD_AC(StdCmdBindViewCamera)

StdCmdBindViewCamera::StdCmdBindViewCamera()
  : Command("Std_BindViewCamera")
{
    sGroup        = "View";
    sMenuText     = QT_TR_NOOP("Bind view camera");
    sToolTipText  = QT_TR_NOOP("Bind the camera position of the active view to another view");
    sWhatsThis    = "Std_BindViewCamera";
    sStatusTip    = sToolTipText;
    eType         = 0;
}

void StdCmdBindViewCamera::activated(int iMsg)
{
    // Handled by the related QAction objects
    Q_UNUSED(iMsg); 
}

bool StdCmdBindViewCamera::isActive()
{
    if(Base::freecad_dynamic_cast<View3DInventor>(Application::Instance->activeView()))
        return true;
    return false;
}

Action * StdCmdBindViewCamera::createAction()
{
    Action *pcAction;
    pcAction = new ViewCameraBindingAction(this, getMainWindow());
    applyCommandData(this->className(), pcAction);
    return pcAction;
}

//===========================================================================
// Std_CloseLinkedView
//===========================================================================

DEF_STD_CMD_A(StdCmdCloseLinkedView)

StdCmdCloseLinkedView::StdCmdCloseLinkedView()
  :Command("Std_CloseLinkedView")
{
  sGroup        = "View";
  sMenuText     = QT_TR_NOOP("Close all linked view");
  sToolTipText  = QT_TR_NOOP("Close all views of the linked documents.\n"
                             "The linked documents stayed open.");
  sWhatsThis    = "Std_CloseLinkedView";
  sStatusTip    = sToolTipText;
  eType         = Alter3DView;
}

void StdCmdCloseLinkedView::activated(int iMsg)
{
    Q_UNUSED(iMsg); 
    App::Document *activeDoc = App::GetApplication().getActiveDocument();
    if (!activeDoc)
        return;
    for(auto doc : activeDoc->getDependentDocuments()) {
        if (doc == activeDoc) continue;
        Gui::Document *gdoc = Application::Instance->getDocument(doc);
        if (!gdoc) continue;
        for(auto view : gdoc->getMDIViews())
            getMainWindow()->removeWindow(view);
    }
}

bool StdCmdCloseLinkedView::isActive()
{
  return App::GetApplication().getActiveDocument() != nullptr;
}

//===========================================================================
// Std_ToolTipDisable
//===========================================================================

VIEW_CMD_DEF(ToolTipDisable, ToolTipDisable)
{
    sGroup        = "View";
    sMenuText     = QT_TR_NOOP("Disable tool tip");
    sToolTipText  = QT_TR_NOOP("Disable showing tool tips for all widgets.");
    sWhatsThis    = "Std_ToolTipDisable";
    sStatusTip    = sToolTipText;
    eType         = 0;
    sAccel        = "D, T";
}

//===========================================================================
// Std_StoreWorkingView
//===========================================================================
DEF_STD_CMD_A(StdStoreWorkingView)

StdStoreWorkingView::StdStoreWorkingView()
  : Command("Std_StoreWorkingView")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Store working view");
    sToolTipText  = QT_TR_NOOP("Store a document-specific temporary working view");
    sStatusTip    = QT_TR_NOOP("Store a document-specific temporary working view");
    sWhatsThis    = "Std_StoreWorkingView";
    sAccel        = "Shift+End";
    eType         = NoTransaction;
}

void StdStoreWorkingView::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    if (auto view = dynamic_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow())) {
        view->getViewer()->saveHomePosition();
    }
}

bool StdStoreWorkingView::isActive()
{
    return dynamic_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow());
}

//===========================================================================
// Std_RecallWorkingView
//===========================================================================
DEF_STD_CMD_A(StdRecallWorkingView)

StdRecallWorkingView::StdRecallWorkingView()
  : Command("Std_RecallWorkingView")
{
    sGroup        = "Standard-View";
    sMenuText     = QT_TR_NOOP("Recall working view");
    sToolTipText  = QT_TR_NOOP("Recall previously stored temporary working view");
    sStatusTip    = QT_TR_NOOP("Recall previously stored temporary working view");
    sWhatsThis    = "Std_RecallWorkingView";
    sAccel        = "End";
    eType         = NoTransaction;
}

void StdRecallWorkingView::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    if (auto view = dynamic_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow())) {
        if (view->getViewer()->hasHomePosition())
            view->getViewer()->resetToHomePosition();
    }
}

bool StdRecallWorkingView::isActive()
{
    auto view = dynamic_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow());
    return view && view->getViewer()->hasHomePosition();
}

//===========================================================================
// Instantiation
//===========================================================================


namespace Gui {

void CreateViewStdCommands()
{
    // NOLINTBEGIN
    CommandManager &rcCmdMgr = Application::Instance->commandManager();

    // views
    rcCmdMgr.addCommand(new StdCmdViewFitAll());
    rcCmdMgr.addCommand(new StdCmdViewVR());
    rcCmdMgr.addCommand(new StdCmdViewSelection());
    rcCmdMgr.addCommand(new StdCmdViewRotateLeft());
    rcCmdMgr.addCommand(new StdCmdViewRotateRight());
    rcCmdMgr.addCommand(new StdStoreWorkingView());
    rcCmdMgr.addCommand(new StdRecallWorkingView());

    rcCmdMgr.addCommand(new StdCmdViewExample1());
    rcCmdMgr.addCommand(new StdCmdViewExample2());
    rcCmdMgr.addCommand(new StdCmdViewExample3());

    rcCmdMgr.addCommand(new StdCmdViewIvStereoQuadBuff());
    rcCmdMgr.addCommand(new StdCmdViewIvStereoRedGreen());
    rcCmdMgr.addCommand(new StdCmdViewIvStereoInterleavedColumns());
    rcCmdMgr.addCommand(new StdCmdViewIvStereoInterleavedRows());
    rcCmdMgr.addCommand(new StdCmdViewIvStereoOff());

    rcCmdMgr.addCommand(new StdCmdViewIvIssueCamPos());

    rcCmdMgr.addCommand(new StdCmdViewCreate());
    rcCmdMgr.addCommand(new StdCmdViewSplitRight());
    rcCmdMgr.addCommand(new StdCmdViewSplitDown());
    rcCmdMgr.addCommand(new StdCmdViewSplitClose());
    rcCmdMgr.addCommand(new StdCmdViewSplitMaximize());
    rcCmdMgr.addCommand(new StdCmdViewCellShowObject());
    rcCmdMgr.addCommand(new StdViewScreenShot());
    rcCmdMgr.addCommand(new StdViewLoadImage());
    rcCmdMgr.addCommand(new StdCmdSaveView());
    rcCmdMgr.addCommand(new StdMainFullscreen());
    rcCmdMgr.addCommand(new StdViewDockUndockFullscreen());
    rcCmdMgr.addCommand(new StdCmdRenderSettings());
    rcCmdMgr.addCommand(new StdCmdToggleVisibility());
    rcCmdMgr.addCommand(new StdCmdToggleGroupVisibility());
    rcCmdMgr.addCommand(new StdCmdToggleShowOnTop());
    rcCmdMgr.addCommand(new StdCmdDisplayModeInView());
    rcCmdMgr.addCommand(new StdCmdToggleTransparency());
    rcCmdMgr.addCommand(new StdCmdToggleSelectability());
    rcCmdMgr.addCommand(new StdCmdShowSelection());
    rcCmdMgr.addCommand(new StdCmdHideSelection());
    rcCmdMgr.addCommand(new StdCmdSelectVisibleObjects());
    rcCmdMgr.addCommand(new StdCmdToggleObjects());
    rcCmdMgr.addCommand(new StdCmdShowObjects());
    rcCmdMgr.addCommand(new StdCmdHideObjects());
    rcCmdMgr.addCommand(new StdCmdSelectDependents());
    rcCmdMgr.addCommand(new StdCmdSelectDependentsRecursive());
    rcCmdMgr.addCommand(new StdOrthographicCamera());
    rcCmdMgr.addCommand(new StdPerspectiveCamera());
    rcCmdMgr.addCommand(new StdCmdToggleClipPlane());
    rcCmdMgr.addCommand(new StdCmdClipPlaneDragger());
    rcCmdMgr.addCommand(new StdCmdDrawStyle());
    rcCmdMgr.addCommand(new StdCmdViewSaveCamera());
    rcCmdMgr.addCommand(new StdCmdViewRestoreCamera());
    rcCmdMgr.addCommand(new StdCmdFreezeViews());
    rcCmdMgr.addCommand(new StdViewZoomIn());
    rcCmdMgr.addCommand(new StdViewZoomOut());
    rcCmdMgr.addCommand(new StdViewBoxZoom());
    rcCmdMgr.addCommand(new StdBoxSelection());
    rcCmdMgr.addCommand(new StdBoxElementSelection());
    rcCmdMgr.addCommand(new StdLassoElementSelection());
    rcCmdMgr.addCommand(new StdTreePreSelection());
    rcCmdMgr.addCommand(new StdCmdTreeExpand());
    rcCmdMgr.addCommand(new StdCmdTreeCollapse());
    rcCmdMgr.addCommand(new StdCmdTreeSelectAllInstances());
    rcCmdMgr.addCommand(new StdCmdMeasureDistance());
    rcCmdMgr.addCommand(new StdCmdSceneInspector());
    rcCmdMgr.addCommand(new StdCmdTextureMapping());
    rcCmdMgr.addCommand(new StdCmdDemoMode());
    rcCmdMgr.addCommand(new StdCmdToggleNavigation());
    rcCmdMgr.addCommand(new StdCmdAxisCross());
    rcCmdMgr.addCommand(new CmdViewMeasureClearAll());
    rcCmdMgr.addCommand(new CmdViewMeasureToggleAll());
    rcCmdMgr.addCommand(new StdCmdSelOptions());
    rcCmdMgr.addCommand(new StdCmdSelBack());
    rcCmdMgr.addCommand(new StdCmdSelForward());
    rcCmdMgr.addCommand(new StdCmdSelUp());
    rcCmdMgr.addCommand(new StdCmdTreeViewActions());
    rcCmdMgr.addCommand(new StdCmdBindViewCamera());
    rcCmdMgr.addCommand(new StdCmdPickGeometry());
    rcCmdMgr.addCommand(new StdCmdCloseLinkedView());
    rcCmdMgr.addCommand(new StdCmdItemMenu());
    rcCmdMgr.addCommand(new StdCmdToolTipDisable());
    rcCmdMgr.addCommand(new StdCmdTreeRelabelObject());

    rcCmdMgr.addCommand(new StdCmdDockOverlay());

    auto hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    if(hGrp->GetASCII("GestureRollFwdCommand").empty())
        hGrp->SetASCII("GestureRollFwdCommand","Std_SelForward");
    if(hGrp->GetASCII("GestureRollBackCommand").empty())
        hGrp->SetASCII("GestureRollBackCommand","Std_SelBack");
    // NOLINTEND
}

} // namespace Gui
