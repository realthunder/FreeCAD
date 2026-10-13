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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <map>
# include <memory>
# include <set>
# include <QCoreApplication>
# include <QJsonArray>
# include <QJsonDocument>
# include <QJsonObject>
# include <QTimer>
#endif

#include <fastsignals/signal.h>

#include <App/Actor.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/TransactionLog.h>
#include <App/TransactionStore.h>
#include <App/TransactionValue.h>
#include <Base/Console.h>
#include <Base/Exception.h>

#include "Application.h"
#include "Document.h"
#include "SceneControl.h"
#include "SceneLog.h"
#include "SceneServeSource.h"
#include "ViewProviderDocumentObject.h"
#include "Renderer/SceneServer.h"

FC_LOG_LEVEL_INIT("SceneLog", true, true)

using namespace Gui;

namespace
{

/// A page of rows: what is asked for with nothing said, and the most
constexpr int kRowsDefault = 100;
constexpr int kRowsMost = 500;
/// The ops of one row that are sent: a recompute of a large document sets
/// thousands of properties, and a page lists none of that at once
constexpr int kOpsMost = 2000;
/// How much of a value's text is sent: a shape is megabytes of it
constexpr int kValueDefault = 64 * 1024;
constexpr int kValueMost = 1024 * 1024;

QJsonObject okReply(const QJsonValue& id)
{
    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    return reply;
}

/** A document whose log some client has asked the rows of: who they are,
 * and the document's signals that say its log has other rows now.
 */
struct Watch
{
    std::set<uint64_t> clients;
    std::vector<fastsignals::scoped_connection> connections;
    bool queued = false;
};

std::map<std::string, std::unique_ptr<Watch>>& watches()
{
    static std::map<std::string, std::unique_ptr<Watch>> all;
    return all;
}

/// The documents opened from a branch to be served (Gui::serveLogBranch),
/// each with the branch it was opened on
std::map<std::string, int64_t>& servedBranches()
{
    static std::map<std::string, int64_t> all;
    return all;
}

/// What is kept by a document's name goes with the document
void forgetWhenDeleted()
{
    static fastsignals::scoped_connection deleted
        = App::GetApplication().signalDeleteDocument.connect([](const App::Document& gone) {
              watches().erase(gone.getName());
              servedBranches().erase(gone.getName());
          });
}

/** Tell those who asked that the log moved on: the name of the document and
 * nothing of the log -- the answer to any number of rows is one more ask.
 * To the connections that may still edit; one that has gone, or may only
 * look now, is forgotten.
 */
void tell(const std::string& name)
{
    auto it = watches().find(name);
    if (it == watches().end())
        return;
    Watch& watch = *it->second;
    watch.queued = false;
    std::vector<Render::SceneClientInfo> connected;
    Render::SceneStreamServer& server = Render::SceneStreamServer::instance();
    server.clients(connected);
    QJsonObject push;
    push[QLatin1String("op")] = QLatin1String("log.changed");
    push[QLatin1String("doc")] = QString::fromStdString(name);
    const std::string json = QJsonDocument(push).toJson(QJsonDocument::Compact).toStdString();
    for (auto client = watch.clients.begin(); client != watch.clients.end();) {
        auto info = std::find_if(connected.begin(), connected.end(),
                                 [&](const Render::SceneClientInfo& c) {
                                     return c.id == *client;
                                 });
        if (info == connected.end() || !info->authorized
                || info->access < Render::ClientAccess::Edit) {
            client = watch.clients.erase(client);
            continue;
        }
        server.sendControl(*client, json);
        ++client;
    }
    if (watch.clients.empty())
        watches().erase(it);
}

void queueTell(const std::string& name)
{
    auto it = watches().find(name);
    if (it == watches().end() || it->second->queued)
        return;
    it->second->queued = true;
    // One recompute commits and records in a burst
    QTimer::singleShot(150, qApp, [name]() { tell(name); });
}

void watchFor(App::Document* doc, uint64_t client)
{
    if (!client)
        return;
    const std::string name = doc->getName();
    auto& watch = watches()[name];
    if (!watch) {
        watch = std::make_unique<Watch>();
        auto changed = [name](const App::Document&) { queueTell(name); };
        //NOLINTBEGIN
        watch->connections.emplace_back(doc->signalCommitTransaction.connect(changed));
        watch->connections.emplace_back(doc->signalUndo.connect(changed));
        watch->connections.emplace_back(doc->signalRedo.connect(changed));
        watch->connections.emplace_back(doc->signalBranchesChanged.connect(changed));
        watch->connections.emplace_back(doc->signalRecomputed.connect(
            [name](const App::Document&, const std::vector<App::DocumentObject*>&) {
                queueTell(name);
            }));
        //NOLINTEND
        forgetWhenDeleted();
    }
    watch->clients.insert(client);
}

/// The document of a request and its log, or the refusal
App::TransactionLog* logOf(const QJsonObject& req, const std::string& boundDoc,
                           App::Document*& doc, QJsonObject& refusal)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    doc = sceneControlDocument(req, boundDoc);
    if (!doc) {
        refusal = sceneControlError(id, "UnknownDocument",
                                    req.value(QLatin1String("doc")).toString());
        return nullptr;
    }
    App::TransactionLog* log = doc->getTransactionLog();
    if (!log) {
        refusal = sceneControlError(id, "NoLog",
                                    QStringLiteral("the document keeps no transaction log"));
    }
    return log;
}

