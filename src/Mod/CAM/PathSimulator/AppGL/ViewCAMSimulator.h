// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Shai Seger <shaise at gmail>                       *
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

#pragma once

#include <Gui/MDIViewWithCamera.h>

class SoCamera;

namespace Gui
{
class View3DSettings;
}  // namespace Gui

namespace CAMSimulator
{
class GuiDisplay;
class DlgCAMSimulator;
class Dummy3DViewer;
class View3DSettings;
class CAMSettings;

class ViewCAMSimulator: public Gui::MDIViewWithCamera
{
public:
    ViewCAMSimulator(
        Gui::Document* pcDocument,
        QWidget* parent,
        Qt::WindowFlags wflags = Qt::WindowFlags()
    );

    ViewCAMSimulator* clone() override;
    ViewCAMSimulator* clone(Gui::Document* doc);

    static ViewCAMSimulator& instance(Gui::Document* doc = nullptr);
    DlgCAMSimulator& dlg();

    bool onMsg(const char* pMsg, const char** ppReturn) override;
    bool onHasMsg(const char* pMsg) const override;

    const std::string& getCamera() const override;
    bool setCamera(const char* pCamera) override;

private Q_SLOTS:
    void onSimulationStarted();

private:
    void initCamera();
    void cloneCamera(SoCamera& camera);
    void applySettings();
    /// Frame the camera on everything there is to see -- the viewer's
    /// own scene AND the shapes the simulator draws itself, which
    /// while attached are not in that scene. Every "view fit" goes
    /// through here rather than calling the viewer's viewAll().
    void viewFit();

public:
    /// Decide between the attached and the standalone drawing path
    /// and put the widget stack in the matching shape. Called at
    /// construction and again whenever the viewer's renderer could
    /// have changed -- a render-cache preference change destroys the
    /// renderer, and a destroyed renderer has forgotten its consumer.
    void updateHostAttachment();

protected:
    GuiDisplay* mGui = nullptr;
    DlgCAMSimulator* mDlg = nullptr;
    Dummy3DViewer* mDummyViewer = nullptr;

    std::unique_ptr<View3DSettings> mViewSettings;
    std::unique_ptr<CAMSettings> mCAMSettings;
};

}  // namespace CAMSimulator
