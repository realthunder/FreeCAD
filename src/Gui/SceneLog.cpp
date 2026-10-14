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
# include <QRegularExpression>
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
/// A value of a merge's preview, as text, at most so long
constexpr int kChangeText = 160;
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

/** The documents of one file share its log (sec 27.7): a row one of them
 * writes is among what the others are listed -- a row of a branch that is
 * read there, to apply; a request dealt with. So what moves the log of one
 * is said to those who watch another of the same file.
 */
void tellTheFile(const App::Document& changed)
{
    App::TransactionLog* log = changed.getTransactionLog();
    if (!log || watches().empty())
        return;
    for (const auto& entry : watches()) {
        if (entry.first == changed.getName())
            continue;   // told by its own signals
        App::Document* other = App::GetApplication().getDocument(entry.first.c_str());
        App::TransactionLog* theirs = other ? other->getTransactionLog() : nullptr;
        if (theirs && &theirs->store() == &log->store())
            queueTell(entry.first);
    }
}

void watchTheFiles()
{
    static std::vector<fastsignals::scoped_connection> connections;
    if (!connections.empty())
        return;
    auto told = [](const App::Document& doc) { tellTheFile(doc); };
    //NOLINTBEGIN
    connections.emplace_back(App::GetApplication().signalCommitTransaction.connect(told));
    connections.emplace_back(App::GetApplication().signalUndoDocument.connect(told));
    connections.emplace_back(App::GetApplication().signalRedoDocument.connect(told));
    connections.emplace_back(App::GetApplication().signalRecomputed.connect(told));
    //NOLINTEND
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
        watch->connections.emplace_back(doc->signalBranchesChanged.connect(
            [name](const App::Document& moved) {
                queueTell(name);
                tellTheFile(moved);
            }));
        watch->connections.emplace_back(doc->signalRecomputed.connect(
            [name](const App::Document&, const std::vector<App::DocumentObject*>&) {
                queueTell(name);
            }));
        //NOLINTEND
        forgetWhenDeleted();
        watchTheFiles();
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

/** What the one who asks may do of the document's log (sec 32.25).
 *
 * The host's connection, and the desktop, may all of it. Anyone else may by
 * branch, and of a branch either read or write:
 *
 * - *read*: the branch's history is listed to it; it applies rows of the
 *   branch to the one the document is on, and asks for the branch to be
 *   merged into another it reads, or another into it;
 * - *write*: it also moves a document that is on the branch -- restores a
 *   version, switches to it, branches from it, deletes what was branched
 *   from it -- from a point on, and deals with the requests to merge into
 *   it.
 *
 * What it has is said in the log's store, by user and branch (LogAccess).
 * Where nothing is said: the branch of the document it is in is read, which
 * is every client of a shared document; a document opened from a branch to
 * be served (Gui.serveBranch) is written by whoever joins it, from where
 * that branch forks, and the branch it was made from is read. A branch it
 * makes (`log.branch`) is said to be its own as it is made
 * (LogAccess::made): written from its fork, and its to delete. Who made a
 * branch is not told from where the branch forks -- the host's branch off
 * one a client writes is the host's.
 */
struct Reach
{
    /// The host's connection, or the desktop: nothing is held from it
    bool host = true;
    /// What the asker may of one branch
    struct Of
    {
        bool read = false;
        bool write = false;
        /// One it made (LogAccess::made): its own to delete
        bool made = false;
        int64_t fromSeq = 0;      ///< the point it writes the branch from
        int64_t fromVersion = 0;
    };
    std::map<int64_t, Of> of;
    /// The branch the document is on
    int64_t working = 0;
    /// The rows of the histories it reads
    std::set<int64_t> visible;

    const Of* find(int64_t branch) const
    {
        auto it = of.find(branch);
        return it == of.end() ? nullptr : &it->second;
    }
    bool reads(int64_t branch) const
    {
        const Of* o = find(branch);
        return host || (o && o->read);
    }
    bool writes(int64_t branch) const
    {
        const Of* o = find(branch);
        return host || (o && o->write);
    }
    /// The document is the asker's to move: it writes the branch it is on
    bool moves() const
    {
        return writes(working);
    }
    /// A row it is listed
    bool sees(const App::LogTransaction& t) const
    {
        return host || visible.count(t.seq) > 0;
    }
    /// A row it may move a document from: of a branch it writes, since the
    /// point it writes that from -- or such a point itself
    bool row(const App::LogTransaction& t) const
    {
        if (host)
            return true;
        for (const auto& entry : of) {
            if (entry.second.write && entry.second.fromSeq == t.seq)
                return true;
        }
        const Of* o = find(t.branch);
        return o && o->write && t.seq >= o->fromSeq;
    }
    /// The same of a version
    bool version(const App::LogVersion& v) const
    {
        if (host)
            return true;
        for (const auto& entry : of) {
            if (entry.second.write && entry.second.fromVersion > 0
                    && entry.second.fromVersion == v.num)
                return true;
        }
        const Of* o = find(v.branch);
        return o && o->write && v.seq >= o->fromSeq;
    }
    /// A version it may name, or unname: of a branch it writes, since the
    /// point. Not the point itself where that is another branch's version
    /// -- the one a branch forks from is kept by its name (sec 16.3), and
    /// is the other branch's to keep
    bool names(const App::LogVersion& v) const
    {
        if (host)
            return true;
        const Of* o = find(v.branch);
        return o && o->write && v.seq >= o->fromSeq;
    }
    /// A version it is listed: of a branch it reads, at a row it is listed
    /// -- one taken on another branch at a row both have is the other's --
    /// or the point it writes a branch from
    bool sees(const App::LogVersion& v) const
    {
        if (host)
            return true;
        if (v.seq > 0 && !visible.count(v.seq))
            return false;
        if (reads(v.branch))
            return true;
        for (const auto& entry : of) {
            if (entry.second.write && entry.second.fromVersion > 0
                    && entry.second.fromVersion == v.num)
                return true;
        }
        return false;
    }
};

/// Who asks, as the log names a user: the kind and the name
std::pair<std::string, std::string> asker()
{
    const auto actor = App::ActorScope::current();
    if (!actor || actor->kind == App::Actor::Local)
        return {App::Actor::kindName(App::Actor::Local), std::string()};
    return {App::Actor::kindName(actor->kind), actor->name};
}

Reach reachOf(App::Document* doc, App::TransactionLog* log)
{
    Reach reach;
    reach.host = sceneControlAccess() >= Render::ClientAccess::Host;
    reach.working = log->branch();
    if (reach.host)
        return reach;
    App::TransactionStore& store = log->store();
    std::map<int64_t, App::LogBranch> byId;
    for (const App::LogBranch& b : store.branches())
        byId[b.id] = b;

    // What is said of this user
    const auto who = asker();
    std::set<int64_t> said;
    for (const App::LogAccess& a : store.accesses()) {
        auto b = byId.find(a.branch);
        if (a.kind != who.first || a.name != who.second || b == byId.end())
            continue;
        said.insert(a.branch);
        Reach::Of& o = reach.of[a.branch];
        o.read = true;
        o.write = a.write;
        o.made = a.write && a.made;
        if (a.write) {
            const bool whole = a.fromSeq == 0 && a.fromVersion == 0;
            o.fromSeq = whole ? b->second.fromSeq : a.fromSeq;
            o.fromVersion = whole ? b->second.fromVersion : a.fromVersion;
        }
    }
    // Where nothing is said: the branch of the document it is in is read,
    // and one opened to be served is written from where it forks
    if (!said.count(reach.working))
        reach.of[reach.working].read = true;
    auto served = servedBranches().find(doc->getName());
    if (served != servedBranches().end() && !said.count(served->second)) {
        auto root = byId.find(served->second);
        if (root != byId.end()) {
            Reach::Of& o = reach.of[root->first];
            o.read = o.write = true;
            o.fromSeq = root->second.fromSeq;
            o.fromVersion = root->second.fromVersion;
            // ... and the branch that one was made from is read, so that
            // what is done here can be asked back into it
            if (byId.count(root->second.target) && !said.count(root->second.target))
                reach.of[root->second.target].read = true;
        }
    }
    // What it is listed: the histories of the branches it reads
    for (const auto& entry : reach.of) {
        if (!entry.second.read)
            continue;
        for (const auto& t : store.history(byId[entry.first].head))
            reach.visible.insert(t.seq);
    }
    // (rows the document has that its branch's head does not name yet)
    for (const auto& t : store.history(log->head()))
        reach.visible.insert(t.seq);
    return reach;
}

/// What is not this connection's to do, said
QJsonObject notReached(const QJsonValue& id, const char* why)
{
    return sceneControlError(id, "Forbidden", QString::fromLatin1(why));
}

const char* const kNeedsWrite
    = "that needs the right to write this branch, which the host gives";
const char* const kNeedsRead = "that branch is not one this connection reads";
const char* const kBeforePoint
    = "that is from before the point this branch is written from";

/// A row of the one who asks (sec 30.10 item 1, as App has it): one user
/// whichever login, by kind and name; no actor is the desktop's user
bool ownRow(const App::LogUser& author)
{
    const auto who = asker();
    if (who.first == App::Actor::kindName(App::Actor::Local))
        return author.kind == who.first;
    return author.kind == who.first && author.name == who.second;
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
    // The asker's own; and one a document may be moved from by the asker
    row[QLatin1String("mine")] = ownRow(author);
    row[QLatin1String("within")] = reach.row(t);
    return row;
}

/// Who asks, said with every list: the host, or one who writes the branch
/// the document is on -- and from which row
void reachJson(QJsonObject& reply, const Reach& reach)
{
    reply[QLatin1String("host")] = reach.host;
    reply[QLatin1String("write")] = reach.moves();
    const Reach::Of* here = reach.find(reach.working);
    if (!reach.host && here && here->write)
        reply[QLatin1String("floor")] = double(here->fromSeq);
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
        const Reach reach = reachOf(doc, log);
        QJsonArray list;
        // Newest first, as the desktop's panel lists them; of the branches
        // the asker reads (a page may come out short of its limit for it:
        // `more` is of the log, and asking for older ones goes on)
        for (auto t = rows.rbegin(); t != rows.rend(); ++t) {
            if (reach.sees(*t))
                list.append(rowJson(*t, authors, store, history, reach));
        }
        reply[QLatin1String("rows")] = list;
        if (!rows.empty())
            reply[QLatin1String("oldest")] = double(rows.front().seq);
        reachJson(reply, reach);
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
        // A row of a branch the asker does not read is none to it
        if (row <= 0 || rows.empty() || rows.front().seq != row
                || !reachOf(doc, log).sees(rows.front()))
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
        const Reach reach = reachOf(doc, log);
        Authors authors(store);
        for (int64_t row : rows) {
            const auto read = store.transactions(row, 1);
            // A row of a branch the asker does not read is none to it
            if (read.empty() || read.front().seq != row || !reach.sees(read.front()))
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
        const Reach reach = reachOf(doc, log);
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
            if (!reach.sees(v))
                continue;
            QJsonObject item;
            item[QLatin1String("num")] = double(v.num);
            item[QLatin1String("named")] = v.kind == "named";
            item[QLatin1String("name")] = QString::fromStdString(v.name);
            // (the point a branch is written from may be a version of a
            // branch the asker does not read, which is not named to it)
            if (reach.reads(v.branch)) {
                item[QLatin1String("branch")] = double(v.branch);
                item[QLatin1String("branchName")] = QString::fromStdString(branches[v.branch]);
            }
            item[QLatin1String("seq")] = double(v.seq);
            item[QLatin1String("time")] = v.created;
            // Of the history the document is at the head of; a version
            // taken at no row is its own branch's
            const bool here = v.seq > 0 ? history.count(v.seq) > 0 : v.branch == log->branch();
            item[QLatin1String("here")] = here;
            item[QLatin1String("head")] = here && v.seq >= changed;
            // One the asker may move a document to, or name
            item[QLatin1String("within")] = reach.version(v);
            list.append(item);
        }
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("versions")] = list;
        reachJson(reply, reach);
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
        const Reach reach = reachOf(doc, log);
        QJsonArray list;
        for (const App::LogBranch& b : log->store().branches()) {
            if (!reach.reads(b.id))
                continue;
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
            // Whether the asker writes it, and from which row; and whether
            // it is one it made, which is its to delete
            const Reach::Of* of = reach.find(b.id);
            item[QLatin1String("write")] = reach.writes(b.id);
            if (!reach.host && of && of->write)
                item[QLatin1String("floor")] = double(of->fromSeq);
            item[QLatin1String("made")] = reach.host || (of && of->made);
            list.append(item);
        }
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("branches")] = list;
        // What the asker may (Reach): said, so that a page does not offer
        // what would be refused
        reachJson(reply, reach);
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
        // A version the asker is not listed is none to it
        App::LogVersion version;
        const Reach reach = reachOf(doc, log);
        if (num <= 0 || !log->store().getVersion(num, version) || !reach.sees(version))
            return sceneControlError(id, "UnknownVersion", QString::number(num));
        // A version's name is what keeps it: for one who writes the
        // branch it is of, since the point
        if (!reach.names(version))
            return notReached(id, reach.writes(version.branch) ? kBeforePoint : kNeedsWrite);
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
        const Reach reach = reachOf(doc, log);
        if (num <= 0 || !log->store().getVersion(num, version) || !reach.sees(version))
            return sceneControlError(id, "UnknownVersion", QString::number(num));
        // It takes back what everyone did since the version: for one who
        // writes the branch the document is on, to a version it may go to
        if (!reach.moves())
            return notReached(id, kNeedsWrite);
        if (!reach.version(version))
            return notReached(id, kBeforePoint);
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
        // The document goes to the new branch, and everyone in it: for
        // one who writes the branch it is on, from a row or a version it
        // may go to
        const Reach reach = reachOf(doc, log);
        if (!reach.moves())
            return notReached(id, kNeedsWrite);
        // (a version or a row the asker is not listed is none to it)
        if (version > 0) {
            App::LogVersion from;
            if (!store.getVersion(version, from) || !reach.sees(from))
                return sceneControlError(id, "UnknownVersion", QString::number(version));
            if (!reach.version(from))
                return notReached(id, kBeforePoint);
        }
        else if (row > 0) {
            const auto read = store.transactions(row, 1);
            if (read.empty() || read.front().seq != row || !reach.sees(read.front()))
                return sceneControlError(id, "UnknownRow", QString::number(row));
            if (!reach.row(read.front()))
                return notReached(id, kBeforePoint);
        }
        // From anywhere but where the document is, the document is
        // checked out
        if ((version > 0 || (row > 0 && row != log->head())) && editOpen(doc))
            return sceneControlError(id, "Editing",
                                     QStringLiteral("an edit is open: leave it first"));
        const int64_t made = doc->createBranch(name, version, row);
        if (made <= 0)
            return sceneControlError(id, "Refused", QStringLiteral("no branch made"));
        // It is its maker's own (sec 32.25): written from its fork, and
        // the maker's to delete. Said in the log, by the user -- where a
        // branch forks does not say who made it
        if (!reach.host) {
            const auto who = asker();
            App::LogAccess own;
            own.kind = who.first;
            own.name = who.second;
            own.branch = made;
            own.write = true;
            own.made = true;
            store.setAccess(own);
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
        const Reach reach = reachOf(doc, log);
        if (name.empty() || !log->store().findBranch(name, branch) || !reach.reads(branch.id))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(name));
        // The whole document is checked out, under everyone in it: for
        // one who writes the branch it is on, to one it writes
        if (!reach.moves() || !reach.writes(branch.id))
            return notReached(id, kNeedsWrite);
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
        const Reach reach = reachOf(doc, log);
        if (name.empty() || !log->store().findBranch(name, branch) || !reach.reads(branch.id))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(name));
        if (!reach.writes(branch.id))
            return notReached(id, kNeedsWrite);
        if (!doc->renameBranch(name, to))
            return sceneControlError(id, "Refused", QStringLiteral("not renamed"));
        return placeReply(id, doc, log);
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/** `log.deleteBranch`: a branch and the rows only it holds, gone. Nothing
 * takes that back. The host's -- and of a branch the asker made, the
 * asker's. Not of a branch it was given, nor of one another made.
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
        const Reach reach = reachOf(doc, log);
        if (name.empty() || !log->store().findBranch(name, branch) || !reach.reads(branch.id))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(name));
        const Reach::Of* of = reach.find(branch.id);
        if (!reach.host && !(of && of->made))
            return notReached(id, of && of->write
                                      ? "that branch was given, not made here: the host deletes it"
                                      : kNeedsWrite);
        const size_t rows = doc->deleteBranch(name);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("rows")] = double(rows);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/// The side asked for every conflict nobody picked: `ours`, `theirs`, or
/// none -- in which case a conflict refuses
std::string fallbackOf(const QJsonObject& req)
{
    const QString side = req.value(QLatin1String("fallback")).toString();
    return side == QLatin1String("ours") || side == QLatin1String("theirs") ? side.toStdString()
                                                                           : std::string();
}

/// A merge or an apply refused for conflicts with no side: how many, so that
/// the one who asked can say a side for them all, or go to where each is
/// picked
QJsonObject conflictsReply(const QJsonValue& id, size_t count)
{
    QJsonObject reply = sceneControlError(
        id, "Conflicts",
        QStringLiteral("%1 of what it sets was also set here since: say a side for them, "
                       "or pick each at the desktop's panel")
            .arg(count));
    reply[QLatin1String("conflicts")] = double(count);
    return reply;
}

/// The side picked for each conflict, by its key (MergeChange::key): `ours`
/// or `theirs`. A key with `ours` of a change that asks nothing leaves it
/// out (Document::pickRows)
std::map<std::string, std::string> picksOf(const QJsonObject& req)
{
    std::map<std::string, std::string> picks;
    const QJsonObject said = req.value(QLatin1String("picks")).toObject();
    for (auto it = said.begin(); it != said.end(); ++it) {
        const QString side = it.value().toString();
        if (side == QLatin1String("ours") || side == QLatin1String("theirs"))
            picks[it.key().toStdString()] = side.toStdString();
    }
    return picks;
}

/// The conflicts of a preview no side is picked for
size_t unpicked(const App::Document::MergePreview& preview,
                const std::map<std::string, std::string>& picks)
{
    size_t count = 0;
    for (const auto& c : preview.changes) {
        if (c.kind == "conflict" && !picks.count(c.key))
            ++count;
    }
    return count;
}

/** The rows an apply, or its preview, names: of a branch the asker reads,
 * and not of this branch's history. False with the refusal.
 */
