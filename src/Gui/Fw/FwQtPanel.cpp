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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <QWidget>
#endif

#include "Fw/FwQtPanel.h"
#include "Fw/FwQtView.h"
#include "Fw/FwWidgets.h"

using namespace Gui::FwQt;

PanelDialog::PanelDialog(std::unique_ptr<PanelHooks> hooks, const QList<Fw::Widget*>& forms)
    : _hooks(std::move(hooks))
    , _forms(forms)
{
    try {
        for (Fw::Widget* form : forms) {
            QWidget* w = realize(form, nullptr);
            if (w)
                Content.push_back(w);
        }
    }
    catch (...) {
        for (QWidget* w : Content)
            delete w;
        Content.clear();
        throw;
    }
}

PanelDialog::~PanelDialog()
{
    detachViews();
}

void PanelDialog::detachViews()
{
    for (Fw::Widget* form : _forms)
        if (View* v = View::of(form))
            v->release(false);
}

bool PanelDialog::boolHook(const char* name, bool fallback) const
{
    if (!_hooks || !_hooks->has(name))
        return fallback;
    QVariant r = _hooks->call(name);
    return r.isValid() ? r.toBool() : fallback;
}

QDialogButtonBox::StandardButtons PanelDialog::getStandardButtons() const
{
    if (_hooks && _hooks->has("getStandardButtons")) {
        QVariant r = _hooks->call("getStandardButtons");
        if (r.isValid())
            return QDialogButtonBox::StandardButtons(r.toInt());
    }
    return TaskDialog::getStandardButtons();
}

bool PanelDialog::isAllowedAlterDocument() const
{
    // TaskDialogPython's default for a Python panel without the hook
    return boolHook("isAllowedAlterDocument", true);
}

bool PanelDialog::isAllowedAlterView() const
{
    return boolHook("isAllowedAlterView", true);
}

bool PanelDialog::isAllowedAlterSelection() const
{
    return boolHook("isAllowedAlterSelection", true);
}

bool PanelDialog::needsFullSpace() const
{
    return boolHook("needsFullSpace", false);
}

void PanelDialog::open()
{
    if (_hooks && _hooks->has("open"))
        _hooks->call("open");
}

void PanelDialog::clicked(int index)
{
    if (_hooks && _hooks->has("clicked"))
        _hooks->call("clicked", QVariantList {index});
}

bool PanelDialog::accept()
{
    bool ok = boolHook("accept", true);
    if (ok)
        detachViews();
    return ok;
}

bool PanelDialog::reject()
{
    bool ok = boolHook("reject", true);
    if (ok)
        detachViews();
    return ok;
}

void PanelDialog::helpRequested()
{
    if (_hooks && _hooks->has("helpRequested"))
        _hooks->call("helpRequested");
}

#include "moc_FwQtPanel.cpp"
