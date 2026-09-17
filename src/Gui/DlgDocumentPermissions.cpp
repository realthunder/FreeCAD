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
# include <cstring>

# include <QApplication>
# include <QDialogButtonBox>
# include <QEvent>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QIcon>
# include <QLabel>
# include <QMessageBox>
# include <QPointer>
# include <QPushButton>
# include <QRegularExpression>
# include <QStyle>
# include <QTimer>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ExpressionEvaluator.h>
#include <App/ExpressionGuestProxy.h>
#include <App/ExpressionSecurityRuntime.h>
#include <Base/Interpreter.h>

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

/** Run the host-side installer (freecad.pyodide, docs/PyodideHost.md
 * sec 12) for one sandbox package.  The name is validated before it is
 * interpolated into Python: it came from the guest's import statement.
 * False, with the reason shown, on failure.
 */
static bool installSandboxPackage(QWidget *parent, const std::string &name)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    if (!valid.match(QString::fromStdString(name)).hasMatch()) {
        QMessageBox::warning(parent, DlgDocumentPermissions::tr("Sandbox package"),
                DlgDocumentPermissions::tr("Refusing to install a package with an "
                                           "unusual name: %1")
                        .arg(QString::fromStdString(name)));
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = true;
    QString error;
    try {
        Base::PyGILStateLocker lock;
        Base::Interpreter().runString(
                ("import freecad.pyodide as _fcp\n_fcp.install_package(\"" + name + "\")").c_str());
    }
    catch (Base::Exception &e) {
        ok = false;
        error = QString::fromUtf8(e.what());
    }
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(parent, DlgDocumentPermissions::tr("Sandbox package"),
                DlgDocumentPermissions::tr("Installing %1 for the sandbox failed:\n%2")
                        .arg(QString::fromStdString(name), error));
    return ok;
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

    bool installed = false;
    for (auto item : items) {
        auto principal = item->data(0, RolePrincipal).toString().toStdString();
        auto permName = item->data(0, RolePermission).toString().toStdString();
        auto target = item->data(0, RoleTarget).toString().toStdString();
        auto perm = Sec::permissionFromName(permName);
        if (!perm)
            continue;
        if (*perm == Sec::Permission::PkgInstall) {
            // An action, not a grant (docs/SandboxNetwork.md sec 9.4):
            // allowing IS the install, whatever scope button was used;
            // the package is then available to every principal.
            if (allow && !installSandboxPackage(this, target))
                continue;
            rt.clearPending(principal, *perm, target);
            installed = installed || allow;
            continue;
        }
        try {
            rt.grant(principal, *perm, target, allow, scope, label, path);
        }
        catch (const Base::Exception &e) {
            // a remote client's run-local id cannot be granted "always",
            // and gui / unsafe.getattr cannot be granted to a client at
            // all (docs/Sandbox.md 7.20, C3)
            QMessageBox::warning(this, tr("Expression sandbox"), QString::fromUtf8(e.what()));
            continue;
        }
        if (!allow)
            rt.clearPending(principal, *perm, target);
    }
    if (installed) {
        // the guest that asked does not have the package; the next
        // evaluation's fresh instance loads it at boot
        App::ExpressionSandbox::resetSandbox();
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

void DlgDocumentPermissions::askHostImport(App::Document *doc)
{
#ifdef FC_EXPR_IMAGE_HOST
    if (!doc || !Gui::getMainWindow())
        return;
    auto modules = App::ExpressionSandbox::deferredProxyModules(doc);
    if (modules.empty())
        return;

    auto &rt = Sec::Runtime::instance();
    const std::string principal = rt.documentPrincipal(doc);

    // A document's principal is a hash over its CODE (2.1), and a file
    // with no expressions at all -- most Path and Fem jobs -- hashes to
    // the same id as every other code-free file.  A persisted answer
    // here would therefore answer for all of them, unprompted, for good.
    // Offer only the scopes that expire (docs/Sandbox.md 7.28, "the
    // code-free principal").
    Sec::DocumentHashBuilder codeFree;
    const bool shared = (principal == codeFree.principalId());

    int total = 0;
    for (const auto &m : modules)
        total += m.second;

    QDialog dlg(Gui::getMainWindow());
    dlg.setWindowTitle(tr("Run this document's Python here?"));
    auto layout = new QVBoxLayout(&dlg);

    auto text = new QLabel(
            tr("<b>%1</b> saved a Python Proxy for %n object(s) in modules the expression "
               "sandbox has no package for. Their code can only run in this process, with "
               "the reach of the whole application -- the sandbox does not contain it. "
               "Nothing of theirs has run yet.", "", total)
                    .arg(QString::fromUtf8(doc->Label.getValue()).toHtmlEscaped()),
            &dlg);
    text->setWordWrap(true);
    layout->addWidget(text);

    auto tree = new QTreeWidget(&dlg);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Module"), tr("Objects")});
    tree->setRootIsDecorated(false);
    for (const auto &m : modules) {
        auto item = new QTreeWidgetItem(tree);
        item->setText(0, QString::fromStdString(m.first));
        item->setText(1, QString::number(m.second));
        item->setCheckState(0, Qt::Checked);
        item->setData(0, RoleTarget, QString::fromStdString(m.first));
    }
    tree->resizeColumnToContents(0);
    layout->addWidget(tree, 1);

    QString hintText = tr("The answer is per module. A module left unchecked stays "
                          "unanswered: its objects keep no Proxy, and the padlock in the "
                          "status bar keeps the tally.");
    if (shared)
        hintText += QLatin1Char(' ')
            + tr("This document carries no expressions, and documents without any are told "
                 "apart by nothing -- so this answer would cover every one of them. It is "
                 "offered for this session only, never remembered.");
    auto hint = new QLabel(hintText, &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // the scope verbs of the permissions panel, in the panel's order
    QString scopeChosen;
    bool allowChosen = false;
    auto buttons = new QHBoxLayout;
    auto addBtn = [&](const QString &label, bool allow, const char *scope) {
        auto btn = new QPushButton(label, &dlg);
        connect(btn, &QPushButton::clicked, &dlg, [&, allow, scope]() {
            allowChosen = allow;
            scopeChosen = QString::fromLatin1(scope);
            dlg.accept();
        });
        buttons->addWidget(btn);
    };
    addBtn(tr("Run once"), true, "once");
    addBtn(tr("Run this session"), true, "session");
    if (!shared) {
        // persisted: keyed to THIS file's code, and void the moment it
        // is edited
        addBtn(tr("Always run"), true, "always");
        addBtn(tr("Never run"), false, "always");
    }
    buttons->addStretch();
    auto later = new QPushButton(tr("Not now"), &dlg);
    connect(later, &QPushButton::clicked, &dlg, &QDialog::reject);
    buttons->addWidget(later);
    layout->addLayout(buttons);

    if (dlg.exec() != QDialog::Accepted || scopeChosen.isEmpty())
        return;  // held, and the padlock says so

    bool answered = false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto item = tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked)
            continue;
        auto target = item->data(0, RoleTarget).toString().toStdString();
        try {
            rt.grant(principal, Sec::Permission::HostImport, target, allowChosen,
                     scopeChosen.toLatin1().constData(),
                     doc->Label.getValue(), doc->FileName.getValue());
        }
        catch (const Base::Exception &e) {
            QMessageBox::warning(Gui::getMainWindow(), tr("Expression sandbox"),
                                 QString::fromUtf8(e.what()));
            continue;
        }
        rt.clearPending(principal, Sec::Permission::HostImport, target);
        answered = true;
    }
    if (!answered)
        return;

    // the grant is what re-runs the held restore -- the file is not reopened
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        if (App::ExpressionSandbox::resolveDeferredProxies(doc) > 0) {
            for (App::DocumentObject *obj : App::ExpressionSandbox::hostProxies(doc))
                obj->enforceRecompute();
            doc->recompute();
        }
    }
    catch (const Base::Exception &e) {
        // a Proxy's own execute() is the file's code: it may throw, and
        // the cursor must come back either way
        e.ReportException();
    }
    QApplication::restoreOverrideCursor();
    if (scopeChosen == QLatin1String("once"))
        rt.clearOnce();
#else
    (void)doc;
#endif
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

// ---- SandboxIndicator ------------------------------------------------
//
// The permission indicator above is an alarm: it appears when something
// was blocked.  This one is a status light, always lit, because the state
// it reports is the one nobody would think to go looking for -- that a
// spreadsheet cell's Python runs with the whole process's reach.

/// Re-read the setting when something else changes it: the Python API
/// (FreeCAD.ExpressionSandbox.setRouting) and the parameter editor both
/// write it, and an indicator that lies is worse than none.
class SandboxIndicator::ParamObserver: public ParameterGrp::ObserverType
{
public:
    explicit ParamObserver(SandboxIndicator *owner)
        : indicator(owner)
    {
        hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Expression/Sandbox");
        hGrp->Attach(this);
    }

    ~ParamObserver() override
    {
        hGrp->Detach(this);
    }

    void OnChange(Base::Subject<const char *> &, const char *reason) override
    {
        if (reason && std::strcmp(reason, "Evaluate") == 0)
            indicator->updateState();
    }

private:
    SandboxIndicator *indicator;
    ParameterGrp::handle hGrp;
};

SandboxIndicator::SandboxIndicator(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    connect(this, &QToolButton::clicked, this, &SandboxIndicator::toggleRouting);
    observer = std::make_unique<ParamObserver>(this);
    updateState();
}

SandboxIndicator::~SandboxIndicator() = default;

bool SandboxIndicator::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip)
        updateState();
    return QToolButton::event(e);
}