bool rowsToApply(const QJsonObject& req, App::TransactionLog* log, const Reach& reach,
                 std::vector<int64_t>& rows, QJsonObject& refusal)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    for (const QJsonValue& value : req.value(QLatin1String("rows")).toArray()) {
        if (value.isDouble() && value.toInteger(0) > 0)
            rows.push_back(value.toInteger(0));
    }
    if (rows.empty()) {
        refusal = sceneControlError(id, "BadRequest", QStringLiteral("no rows"));
        return false;
    }
    App::TransactionStore& store = log->store();
    const std::set<int64_t> history = historyOf(log);
    for (int64_t row : rows) {
        const auto read = store.transactions(row, 1);
        // A row of a branch the asker does not read is none to it
        if (read.empty() || read.front().seq != row || !reach.sees(read.front())) {
            refusal = sceneControlError(id, "UnknownRow", QString::number(row));
            return false;
        }
        if (history.count(row)) {
            refusal = sceneControlError(
                id, "Here", QStringLiteral("row %1 is of this branch's history already").arg(row));
            return false;
        }
    }
    return true;
}

/** `log.apply`: rows of a branch the asker reads, applied to the branch the
 * document is on as a step of its own (sec 31.12) -- the asker's, as any
 * edit of the document is. Where what they set meets what was set here
 * since, each needs a side: `picks` says one for each, by its key
 * (`log.preview` lists them), `fallback` one for the rest, and without
 * them the op is refused with how many (`Conflicts`).
 */
