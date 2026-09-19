// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef GUI_TASKORIENTATION_H
#define GUI_TASKORIENTATION_H

/* The first task panel ported onto the host widget layer (docs/Sandbox.md
 * 7.12, H1).  The panel is a tree of MODELS (Gui::Fw), not of Qt
 * widgets: `ui` is the form generated from TaskOrientation.ui by
 * src/Tools/fwuic.py, its members typed models with Qt's method and
 * signal names, so the connects below are what they were.  The dialog
 * realizes the form through the Qt backend (FwQt::realize) when it is
 * shown, and the same models will render in the browser tier through
 * the DOM backend later.  This translation unit includes no QtWidgets. */

#include <Gui/TaskView/TaskDialog.h>
#include <App/DocumentObserver.h>
#include <App/GeoFeature.h>
#include <memory>

namespace Gui {

namespace Fw {
class UiForm;
}
class Ui_TaskOrientation;

class GuiExport TaskOrientation : public QObject
{
    Q_OBJECT

public:
    explicit TaskOrientation(App::GeoFeature* obj, QObject* parent = nullptr);
    ~TaskOrientation() override;

    void open();
    void accept();
    void reject();

    /// The form's models (the root the backend realizes).
    Fw::UiForm* form() const
    {
        return _form.get();
    }
    /// The form's title, as the file has it (translated once realized).
    QString windowTitle() const;

private:
    void restore(const Base::Placement&);
    void onPreview();
    void updateIcon();
    void updatePlacement();

private:
    std::unique_ptr<Fw::UiForm> _form;
    std::unique_ptr<Ui_TaskOrientation> ui;
    App::WeakPtrT<App::GeoFeature> feature;
};

class GuiExport TaskOrientationDialog : public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    explicit TaskOrientationDialog(App::GeoFeature* obj);
    ~TaskOrientationDialog() override;

public:
    void open() override;
    bool accept() override;
    bool reject() override;

    QDialogButtonBox::StandardButtons getStandardButtons() const override {
        return QDialogButtonBox::Ok | QDialogButtonBox::Cancel;
    }

    TaskOrientation* panel() const
    {
        return widget;
    }

private:
    TaskOrientation* widget;
};

}

#endif // GUI_TASKORIENTATION_H