/// Who wrote the rows of a store: a row's session, and that session's user
struct Authors
{
    explicit Authors(App::TransactionStore& store)
    {
        for (auto& u : store.users())
            users[u.id] = u;
        for (auto& s : store.sessions())
            sessions[s.id] = s;
        for (auto& b : store.branches())
            branches[b.id] = b.name;
    }
    std::map<int64_t, App::LogUser> users;
    std::map<int64_t, App::LogSession> sessions;
    std::map<int64_t, std::string> branches;
};

/** Whose a served document's place in its log is (sec 32.23).
 *
 * A document served as it is, is shared: every client and the desktop are
 * in it, and where it is in its log -- its branch, a version restored --
 * is the host's to move. A client there takes back its own rows and takes
 * versions. A document opened from a branch to be served
 * (Gui.serveBranch) is its clients' from the serving point on: the branch
 * it was opened on, and whatever was branched from that since. Before the
 * serving point it is the host's, as a shared one is.
 */
struct Reach
{
    /// The host's connection, or the desktop: nothing is held from it
    bool host = true;
    /// The document was opened from a branch to be served
    bool own = false;
    int64_t root = 0;            ///< the branch it was opened on
    int64_t floor = 0;           ///< the row that branch forks at: the serving point
    int64_t floorVersion = 0;    ///< and the version
    std::set<int64_t> branches;  ///< that branch and those made from it since

    bool branch(int64_t id) const
    {
        return branches.count(id) > 0;
    }
    /// A row made since the serving point
    bool row(const App::LogTransaction& t) const
    {
        return branch(t.branch);
    }
    /// A version at the serving point, or taken since
    bool version(const App::LogVersion& v) const
    {
        return (floorVersion > 0 && v.num == floorVersion) || (branch(v.branch) && v.seq >= floor);
    }
};

Reach reachOf(App::Document* doc, App::TransactionStore& store)
{
    Reach reach;
    reach.host = sceneControlAccess() >= Render::ClientAccess::Host;
    auto served = servedBranches().find(doc->getName());
    App::LogBranch root;
    if (served == servedBranches().end() || !store.getBranch(served->second, root))
        return reach;
    reach.own = true;
    reach.root = root.id;
    reach.floor = root.fromSeq;
    reach.floorVersion = root.fromVersion;
    reach.branches.insert(root.id);
    // A branch is made from another (LogBranch::target): those made from
    // the one served, and from those
    const auto all = store.branches();
    for (bool grew = true; grew;) {
        grew = false;
        for (const App::LogBranch& b : all) {
            if (!reach.branches.count(b.id) && reach.branches.count(b.target)) {
                reach.branches.insert(b.id);
                grew = true;
            }
        }
    }
    return reach;
}

/// What is not this connection's to do, said
QJsonObject notReached(const QJsonValue& id, const Reach& reach)
{
    return sceneControlError(
        id, "Forbidden",
        reach.own ? QStringLiteral("that is before where this document was served from")
                  : QStringLiteral("in a shared document that is the host's to do"));
}