QJsonObject applyRowsOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        const Reach reach = reachOf(doc, log);
        std::vector<int64_t> rows;
        if (!rowsToApply(req, log, reach, rows, refusal))
            return refusal;
        // Asked of the preview first, which changes nothing: the apply
        // itself, before it finds a conflict with no side, takes out a
        // merge that was undone (sec 31.21) -- and what is refused moves
        // nothing
        const std::string fallback = fallbackOf(req);
        const auto picks = picksOf(req);
        if (fallback.empty()) {
            const size_t conflicts = unpicked(doc->previewPick(rows), picks);
            if (conflicts > 0)
                return conflictsReply(id, conflicts);
        }
        const App::Document::MergeResult result = doc->pickRows(rows, picks, fallback);
        if (!result.unresolved.empty())
            return conflictsReply(id, result.unresolved.size());
        objectsMoved(doc);
        QJsonObject reply = placeReply(id, doc, log);
        // 0 where they changed nothing here: what they set is what is
        reply[QLatin1String("row")] = double(result.seq);
        reply[QLatin1String("failed")] = double(result.failed.size());
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/// The requests to merge a branch into another (sec 32.25)
QJsonObject requestJson(const App::LogMergeRequest& r, const std::map<int64_t, std::string>& names,
                        const Reach& reach, bool mine)
{
    auto name = [&](int64_t branch) {
        auto it = names.find(branch);
        return it == names.end() ? QString() : QString::fromStdString(it->second);
    };
    QJsonObject item;
    item[QLatin1String("id")] = double(r.id);
    item[QLatin1String("source")] = name(r.source);
    item[QLatin1String("target")] = name(r.target);
    item[QLatin1String("by")] = QString::fromStdString(r.name);
    item[QLatin1String("byKind")] = QString::fromStdString(r.kind);
    item[QLatin1String("note")] = QString::fromStdString(r.note);
    item[QLatin1String("time")] = r.created;
    item[QLatin1String("state")] = QString::fromStdString(r.state);
    if (r.state != "open") {
        item[QLatin1String("closed")] = r.closed;
        item[QLatin1String("closedBy")] = QString::fromStdString(r.closedName);
        if (r.result > 0)
            item[QLatin1String("result")] = double(r.result);
    }
    // The asker's own to withdraw; and its to deal with, who writes the
    // branch it would be merged into
    item[QLatin1String("mine")] = mine;
    item[QLatin1String("handles")] = reach.writes(r.target);
    // Dealt with in a document that is on the branch merged into
    item[QLatin1String("here")] = r.target == reach.working;
    return item;
}

