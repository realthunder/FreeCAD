/****************************************************************************
 *   Copyright (c) 2024 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
# include <sstream>
# include <QMessageBox>
#endif

#include <iomanip>
#include <boost/algorithm/string/predicate.hpp>

#include <QApplication>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObserver.h>
#include <App/Animation.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "Action.h"
#include "ActiveObjectList.h"
#include "Application.h"
#include "CommandT.h"
#include "Document.h"
#include "MainWindow.h"
#include "Selection.h"
#include "ViewProviderAnimation.h"
#include "WaitCursor.h"

FC_LOG_LEVEL_INIT("CommandAnimation", true, true)

using namespace Gui;

static inline App::KeyFrame *getActiveFrame()
{
    Gui::MDIView *activeView = Gui::Application::Instance->activeView();
    if ( activeView ) {
        return activeView->getActiveObject<App::KeyFrame*>(App::KeyFrame::ActiveKey);
    } else {
        return nullptr;
    }
}

static inline App::Animation *getActiveAnimation()
{
    auto frame = getActiveFrame();
    if (!frame)
        return nullptr;
    if (auto animation = Base::freecad_dynamic_cast<App::Animation>(frame))
        return animation;
    return Base::freecad_dynamic_cast<App::Animation>(frame->AnimationGroup.getValue());
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationNew)

StdCmdAnimationNew::StdCmdAnimationNew()
  : Command("Std_AnimationNew")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("New animation");
    sToolTipText  = QT_TR_NOOP("Create a new animation container");
    sWhatsThis    = "Std_AnimationNew";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationNew";
}

bool StdCmdAnimationNew::isActive() {
    return !!App::GetApplication().getActiveDocument();
}

static App::Animation *newAnimation()
{
    auto doc = App::GetApplication().getActiveDocument();
    if(!doc) {
        FC_ERR("no active document");
        return nullptr;
    }
    std::string name = doc->getUniqueObjectName("Animation");
    cmdAppDocument(doc, std::ostringstream() << "addObject('App::Animation','" << name << "')");
    auto animation = Base::freecad_dynamic_cast<App::Animation>(doc->getObject(name.c_str()));
    if (!animation)
        FC_THROWM(Base::RuntimeError, "Failed to create object");
    if (auto container = getActiveAnimation()) {
        container->Frames.set1Value(-1, animation);
    }
    Command::doCommand(Gui::Command::Gui, "Gui.activateView('Gui::View3DInventor', True)");
    cmdGuiDocument(App::GetApplication().getActiveDocument(), std::ostringstream()
                << "ActiveView.setActiveObject('" << App::Animation::ActiveKey << "', "
                << animation->getFullName(/*python*/true) << ")");
    return animation;
}

void StdCmdAnimationNew::activated(int)
{
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "New animation"));
    try {
        newAnimation();
        updateActive();
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Create animation failed"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationNewFrame)

StdCmdAnimationNewFrame::StdCmdAnimationNewFrame()
  : Command("Std_AnimationNewFrame")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("New key frame");
    sToolTipText  = QT_TR_NOOP("Create a new key frame and start capturing property changes");
    sWhatsThis    = "Std_AnimationNewFrame";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationNewFrame";
}

bool StdCmdAnimationNewFrame::isActive() {
    return !!App::GetApplication().getActiveDocument();
}

void StdCmdAnimationNewFrame::activated(int) {
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "New key frame"));
    try {
        auto container = getActiveAnimation();
        if (!container) {
            container = newAnimation();
        }
        cmdAppObject(container, "Recroding = True");
        updateActive();
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Failed to create key frame"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
    return;
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationPlay)

StdCmdAnimationPlay::StdCmdAnimationPlay()
  : Command("Std_AnimationPlay")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("Play animation");
    sToolTipText  = QT_TR_NOOP("Start play animation");
    sWhatsThis    = "Std_AnimationPlay";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationPlay";
}

bool StdCmdAnimationPlay::isActive()
{
    return !!App::GetApplication().getActiveDocument();
}

