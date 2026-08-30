/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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

#ifndef GUI_DLG_DOCUMENT_PERMISSIONS_H
#define GUI_DLG_DOCUMENT_PERMISSIONS_H

// The Document Permissions panel and its status-bar indicator: the user
// surface of the expression permission service (expression sandbox phase 1
// step 3b, docs/ExpressionSandbox.md sec 3.3). Prompt mechanics follow the
// popup blocker: a blocked expression fails fast as a cell/object error, a
// passive indicator lights up, and grants happen here -- on user gesture,
// never from a modal raised mid-recompute.

#include <QDialog>
#include <QToolButton>
#include <fastsignals/signal.h>

#include <FCGlobal.h>

class QLabel;
class QTreeWidget;

namespace Gui {
namespace Dialog {

class GuiExport DlgDocumentPermissions: public QDialog
{
public:
    explicit DlgDocumentPermissions(QWidget *parent = nullptr);
    ~DlgDocumentPermissions() override;

    /// Show the (single, modeless) dialog, creating it on first use.
    static void showDialog();

    void refresh();

private:
    void applyDecision(bool allow, const char *scope);
    void revokeSelected();

    QLabel *docLabel = nullptr;
    QTreeWidget *pendingTree = nullptr;
    QTreeWidget *grantTree = nullptr;
    fastsignals::connection connPending;
    fastsignals::connection connGrants;
    fastsignals::connection connActiveDoc;
};

/// Status-bar indicator: visible with the count of unanswered permission
/// requests of the active document (plus session requests); a click opens
/// the Document Permissions dialog.
class GuiExport PermissionIndicator: public QToolButton
{
public:
    explicit PermissionIndicator(QWidget *parent = nullptr);
    ~PermissionIndicator() override;

    void updateState();

private:
    fastsignals::connection connPending;
    fastsignals::connection connActiveDoc;
    fastsignals::connection connDeleteDoc;
};

}  // namespace Dialog
}  // namespace Gui

#endif  // GUI_DLG_DOCUMENT_PERMISSIONS_H