bool asked(const App::LogMergeRequest& r, const std::pair<std::string, std::string>& who)
{
    return r.kind == who.first && r.name == who.second;
}

/// Whether the asker is listed a request: its own, or of branches it reads
bool listedRequest(const App::LogMergeRequest& r, const Reach& reach,
                   const std::pair<std::string, std::string>& who)
{
    return asked(r, who) || (reach.reads(r.source) && reach.reads(r.target));
}

/// `log.requests`: those of branches the asker reads, and its own
QJsonObject requestsOp(const QJsonObject& req, const std::string& boundDoc, uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        App::TransactionStore& store = log->store();
        const Reach reach = reachOf(doc, log);
        const auto who = log->userOf(App::ActorScope::current().get());
        std::map<int64_t, std::string> names;
        for (const auto& b : store.branches())
            names[b.id] = b.name;
        QJsonArray list;
        for (const App::LogMergeRequest& r : store.mergeRequests()) {
            if (!listedRequest(r, reach, who))
                continue;
            list.append(requestJson(r, names, reach, asked(r, who)));
        }
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("requests")] = list;
        reachJson(reply, reach);
        watchFor(doc, client);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "LogError", QString::fromUtf8(e.what()));
    }
}

/** `log.request`: ask for one branch to be merged into another. For one
 * who reads both; it is whoever writes the second that deals with it.
 */