/// A row of the one who asks (sec 30.10 item 1, as App has it): one user
/// whichever login, by kind and name; no actor is the desktop's user
bool ownRow(const App::LogUser& author)
{
    const auto actor = App::ActorScope::current();
    if (!actor || actor->kind == App::Actor::Local)
        return author.kind == App::Actor::kindName(App::Actor::Local);
    return author.kind == App::Actor::kindName(actor->kind) && author.name == actor->name;
}

QJsonObject rowJson(const App::LogTransaction& t, Authors& authors, App::TransactionStore& store,
                    const std::set<int64_t>& history, const Reach& reach)
{
    QJsonObject row;
    row[QLatin1String("seq")] = double(t.seq);
    row[QLatin1String("parent")] = double(t.parent);
    row[QLatin1String("kind")] = QString::fromStdString(t.kind);
    row[QLatin1String("origin")] = QString::fromStdString(t.origin);
    row[QLatin1String("name")] = QString::fromStdString(t.name);
    row[QLatin1String("time")] = t.time;
    const App::LogUser& author = authors.users[authors.sessions[t.session].user];
    row[QLatin1String("author")] = QString::fromStdString(author.name);
    row[QLatin1String("authorKind")] = QString::fromStdString(author.kind);
    row[QLatin1String("branch")] = double(t.branch);
    row[QLatin1String("branchName")] = QString::fromStdString(authors.branches[t.branch]);
    if (t.inverts > 0)
        row[QLatin1String("inverts")] = double(t.inverts);
    if (t.mergeFrom > 0)
        row[QLatin1String("merged")] = double(t.mergeFrom);
    // A row with no ops is a record (sec 11): it changed nothing
    row[QLatin1String("ops")] = int(store.ops(t.seq).size());
    // Of the history the document is at the head of, or of another branch
    row[QLatin1String("here")] = history.count(t.seq) > 0;
    // The asker's own, and made since the serving point where there is one
    row[QLatin1String("mine")] = ownRow(author);
    row[QLatin1String("within")] = !reach.own || reach.row(t);
    return row;
}

