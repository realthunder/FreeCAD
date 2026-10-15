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
# include <chrono>
# include <cstdlib>
# include <map>
# include <set>
# include <QDir>
# include <QFile>
# include <QFileInfo>
#endif

#include <App/Actor.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/FileHistory.h>
#include <App/TransactionLog.h>
#include <Base/Console.h>
#include <Base/Parameter.h>

#include "SceneActors.h"
#include "SceneControl.h"
#include "SceneRequests.h"

FC_LOG_LEVEL_INIT("SceneRequests", true, true)

using namespace Gui;

namespace
{

/// What no setting raises the limit past. One frame of the socket carries
/// 46 MB once the bytes are base64 (Render::SceneStreamServer reads messages
/// of 64 MB at most); a larger file comes in pieces (sec 32.36), held in
/// memory until the op that takes it, so this bounds what one upload holds.
constexpr qint64 kCeilingBytes = qint64(1024) * 1024 * 1024;
/// What a client is told to send in one piece, before base64.
constexpr qint64 kPieceBytes = qint64(4) * 1024 * 1024;
/// What one piece may carry at most, before base64: whatever the client
/// was told, a frame's worth.
constexpr qint64 kPieceMost = qint64(46) * 1024 * 1024;
/// Uploads arriving in pieces at once, all connections together: one per
/// connection, a new one replacing that connection's last.
constexpr size_t kStagedMost = 4;
/// An upload no piece has come for in this long is let go.
constexpr auto kStagedIdle = std::chrono::seconds(120);
constexpr qint64 kDefaultMB = 16;
constexpr qint64 kDefaultTotalMB = 64;
/// A number of this process's, clear of any row a log numbers.
constexpr uint64_t kRegistryBase = uint64_t(1) << 40;

std::vector<SceneRequest>& registry()
{
    static std::vector<SceneRequest> requests;
    return requests;
}

uint64_t& counter()
{
    static uint64_t last = 0;
    return last;
}

/// One upload whose pieces are arriving (sec 32.36): what the connection
/// said it would send, and what has come so far, in order.
struct Staged
{
    uint64_t client {0};
    qint64 size {0};
    QByteArray bytes;
    std::chrono::steady_clock::time_point last;
};

std::map<uint64_t, Staged>& staged()
{
    static std::map<uint64_t, Staged> uploads;
    return uploads;
}

/// Let go of the uploads no piece has come for in a while: a connection
/// that left in the middle sends no more, and says nothing.
void purgeStaged()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = staged().begin(); it != staged().end();) {
        if (now - it->second.last > kStagedIdle) {
            FC_LOG("upload " << it->first << " let go, idle");
            it = staged().erase(it);
        }
        else
            ++it;
    }
}

qint64& override()
{
    static qint64 bytes = 0;
    return bytes;
}

qint64& totalOverride()
{
    static qint64 bytes = 0;
    return bytes;
}

/// The sent files the log of `doc` holds; none when it keeps no log.
std::vector<App::Document::SentFile> heldIn(App::Document* doc)
{
    try {
        return doc->sentFiles();
    }
    catch (Base::Exception& e) {
        FC_WARN("sent files of " << doc->getName() << ": " << e.what());
    }
    return {};
}

/// Where a sent file lands: one directory under the host's temp path, of
/// its own -- a request is kept until the owner has looked at it, and is
/// not a panel's font.
QString requestDir()
{
    const QString dir = QString::fromStdString(App::Application::getTempPath())
        + QStringLiteral("BrowserRequests");
    return QDir().mkpath(dir) ? dir : QString();
}

QJsonObject okReply(const QJsonValue& id)
{
    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    return reply;
}

}  // namespace

std::vector<SceneRequest> SceneRequests::list(const std::string& document)
{
    std::vector<SceneRequest> out;
    for (const auto& r : registry()) {
        if (document.empty() || r.document == document)
            out.push_back(r);
    }
    // Those a log holds (sec 30.29). One brought in is a branch from then
    // on, and the panel lists it as that.
    // A file's history is one, however many documents of it are open: its
    // files are listed once, under the first of them.
    std::set<const App::FileHistory*> seen;
    for (App::Document* doc : App::GetApplication().getDocuments()) {
        if (!document.empty() && document != doc->getName())
            continue;
        // (a document that keeps no log holds none, and has no history to
        // be made for the asking)
        if (!doc->getTransactionLog() || !seen.insert(&doc->getFileHistory()).second)
            continue;
        for (const auto& f : heldIn(doc)) {
            if (!f.branch.empty())
                continue;
            SceneRequest r;
            r.id = static_cast<uint64_t>(f.seq);
            r.inLog = true;
            r.document = doc->getName();
            r.name = QString::fromStdString(f.name);
            r.size = f.size;
            r.sender = QString::fromStdString(f.sender);
            r.senderKind = QString::fromStdString(f.senderKind);
            r.time = f.when;
            out.push_back(std::move(r));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const SceneRequest& a, const SceneRequest& b) {
        return a.time < b.time;
    });
    return out;
}

