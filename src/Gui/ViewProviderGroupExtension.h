/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef GUI_VIEWPROVIDERGROUPEXTENSION_H
#define GUI_VIEWPROVIDERGROUPEXTENSION_H

#include "ViewProviderExtensionPython.h"


namespace Gui
{

class GuiExport ViewProviderGroupExtension : public ViewProviderExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderGroupExtension);

public:
    /// Constructor
    ViewProviderGroupExtension();
    ~ViewProviderGroupExtension() override;

    void extensionUpdateData(const App::Property*) override;
    void extensionClaimChildren(std::vector<App::DocumentObject*> &)const override;
    bool extensionCanDragObjects() const override;
    bool extensionCanDragObject(App::DocumentObject*) const override;
    void extensionDragObject(App::DocumentObject*) override;
    bool extensionCanDropObjects() const override;
    bool extensionCanDropObject(App::DocumentObject*) const override;
    void extensionDropObject(App::DocumentObject*) override;
    bool extensionOnDelete(const std::vector<std::string> &) override;

    void extensionAttach(App::DocumentObject* pcObject) override;

    void extensionFinishRestoring() override;

    int extensionReorderObjects(const std::vector<App::DocumentObject*> &objs, App::DocumentObject* before) override;
    int extensionCanReorderObject(App::DocumentObject* obj, App::DocumentObject* before) override;

    bool extensionGetToolTip(const QByteArray &tag, QString &tooltip) const override;
    bool extensionIconMouseEvent(QMouseEvent *, const QByteArray &) override;

protected:
    void buildExport() const;

    /** Request a buildExport(), coalesced to at most one per event loop turn.
     *
     * buildExport() is O(children), so a burst of changes touching the group
     * (filling it, or a recompute cascading through it) would otherwise pay
     * the full scan once per change. Any pending rebuild is flushed
     * synchronously before the children are read, so no caller observes a
     * stale export list through the view provider.
     */
    void scheduleBuildExport() const;
    /// Run this group's pending buildExport(), if any.
    void flushBuildExport() const;

    virtual bool shouldCheckExport(App::DocumentObject *) const;
};

using ViewProviderGroupExtensionPython = ViewProviderExtensionPythonT<Gui::ViewProviderGroupExtension>;

} //namespace Gui

#endif // GUI_VIEWPROVIDERGROUPEXTENSION_H
