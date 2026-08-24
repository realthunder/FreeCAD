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

#ifndef TECHDRAWGUI_PAGESERVE_H
#define TECHDRAWGUI_PAGESERVE_H

/// Serves a TechDraw page over the scene stream -- milestone M3 of the
/// vg page engine (docs/TechDrawPortAndSection.md sec 24). The page
/// becomes its own document group ("<doc>#<page>") on
/// SceneStreamServer, publishing FCPD payloads: the retained Page2D op
/// buffers, image rasters and fonts behind content keys, damage-driven
/// deltas derived by the server per viewer. Mirrors
/// Gui::SceneServeSource's lifecycle for the 3D scene.

#include <QObject>
#include <QTimer>

#include <memory>
#include <string>

#include <Mod/TechDraw/TechDrawGlobal.h>

namespace TechDraw {
class DrawPage;
}

namespace TechDrawGui {

class TechDrawGuiExport PageServe: public QObject
{
    Q_OBJECT

public:
    /// Serve \a page; idempotent. A \a port > 0 starts the shared
    /// listener when it is not running; 0 means the caller arranged
    /// the server (another serve call, FC_BGFX_SERVE_SCENE). Returns
    /// null when the page is not servable.
    static PageServe* serve(TechDraw::DrawPage* page, int port);
    static void unserve(TechDraw::DrawPage* page);
    static bool serving(TechDraw::DrawPage* page);

    /// The wire group name this page publishes into.
    const std::string& group() const;

    /// Coalesced republish; the damage hooks call this.
    void schedulePublish();
    /// Feed the damaged views and publish if anything changed on the
    /// wire. Exposed for tests; returns whether a publish happened.
    bool publishNow();

    ~PageServe() override;

private:
    explicit PageServe(TechDraw::DrawPage* page);
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace TechDrawGui

#endif // TECHDRAWGUI_PAGESERVE_H