bool SceneRequests::find(uint64_t id, SceneRequest& request, const std::string& document)
{
    for (const auto& r : list(document)) {
        if (r.id == id) {
            request = r;
            return true;
        }
    }
    return false;
}

bool SceneRequests::drop(uint64_t id, bool keepFile, const std::string& document)
{
    SceneRequest request;
    if (!find(id, request, document))
        return false;
    if (request.inLog) {
        App::Document* doc = App::GetApplication().getDocument(request.document.c_str());
        bool dropped = false;
        try {
            dropped = doc && doc->dropSentFile(static_cast<int64_t>(id));
        }
        catch (Base::Exception& e) {
            FC_ERR("drop request " << id << ": " << e.what());
        }
        if (dropped)
            signalChanged()();
        return dropped;
    }
    auto& all = registry();
    auto it = std::find_if(all.begin(), all.end(),
                           [id](const SceneRequest& r) { return r.id == id; });
    if (it == all.end())
        return false;
    if (!keepFile)
        QFile::remove(it->path);
    all.erase(it);
    signalChanged()();
    return true;
}

qint64 SceneRequests::heldBytes(const std::string& document)
{
    qint64 bytes = 0;
    for (const auto& r : registry()) {
        if (r.document == document)
            bytes += r.size;
    }
    // Brought in and not merged, its bytes are held still.
    if (App::Document* doc = App::GetApplication().getDocument(document.c_str())) {
        for (const auto& f : heldIn(doc))
            bytes += f.size;
    }
    return bytes;
}

fastsignals::signal<void()>& SceneRequests::signalChanged()
{
    static fastsignals::signal<void()> changed;
    return changed;
}

qint64 SceneRequests::ceiling()
{
    return kCeilingBytes;
}

qint64 SceneRequests::pieceSize()
{
    return kPieceBytes;
}

int SceneRequests::stagedUploads(qint64* bytes)
{
    purgeStaged();
    if (bytes) {
        *bytes = 0;
        for (const auto& [id, up] : staged())
            *bytes += up.bytes.size();
    }
    return int(staged().size());
}

namespace
{

/// A headless serve has no preferences dialog: the environment presets the
/// limit, as it does the token (docs/ShareAccess.md).
qint64 preset()
{
    static const qint64 bytes = []() -> qint64 {
        const char* env = std::getenv("FC_SERVE_UPLOAD_MB");
        const long mb = env ? std::atol(env) : 0;
        return mb > 0 ? qint64(mb) * 1024 * 1024 : 0;
    }();
    return bytes;
}

}  // namespace

bool SceneRequests::uploadLimitIsPreference()
{
    return override() <= 0 && preset() <= 0;
}

qint64 SceneRequests::uploadLimit()
{
    qint64 bytes = override();
    if (bytes <= 0)
        bytes = preset();
    if (bytes <= 0) {
        auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/SceneShare");
        const long mb = hGrp->GetInt("UploadLimitMB", kDefaultMB);
        bytes = qint64(mb > 0 ? mb : kDefaultMB) * 1024 * 1024;
    }
    return std::min(bytes, kCeilingBytes);
}

void SceneRequests::setUploadLimit(qint64 bytes)
{
    override() = bytes > 0 ? bytes : 0;
}

namespace
{

qint64 totalPreset()
{
    static const qint64 bytes = []() -> qint64 {
        const char* env = std::getenv("FC_SERVE_REQUESTS_MB");
        const long mb = env ? std::atol(env) : 0;
        return mb > 0 ? qint64(mb) * 1024 * 1024 : 0;
    }();
    return bytes;
}

}  // namespace

bool SceneRequests::totalLimitIsPreference()
{
    return totalOverride() <= 0 && totalPreset() <= 0;
}