QJsonObject requestOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string source = req.value(QLatin1String("source")).toString().toStdString();
    const std::string target = req.value(QLatin1String("target")).toString().toStdString();
    const std::string note = req.value(QLatin1String("note")).toString().trimmed().toStdString();
    try {
        App::TransactionStore& store = log->store();
        const Reach reach = reachOf(doc, log);
        App::LogBranch from;
        App::LogBranch into;
        // A branch the asker does not read is none to it
        if (source.empty() || !store.findBranch(source, from) || !reach.reads(from.id))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(source));
        if (target.empty() || !store.findBranch(target, into) || !reach.reads(into.id))
            return sceneControlError(id, "UnknownBranch", QString::fromStdString(target));
        const int64_t made = doc->requestMerge(source, target, note);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("request")] = double(made);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/// The request of an op, still open; or the refusal. One the asker is not
/// listed is none to it.
bool openRequest(const QJsonObject& req, App::TransactionStore& store, const Reach& reach,
                 const std::pair<std::string, std::string>& who, App::LogMergeRequest& found,
                 QJsonObject& refusal)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    const int64_t which = req.value(QLatin1String("request")).toInteger(0);
    for (const App::LogMergeRequest& r : store.mergeRequests()) {
        if (r.id != which || !listedRequest(r, reach, who))
            continue;
        if (r.state != "open") {
            refusal = sceneControlError(
                id, "Refused",
                QStringLiteral("request %1 is %2 already").arg(which)
                    .arg(QString::fromStdString(r.state)));
            return false;
        }
        found = r;
        return true;
    }
    refusal = sceneControlError(id, "UnknownRequest", QString::number(which));
    return false;
}