void SandboxIndicator::updateState()
{
    auto status = App::ExpressionSandbox::sandboxStatus();
    bool confined = status.confined();

    // One object in two states -- a padlock that is open or shut -- so
    // the button reads as a switch.  A generic tick and warning triangle
    // said "ok" and "problem", which is not the same statement.
    setIcon(QIcon(confined
                          ? QStringLiteral(":/icons/expression-sandbox-on.svg")
                          : QStringLiteral(":/icons/expression-sandbox-off.svg")));

    // The tooltip is the whole explanation, so it says what is true now,
    // what a click does, and what each side costs.  Users meet this
    // setting here and nowhere else.
    QString state;
    if (confined) {
        state = tr("<b>Expression Python is sandboxed.</b><br/>"
                   "Formulas and spreadsheet cells are evaluated inside a "
                   "WebAssembly image with no file system, no network and no "
                   "reach into FreeCAD's process.");
    }
    else if (!status.hostBuilt) {
        state = tr("<b>Expression Python runs in this process.</b><br/>"
                   "This build has no sandbox, so there is nothing to switch "
                   "to. Permission checks still apply.");
    }
    else if (!status.imagePresent) {
        state = tr("<b>Expression Python runs in this process.</b><br/>"
                   "The sandbox image is missing, so it cannot be used:<br/>"
                   "%1").arg(QString::fromStdString(status.image).toHtmlEscaped());
    }
    else {
        state = tr("<b>Expression Python runs in this process.</b><br/>"
                   "A formula can reach anything FreeCAD can -- your files "
                   "included. Permission checks apply, but they are a policy, "
                   "not a wall.");
    }

    // What the sandbox did NOT take: a Proxy whose module no guest
    // wheel carries restores in this process and is named here, so the
    // shut padlock never claims more than it holds (docs/Sandbox.md
    // 7.28).
    QString hosted;
#ifdef FC_EXPR_IMAGE_HOST
    if (confined) {
        if (App::Document *doc = App::GetApplication().getActiveDocument()) {
            auto objs = App::ExpressionSandbox::hostProxies(doc);
            if (!objs.empty()) {
                QStringList names;
                for (App::DocumentObject *obj : objs) {
                    if (names.size() == 8) {
                        names << QStringLiteral("...");
                        break;
                    }
                    names << QString::fromUtf8(obj->Label.getValue()).toHtmlEscaped();
                }
                hosted = tr("<b>%n object(s) of the active document run their Python "
                            "Proxy in this process</b>, because the sandbox has no "
                            "module for them: %1", "", int(objs.size()))
                             .arg(names.join(QStringLiteral(", ")));
            }
        }
    }
#endif

    QString trade = tr("Sandboxed evaluation is slower per formula (a few "
                       "microseconds each); model recompute is dominated by "
                       "geometry, not by expressions.");
    QString action;
    if (!status.hostBuilt)
        action = tr("Click for details.");
    else if (confined)
        action = tr("Click to switch back to in-process evaluation.");
    else
        action = tr("Click to evaluate in the sandbox instead. The change "
                    "takes effect immediately -- nothing restarts.");

    if (!hosted.isEmpty())
        state += QStringLiteral("<br/><br/>") + hosted;
    setToolTip(QStringLiteral("%1<br/><br/>%2<br/><br/>%3")
                       .arg(state, trade, action));
}

