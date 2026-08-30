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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QDialogButtonBox>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QLabel>
# include <QPointer>
# include <QPushButton>
# include <QStyle>
# include <QTimer>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ExpressionSecurityRuntime.h>

#include "DlgDocumentPermissions.h"
#include "MainWindow.h"

using namespace Gui::Dialog;
namespace Sec = App::ExpressionSecurity;

// item data roles for both trees
enum { RolePrincipal = Qt::UserRole, RolePermission, RoleTarget, RoleScope };

static QString shortPrincipal(const std::string &principal)
{
    // "document:sha256:<64 hex>" -> "sha256:0123abcd"
    static const char prefix[] = "document:sha256:";
    if (principal.compare(0, sizeof(prefix) - 1, prefix) == 0)
        return QStringLiteral("sha256:%1").arg(
                QString::fromStdString(principal.substr(sizeof(prefix) - 1, 8)));
    return QString::fromStdString(principal);
}

static QString permissionLabel(Sec::Permission perm, const std::string &target)
{
    QString s = QString::fromLatin1(Sec::permissionName(perm));
    if (!target.empty() && target != "*")
        s += QStringLiteral(":%1").arg(QString::fromStdString(target));
    return s;
}

// queue a member call onto the GUI thread, guarded against deletion
template<class W, class F>
static auto queued(W *widget, F method)
{
    return [ptr = QPointer<W>(widget), method]() {
        if (!ptr)
            return;
        QMetaObject::invokeMethod(ptr, [ptr, method]() {
            if (ptr)
                (ptr->*method)();
        }, Qt::QueuedConnection);
    };
}

////////////////////////////////////////////////////////////////////////////////////
//
// DlgDocumentPermissions
//

static QPointer<DlgDocumentPermissions> _Dialog;

void DlgDocumentPermissions::showDialog()
{
    if (!_Dialog)
        _Dialog = new DlgDocumentPermissions(Gui::getMainWindow());
    _Dialog->refresh();
    _Dialog->show();
    _Dialog->raise();
    _Dialog->activateWindow();
}

DlgDocumentPermissions::DlgDocumentPermissions(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Document permissions"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(560, 480);

    auto layout = new QVBoxLayout(this);

    docLabel = new QLabel(this);
    layout->addWidget(docLabel);

    layout->addWidget(new QLabel(tr("Pending requests:"), this));
    pendingTree = new QTreeWidget(this);
    pendingTree->setColumnCount(4);
    pendingTree->setHeaderLabels({tr("Permission"), tr("Target"),
            tr("Object"), tr("Count")});
    pendingTree->setRootIsDecorated(false);
    pendingTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(pendingTree, 2);

    auto pendingButtons = new QHBoxLayout;
    auto addBtn = [&](const QString &text, bool allow, const char *scope) {
        auto btn = new QPushButton(text, this);
        connect(btn, &QPushButton::clicked, this, [this, allow, scope]() {
            applyDecision(allow, scope);
        });
        pendingButtons->addWidget(btn);
    };
    addBtn(tr("Allow once"), true, "once");
    addBtn(tr("Allow this session"), true, "session");
    addBtn(tr("Always allow"), true, "always");
    addBtn(tr("Always deny"), false, "always");
    pendingButtons->addStretch();
    layout->addLayout(pendingButtons);

    layout->addWidget(new QLabel(tr("Grants:"), this));
    grantTree = new QTreeWidget(this);
    grantTree->setColumnCount(4);
    grantTree->setHeaderLabels({tr("Permission"), tr("Target"),
            tr("Decision"), tr("Scope")});
    grantTree->setRootIsDecorated(false);
    grantTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(grantTree, 1);

    auto grantButtons = new QHBoxLayout;
    auto revokeBtn = new QPushButton(tr("Revoke"), this);
    connect(revokeBtn, &QPushButton::clicked, this,
            [this]() { revokeSelected(); });
    grantButtons->addWidget(revokeBtn);
    grantButtons->addStretch();
    layout->addLayout(grantButtons);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttonBox);

    auto &rt = Sec::Runtime::instance();
    connPending = rt.signalPendingChanged.connect(
            queued(this, &DlgDocumentPermissions::refresh));
    connGrants = rt.signalGrantsChanged.connect(
            queued(this, &DlgDocumentPermissions::refresh));
    connActiveDoc = App::GetApplication().signalActiveDocument.connect(
            [cb = queued(this, &DlgDocumentPermissions::refresh)]
            (const App::Document &) { cb(); });
}

DlgDocumentPermissions::~DlgDocumentPermissions()
{
    connPending.disconnect();
    connGrants.disconnect();
    connActiveDoc.disconnect();
}

