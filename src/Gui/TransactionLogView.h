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

#ifndef GUI_DOCKWND_TRANSACTIONLOGVIEW_H
#define GUI_DOCKWND_TRANSACTIONLOGVIEW_H

#include <cstdint>
#include <string>
#include <vector>
#include <fastsignals/signal.h>
#include <QTimer>

#include <Gui/DockWindow.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace App {
class Document;
class TransactionLog;
}

namespace Gui {
namespace DockWnd {

/** The transaction log browser (docs/TransactionLog.md sec 22.2).
 *
 * A dockable, read-only view of the active document's log as the store
 * holds it: the transaction rows, the ops of the selected transaction,
 * and the value behind the selected op; on a second tab the versions
 * (sec 16.3), the manifest of the selected one, and the entry or blob
 * behind the selected manifest row. It follows the active document
 * and appends rows as commits land, so what a gtest asserts about a row
 * can be looked at in the running application. It grows into the log
 * manager as the later phases add versions, branches and restore.
 */
class TransactionLogView : public Gui::DockWindow
{
    Q_OBJECT

public:
    explicit TransactionLogView(Gui::Document* pcDocument, QWidget* parent = nullptr);
    ~TransactionLogView() override;

    const char* getName() const override { return "TransactionLogView"; }
    void onUpdate() override;

private Q_SLOTS:
    void refresh();
    void onTransactionSelected();
    void onOpSelected();
    void onVersionSelected();
    void onManifestSelected();
    void onTabChanged(int index);
    void onFilterChanged(const QString& text);
    void onResolvePending();
    void onSnapshot();
    void onTransactionContextMenu(const QPoint& pos);
    void onVersionContextMenu(const QPoint& pos);
    void onBranchChosen(int index);
    void onNewBranch();
    void onDeleteBranch();
    void applyVisibility();

protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    void attach(App::Document* doc);
    void detach();
    void reload();
    void appendTransactions(int64_t fromSeq);
    void showOps(int64_t seq);
    void refreshVersions();
    /// The branch switcher's items (docs/TransactionLog.md sec 26).
    void refreshBranches();
    /// Ask for a new branch's name and make it from `version`, else `seq`,
    /// else the current head.
    void createBranch(int64_t version, int64_t seq);
    void showManifest(int64_t num);
    void updateStatus();
    App::TransactionLog* log() const;
    void scheduleRefresh();

    App::Document* _doc {nullptr};
    int64_t _lastSeq {0};
    int64_t _lastVersion {0};
    bool _stale {true};
    QTimer _refreshTimer;
    std::vector<fastsignals::scoped_connection> _connections;
    fastsignals::scoped_connection _connActiveDoc;
    fastsignals::scoped_connection _connDeleteDoc;
    fastsignals::scoped_connection _connNewDoc;
    fastsignals::scoped_connection _connRestoreDoc;
    bool _fillingBranches {false};

    QLabel* _status {nullptr};
    QLineEdit* _filter {nullptr};
    QPushButton* _resolve {nullptr};
    QPushButton* _snapshot {nullptr};
    QComboBox* _branch {nullptr};
    QPushButton* _newBranch {nullptr};
    QPushButton* _deleteBranch {nullptr};
    QCheckBox* _allBranches {nullptr};
    QTabWidget* _tabs {nullptr};
    QStackedWidget* _detail {nullptr};
    QTreeWidget* _transactions {nullptr};
    QTreeWidget* _ops {nullptr};
    QTreeWidget* _versions {nullptr};
    QTreeWidget* _manifest {nullptr};
    QPlainTextEdit* _value {nullptr};
};

} // namespace DockWnd
} // namespace Gui

#endif // GUI_DOCKWND_TRANSACTIONLOGVIEW_H
