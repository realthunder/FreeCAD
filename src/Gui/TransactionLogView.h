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
#include <memory>
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
class QTreeView;
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
    void onRenameBranch();
    void onMergeBranch();
    void onImportFile();
    void onOpenBranch();
    void onRequestActivated(QTreeWidgetItem* item);
    void onRequestContextMenu(const QPoint& pos);
    /// Ask the merge's preview of each request how many conflicts it finds.
    void previewRequests();
    void applyVisibility();
    /// Lay the graph out over the rows shown (docs/TransactionLog.md sec 26).
    void layoutGraph();
    /// The transaction row's context menu, at `global`.
    void transactionMenu(QTreeWidgetItem* item, const QPoint& global);

public Q_SLOTS:
    /// Merge branch `name` into the one the document is on
    /// (docs/TransactionLog.md sec 28): the preview in a dialog where each
    /// conflict gets a side, then the merge.
    void mergeBranch(const QString& name);
    /** Bring another copy of this file in as a branch, then merge it
     * (docs/TransactionLog.md sec 30.13, 30.14): the copy's rows since the
     * two parted are imported as a branch named after it, and that branch
     * is put to mergeBranch() -- the preview, a side for each conflict, the
     * merge, or nothing. `branch` names the copy's branch; empty, the one
     * its file reopens on, chosen from a list when it has more than one
     * with something to bring.
     */
    void importFile(const QString& path, const QString& branch);
    /** Bring in a file a client of the served document sent
     * (docs/TransactionLog.md sec 30.20 H6, 30.23): request `id` of
     * Gui::SceneRequests, which until now was kept and not read. It is
     * imported as importFile() imports any file, the import's record naming
     * who sent it, and put to the merge; the file itself is then dropped.
     */
    void bringRequest(qulonglong id);
    /// bringRequest() without the merge (sec 30.29): the file becomes a
    /// branch, which waits in the list as any imported branch does.
    void bringRequestOnly(qulonglong id);
    /// Write the bytes of sent file `id` to `path`, as they came: to look
    /// at somewhere else before anything of it is brought in here.
    void saveRequestAs(qulonglong id, const QString& path);
    /// Refuse a request: a sent file not yet read (`id`) is dropped unread.
    void dropRequest(qulonglong id);
    /// dropRequest() of every sent file that waits.
    void dropAllRequests();
    /// Refuse a request that is a branch: the branch is deleted.
    void deleteRequest(const QString& branch);

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
    /// The request list (sec 30.20 H2): files sent and not read, and the
    /// branches an import made that this branch has not taken.
    void refreshRequests();
    /// importFile() with who sent the file; false when nothing came.
    /// `sent` is the row of a file the log holds (sec 30.29), whose bytes
    /// `path` is a copy of: it is then brought in as that file. Without
    /// `merge` the branch is left for later.
    bool importFrom(const QString& path, const QString& branch, const QString& sender,
                    qulonglong sent = 0, bool merge = true);
    /// Bring in sent file `id`, with the merge or without.
    void bringSent(qulonglong id, bool merge);
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
    fastsignals::scoped_connection _connRequests;
    /// What the preview said of a request, by branch: the head it was
    /// asked of, this branch's head then, and the conflicts -- -2 for a
    /// branch with nothing to give, which is no request.
    struct Previewed
    {
        int64_t theirs {0};
        int64_t ours {0};
        int conflicts {-1};
    };
    std::map<std::string, Previewed> _previewed;
    QTimer _previewTimer;
    bool _fillingBranches {false};

    QLabel* _status {nullptr};
    QLineEdit* _filter {nullptr};
    QPushButton* _resolve {nullptr};
    QPushButton* _snapshot {nullptr};
    QComboBox* _branch {nullptr};
    QPushButton* _newBranch {nullptr};
    QPushButton* _deleteBranch {nullptr};
    QPushButton* _renameBranch {nullptr};
    QPushButton* _mergeBranch {nullptr};
    QPushButton* _importFile {nullptr};
    QTreeWidget* _requests {nullptr};
    QPushButton* _openBranch {nullptr};
    QCheckBox* _allBranches {nullptr};
    QCheckBox* _hideRecords {nullptr};
    QCheckBox* _showLogins {nullptr};
public:
    /// The graph column's lanes, nodes and labels per row, for its delegate.
    struct GraphLayout;
private:
    std::unique_ptr<GraphLayout> _graph;
    QTabWidget* _tabs {nullptr};
    QStackedWidget* _detail {nullptr};
    QTreeWidget* _transactions {nullptr};
    /// The graph, a pane of its own beside the list (sec 26.7): a second
    /// view on the list's model and selection, scrolled with it.
    QTreeView* _graphView {nullptr};
    QTreeWidget* _ops {nullptr};
    QTreeWidget* _versions {nullptr};
    QTreeWidget* _manifest {nullptr};
    QPlainTextEdit* _value {nullptr};
};

} // namespace DockWnd
} // namespace Gui

#endif // GUI_DOCKWND_TRANSACTIONLOGVIEW_H
