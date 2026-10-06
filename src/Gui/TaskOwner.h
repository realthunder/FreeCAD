/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef GUI_TASKOWNER_H
#define GUI_TASKOWNER_H

#include <memory>

#include <QObject>
#include <QPointer>
#include <FCGlobal.h>

namespace Gui
{

class Document;
class MDIView;
class ViewerContext;

/** The view a task panel belongs to (docs/TaskPanelPerView.md sec 3).
 *
 * A value, cheap to copy and compare. Upstream keys a task panel by its
 * document; here the key is a VIEW, and the document is what the view
 * gives when asked, so everything keyed by document still has an answer.
 *
 * Three kinds:
 *
 *  - a desktop view of any kind (an MDIView). A desktop 3D view has two
 *    faces, its View3DInventor and that view's viewer, and they are ONE
 *    owner: a ViewerContext that sits in an MDIView is normalised to it
 *    on construction, so the two constructors cannot make two keys;
 *  - a view with no MDIView: a served client's mirror above all;
 *  - nobody (the default). A dialog shown with no view to name is
 *    unowned and behaves as a dialog always has: it is everybody's.
 *
 * An owner outlives its view without dangling. isValid() says whether the
 * view is still there, and a dead owner equals only owners of that same
 * dead view, never a new view that came to stand at its address.
 */
class GuiExport TaskOwner
{
public:
    /// Nobody.
    TaskOwner() = default;
    /// A desktop view. A container (Gui::ViewArea) names its active
    /// embedded view; null is nobody.
    explicit TaskOwner(MDIView* view);
    /// A 3D view or a client's mirror; null is nobody.
    explicit TaskOwner(ViewerContext* view);

    /** The view being handled now.
     *
     * ViewerContext::current() while a scope is open -- a client's request,
     * its replayed events, a view's own events -- else the main window's
     * active view, else nobody.
     *
     * A DEFERRED call (a timer, a queued connection) runs with no scope
     * open and names whichever view is active by then: capture the owner
     * when the deferral is made and pass it along.
     */
    static TaskOwner current();

    /// Whether this names nobody.
    bool isNull() const
    {
        return kind == Kind::None;
    }
    /// Whether this names a view that still exists.
    bool isValid() const;
    /// The desktop view, or null for a view that has none (and for a dead
    /// or null owner).
    MDIView* mdiView() const;
    /// The 3D view's context, or null for a view that is not one (and
    /// for a dead or null owner).
    ViewerContext* context() const;
    /// The view's document, or null.
    Gui::Document* document() const;
    /// Whether this is a served client's view.
    bool isRemote() const;

    bool operator==(const TaskOwner& other) const;
    bool operator!=(const TaskOwner& other) const
    {
        return !(*this == other);
    }

private:
    enum class Kind : unsigned char
    {
        None,
        View,     ///< an MDIView
        Context,  ///< a ViewerContext in no MDIView
    };
    Kind kind {Kind::None};
    /// Kind::View. Held as a QObject so that this header needs no view
    /// class; the key is the address it was made with, kept apart because
    /// the pointer nulls itself when the view goes.
    QPointer<QObject> view;
    const QObject* viewKey {nullptr};
    /// Kind::Context, valid while \a life has not expired.
    ViewerContext* ctx {nullptr};
    std::weak_ptr<const void> life;
};

}  // namespace Gui

#endif  // GUI_TASKOWNER_H