/** The bootstrap offer (docs/PyodideHost.md sec 12): this build carries
 * the pyodide host but no pyodide runtime is installed for this user.
 * Explicit user action only -- nothing downloads at startup -- so the
 * offer is made here, on the click that asked for the sandbox.  True
 * when a runtime is installed afterwards.
 */
static bool offerPyodideBootstrap(QWidget *parent)
{
    auto answer = QMessageBox::question(parent,
            SandboxIndicator::tr("Expression sandbox"),
            SandboxIndicator::tr("The sandbox runs expressions in pyodide, a Python "
                                 "built for WebAssembly, which is not installed for "
                                 "this user yet.\n\n"
                                 "Download it now (about 14 MB, from the pyodide "
                                 "project's release on github.com, verified against "
                                 "the checksums this FreeCAD was built with)?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    try {
        Base::PyGILStateLocker lock;
        Base::Interpreter().runString("import freecad.pyodide as _fcp\n_fcp.install_runtime()");
    }
    catch (Base::Exception &e) {
        error = QString::fromUtf8(e.what());
    }
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(parent, SandboxIndicator::tr("Expression sandbox"),
                SandboxIndicator::tr("Installing the pyodide runtime failed:\n%1")
                        .arg(error));
        return false;
    }
    return true;
}

void SandboxIndicator::toggleRouting()
{
    auto status = App::ExpressionSandbox::sandboxStatus();
    if (status.hostBuilt && !status.imagePresent && status.runtime == "pyodide"
            && !status.enabled) {
        if (!offerPyodideBootstrap(getMainWindow()))
            return;
        status = App::ExpressionSandbox::sandboxStatus();
    }
    if (!status.hostBuilt || !status.imagePresent) {
        QMessageBox::information(
                getMainWindow(), tr("Expression sandbox"),
                status.hostBuilt
                        ? tr("This installation has no sandbox image, so "
                             "expressions cannot be confined.\n\nLooked for:\n"
                             "%1\n%2")
                                  .arg(QString::fromStdString(status.image),
                                       QString::fromStdString(status.stdlib))
                        : tr("This build of FreeCAD was made without the "
                             "expression sandbox, so expressions cannot be "
                             "confined."));
        return;
    }

    if (status.enabled) {
        App::ExpressionSandbox::setEvaluationRouted(false);
        updateState();
        return;
    }

    // Switching ON is where the image is first loaded, and loading can
    // still fail (a stale or unreadable image).  Do it here, at a user
    // gesture that can be answered, rather than leaving a green light
    // over an evaluator that quietly never engaged.
    App::ExpressionSandbox::setEvaluationRouted(true);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool live = App::ExpressionSandbox::evaluationRouted();
    QApplication::restoreOverrideCursor();
    if (!live) {
        App::ExpressionSandbox::setEvaluationRouted(false);
        QMessageBox::warning(getMainWindow(), tr("Expression sandbox"),
                             tr("The sandbox image is present but could not be "
                                "loaded, so evaluation stays in this process. "
                                "The report view has the reason."));
    }
    updateState();
}