static void playAnimation(bool reset)
{
    const auto &sels = Selection().getSelection("*", ResolveMode::FollowLink, true);
    if (sels.empty())
        return;
    auto frame = Base::freecad_dynamic_cast<App::KeyFrame>(sels[0].pObject);
    if (!frame)
        frame = getActiveFrame();
    if (!frame)
        FC_ERR("No key frame");

    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Play animation"));
    try {
        AnimationManager::instance()->play(frame, reset);
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Animation failure"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
}

void StdCmdAnimationPlay::activated(int)
{
    if (AnimationManager::instance()->getState() == AnimationManager::State::Paused)
        playAnimation(/*reset*/false);
    else
        playAnimation(/*reset*/true);
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationPrev)

StdCmdAnimationPrev::StdCmdAnimationPrev()
  : Command("Std_AnimationPrev")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("Previous frame");
    sToolTipText  = QT_TR_NOOP("Jump to previous key frame. Hold CTRL to jump to previous frame");
    sWhatsThis    = "Std_AnimationPrev";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationPrev";
}

bool StdCmdAnimationPrev::isActive()
{
    return !!App::GetApplication().getActiveDocument();
}

void StdCmdAnimationPrev::activated(int)
{
    AnimationManager::instance()->gotoPrev(
            QApplication::queryKeyboardModifiers() != Qt::ControlModifier);
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationNext)

StdCmdAnimationNext::StdCmdAnimationNext()
  : Command("Std_AnimationNext")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("Next frame");
    sToolTipText  = QT_TR_NOOP("Jump to next key frame. Hold CTRL to jump to next frame");
    sWhatsThis    = "Std_AnimationNext";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationNext";
}

bool StdCmdAnimationNext::isActive()
{
    return !!App::GetApplication().getActiveDocument();
}

void StdCmdAnimationNext::activated(int)
{
    AnimationManager::instance()->gotoNext(
            QApplication::queryKeyboardModifiers() != Qt::ControlModifier);
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationStop)

StdCmdAnimationStop::StdCmdAnimationStop()
  : Command("Std_AnimationStop")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("Stop animation");
    sToolTipText  = QT_TR_NOOP("Stop playing animation");
    sWhatsThis    = "Std_AnimationStop";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationStop";
}

bool StdCmdAnimationStop::isActive()
{
    return !!App::GetApplication().getActiveDocument();
}

void StdCmdAnimationStop::activated(int)
{
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Stop animation"));
    try {
        AnimationManager::instance()->stop();
    } catch (Base::Exception &e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Animation failure"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdAnimationPause)

StdCmdAnimationPause::StdCmdAnimationPause()
  : Command("Std_AnimationPause")
{
    sGroup        = "Animation";
    sMenuText     = QT_TR_NOOP("Pause animation");
    sToolTipText  = QT_TR_NOOP("Pause playing animation");
    sWhatsThis    = "Std_AnimationPause";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "AnimationPause";
}

bool StdCmdAnimationPause::isActive()
{
    return !!App::GetApplication().getActiveDocument();
}

void StdCmdAnimationPause::activated(int)
{
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Pause animation"));
    try {
        AnimationManager::instance()->pause();
    } catch (Base::Exception &e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Animation failure"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

class StdCmdAnimationGroup : public GroupCommand
{
public:
    StdCmdAnimationGroup()
        : GroupCommand("Std_AnimationGroup")
    {
        sGroup        = "Animation";
        sMenuText     = QT_TR_NOOP("Animation");
        sToolTipText  = QT_TR_NOOP("Animation related actions");
        sWhatsThis    = "Std_AnimationGroup";
        sStatusTip    = sToolTipText;
        eType         = AlterSelection;
        bCanLog       = false;

        addCommand(new StdCmdAnimationNew());
        addCommand(new StdCmdAnimationNewFrame());
        addCommand(new StdCmdAnimationPrev());
        addCommand(new StdCmdAnimationPlay());
        addCommand(new StdCmdAnimationNext());
        addCommand(new StdCmdAnimationPause());
        addCommand(new StdCmdAnimationStop());
    }

    const char* className() const override {return "StdCmdAnimationGroup";}
};


//===========================================================================
// Instantiation
//===========================================================================


namespace Gui {

void CreateAnimationCommands()
{
    CommandManager &rcCmdMgr = Application::Instance->commandManager();
    rcCmdMgr.addCommand(new StdCmdAnimationGroup());
}

} // namespace Gui

