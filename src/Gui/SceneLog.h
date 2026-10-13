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

#ifndef GUI_SCENELOG_H
#define GUI_SCENELOG_H

#include <FCGlobal.h>

namespace App
{
class Document;
}

namespace Gui
{

/** The transaction log of a served document, for a client's own panel of it
 * (docs/TransactionLog.md sec 32.21).
 *
 * The desktop's panel is a dock of the host's window, and one widget: a
 * client is not shown it. A client has a panel of its own, drawn in its
 * page, and asks the host for what it lists:
 *
 *   log.rows   the rows, newest first and a page at a time: who, when,
 *              what kind, on which branch, how many ops
 *   log.ops    the ops of one row: object, property, the refs of the value
 *              before and after, and whether the change can be shown
 *   log.value  one value by its ref, as the text the log holds
 *
 * and is told `log.changed` when the log of a document it has asked the
 * rows of has more, or other, rows.
 *
 * Seeing the log needs the right to edit (ruled 2026-10-13): the three are
 * registered as ops that write, so a connection that may only look is
 * answered ViewOnly before any of them runs, and the push goes to no such
 * connection.
 *
 * A row's change is shown as the edit it is (ViewProvider::ValueDiff): the
 * `edit` op with the row named.
 *
 * And what the panel does with what it lists (sec 32.22), each the call the
 * desktop's panel makes, run as the connection that asked:
 *
 *   log.undo          rows taken back as one step, though none is the last
 *   log.versions      the versions, and which is the document as it stands
 *   log.snapshot      a version of the document as it stands
 *   log.nameVersion   a version named, or unnamed again
 *   log.restore       the document made what a version was
 *   log.branches      the branches, and which the document is on
 *   log.branch        a new branch from here, a row or a version
 *   log.switch        the document made the head of another branch
 *   log.renameBranch
 *   log.deleteBranch  nothing takes it back
 *
 * A switch, and a branch from anywhere but where the document is, wait for
 * an edit that is open to be left (`Editing`).
 *
 * Whose the document's place in its log is (sec 32.23). A document served
 * as it is, is shared: below the host a connection takes back its own rows
 * (sec 30.10: a step is its author's) and takes versions; a restore, a
 * switch, a branch made, renamed or deleted and a version's name are the
 * host's, `Forbidden` to the others. A document opened from a branch to be
 * served (serveLogBranch) is its clients' from the serving point on: those
 * acts are theirs of the branch it was opened on and of what was branched
 * from it since, and not of anything before.
 */
GuiExport void installSceneLogOps();

/** Say that `doc` was opened from a branch to be served: the branch it is
 * on now is the one its clients' reach starts at, where that branch forks
 * being the serving point. Until the document is closed. False when the
 * document keeps no log.
 */
GuiExport bool serveLogBranch(App::Document* doc);

}  // namespace Gui

#endif  // GUI_SCENELOG_H
