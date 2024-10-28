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

#ifndef GUI_VIEWPROVIDER_ANIMATION_H
#define GUI_VIEWPROVIDER_ANIMATION_H

#include <QElapsedTimer>

#include "MDIView.h"
#include "ViewProviderDocumentObject.h"

namespace App {
class KeyFrame;
class Animation;
}

namespace Gui {

class GuiExport ViewProviderKeyFrame : public ViewProviderDocumentObject {
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderKeyFrame);
    typedef ViewProviderDocumentObject inherited;
public:
    ViewProviderKeyFrame();
    virtual ~ViewProviderKeyFrame();

    bool doubleClicked() override;

protected:
    bool setEdit(int ModNum) override;
};

class GuiExport ViewProviderAnimation : public ViewProviderKeyFrame {
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderKeyFrame);
    typedef ViewProviderKeyFrame inherited;
public:
    ViewProviderAnimation();
    virtual ~ViewProviderAnimation();

protected:
    bool setEdit(int ModNum) override;
};

class GuiExport AnimationManager {
public:
    AnimationManager();
    virtual ~AnimationManager();

    bool nextFrame(Gui::MDIView *view);
    void stop();
    void pause();
    void play(App::KeyFrame *frame, bool reset = true);
    void gotoNext(bool keyframe = true);
    void gotoPrev(bool keyframe = true);

    enum class State {
        Idle,
        Playing,
        Paused,
    };
    State getState();
    boost::signals2::signal<void (State /*oldState*/, State /*newState*/)> signalChangeState;

    static AnimationManager *instance();

private:
    void onChangedProperty(const App::DocumentObject &obj, const App::Property &prop);
    void changeState(State state);

private:
    App::KeyFrame *recordingFrame = nullptr;
    App::KeyFrame *playingFrame = nullptr;
    QElapsedTimer timer;
    State currentState = State::Idle;
};

} // namespace Gui

#endif //GUI_VIEWPROVIDER_ANIMATION_H
