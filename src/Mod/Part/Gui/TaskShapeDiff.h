/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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


#ifndef PARTGUI_TASKSHAPEDIFF_H
#define PARTGUI_TASKSHAPEDIFF_H

#include <QTimer>
#include <QWidget>

#include <App/DocumentObserver.h>
#include <Gui/TaskView/TaskDialog.h>
#include <Mod/Part/PartGlobal.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace PartGui
{

class ViewProviderPartExt;

/** The panel of the edit mode that shows an object's last change
 * (Gui::ViewProvider::ValueDiff; docs/TransactionLog.md sec 32.19).
 *
 * Which change it is, and how its two states are drawn over the object: by
 * the faces or by the material, where between before and after, with what
 * did not change or without -- and what was found, in words. It asks the
 * view provider (showValueDiff()), which draws the two as a node of the
 * edit session. Of plain widgets only, so that the panel a browser is sent
 * of it is the same one.
 */
class PartGuiExport ShapeDiffPanel: public QWidget
{
    Q_OBJECT

public:
    explicit ShapeDiffPanel(ViewProviderPartExt* vp, QWidget* parent = nullptr);

private:
    ViewProviderPartExt* view() const;
    /// Ask for the two states as the panel has them set, and say what is shown
    void apply();
    void say();

    App::DocumentObjectT _object;
    std::string _prop;
    std::string _before;
    std::string _after;
    QLabel* _what {nullptr};
    QComboBox* _mode {nullptr};
    QSlider* _position {nullptr};
    QCheckBox* _only {nullptr};
    QLabel* _found {nullptr};
    /// The material of two heavy solids is not waited for: asked again
    QTimer _pending;
};

class PartGuiExport TaskShapeDiff: public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    explicit TaskShapeDiff(ViewProviderPartExt* vp);

    bool accept() override;
    bool reject() override;
    QDialogButtonBox::StandardButtons getStandardButtons() const override
    {
        return QDialogButtonBox::Close;
    }

private:
    bool leave();

    App::DocumentObjectT _object;
};

}  // namespace PartGui

#endif  // PARTGUI_TASKSHAPEDIFF_H
