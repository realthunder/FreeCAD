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
# include <QDir>
# include <QFile>
# include <QFileInfo>
#endif

#include <App/Actor.h>
#include <App/Application.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Parameter.h>

#include "SceneActors.h"
#include "SceneControl.h"
#include "SceneRequests.h"

FC_LOG_LEVEL_INIT("SceneRequests", true, true)

using namespace Gui;

namespace
{

/// What one frame of the socket carries (Render::SceneStreamServer reads
/// messages of 64 MB at most), less the base64 and the envelope round it.
constexpr qint64 kCeilingBytes = qint64(46) * 1024 * 1024;
constexpr qint64 kDefaultMB = 16;

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

qint64& override()
{
    static qint64 bytes = 0;
    return bytes;
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
    return out;
}

bool SceneRequests::find(uint64_t id, SceneRequest& request)
{
    for (const auto& r : registry()) {
        if (r.id == id) {
            request = r;
            return true;
        }
    }
    return false;
}

bool SceneRequests::drop(uint64_t id, bool keepFile)
{
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

fastsignals::signal<void()>& SceneRequests::signalChanged()
{
    static fastsignals::signal<void()> changed;
    return changed;
}

qint64 SceneRequests::ceiling()
{
    return kCeilingBytes;
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

bool SceneRequests::decode(const QJsonObject& req, QByteArray& bytes, QJsonObject& error)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    const qint64 limit = uploadLimit();
    const QString encoded = req.value(QLatin1String("data")).toString();
    // Checked before decoding: base64 is 4 bytes per 3, so this bounds the
    // allocation the decode would make.
    if (qint64(encoded.size()) / 4 * 3 > limit) {
        error = sceneControlError(id, "TooLarge", QStringLiteral("%1 bytes at most").arg(limit));
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
    if (bytes.size() > limit) {
        error = sceneControlError(id, "TooLarge", QStringLiteral("%1 bytes at most").arg(limit));
        return false;
    }
    return true;
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
            if (!decode(req, bytes, error))
                return error;
            if (bytes.isEmpty())
                return sceneControlError(id, "BadRequest", QStringLiteral("an empty file"));
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
            request.id = ++counter();
            request.document = doc->getName();
            request.name = base;
            request.path = path;
            request.size = bytes.size();
            // H7: who sent it, as the roster knows them. No connection is
            // the desktop's own call.
            if (client) {
                const auto actor = SceneActors::of(client);
                request.sender = QString::fromStdString(actor->name);
                request.senderKind = QString::fromLatin1(App::Actor::kindName(actor->kind));
            }
            else {
                request.sender = QStringLiteral("host");
                request.senderKind = QString::fromLatin1(App::Actor::kindName(App::Actor::Local));
            }
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