qint64 SceneRequests::totalLimit()
{
    qint64 bytes = totalOverride();
    if (bytes <= 0)
        bytes = totalPreset();
    if (bytes <= 0) {
        auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/SceneShare");
        const long mb = hGrp->GetInt("RequestsTotalMB", kDefaultTotalMB);
        bytes = qint64(mb > 0 ? mb : kDefaultTotalMB) * 1024 * 1024;
    }
    return bytes;
}

void SceneRequests::setTotalLimit(qint64 bytes)
{
    totalOverride() = bytes > 0 ? bytes : 0;
}

QString SceneRequests::sanitizedName(const QString& given)
{
    // A client's string is never treated as a path: QFileInfo::fileName
    // drops every directory part, so `../../.ssh/config` arrives as
    // `config`, and what is left is filtered to a conservative set. A
    // leading dot goes too -- a name of dots alone would name the
    // directory itself.
    const QString base = QFileInfo(given.trimmed()).fileName();
    QString out;
    out.reserve(base.size());
    for (const QChar c : base) {
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-')
            || c == QLatin1Char('_') || c == QLatin1Char(' '))
            out.append(c);
        else
            out.append(QLatin1Char('_'));
    }
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    return out.trimmed().left(120);
}

QString SceneRequests::uniquePath(const QString& dir, const QString& base)
{
    // The same name sent twice must not overwrite the first, which
    // something may still be pointing at.
    const QDir at(dir);
    const QFileInfo info(base);
    const QString stem = info.baseName();
    const QString suffix = info.completeSuffix();
    QString name = base;
    for (int n = 1; at.exists(name) && n < 10000; ++n) {
        name = suffix.isEmpty() ? QStringLiteral("%1-%2").arg(stem).arg(n)
                                : QStringLiteral("%1-%2.%3").arg(stem).arg(n).arg(suffix);
    }
    return at.filePath(name);
}

namespace
{

/// The bytes of one base64 field, no more than `most` once decoded.
bool decodeField(const QJsonValue& id, const QString& encoded, qint64 most, QByteArray& bytes,
                 QJsonObject& error)
{
    // Checked before decoding: base64 is 4 bytes per 3, so this bounds the
    // allocation the decode would make. Its last 4 may stand for 1 or 2,
    // the rest being padding: a file of exactly `most` passes here.
    if (qint64(encoded.size()) / 4 * 3 > most + 2) {
        error = sceneControlError(id, "TooLarge", QStringLiteral("%1 bytes at most").arg(most));
        return false;
    }
    const auto decoded = QByteArray::fromBase64Encoding(
        encoded.toLatin1(),
        QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        error = sceneControlError(id, "BadRequest", QStringLiteral("data is not base64"));
        return false;
    }
    bytes = *decoded;
    if (bytes.size() > most) {
        error = sceneControlError(id, "TooLarge", QStringLiteral("%1 bytes at most").arg(most));
        return false;
    }
    return true;
}

/// The staged upload a request names, if it is this connection's.
Staged* stagedFor(const QJsonObject& req, uint64_t client, uint64_t* key = nullptr)
{
    purgeStaged();
    const uint64_t upload = uint64_t(req.value(QLatin1String("upload")).toDouble());
    auto it = staged().find(upload);
    // Another connection's upload is as unknown as one that never was: the
    // number is no handle on someone else's bytes.
    if (it == staged().end() || it->second.client != client)
        return nullptr;
    if (key)
        *key = upload;
    return &it->second;
}

}  // namespace

bool SceneRequests::decode(const QJsonObject& req, QByteArray& bytes, QJsonObject& error,
                           uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    const qint64 limit = uploadLimit();
    if (req.contains(QLatin1String("upload"))) {
        uint64_t key = 0;
        Staged* up = stagedFor(req, client, &key);
        if (!up) {
            error = sceneControlError(id, "UnknownUpload", QStringLiteral("no such upload"));
            return false;
        }
        if (up->bytes.size() != up->size) {
            error = sceneControlError(
                id, "BadRequest",
                QStringLiteral("%1 of %2 bytes arrived").arg(up->bytes.size()).arg(up->size));
            return false;
        }
        // The limit is read again: it may have been lowered while the
        // pieces came.
        if (up->size > limit) {
            staged().erase(key);
            error = sceneControlError(id, "TooLarge", QStringLiteral("%1 bytes at most").arg(limit));
            return false;
        }
        bytes = std::move(up->bytes);
        staged().erase(key);
        return true;
    }
    return decodeField(id, req.value(QLatin1String("data")).toString(), limit, bytes, error);
}

