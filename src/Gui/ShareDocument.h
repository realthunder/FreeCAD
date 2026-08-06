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

#ifndef GUI_SHAREDOCUMENT_H
#define GUI_SHAREDOCUMENT_H

#include <FCGlobal.h>

namespace Gui
{

/*!
 * Desktop document sharing (docs/MultiDocServe.md §8).
 *
 * The Share dialog starts serving the active document through
 * SceneServeSource on a port of the user's choosing, gated by a
 * generated door token, and hands back the viewer URL to send around.
 * While sharing is up a clickable overlay sits on the 3D area; it opens
 * the client panel — who is connected, per-client view-only/can-edit,
 * kick — which also holds the stop-sharing button.
 *
 * This is the desktop face of the serving wire; everything it does goes
 * through Render::SceneStreamServer and SceneServeSource, so a share
 * and a scripted Gui.serveDocument are the same thing underneath.
 */
class GuiExport ShareDocumentManager
{
public:
    static ShareDocumentManager &instance();

    /// Whether sharing is up: the listener runs and at least one
    /// document this manager started serving is still served.
    bool active() const;

    /// The command entry: a dialog to start sharing the active
    /// document — or, when sharing is already up, the client panel.
    void openShareDialog();

    /// The overlay's click: connected clients, their mode, kick,
    /// stop sharing.
    void showPanel();

    /// Tear sharing down: unserve every document this manager served,
    /// stop the listener, drop the token, hide the overlay.
    void stopSharing();

    /// Re-read the roster and repaint the overlay/panel. Called on the
    /// GUI thread; the server's roster notifier marshals here.
    void refreshUi();

private:
    ShareDocumentManager();
    ~ShareDocumentManager();
    class Private;
    Private *pimpl;
};

}  // namespace Gui

#endif  // GUI_SHAREDOCUMENT_H
