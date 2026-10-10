/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef GUI_READBACKFRAMEPACER_H
#define GUI_READBACKFRAMEPACER_H

#include <functional>
#include <utility>

#include <QTimer>

#include "RenderParams.h"
#include "Renderer/Renderer.h"

namespace Gui
{

/// The host's half of Render/ReadbackFrameMode, for a view whose backend
/// reaches the screen through a read-back (Direct3D, Vulkan, Metal:
/// docs/DeviceAdoption.md section 10).
///
/// A pipelined frame shows the newest copy that has landed, which is a
/// frame or two old. In a run of frames that costs nothing to look at
/// and saves the wait; at the end of the run it leaves the screen behind
/// the scene until something draws again -- a highlight that never
/// comes, a view still at its old size. So the host says before each
/// frame whether it is one of a run (begin), and after a pipelined
/// frame it owes the backend one frame that waits (end): asked for by a
/// timer that every further frame puts off, so it comes once, when the
/// run has stopped.
class ReadbackFramePacer
{
public:
    /// The values of Render/ReadbackFrameMode.
    enum Mode
    {
        Wait = 0,
        PipelinedAnimation = 1,
        Pipelined = 2,
    };

    /// \a redraw asks the host for a frame.
    explicit ReadbackFramePacer(std::function<void()> redraw)
    {
        _timer.setSingleShot(true);
        // Long enough that a run of frames never meets it, since each
        // frame restarts it at its end; short enough that the frame it
        // asks for does not read as a second, late change.
        _timer.setInterval(50);
        QObject::connect(&_timer, &QTimer::timeout, &_timer,
                         [this, redraw = std::move(redraw)]() {
                             _settle = true;
                             redraw();
                         });
    }

    /// Before the backend's frame. \a animating: the view is redrawing
    /// by itself -- a camera animation, a spin, animated content.
    void begin(Render::Renderer *renderer, bool animating)
    {
        _timer.stop();
        const long mode = RenderParams::getReadbackFrameMode();
        const bool pipelined = !_settle
            && (mode >= Pipelined
                || (mode == PipelinedAnimation && animating));
        _settle = false;
        renderer->setFramePipelined(pipelined);
    }

    /// After it. \a drawn: the backend drew the frame. One it did not
    /// draw changes nothing on screen, and asking again for the frame
    /// that waits would only repeat the failure twenty times a second.
    void end(Render::Renderer *renderer, bool drawn)
    {
        if (drawn && renderer->frameTrails())
            _timer.start();
    }

private:
    QTimer _timer;
    /// The next frame is the one the timer asked for: it waits whatever
    /// the mode says.
    bool _settle = false;
};

} // namespace Gui

#endif // GUI_READBACKFRAMEPACER_H
