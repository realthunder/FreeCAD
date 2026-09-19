/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef GUI_FW_QTPANEL_H
#define GUI_FW_QTPANEL_H

/* The host widget layer (docs/Sandbox.md 7.12): the task panel.
 *
 * A TaskDialog whose content is the Qt rendering of form models and
 * whose hooks (accept, reject, clicked, open, getStandardButtons, the
 * isAllowedAlter* queries, helpRequested, needsFullSpace) forward to a
 * `PanelHooks` -- the sandbox guest's panel stand-in behind the bridge,
 * or nothing.  `Control` and `TaskView` see a TaskDialog like any
 * other; TaskDialogPython's defaults apply where a hook is absent.
 */

#include <memory>

#include <QList>
#include <QVariant>
#include <QVariantList>

#include "TaskView/TaskDialog.h"

namespace Gui
{
namespace Fw
{
class Widget;
}

namespace FwQt
{

/// Where a panel's hooks live.
class GuiExport PanelHooks
{
public:
    virtual ~PanelHooks() = default;
    virtual bool has(const char* hook) const = 0;
    /// Call `hook`; an invalid variant when it answered nothing.
    virtual QVariant call(const char* hook, const QVariantList& args = QVariantList()) = 0;
};

class GuiExport PanelDialog : public Gui::TaskView::TaskDialog
{
    Q_OBJECT
public:
    /// Realizes each of `forms` as the dialog's content.  Throws
    /// Base::Exception when a form cannot be realized.
    PanelDialog(std::unique_ptr<PanelHooks> hooks, const QList<Fw::Widget*>& forms);
    ~PanelDialog() override;

    const QList<Fw::Widget*>& forms() const
    {
        return _forms;
    }
    /// Detach the views from the content widgets (the dialog owns and
    /// deletes them).
    void detachViews();

    QDialogButtonBox::StandardButtons getStandardButtons() const override;
    bool isAllowedAlterDocument() const override;
    bool isAllowedAlterView() const override;
    bool isAllowedAlterSelection() const override;
    bool needsFullSpace() const override;
    void open() override;
    void clicked(int) override;
    bool accept() override;
    bool reject() override;
    void helpRequested() override;

private:
    bool boolHook(const char* name, bool fallback) const;

    std::unique_ptr<PanelHooks> _hooks;
    QList<Fw::Widget*> _forms;
};

}  // namespace FwQt
}  // namespace Gui

#endif  // GUI_FW_QTPANEL_H