QJsonObject rowsOp(const QJsonObject& req, const std::string& boundDoc, uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const int limit = std::min(
        kRowsMost, std::max(1, req.value(QLatin1String("limit")).toInt(kRowsDefault)));
    QJsonObject reply = okReply(id);
    try {
        App::TransactionStore& store = log->store();
        const int64_t last = store.lastSeq();
        std::vector<App::LogTransaction> rows;
        bool more = false;
        const QJsonValue afterValue = req.value(QLatin1String("after"));
        if (afterValue.isDouble()) {
            // What came since: all of it, or where that is more than a
            // page the newest of it -- `reset`, and the client starts over
            const int64_t after = afterValue.toInteger(0);
            rows = store.transactions(after + 1, 0);
            if (int(rows.size()) > limit) {
                rows.erase(rows.begin(), rows.end() - limit);
                reply[QLatin1String("reset")] = true;
                more = true;
            }
        }
        else {
            // A page ending before `before`: the newest when not said.
            // Rows go as well as come (a trim, a squash), so the numbers
            // have gaps and a page is read back until it is full.
            const QJsonValue beforeValue = req.value(QLatin1String("before"));
            const int64_t before = beforeValue.isDouble() ? beforeValue.toInteger(0) : last + 1;
            int64_t from = before;
            while (int(rows.size()) < limit && from > 1) {
                const int64_t until = from;
                from = std::max<int64_t>(1, from - limit);
                std::vector<App::LogTransaction> read
                    = store.transactions(from, int(until - from));
                read.erase(std::remove_if(read.begin(), read.end(),
                                          [until](const App::LogTransaction& t) {
                                              return t.seq >= until;
                                          }),
                           read.end());
                rows.insert(rows.begin(), read.begin(), read.end());
            }
            if (int(rows.size()) > limit)
                rows.erase(rows.begin(), rows.end() - limit);
            if (!rows.empty()) {
                const auto older = store.transactions(1, 1);
                more = !older.empty() && older.front().seq < rows.front().seq;
            }
        }
        std::set<int64_t> history;
        if (!rows.empty()) {
            for (const auto& t : store.chain(log->head(), rows.front().seq))
                history.insert(t.seq);
        }
        Authors authors(store);
        const Reach reach = reachOf(doc, store);
        QJsonArray list;
        // Newest first, as the desktop's panel lists them
        for (auto t = rows.rbegin(); t != rows.rend(); ++t)
            list.append(rowJson(*t, authors, store, history, reach));
        reply[QLatin1String("rows")] = list;
        reply[QLatin1String("host")] = reach.host;
        reply[QLatin1String("own")] = reach.own;
        if (reach.own)
            reply[QLatin1String("floor")] = double(reach.floor);
        reply[QLatin1String("more")] = more;
        reply[QLatin1String("last")] = double(last);
        reply[QLatin1String("head")] = double(log->head());
        reply[QLatin1String("branch")] = double(log->branch());
        reply[QLatin1String("branchName")]
            = QString::fromStdString(authors.branches[log->branch()]);
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    watchFor(doc, client);
    return reply;
}

QJsonObject opsOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const int64_t row = req.value(QLatin1String("row")).toInteger(0);
    QJsonObject reply = okReply(id);
    try {
        App::TransactionStore& store = log->store();
        const auto rows = store.transactions(row, 1);
        if (row <= 0 || rows.empty() || rows.front().seq != row)
            return sceneControlError(id, "UnknownRow", QString::number(row));
        reply[QLatin1String("row")] = double(row);
        reply[QLatin1String("name")] = QString::fromStdString(rows.front().name);
        // What a record carries in place of ops (sec 11), and the lines a
        // macro recorded of any row
        if (!rows.front().script.empty())
            reply[QLatin1String("script")] = QString::fromStdString(rows.front().script);
        // An object that is gone is known by the name the log made it with
        std::map<long, std::string> names;
        bool namesRead = false;
        QJsonArray list;
        const auto ops = store.ops(row);
        for (const App::LogOp& op : ops) {
            if (list.size() >= kOpsMost)
                break;
            QJsonObject item;
            item[QLatin1String("idx")] = op.idx;
            item[QLatin1String("op")] = QString::fromStdString(op.op);
            item[QLatin1String("kind")] = QString::fromStdString(op.ckind);
            item[QLatin1String("prop")] = QString::fromStdString(op.prop);
            item[QLatin1String("type")]
                = QString::fromStdString(op.prop.empty() ? op.ctype : op.ptype);
            if (!op.vbefore.empty())
                item[QLatin1String("before")] = QString::fromStdString(op.vbefore);
            if (!op.vafter.empty())
                item[QLatin1String("after")] = QString::fromStdString(op.vafter);
            if (op.derived)
                item[QLatin1String("derived")] = true;
            if (op.followed)
                item[QLatin1String("followed")] = true;
            if (op.ckind != "doc") {
                item[QLatin1String("cid")] = double(op.cid);
                App::DocumentObject* obj = doc->getObjectByID(op.cid);
                item[QLatin1String("here")] = obj != nullptr;
                if (obj) {
                    item[QLatin1String("obj")] = QString::fromUtf8(obj->getNameInDocument());
                    item[QLatin1String("label")] = QString::fromUtf8(obj->Label.getValue());
                    // A change of the object's own value that its view
                    // shows the two states of (ViewProvider::ValueDiff)
                    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                        Application::Instance->getViewProvider(obj));
                    if (vp && op.ckind == "obj" && op.op == "set" && !op.vafter.empty()
                            && vp->showsValueDiffOf(op.prop.c_str()))
                        item[QLatin1String("shows")] = true;
                }
                else {
                    if (!op.cname.empty()) {
                        item[QLatin1String("obj")] = QString::fromStdString(op.cname);
                    }
                    else {
                        if (!namesRead) {
                            namesRead = true;
                            // newest first: the first of an id is the one
                            for (const auto& entry : store.objectNames())
                                names.emplace(entry.first, entry.second);
                        }
                        auto found = names.find(op.cid);
                        if (found != names.end())
                            item[QLatin1String("obj")] = QString::fromStdString(found->second);
                    }
                }
            }
            list.append(item);
        }
        reply[QLatin1String("ops")] = list;
        reply[QLatin1String("count")] = int(ops.size());
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    return reply;
}

