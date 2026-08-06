/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#ifndef GUI_SCENECONTROL_H
#define GUI_SCENECONTROL_H

#include <string>

namespace Gui {

/// The semantic control channel of the scene stream (docs/ThinClient.md
/// §4.2): id-correlated JSON operations from a remote viewer, answered
/// against the live document. getProperties today; setProperty and
/// modeling operations grow here.

/// Answer one control request. GUI thread only — callers on any other
/// thread marshal first (installSceneControlHandler does). \a boundDoc
/// names the served document the request's connection is joined to
/// (empty = unbound, windowed behavior): it resolves the "view3d"
/// subject to that document's serving container and stands in for an
/// unnamed document (docs/MultiDocServe.md §5).
std::string handleSceneControlRequest(const std::string &json,
                                      const std::string &boundDoc = {});

/// Route the scene stream server's control requests ("op" JSON text
/// frames) through handleSceneControlRequest on the GUI thread.
/// Idempotent; safe to call whenever a serving renderer comes up.
/// \a docName installs on that document's server group and binds the
/// handler to it; empty installs on the default group, unbound.
void installSceneControlHandler(const std::string &docName = {});

} // namespace Gui

#endif // GUI_SCENECONTROL_H
