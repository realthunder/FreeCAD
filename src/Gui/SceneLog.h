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

#include <cstdint>
#include <string>

namespace App
{
class Document;
class DocumentObject;
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
 * What the one who asks may (sec 32.25) is by branch, and of a branch read
 * or write; the host may all of it. Read: the branch's history is listed,
 * its rows are applied to the branch the document is on (`log.apply`), and
 * it is asked to be merged into another that is read, or another into it
 * (`log.request`). Write: a document on the branch is moved -- a restore, a
 * switch, a branch made, renamed or deleted, a version's name -- from a
 * point on, and the requests to merge into it are dealt with
 * (`log.mergeRequest`, `log.closeRequest`). It is said in the log's store,
 * by user and branch (App::Document::setBranchAccess). Where nothing is
 * said the branch of the document a connection is in is read -- there it
 * takes back its own rows (sec 30.10) and takes versions, as every client
 * of a shared document -- and a document opened from a branch to be served
 * (serveLogBranch) is written by whoever joins it, from where that branch
 * forks. A branch a connection makes is said to be its own as it is made.
 * What is not the asker's is `Forbidden`; a branch it does not read is not
 * listed and is unknown to it, as its rows, its versions and the requests
 * of it are.
 *
 * What a save writes into the file though no kept version reaches it --
 * the values a recompute wrote, kept (sec 32.11, 32.27):
 *
 *   log.kept        each value kept by itself, each object kept
 *   log.keep        the value of an object at a row kept, or let go
 *   log.keepObject  every value of an object kept, or no longer
 *   log.release     a value kept by itself let go, by its ref
 *
 * Keeping takes nothing, and is anyone's who is listed the row. A value let
 * go is left out of the file by the next save: the host's, and of one who
 * writes the branch of the row it was kept at, since the point. Stopping an
 * object's being kept lets its values go on every branch: the host's.
 */
GuiExport void installSceneLogOps();

/** Whether the one who asks -- the connection an op is being done for --
 * is listed row `row` of the document's log: a row of a branch it reads
 * (sec 32.25). For what shows a row by its number and is no op of the log:
 * the edit that shows a row's change (`edit` with `row`, sec 32.21).
 */
GuiExport bool logRowListed(App::Document* doc, int64_t row);

/** Whether the one who asks is listed `ref` among what the log of `doc`
 * keeps in the file as a value of `obj` (sec 32.27, `log.kept`): kept by
 * itself, or of an object kept, at a row it is listed. `row` is given the
 * row it was kept at, or set at. For the edit that shows a kept value
 * against what the object has now (`edit` with `kept`).
 */
GuiExport bool logKeptListed(App::Document* doc, App::DocumentObject* obj, const std::string& ref,
                             int64_t* row = nullptr);

/** Say that `doc` was opened from a branch to be served: the branch it is
 * on now is the one its clients' reach starts at, where that branch forks
 * being the serving point. Until the document is closed. False when the
 * document keeps no log.
 */
GuiExport bool serveLogBranch(App::Document* doc);

/** Serve a branch of `doc` in a document of its own (sec 32.23, 32.25): a
 * branch is made where `doc` is -- `name`, or `<branch>~<n>` -- opened in a
 * second document of the file with no window, served as a document is and
 * said to be one opened to be served. Returns that document. Throws when
 * the name is taken, `doc` keeps no log or the branch cannot be served.
 */
GuiExport App::Document* serveBranchOf(App::Document* doc, const std::string& name, int port = 0);

}  // namespace Gui

#endif  // GUI_SCENELOG_H
