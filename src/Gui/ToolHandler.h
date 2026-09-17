/***************************************************************************
 *   Copyright (c) 2024 Pierre-Louis Boyer <development@ondsel.com>        *
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

#include <list>
#include <map>
#include <vector>

#include <QCursor>
#include <QPixmap>
#include <QString>

#include <FCGlobal.h>

class QWidget;

namespace Gui
{
class View3DInventorViewer;
class ViewerContext;
struct InputHint;

/** The cursor and activation lifecycle every modal tool shares.
 *
 * A tool handler takes over a view: it swaps in its own cursor, announces
 * what the user can do next in the hint bar, and gives both back when it
 * is done. That much is the same for a sketch tool and a drawing tool, so
 * it lives here rather than in either workbench.
 *
 * Activation and deactivation are NVI: the public entry points do the
 * shared work and call the private hooks a handler overrides.
 */
class GuiExport ToolHandler
{
public:
    ToolHandler() = default;
    virtual ~ToolHandler() = default;

    /** Takes the view: remembers its cursor, sets the tool's, shows the
     * hints, then runs the handler's own activation hooks.
     *
     * Returns false, having done nothing, when there is no view to take.
     */
    bool activate();
    virtual void deactivate();

    virtual void quit()
    {}

    /// updates the actCursor with the icon by calling getCrosshairCursorSVGName(),
    /// enabling to set data member dependent icons (i.e. for different construction methods)
    void updateCursor();

    /// what the user can do next, shown in the hint bar; empty by default
    virtual std::list<InputHint> getToolHints() const;
    void updateHint() const;

private:  // NVI
    virtual void preActivated()
    {}
    virtual void activated()
    {}
    virtual void deactivated()
    {}
    virtual void postDeactivated()
    {}

protected:  // NVI requiring base implementation
    virtual QString getCrosshairCursorSVGName() const;

protected:
    // helpers
    /**
     * Sets a cursor for 3D inventor view.
     * pixmap as a cursor image in device independent pixels.
     *
     * \param autoScale - set this to false if pixmap already scaled for HiDPI
     **/

    /** @name Icon helpers */
    //@{
    void setCursor(const QPixmap& pixmap, int x, int y, bool autoScale = true);

    /// restitutes the cursor that was in use at the moment of starting the tool (i.e. oldCursor)
    void unsetCursor();

    /// restitutes the cached cursor (e.g. without any tail due to autoconstraints, ...)
    void applyCursor();

    void addCursorTail(std::vector<QPixmap>& pixmaps);

    /// returns the color to be used for the crosshair (configurable as a parameter)
    unsigned long getCrosshairColor();

    /** The client's pixel ratio, not a screen's: a mirror answers this
     * from what the browser stated over the wire.
     */
    qreal devicePixelRatio();
    //@}

    /** The view this tool runs in, or null.
     *
     * The base answers with the active window's, which is what a tool
     * started from a toolbar means. A tool that belongs to one edit
     * session overrides this to name that session's view: "which window
     * is active" is a different question, and in a process serving
     * several browsers it has no useful answer (docs/ThinClient.md sec 8.3).
     */
    virtual Gui::ViewerContext* getViewer();

    /** The same view, when it is a desktop one with a widget.
     *
     * Null for a client's mirror, which is how the surfaces that need a
     * Qt widget -- the cursor, the on-view parameters -- find out they
     * cannot run here. Those are the DOM layer's job instead
     * (docs/ThinClient.md sec 8.7).
     */
    Gui::View3DInventorViewer* getDesktopViewer();

private:
    void setSvgCursor(const QString& svgName,
                      int x,
                      int y,
                      const std::map<unsigned long, unsigned long>& colorMapping =
                          std::map<unsigned long, unsigned long>());

    void applyCursor(QCursor& newCursor);

    void setCrosshairCursor(const QString& svgName);
    void setCrosshairCursor(const char* svgName);

protected:
    QCursor oldCursor;
    QCursor actCursor;
    QPixmap actCursorPixmap;
};

}  // namespace Gui