QJsonObject valueOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string ref = req.value(QLatin1String("ref")).toString().toStdString();
    if (ref.empty())
        return sceneControlError(id, "BadRequest", QStringLiteral("no ref"));
    const int most = std::min(
        kValueMost, std::max(1, req.value(QLatin1String("max")).toInt(kValueDefault)));
    QJsonObject reply = okReply(id);
    reply[QLatin1String("ref")] = QString::fromStdString(ref);
    try {
        App::CapturedValue value;
        if (!log->readValue(ref, value))
            return sceneControlError(id, "UnknownValue", QString::fromStdString(ref));
        const std::string& text = value.fragment;
        size_t cut = std::min(text.size(), size_t(most));
        // Not in the middle of a character
        while (cut < text.size() && cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;
        reply[QLatin1String("text")] = QString::fromUtf8(text.data(), qsizetype(cut));
        reply[QLatin1String("bytes")] = double(text.size());
        reply[QLatin1String("truncated")] = cut < text.size();
        QJsonArray attachments;
        for (const auto& a : value.attachments) {
            QJsonObject item;
            item[QLatin1String("name")] = QString::fromStdString(a.name);
            item[QLatin1String("bytes")] = double(a.bytes.size());
            attachments.append(item);
        }
        reply[QLatin1String("attachments")] = attachments;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    return reply;
}

/// The rows of the history the document is at the head of
std::set<int64_t> historyOf(App::TransactionLog* log)
{
    std::set<int64_t> rows;
    for (const auto& t : log->store().history(log->head()))
        rows.insert(t.seq);
    return rows;
}

/// A row that has something to take back: the desktop's panel offers the
/// same kinds (sec 24.4)
bool undoableKind(const std::string& kind)
{
    return kind == "user" || kind == "implicit" || kind == "undo" || kind == "redo"
        || kind == "pick";
}

/** What an act left: the place of the document in its log, which is what
 * the page lists its rows against. The cue (`log.changed`) follows by
 * itself; this is so that the one who asked need not wait for it.
 */
QJsonObject placeReply(const QJsonValue& id, App::Document* doc, App::TransactionLog* log)
{
    QJsonObject reply = okReply(id);
    App::TransactionStore& store = log->store();
    reply[QLatin1String("last")] = double(store.lastSeq());
    reply[QLatin1String("head")] = double(log->head());
    reply[QLatin1String("branch")] = double(log->branch());
    App::LogBranch branch;
    if (store.getBranch(log->branch(), branch))
        reply[QLatin1String("branchName")] = QString::fromStdString(branch.name);
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    return reply;
}

/// What a client had selected may be of objects that are gone now
void objectsMoved(App::Document* doc)
{
    if (SceneServeSource* source = SceneServeSource::sourceFor(doc))
        source->clearClientSelections();
}

/** An edit is open in the document. What checks the whole document out
 * under it -- another branch's head -- waits for it to be left: the one in
 * the edit may be another client, who did not ask.
 */
bool editOpen(App::Document* doc)
{
    Gui::Document* gdoc = Application::Instance->getDocument(doc);
    return gdoc && gdoc->getInEdit() != nullptr;
}

/** `log.undo`: the rows named taken back as one step, though none is the
 * last (sec 24.4, 33.13). All of them or none; the refusal says which row
 * is in the way and what changed since.
 */
QJsonObject undoRowsOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    std::vector<int64_t> rows;
    for (const QJsonValue& value : req.value(QLatin1String("rows")).toArray()) {
        if (value.isDouble() && value.toInteger(0) > 0)
            rows.push_back(value.toInteger(0));
    }
    if (rows.empty())
        return sceneControlError(id, "BadRequest", QStringLiteral("no rows"));
    try {
        App::TransactionStore& store = log->store();
        const std::set<int64_t> history = historyOf(log);
        const Reach reach = reachOf(doc, store);
        Authors authors(store);
        for (int64_t row : rows) {
            const auto read = store.transactions(row, 1);
            if (read.empty() || read.front().seq != row)
                return sceneControlError(id, "UnknownRow", QString::number(row));
            // A row of another branch is not this document's to take back:
            // what it set is not here
            if (!history.count(row))
                return sceneControlError(
                    id, "NotHere",
                    QStringLiteral("row %1 is not of this branch's history").arg(row));
            if (!undoableKind(read.front().kind) || store.ops(row).empty())
                return sceneControlError(
                    id, "NothingToUndo",
                    QStringLiteral("row %1 changed nothing that is taken back").arg(row));
            if (!reach.host) {
                if (reach.own && !reach.row(read.front()))
                    return notReached(id, reach);
                // A step is its author's (sec 30.10): the undo of a row
                // from the panel is the same act as the undo of the last
                const App::LogUser& author
                    = authors.users[authors.sessions[read.front().session].user];
                if (!ownRow(author))
                    return sceneControlError(
                        id, "NotYours",
                        QStringLiteral("row %1 is %2's: a row is taken back by who made it, "
                                       "or by the host")
                            .arg(row)
                            .arg(QString::fromStdString(author.name)));
            }
        }
        if (!doc->undoLogged(rows)) {
            const std::string why = doc->undoRefusal();
            return sceneControlError(id, "Refused",
                                     why.empty() ? QStringLiteral("not undone")
                                                 : QString::fromStdString(why).trimmed());
        }
        objectsMoved(doc);
        return placeReply(id, doc, log);
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
}

/// `log.versions`: the versions of the log, oldest first as they are numbered
QJsonObject versionsOp(const QJsonObject& req, const std::string& boundDoc, uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        App::TransactionStore& store = log->store();
        std::map<int64_t, std::string> branches;
        for (const auto& b : store.branches())
            branches[b.id] = b.name;
        const std::set<int64_t> history = historyOf(log);
        const Reach reach = reachOf(doc, store);
        // The newest row of the history that changed the document: a
        // version at it, or after it with only records between (a save, a
        // snapshot, a switch), is the document as it stands
        int64_t changed = 0;
        for (auto row = history.rbegin(); row != history.rend() && !changed; ++row) {
            if (!store.ops(*row).empty())
                changed = *row;
        }
        QJsonArray list;
        for (const App::LogVersion& v : store.versions()) {
            QJsonObject item;
            item[QLatin1String("num")] = double(v.num);
            item[QLatin1String("named")] = v.kind == "named";
            item[QLatin1String("name")] = QString::fromStdString(v.name);
            item[QLatin1String("branch")] = double(v.branch);
            item[QLatin1String("branchName")] = QString::fromStdString(branches[v.branch]);
            item[QLatin1String("seq")] = double(v.seq);
            item[QLatin1String("time")] = v.created;
            // Of the history the document is at the head of; a version
            // taken at no row is its own branch's
            const bool here = v.seq > 0 ? history.count(v.seq) > 0 : v.branch == log->branch();
            item[QLatin1String("here")] = here;
            item[QLatin1String("head")] = here && v.seq >= changed;
            // At the serving point or since, where there is one
            item[QLatin1String("within")] = !reach.own || reach.version(v);
            list.append(item);
        }
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("versions")] = list;
        reply[QLatin1String("host")] = reach.host;
        reply[QLatin1String("own")] = reach.own;
        watchFor(doc, client);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
}

