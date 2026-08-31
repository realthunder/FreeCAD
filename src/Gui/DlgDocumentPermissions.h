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

// The user surface of expression security: the Document Permissions panel
// and the two status-bar indicators.
//
// PermissionIndicator covers the permission service (expression sandbox
// phase 1 step 3b, docs/ExpressionSandbox.md sec 3.3). Prompt mechanics
// follow the popup blocker: a blocked expression fails fast as a
// cell/object error, a passive indicator lights up, and grants happen
// here -- on user gesture, never from a modal raised mid-recompute.
//
// SandboxIndicator covers the stronger control next to it: whether
// expression Python runs in THIS process or inside the WebAssembly
// sandbox (docs/ExpressionImage.md). It is a permanent status light with
// a click that flips the setting, because that setting is live -- nothing
// restarts, and the user can put it back the moment it costs them.

#include <memory>

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

/// Status-bar indicator: whether expression Python is confined to the
/// sandbox image or runs in this process.  Always visible -- an unconfined
/// evaluator is a thing to be told about, not a thing to discover -- and a
/// click flips it.
class GuiExport SandboxIndicator: public QToolButton
{
    // Q_OBJECT here and not on the indicator above: every string this
    // widget shows is user-facing prose that a translator needs the
    // class context for.
    Q_OBJECT

public:
    explicit SandboxIndicator(QWidget *parent = nullptr);
    ~SandboxIndicator() override;

    void updateState();

private:
    void toggleRouting();

    class ParamObserver;
    std::unique_ptr<ParamObserver> observer;
};

}  // namespace Dialog
}  // namespace Gui

#endif  // GUI_DLG_DOCUMENT_PERMISSIONS_H