/** `log.closeRequest`: end a request without merging -- `withdrawn` by the
 * one who asked, `rejected` by one who writes the branch it would be merged
 * into; the host either.
 */
QJsonObject closeRequestOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    const std::string state = req.value(QLatin1String("state")).toString().toStdString();
    if (state != "withdrawn" && state != "rejected")
        return sceneControlError(id, "BadRequest", QStringLiteral("withdrawn, or rejected"));
    try {
        App::LogMergeRequest request;
        const Reach reach = reachOf(doc, log);
        const auto who = log->userOf(App::ActorScope::current().get());
        if (!openRequest(req, log->store(), reach, who, request, refusal))
            return refusal;
        const bool mine = asked(request, who);
        if (!reach.host && !(state == "withdrawn" ? mine : reach.writes(request.target)))
            return notReached(id, state == "withdrawn" ? "that request is another's to withdraw"
                                                       : kNeedsWrite);
        if (!doc->closeMergeRequest(request.id, state))
            return sceneControlError(id, "Refused", QStringLiteral("not ended"));
        return placeReply(id, doc, log);
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/** `log.mergeRequest`: do what a request asks -- merge its branch into the
 * one the document is on, which is the one it names (sec 28). For one who
 * writes that branch. A conflict needs a side: `picks` says one for each,
 * `fallback` one for the rest, and without them the merge is refused with
 * how many (`Conflicts`) and the request stays open.
 */
QJsonObject mergeRequestOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        App::TransactionStore& store = log->store();
        App::LogMergeRequest request;
        const Reach reach = reachOf(doc, log);
        if (!openRequest(req, store, reach, log->userOf(App::ActorScope::current().get()),
                         request, refusal))
            return refusal;
        // What is merged is read by the one who merges it: its own request
        // of a branch it reads no more is not its way to that branch
        if (!reach.reads(request.source))
            return notReached(id, kNeedsRead);
        if (!reach.writes(request.target))
            return notReached(id, kNeedsWrite);
        App::LogBranch from;
        App::LogBranch into;
        if (!store.getBranch(request.source, from) || !store.getBranch(request.target, into))
            return sceneControlError(id, "UnknownBranch", QStringLiteral("a branch of it is gone"));
        // A merge is into the branch a document is on: this one's
        if (into.id != log->branch())
            return sceneControlError(
                id, "Refused",
                QStringLiteral("the document is not on %1, which the request is to merge into")
                    .arg(QString::fromStdString(into.name)));
        if (editOpen(doc))
            return sceneControlError(id, "Editing",
                                     QStringLiteral("an edit is open: leave it first"));
        // The preview first, as for `log.apply`: what is refused moves
        // nothing
        const std::string fallback = fallbackOf(req);
        const auto picks = picksOf(req);
        if (fallback.empty()) {
            const size_t conflicts = unpicked(doc->previewMerge(from.name), picks);
            if (conflicts > 0)
                return conflictsReply(id, conflicts);
        }
        const App::Document::MergeResult result = doc->mergeBranch(from.name, picks, fallback);
        if (!result.unresolved.empty())
            return conflictsReply(id, result.unresolved.size());
        doc->closeMergeRequest(request.id, "merged", result.seq);
        objectsMoved(doc);
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("row")] = double(result.seq);
        reply[QLatin1String("forwarded")] = double(result.forwarded);
        reply[QLatin1String("failed")] = double(result.failed.size());
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/// A value as one line, as the desktop's merge dialog has it: its text, cut,
/// and how many files go with it. A derived value -- a shape -- by its ref
/// alone: reading it would read its geometry
QString changeText(App::TransactionLog* log, const std::string& ref, bool derived)
{
    if (ref.empty())
        return QString();
    App::CapturedValue v;
    if (derived || !log->readValue(ref, v))
        return QStringLiteral("#") + QString::fromStdString(ref.substr(0, 10));
    QString text = QString::fromStdString(v.fragment).simplified();
    // A property saved as one element with its value -- a number, a
    // length, a word -- said as that value: `60`, not `<Float value=...`
    static const QRegularExpression one(
        QStringLiteral(R"re(^<(\w+)\s+value="([^"]*)"\s*/>$)re"));
    const QRegularExpressionMatch m = one.match(text);
    if (m.hasMatch() && v.attachments.empty()) {
        QString value = m.captured(2);
        value.replace(QLatin1String("&quot;"), QLatin1String("\""))
            .replace(QLatin1String("&lt;"), QLatin1String("<"))
            .replace(QLatin1String("&gt;"), QLatin1String(">"))
            .replace(QLatin1String("&apos;"), QLatin1String("'"))
            .replace(QLatin1String("&amp;"), QLatin1String("&"));
        bool number = false;
        const double d = value.toDouble(&number);
        return number ? QString::number(d, 'g', 12) : value;
    }
    if (text.size() > kChangeText)
        text = text.left(kChangeText) + QStringLiteral("...");
    if (!v.attachments.empty())
        text += QStringLiteral(" [+%1]").arg(v.attachments.size());
    return text;
}

/** `log.preview`: what an apply (`rows`) or the merge a request asks
 * (`request`) would do, changing nothing: every change, each conflict with
 * the key a side is picked by (`picks` of `log.apply`, `log.mergeRequest`),
 * the values at the base, ours and theirs as text. Asked as the act would
 * be: rows of a branch the asker reads, a request it is listed, in a
 * document on its target.
 */
QJsonObject previewOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        App::TransactionStore& store = log->store();
        const Reach reach = reachOf(doc, log);
        App::Document::MergePreview preview;
        QJsonObject reply;
        if (req.contains(QLatin1String("request"))) {
            App::LogMergeRequest request;
            if (!openRequest(req, store, reach, log->userOf(App::ActorScope::current().get()),
                             request, refusal))
                return refusal;
            if (!reach.reads(request.source))
                return notReached(id, kNeedsRead);
            App::LogBranch from;
            App::LogBranch into;
            if (!store.getBranch(request.source, from) || !store.getBranch(request.target, into))
                return sceneControlError(id, "UnknownBranch",
                                         QStringLiteral("a branch of it is gone"));
            if (into.id != log->branch())
                return sceneControlError(
                    id, "Refused",
                    QStringLiteral("the document is not on %1, which the request is to merge into")
                        .arg(QString::fromStdString(into.name)));
            preview = doc->previewMerge(from.name);
            reply = placeReply(id, doc, log);
            reply[QLatin1String("what")] = QStringLiteral("merge");
            reply[QLatin1String("request")] = double(request.id);
            reply[QLatin1String("source")] = QString::fromStdString(from.name);
        }
        else {
            std::vector<int64_t> rows;
            if (!rowsToApply(req, log, reach, rows, refusal))
                return refusal;
            preview = doc->previewPick(rows);
            reply = placeReply(id, doc, log);
            reply[QLatin1String("what")] = QStringLiteral("rows");
        }
        reply[QLatin1String("base")] = double(preview.base);
        reply[QLatin1String("forward")] = double(preview.forward.size());
        reply[QLatin1String("fastForward")] = preview.fastForward;
        reply[QLatin1String("conflicts")] = double(preview.conflicts);
        QJsonArray changes;
        for (const auto& c : preview.changes) {
            if (changes.size() >= kOpsMost)
                break;
            QJsonObject item;
            item[QLatin1String("key")] = QString::fromStdString(c.key);
            item[QLatin1String("kind")] = QString::fromStdString(c.kind);
            item[QLatin1String("op")] = QString::fromStdString(c.op);
            item[QLatin1String("ckind")] = QString::fromStdString(c.ckind);
            item[QLatin1String("object")] = QString::fromStdString(c.object);
            if (App::DocumentObject* obj = doc->getObject(c.object.c_str()))
                item[QLatin1String("label")] = QString::fromUtf8(obj->Label.getValue());
            item[QLatin1String("prop")] = QString::fromStdString(c.prop);
            item[QLatin1String("derived")] = c.derived;
            if (!c.prop.empty()) {
                item[QLatin1String("base")] = changeText(log, c.base, c.derived);
                item[QLatin1String("ours")] = changeText(log, c.ours, c.derived);
                item[QLatin1String("theirs")] = changeText(log, c.theirs, c.derived);
            }
            if (!c.note.empty())
                item[QLatin1String("note")] = QString::fromStdString(c.note);
            changes.append(item);
        }
        reply[QLatin1String("changes")] = changes;
        reply[QLatin1String("count")] = double(preview.changes.size());
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

/** Why the merge undone at the head is not the asker's to roll back, or
 * nothing. It takes the merge's rows out for good (sec 31.21): the
 * host's, and of one who writes the branch the document is on, a merge of
 * its own since the point.
 */
QString rollBackWhy(App::TransactionLog* log, const Reach& reach, int64_t seq)
{
    if (reach.host)
        return QString();
    if (!reach.moves())
        return QString::fromLatin1(kNeedsWrite);
    const auto read = log->store().transactions(seq, 1);
    if (read.empty() || read.front().seq != seq)
        return QStringLiteral("the merge is not in the log");
    Authors authors(log->store());
    if (!ownRow(authors.users[authors.sessions[read.front().session].user]))
        return QStringLiteral("that merge is another's: its maker or the host rolls it back");
    if (!reach.row(read.front()))
        return QString::fromLatin1(kBeforePoint);
    return QString();
}

/** `log.rollBack`: the merge undone at the head taken out of the log (sec
 * 31.21, Document::rollBackMerge): its rows and its undoing go, the other
 * branch is no longer merged. The document does not change. With `dry`,
 * only what it would take out and whether the asker may (`may`, `why`):
 * asked when a merge is chosen, as the desktop's menu asks when it opens
 * -- not with every list, since asking commits an edit left open and
 * waits for the rows being written.
 */
QJsonObject rollBackOp(const QJsonObject& req, const std::string& boundDoc, uint64_t)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document* doc = nullptr;
    QJsonObject refusal;
    App::TransactionLog* log = logOf(req, boundDoc, doc, refusal);
    if (!log)
        return refusal;
    try {
        const auto undone = doc->undoneMerge();
        if (undone.seq <= 0)
            return sceneControlError(id, "Refused",
                                     QStringLiteral("no merge undone at the head to roll back"));
        const QString why = rollBackWhy(log, reachOf(doc, log), undone.seq);
        if (req.value(QLatin1String("dry")).toBool()) {
            QJsonObject reply = placeReply(id, doc, log);
            reply[QLatin1String("merge")] = double(undone.seq);
            reply[QLatin1String("source")] = QString::fromStdString(undone.branch);
            reply[QLatin1String("rows")] = double(undone.rows);
            reply[QLatin1String("may")] = why.isEmpty();
            if (!why.isEmpty())
                reply[QLatin1String("why")] = why;
            return reply;
        }
        if (!why.isEmpty())
            return sceneControlError(id, "Forbidden", why);
        const auto done = doc->rollBackMerge();
        if (done.seq <= 0)
            return sceneControlError(id, "Refused", QStringLiteral("not rolled back"));
        QJsonObject reply = placeReply(id, doc, log);
        reply[QLatin1String("merge")] = double(done.seq);
        reply[QLatin1String("source")] = QString::fromStdString(done.branch);
        reply[QLatin1String("rows")] = double(done.rows);
        return reply;
    }
    catch (const Base::Exception& e) {
        return sceneControlError(id, "Refused", QString::fromUtf8(e.what()));
    }
}

}  // namespace

bool Gui::logRowListed(App::Document* doc, int64_t row)
{
    App::TransactionLog* log = doc ? doc->getTransactionLog() : nullptr;
    if (!log || row <= 0)
        return false;
    try {
        const auto rows = log->store().transactions(row, 1);
        return !rows.empty() && rows.front().seq == row && reachOf(doc, log).sees(rows.front());
    }
    catch (const Base::Exception&) {
        return false;
    }
}

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

App::Document* Gui::serveBranchOf(App::Document* doc, const std::string& name, int port)
{
    App::Document* branch = doc ? doc->openNewBranch(name, false) : nullptr;
    if (!branch)
        throw Base::RuntimeError("the document keeps no transaction log");
    Gui::Document* gdoc = Application::Instance->getDocument(branch);
    if (!gdoc || !SceneServeSource::serve(gdoc, port) || !serveLogBranch(branch)) {
        // The branch is made and stays in the log; its document goes
        App::GetApplication().closeDocument(branch->getName());
        throw Base::RuntimeError("the branch could not be served");
    }
    return branch;
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
    // Rows of a branch that is read, and the requests to merge (sec 32.25)
    registerSceneControlOp(QStringLiteral("log.apply"), true, applyRowsOp);
    registerSceneControlOp(QStringLiteral("log.requests"), true, requestsOp);
    registerSceneControlOp(QStringLiteral("log.request"), true, requestOp);
    registerSceneControlOp(QStringLiteral("log.closeRequest"), true, closeRequestOp);
    registerSceneControlOp(QStringLiteral("log.mergeRequest"), true, mergeRequestOp);
    // The merge's own (sec 32.26): what it would do, each conflict picked,
    // and a merge undone rolled back
    registerSceneControlOp(QStringLiteral("log.preview"), true, previewOp);
    registerSceneControlOp(QStringLiteral("log.rollBack"), true, rollBackOp);
}