/// `log.branches`: the branches of the log, and which the document is on
QJsonObject branchesOp(const QJsonObject& req, const std::string& boundDoc, uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        const Reach reach = reachOf(doc, log->store());
        QJsonArray list;
        for (const App::LogBranch& b : log->store().branches()) {
            QJsonObject item;
            item[QLatin1String("id")] = double(b.id);
            item[QLatin1String("name")] = QString::fromStdString(b.name);
            item[QLatin1String("head")] = double(b.head);
            item[QLatin1String("fromVersion")] = double(b.fromVersion);
            item[QLatin1String("fromSeq")] = double(b.fromSeq);
            item[QLatin1String("time")] = b.created;
            // A closed branch is not switched to (sec 26.6)
            item[QLatin1String("closed")] = b.closed != 0;
            item[QLatin1String("current")] = b.id == log->branch();
            // The one served or made from it since, where the document
            // was opened from a branch; and the one served itself
            item[QLatin1String("within")] = !reach.own || reach.branch(b.id);
            if (reach.own && b.id == reach.root)
                item[QLatin1String("root")] = true;
            list.append(item);
        }
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("branches")] = list;
        // Whose the document's place in its log is (Reach): said, so that
        // a page does not offer what would be refused
        reply[QLatin1String("host")] = reach.host;
        reply[QLatin1String("own")] = reach.own;
        watchFor(doc, client);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
}