void DlgDocumentPermissions::refresh()
{
    auto &rt = Sec::Runtime::instance();
    auto doc = App::GetApplication().getActiveDocument();

    std::string docName;
    std::string principal = "session";
    if (doc) {
        docName = doc->getName();
        principal = rt.documentPrincipal(doc);
        docLabel->setText(tr("Document: %1 (%2)")
                .arg(QString::fromUtf8(doc->Label.getValue()),
                     shortPrincipal(principal)));
    } else
        docLabel->setText(tr("No active document (session grants only)"));

    pendingTree->clear();
    for (const auto &req : rt.pendingRequests(docName)) {
        auto item = new QTreeWidgetItem(pendingTree);
        item->setText(0, permissionLabel(req.permission, std::string()));
        item->setText(1, QString::fromStdString(req.target));
        item->setText(2, req.objectName.empty()
                ? shortPrincipal(req.principal)
                : QString::fromStdString(req.objectName));
        item->setText(3, QString::number(req.count));
        item->setData(0, RolePrincipal, QString::fromStdString(req.principal));
        item->setData(0, RolePermission,
                QString::fromLatin1(Sec::permissionName(req.permission)));
        item->setData(0, RoleTarget, QString::fromStdString(req.target));
    }

    grantTree->clear();
    auto addGrants = [this](const std::string &principal) {
        for (const auto &g :
                Sec::Runtime::instance().activeGrants(principal)) {
            auto item = new QTreeWidgetItem(grantTree);
            item->setText(0, permissionLabel(g.permission, std::string()));
            item->setText(1, QString::fromStdString(g.target));
            item->setText(2, g.allow ? tr("allow") : tr("deny"));
            item->setText(3, QString::fromStdString(g.scope));
            item->setData(0, RolePrincipal,
                    QString::fromStdString(g.principal));
            item->setData(0, RolePermission,
                    QString::fromLatin1(Sec::permissionName(g.permission)));
            item->setData(0, RoleTarget, QString::fromStdString(g.target));
        }
    };
    addGrants(principal);
    if (principal != "session")
        addGrants("session");
}

void DlgDocumentPermissions::applyDecision(bool allow, const char *scope)
{
    auto items = pendingTree->selectedItems();
    if (items.isEmpty()) {
        for (int i = 0; i < pendingTree->topLevelItemCount(); ++i)
            items.push_back(pendingTree->topLevelItem(i));
    }
    if (items.isEmpty())
        return;

    auto &rt = Sec::Runtime::instance();
    auto doc = App::GetApplication().getActiveDocument();
    std::string label = doc ? doc->Label.getValue() : "";
    std::string path = doc ? doc->FileName.getValue() : "";

    // collect the re-run set BEFORE granting: grants clear the pending list
    std::set<std::pair<std::string, std::string>> rerun;
    for (auto item : items) {
        auto principal = item->data(0, RolePrincipal).toString().toStdString();
        for (auto &v : rt.pendingObjects(principal))
            rerun.insert(v);
    }

    for (auto item : items) {
        auto principal = item->data(0, RolePrincipal).toString().toStdString();
        auto permName = item->data(0, RolePermission).toString().toStdString();
        auto target = item->data(0, RoleTarget).toString().toStdString();
        auto perm = Sec::permissionFromName(permName);
        if (!perm)
            continue;
        rt.grant(principal, *perm, target, allow, scope, label, path);
        if (!allow)
            rt.clearPending(principal, *perm, target);
    }

    if (allow) {
        // granting re-runs the evaluation: recompute what was blocked
        std::set<App::Document *> docs;
        for (auto &v : rerun) {
            auto d = App::GetApplication().getDocument(v.first.c_str());
            if (!d)
                continue;
            auto obj = d->getObject(v.second.c_str());
            if (obj) {
                obj->enforceRecompute();
                docs.insert(d);
            }
        }
        for (auto d : docs)
            d->recompute();
        if (strcmp(scope, "once") == 0)
            rt.clearOnce();
    }
    refresh();
}

void DlgDocumentPermissions::revokeSelected()
{
    auto &rt = Sec::Runtime::instance();
    const auto items = grantTree->selectedItems();
    for (auto item : items) {
        auto principal = item->data(0, RolePrincipal).toString().toStdString();
        auto permName = item->data(0, RolePermission).toString().toStdString();
        auto target = item->data(0, RoleTarget).toString().toStdString();
        auto perm = Sec::permissionFromName(permName);
        if (perm)
            rt.revoke(principal, *perm, target);
    }
    refresh();
}

////////////////////////////////////////////////////////////////////////////////////
//
// PermissionIndicator
//

PermissionIndicator::PermissionIndicator(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    setToolTip(tr("Expressions blocked pending permissions -- click to review"));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    hide();

    connect(this, &QToolButton::clicked,
            []() { DlgDocumentPermissions::showDialog(); });

    auto &rt = Sec::Runtime::instance();
    connPending = rt.signalPendingChanged.connect(
            queued(this, &PermissionIndicator::updateState));
    connActiveDoc = App::GetApplication().signalActiveDocument.connect(
            [cb = queued(this, &PermissionIndicator::updateState)]
            (const App::Document &) { cb(); });
    connDeleteDoc = App::GetApplication().signalDeleteDocument.connect(
            [cb = queued(this, &PermissionIndicator::updateState)]
            (const App::Document &) { cb(); });
}

PermissionIndicator::~PermissionIndicator()
{
    connPending.disconnect();
    connActiveDoc.disconnect();
    connDeleteDoc.disconnect();
}

void PermissionIndicator::updateState()
{
    auto doc = App::GetApplication().getActiveDocument();
    auto reqs = Sec::Runtime::instance().pendingRequests(
            doc ? doc->getName() : "");
    int count = 0;
    for (const auto &r : reqs)
        count += r.count;
    setText(QString::number(count));
    setVisible(!reqs.empty());
}