void SceneRequests::install()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    // A client sends its copy of the served document to be merged
    // (docs/TransactionLog.md sec 30.20 H4-H7, 30.23).
    //
    // Built as `widgets.upload` is (docs/Sandbox.md 7.22): the traffic goes
    // one way, the client sends a NAME and bytes, and the host alone decides
    // where they land -- a directory of its own, since a request is kept.
    // The answer is the request's number, its name and its size; no path.
    //
    // What it does NOT do is the point (H6): the file is written and
    // listed, and that is all. It is not opened, not parsed, not replayed.
    // A file from someone else names object types to make and holds values
    // to restore, a Python proxy among them; that happens when the owner
    // asks for it in the log panel, as it would for a file the owner was
    // handed any other way, and not because a connection sent bytes.
    //
    // Mutating (H5): a request becomes rows of the log, so it is sent by
    // someone who may write. A view-only connection is refused before the
    // handler runs.
    // A file larger than one frame carries comes in pieces (sec 32.36):
    // `upload.begin` says how large it is and is answered with a number and
    // the size of a piece; `upload.piece` brings the next bytes, in order;
    // and the op that takes the file -- `requests.send`, `widgets.upload`
    // -- names the upload instead of carrying `data`. Mutating, as those
    // ops are: what may not be sent whole may not be sent in pieces either.
    registerSceneControlOp(
        QStringLiteral("upload.begin"), true,
        [](const QJsonObject& req, const std::string& boundDoc, uint64_t client) {
            const QJsonValue id = req.value(QLatin1String("id"));
            const qint64 size = qint64(req.value(QLatin1String("size")).toDouble());
            if (size <= 0)
                return sceneControlError(id, "BadRequest", QStringLiteral("size required"));
            // Refused before a byte travels, which is the point of saying
            // the size first.
            const qint64 limit = uploadLimit();
            if (size > limit)
                return sceneControlError(id, "TooLarge",
                                         QStringLiteral("%1 bytes at most").arg(limit));
            if (req.value(QLatin1String("for")).toString() == QLatin1String("requests.send")) {
                App::Document* doc = sceneControlDocument(req, boundDoc);
                if (!doc)
                    return sceneControlError(id, "UnknownDocument", QStringLiteral("no document"));
                const qint64 most = totalLimit();
                if (heldBytes(doc->getName()) + size > most)
                    return sceneControlError(
                        id, "TooMany",
                        QStringLiteral("%1 bytes may wait to be merged at most").arg(most));
            }
            purgeStaged();
            // One at a time per connection: a new one replaces what the
            // last left half sent.
            for (auto it = staged().begin(); it != staged().end();) {
                if (it->second.client == client)
                    it = staged().erase(it);
                else
                    ++it;
            }
            if (staged().size() >= kStagedMost)
                return sceneControlError(id, "Busy",
                                         QStringLiteral("other uploads are arriving"));
            static uint64_t uploads = 0;
            const uint64_t upload = ++uploads;
            Staged& up = staged()[upload];
            up.client = client;
            up.size = size;
            up.last = std::chrono::steady_clock::now();
            QJsonObject reply = okReply(id);
            reply[QLatin1String("upload")] = double(upload);
            reply[QLatin1String("piece")] = double(kPieceBytes);
            return reply;
        });
    registerSceneControlOp(
        QStringLiteral("upload.piece"), true,
        [](const QJsonObject& req, const std::string&, uint64_t client) {
            const QJsonValue id = req.value(QLatin1String("id"));
            uint64_t key = 0;
            Staged* up = stagedFor(req, client, &key);
            if (!up)
                return sceneControlError(id, "UnknownUpload", QStringLiteral("no such upload"));
            // In order: a piece names where it starts, and a piece that
            // does not start where the last ended is refused, not placed.
            const qint64 at = qint64(req.value(QLatin1String("at")).toDouble(-1));
            if (at != up->bytes.size())
                return sceneControlError(
                    id, "BadRequest",
                    QStringLiteral("expected the piece at %1").arg(up->bytes.size()));
            QByteArray piece;
            QJsonObject error;
            if (!decodeField(id, req.value(QLatin1String("data")).toString(),
                             std::min(kPieceMost, up->size - at), piece, error)) {
                staged().erase(key);
                return error;
            }
            if (piece.isEmpty())
                return sceneControlError(id, "BadRequest", QStringLiteral("an empty piece"));
            up->bytes.append(piece);
            up->last = std::chrono::steady_clock::now();
            QJsonObject reply = okReply(id);
            reply[QLatin1String("received")] = double(up->bytes.size());
            return reply;
        });
    registerSceneControlOp(
        QStringLiteral("upload.cancel"), true,
        [](const QJsonObject& req, const std::string&, uint64_t client) {
            uint64_t key = 0;
            if (stagedFor(req, client, &key))
                staged().erase(key);
            return okReply(req.value(QLatin1String("id")));
        });

    registerSceneControlOp(
        QStringLiteral("requests.send"), true,
        [](const QJsonObject& req, const std::string& boundDoc, uint64_t client) {
            const QJsonValue id = req.value(QLatin1String("id"));
            App::Document* doc = sceneControlDocument(req, boundDoc);
            if (!doc)
                return sceneControlError(id, "UnknownDocument", QStringLiteral("no document"));
            const QString base = sanitizedName(req.value(QLatin1String("name")).toString());
            if (base.isEmpty())
                return sceneControlError(id, "BadRequest", QStringLiteral("name required"));
            QByteArray bytes;
            QJsonObject error;
            if (!decode(req, bytes, error, client))
                return error;
            if (bytes.isEmpty())
                return sceneControlError(id, "BadRequest", QStringLiteral("an empty file"));
            // J4: what waits is bounded, all of one document's together. A
            // waiting file is in the history and is saved with it.
            const qint64 most = totalLimit();
            if (heldBytes(doc->getName()) + bytes.size() > most)
                return sceneControlError(
                    id, "TooMany",
                    QStringLiteral("%1 bytes may wait to be merged at most").arg(most));

            // H7: who sent it, as the roster knows them. No connection is
            // the desktop's own call.
            QString sender = QStringLiteral("host");
            QString senderKind = QString::fromLatin1(App::Actor::kindName(App::Actor::Local));
            if (client) {
                const auto actor = SceneActors::of(client);
                sender = QString::fromStdString(actor->name);
                senderKind = QString::fromLatin1(App::Actor::kindName(actor->kind));
            }

            // Kept in the log (sec 30.29): a row under the sender, which
            // this handler runs as, and the bytes as one blob.
            App::TransactionLog* log = doc->getTransactionLog();
            if (log && !log->detached()) {
                int64_t seq = 0;
                try {
                    seq = doc->keepSentFile(std::string(bytes.constData(), size_t(bytes.size())),
                                            base.toStdString(), sender.toStdString(),
                                            senderKind.toStdString());
                }
                catch (Base::Exception& e) {
                    return sceneControlError(id, "UploadFailed", QString::fromUtf8(e.what()));
                }
                FC_LOG("request row " << seq << " of " << doc->getName() << ": "
                       << base.toStdString() << ", " << bytes.size() << " bytes, from "
                       << sender.toStdString());
                signalChanged()();
                QJsonObject reply = okReply(id);
                reply[QLatin1String("request")] = double(seq);
                reply[QLatin1String("name")] = base;
                reply[QLatin1String("size")] = double(bytes.size());
                return reply;
            }

            // A document with no log: kept by this process, as all were.
            const QString dir = requestDir();
            if (dir.isEmpty())
                return sceneControlError(id, "UploadFailed",
                                         QStringLiteral("no request directory"));
            const QString path = uniquePath(dir, base);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return sceneControlError(id, "UploadFailed", file.errorString());
            const bool whole = file.write(bytes) == bytes.size();
            file.close();
            if (!whole) {
                file.remove();
                return sceneControlError(id, "UploadFailed", QStringLiteral("short write"));
            }

            SceneRequest request;
            request.id = kRegistryBase + ++counter();
            request.document = doc->getName();
            request.name = base;
            request.path = path;
            request.size = bytes.size();
            request.sender = sender;
            request.senderKind = senderKind;
            request.time = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch()).count();
            registry().push_back(request);
            FC_LOG("request " << request.id << " for " << request.document << ": "
                   << base.toStdString() << ", " << bytes.size() << " bytes, from "
                   << request.sender.toStdString());
            signalChanged()();

            QJsonObject reply = okReply(id);
            reply[QLatin1String("request")] = double(request.id);
            reply[QLatin1String("name")] = base;
            reply[QLatin1String("size")] = double(bytes.size());
            return reply;
        });
}