/** `log.snapshot`: a version of the document as it stands (sec 16.3), named
 * where a name is given -- a named one is never evicted.
 */
QJsonObject snapshotOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string name = req.value(QLatin1String("name")).toString().trimmed().toStdString();
    try {
        const int64_t num = doc->snapshotToLog();
        if (num <= 0)
            return sceneControlError(id, "Refused",
                                     QStringLiteral("the document cannot be snapshotted now"));
        if (!name.empty())
            log->store().nameVersion(num, name);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("version")] = double(num);
        // A version is no row, and nothing else says the list of them moved
        queueTell(doc->getName());
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
}

/// `log.nameVersion`: name a version, or with no name make it unnamed again
QJsonObject nameVersionOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const int64_t num = req.value(QLatin1String("version")).toInteger(0);
    const std::string name = req.value(QLatin1String("name")).toString().trimmed().toStdString();
    try {
        App::LogVersion version;
        if (num <= 0 || !log->store().getVersion(num, version))
            return sceneControlError(id, "UnknownVersion", QString::number(num));
        // A version's name is what keeps it, and whose it is nothing
        // says: the host's, but in a document of the clients' own
        const Reach reach = reachOf(doc, log->store());
        if (!reach.host && !(reach.own && reach.version(version)))
            return notReached(id, reach);
        if (!log->store().nameVersion(num, name))
            return sceneControlError(id, "Refused", QStringLiteral("not named"));
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("version")] = double(num);
        queueTell(doc->getName());
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
}

/** `log.restore`: the document made what a version was, as one step that
 * undo takes back like any (sec 24.5).
 */
QJsonObject restoreOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const int64_t num = req.value(QLatin1String("version")).toInteger(0);
    try {
        App::LogVersion version;
        if (num <= 0 || !log->store().getVersion(num, version))
            return sceneControlError(id, "UnknownVersion", QString::number(num));
        // It takes back what everyone did since the version
        const Reach reach = reachOf(doc, log->store());
        if (!reach.host && !(reach.own && reach.version(version)))
            return notReached(id, reach);
        if (!doc->restoreVersion(num))
            return sceneControlError(id, "Refused", QStringLiteral("not restored"));
        objectsMoved(doc);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("version")] = double(num);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/** `log.branch`: a new branch -- from a version, or else from a row, or
 * else from where the document is -- and the document switched to it
 * (sec 17, 26).
 */
QJsonObject newBranchOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string name = req.value(QLatin1String("name")).toString().trimmed().toStdString();
    if (name.empty())
        return sceneControlError(id, "BadRequest", QStringLiteral("no name"));
    const int64_t version = req.value(QLatin1String("version")).toInteger(0);
    const int64_t row = req.value(QLatin1String("row")).toInteger(0);
    try {
        App::TransactionStore& store = log->store();
        // The document goes to the new branch, and everyone in it
        const Reach reach = reachOf(doc, store);
        if (!reach.host) {
            bool reached = reach.own;
            if (reached && version > 0) {
                App::LogVersion from;
                reached = !store.getVersion(version, from) || reach.version(from);
            }
            else if (reached && row > 0) {
                const auto read = store.transactions(row, 1);
                reached = read.empty() || read.front().seq != row || row == reach.floor
                    || reach.row(read.front());
            }
            else if (reached) {
                reached = reach.branch(log->branch());
            }
            if (!reached)
                return notReached(id, reach);
        }
        // From anywhere but where the document is, the document is
        // checked out
        if ((version > 0 || (row > 0 && row != log->head())) && editOpen(doc))
            return sceneControlError(id, "Editing",
                                     QStringLiteral("an edit is open: leave it first"));
        const int64_t left = log->branch();
        const int64_t made = doc->createBranch(name, version, row);
        if (made <= 0)
            return sceneControlError(id, "Refused", QStringLiteral("no branch made"));
        // One made at the serving point is made from the row of the
        // branch the document was served from, which is not the clients':
        // it is said to be made from where the document was, so that it
        // is one of theirs
        App::LogBranch branch;
        if (reach.own && store.getBranch(made, branch) && !reach.branch(branch.target)) {
            branch.target = reach.branch(left) ? left : reach.root;
            store.updateBranch(branch);
        }
        objectsMoved(doc);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("made")] = double(made);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/// `log.switch`: the document made the head of another branch, in place
