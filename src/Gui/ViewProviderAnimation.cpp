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
# include <QApplication>
# include <QMouseEvent>
# include <QByteArray>
# include <QMenu>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <App/Animation.h>
#include <App/AutoTransaction.h>
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "BitmapFactory.h"
#include "Application.h"
#include "Document.h"
#include "Command.h"
#include "Action.h"
#include "ViewParams.h"
#include "ViewProviderAnimation.h"

using namespace Gui;
namespace sp = std::placeholders;

/////////////////////////////////////////////////////////////////////////

AnimationManager::AnimationManager()
{
    App::GetApplication().signalDeleteDocument.connect([this](const App::Document &doc) {
        if (recordingFrame && &doc == recordingFrame->getDocument()) {
            changeState(State::Idle);
            recordingFrame = nullptr;
        }
    });
    App::GetApplication().signalDeletedObject.connect([this](const App::DocumentObject &obj) {
        if (&obj == recordingFrame) {
            changeState(State::Idle);
            recordingFrame = nullptr;
        }
    });
    App::GetApplication().signalBeforeChangeDocument.connect(
        [this](const App::Document &doc, const App::Property &prop) {
            if (recordingFrame && recordingFrame->getDocument() == &doc && prop.canInterpolate())
                recordingFrame->beforeChangeProperty(prop);
        }
    );
    App::GetApplication().signalChangedDocument.connect(
        [this](const App::Document &doc, const App::Property &prop) {
            if (recordingFrame && recordingFrame->getDocument() == &doc && prop.canInterpolate())
                recordingFrame->onChangeProperty(prop);
        }
    );
    App::GetApplication().signalBeforeChangeObject.connect(
        [this](const App::DocumentObject &obj, const App::Property &prop) {
            if (recordingFrame && !obj.isDerivedFrom(App::KeyFrame::getClassTypeId()) && prop.canInterpolate())
                recordingFrame->beforeChangeProperty(prop);
        }
    );
    App::GetApplication().signalChangedObject.connect(
            std::bind(&AnimationManager::onChangedProperty, this, sp::_1, sp::_2));
}

AnimationManager::~AnimationManager()
{
}

void AnimationManager::changeState(State state)
{
    if (!playingFrame)
        state = State::Idle;

    if (currentState == state)
        return;

    State oldState = currentState;
    currentState = state;

    try {
        signalChangeState(oldState, state);
    } catch (Base::Exception &) {
    } catch (...)
    {}

    switch (state) {
    case State::Playing:
        playingFrame->Playing.setValue(true);
        playingFrame->Progress.touch();
        timer.restart();
        break;
    case State::Paused:
        playingFrame->Playing.setValue(false);
        break;
    case State::Idle:
        if (playingFrame)
            playingFrame->Playing.setValue(false);
        break;
    }
}

void AnimationManager::onChangedProperty(const App::DocumentObject &obj, const App::Property &prop)
{
    if (auto keyFrame = Base::freecad_dynamic_cast<const App::KeyFrame>(&obj)) {
        if (&prop == &keyFrame->Recording
                && !keyFrame->isDerivedFrom(App::Animation::getClassTypeId())) {
            if (keyFrame->Recording.getValue()) {
                if (recordingFrame && recordingFrame != keyFrame)
                    recordingFrame->Recording.setValue(false);
                changeState(State::Idle);
                recordingFrame = const_cast<App::KeyFrame*>(keyFrame);
            }
            else if (recordingFrame && recordingFrame == keyFrame) {
                recordingFrame = nullptr;
            }
        }
        else if (&prop == &keyFrame->Playing) {
            if (!keyFrame->Playing.getValue()) {
                if (keyFrame == playingFrame) {
                    changeState(State::Idle);
                }
            }
            else if (keyFrame != playingFrame) {
                auto parent = Base::freecad_dynamic_cast<App::KeyFrame>(
                        keyFrame->AnimationGroup.getValue());
                if (!parent || !parent->Playing.getValue()) {
                    changeState(State::Idle);
                    playingFrame = const_cast<App::KeyFrame*>(keyFrame);
                    changeState(State::Playing);
                }
            }
        }
    }
    else if (recordingFrame && prop.canInterpolate())
        recordingFrame->onChangeProperty(prop);
}

bool AnimationManager::nextFrame(MDIView *view)
{
    if (!playingFrame
            || currentState != State::Playing
            || !view
            || playingFrame->getDocument() != view->getAppDocument()
            || view != view->getGuiDocument()->getActiveView())
        return false;
    double elapsed = timer.restart() / 1000.0;
    return playingFrame->next(elapsed);
}

void AnimationManager::gotoNext(bool keyframe)
{
}

void AnimationManager::gotoPrev(bool keyframe)
{
}

AnimationManager *AnimationManager::instance()
{
    static AnimationManager *inst;
    if (!inst)
        inst = new AnimationManager();
    return inst;
}

void AnimationManager::play(App::KeyFrame *frame, bool reset)
{
    changeState(State::Idle);
    playingFrame = frame;
    if (reset)
        frame->Progress.setValue(0);
    changeState(State::Playing);
}

void AnimationManager::pause()
{
    changeState(State::Paused);
}

void AnimationManager::stop()
{
    changeState(State::Idle);
}

AnimationManager::State AnimationManager::getState()
{
    return currentState;
}

/////////////////////////////////////////////////////////////////////////

PROPERTY_SOURCE(Gui::ViewProviderKeyFrame, Gui::ViewProviderDocumentObject)

ViewProviderKeyFrame::ViewProviderKeyFrame()
{
    sPixmap = "KeyFrame";
}

ViewProviderKeyFrame::~ViewProviderKeyFrame()
{
}

bool ViewProviderKeyFrame::doubleClicked()
{
    auto activeDoc = Gui::Application::Instance->activeDocument();
    if(!activeDoc)
        activeDoc = getDocument();
    auto activeView = activeDoc->setActiveView(this);
    if(!activeView)
        return false;

    if (activeView->isActiveObject(getObject(),App::Animation::ActiveKey)) {
        Gui::Command::doCommand(Gui::Command::Gui,
                "Gui.ActiveDocument.ActiveView.setActiveObject('%s', None)",
                App::Animation::ActiveKey);
    } else {
        Gui::Command::doCommand(Gui::Command::Gui,
                "Gui.ActiveDocument.ActiveView.setActiveObject('%s', %s)",
                App::Animation::ActiveKey,
                getObject()->getFullName(/*python*/true));
    }

    return true;
}

bool ViewProviderKeyFrame::setEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default) {
        return false;
    }
    return inherited::setEdit(ModNum);
}

/////////////////////////////////////////////////////////////////////////

PROPERTY_SOURCE(Gui::ViewProviderAnimation, Gui::ViewProviderKeyFrame)

ViewProviderAnimation::ViewProviderAnimation()
{
    sPixmap = "Animation";
}

ViewProviderAnimation::~ViewProviderAnimation()
{
}

bool ViewProviderAnimation::setEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default) {
        return false;
    }
    return inherited::setEdit(ModNum);
}