QJsonObject switchOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string name = req.value(QLatin1String("name")).toString().toStdString();
    try {
        App::LogBranch branch;
        if (name.empty() || !log->store().findBranch(name, branch))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(name));
        // The whole document is checked out, under everyone in it
        const Reach reach = reachOf(doc, log->store());
        if (!reach.host && !(reach.own && reach.branch(branch.id)))
            return notReached(id, reach);
        if (branch.id != log->branch() && editOpen(doc))
            return sceneControlError(id, "Editing",
                                     QStringLiteral("an edit is open: leave it first"));
        if (!doc->switchBranch(name))
            return sceneControlError(id, "Refused", QStringLiteral("not switched"));
        objectsMoved(doc);
        return placeReply(id, doc, log);
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/// `log.renameBranch`
QJsonObject renameBranchOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string name = req.value(QLatin1String("name")).toString().toStdString();
    const std::string to = req.value(QLatin1String("to")).toString().trimmed().toStdString();
    if (to.empty())
        return sceneControlError(id, "BadRequest", QStringLiteral("no name"));
    try {
        App::LogBranch branch;
        if (name.empty() || !log->store().findBranch(name, branch))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(name));
        const Reach reach = reachOf(doc, log->store());
        if (!reach.host && !(reach.own && reach.branch(branch.id)))
            return notReached(id, reach);
        if (!doc->renameBranch(name, to))
            return sceneControlError(id, "Refused", QStringLiteral("not renamed"));
        return placeReply(id, doc, log);
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/** `log.deleteBranch`: a branch and the rows only it holds, gone. Nothing
 * takes that back. The host's -- and, in a document opened from a branch
 * to be served, its clients' of the branches made there since, but the one
 * it was opened on.
 */
QJsonObject deleteBranchOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string name = req.value(QLatin1String("name")).toString().toStdString();
    try {
        App::LogBranch branch;
        if (name.empty() || !log->store().findBranch(name, branch))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(name));
        const Reach reach = reachOf(doc, log->store());
        // The one the document was opened on is what the clients' reach
        // is counted from
        if (!reach.host && reach.own && branch.id == reach.root)
            return sceneControlError(
                id, "Forbidden",
                QStringLiteral("this document was served from that branch: the host deletes it"));
        if (!reach.host && !(reach.own && reach.branch(branch.id)))
            return notReached(id, reach);
        const size_t rows = doc->deleteBranch(name);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("rows")] = double(rows);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

}  // namespace

bool Gui::serveLogBranch(App::Document* doc)
{
    App::TransactionLog* log = doc ? doc->getTransactionLog() : nullptr;
    App::LogBranch branch;
    if (!log || !log->store().getBranch(log->branch(), branch))
        return false;
    forgetWhenDeleted();
    servedBranches()[doc->getName()] = branch.id;
    return true;
}

void Gui::installSceneLogOps()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    // Ops that write, though they read: what is in the log is for those who
    // may edit (ruled 2026-10-13), and it is the dispatch that refuses a
    // connection that may only look, before a handler runs
    registerSceneControlOp(QStringLiteral("log.rows"), true, rowsOp);
    registerSceneControlOp(QStringLiteral("log.ops"), true, opsOp);
    registerSceneControlOp(QStringLiteral("log.value"), true, valueOp);
    registerSceneControlOp(QStringLiteral("log.versions"), true, versionsOp);
    registerSceneControlOp(QStringLiteral("log.branches"), true, branchesOp);
    // And those that do write (sec 32.22)
    registerSceneControlOp(QStringLiteral("log.undo"), true, undoRowsOp);
    registerSceneControlOp(QStringLiteral("log.snapshot"), true, snapshotOp);
    registerSceneControlOp(QStringLiteral("log.nameVersion"), true, nameVersionOp);
    registerSceneControlOp(QStringLiteral("log.restore"), true, restoreOp);
    registerSceneControlOp(QStringLiteral("log.branch"), true, newBranchOp);
    registerSceneControlOp(QStringLiteral("log.switch"), true, switchOp);
    registerSceneControlOp(QStringLiteral("log.renameBranch"), true, renameBranchOp);
    registerSceneControlOp(QStringLiteral("log.deleteBranch"), true, deleteBranchOp);
}
