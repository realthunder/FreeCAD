/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
#include <map>
#include <memory>
#include <sstream>
#endif

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/Range.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include <Gui/SceneControl.h>
#include <Gui/Renderer/SceneServer.h>

#include <Mod/Spreadsheet/App/Cell.h>
#include <Mod/Spreadsheet/App/Sheet.h>

#include "SheetControl.h"
#include "SheetModel.h"

using namespace SpreadsheetGui;
using App::CellAddress;
using Spreadsheet::Cell;
using Spreadsheet::Sheet;

namespace
{

/// What one served sheet needs: a SheetModel to format through, the
/// subscriptions that notice edits, and a version the client compares.
///
/// The model is REUSED rather than reimplemented.  Everything a cell
/// displays -- locale decimal separators, display units, the units
/// schema, error text, the alias and negative-number colours -- already
/// lives in SheetModel::data(), several hundred lines of it, and a
/// second implementation would drift from the desktop the first time
/// either changed.  SheetModel is a QAbstractTableModel, not a widget,
/// so there is nothing to host: it is the App-side state read through
/// the role mapping the desktop uses.
struct Feed
{
    Sheet* sheet = nullptr;
    std::unique_ptr<SheetModel> model;
    quint64 version = 1;
    bool pushQueued = false;
    fastsignals::scoped_connection cellConn;
    fastsignals::scoped_connection rangeConn;
    fastsignals::scoped_connection refreshConn;
};

/// Keyed by the BASE pointer: the only other thing that erases from
/// here is the application's deletion signal, which hands out an
/// App::DocumentObject and must not downcast one that is already being
/// destroyed.
std::map<const App::DocumentObject*, std::unique_ptr<Feed>>& feeds()
{
    static std::map<const App::DocumentObject*, std::unique_ptr<Feed>> map;
    return map;
}

QString colorToHex(const QColor& c)
{
    return QStringLiteral("#%1%2%3")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'));
}

/// Tell every connected viewer this sheet moved on.  Debounced like
/// PageServe's damage timer: a recompute can touch hundreds of cells,
/// and the client's answer to any number of them is the same single
/// re-get.  The push carries only the identity and the version -- never
/// cell data -- so a viewer looking at another sheet ignores it cheaply.
void queueChangePush(Feed& feed)
{
    ++feed.version;
    if (feed.pushQueued)
        return;
    feed.pushQueued = true;
    Sheet* sheet = feed.sheet;
    QTimer::singleShot(120, [sheet]() {
        auto it = feeds().find(sheet);
        if (it == feeds().end())
            return;
        Feed& f = *it->second;
        f.pushQueued = false;
        if (!f.sheet->isAttachedToDocument())
            return;
        QJsonObject push;
        push[QLatin1String("op")] = QLatin1String("sheet.changed");
        push[QLatin1String("doc")] =
            QString::fromUtf8(f.sheet->getDocument()->getName());
        push[QLatin1String("obj")] =
            QString::fromUtf8(f.sheet->getNameInDocument());
        push[QLatin1String("version")] = double(f.version);
        Render::SceneStreamServer::instance().broadcastControl(
            QJsonDocument(push).toJson(QJsonDocument::Compact).toStdString());
    });
}

Feed& feedFor(Sheet* sheet)
{
    auto it = feeds().find(sheet);
    if (it != feeds().end())
        return *it->second;

    auto feed = std::make_unique<Feed>();
    feed->sheet = sheet;
    feed->model = std::make_unique<SheetModel>(sheet);
    Feed& ref = *feed;
    feeds()[sheet] = std::move(feed);

    ref.cellConn = sheet->cellUpdated.connect([sheet](CellAddress) {
        auto it2 = feeds().find(sheet);
        if (it2 != feeds().end())
            queueChangePush(*it2->second);
    });
    ref.rangeConn = sheet->rangeUpdated.connect([sheet](App::Range) {
        auto it2 = feeds().find(sheet);
        if (it2 != feeds().end())
            queueChangePush(*it2->second);
    });
    ref.refreshConn = sheet->tableRefresh.connect([sheet]() {
        auto it2 = feeds().find(sheet);
        if (it2 != feeds().end())
            queueChangePush(*it2->second);
    });
    return ref;
}

/// Resolve the {doc, obj} a request names to a Sheet, with the same
/// document-defaulting rule the other control ops use: the named
/// document, else the one this connection's group serves.
Sheet* resolveSheet(const QJsonObject& req, const std::string& boundDoc,
                    QJsonObject& error)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    const QString docName = req.value(QLatin1String("doc")).toString();
    App::Document* doc = nullptr;
    if (!docName.isEmpty())
        doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    else if (!boundDoc.empty())
        doc = App::GetApplication().getDocument(boundDoc.c_str());
    else
        doc = App::GetApplication().getActiveDocument();
    if (!doc) {
        error = Gui::sceneControlError(id, "UnknownDocument", docName);
        return nullptr;
    }

    const QString objName = req.value(QLatin1String("obj")).toString();
    App::DocumentObject* obj = doc->getObject(objName.toUtf8().constData());
    if (!obj) {
        error = Gui::sceneControlError(id, "UnknownObject", objName);
        return nullptr;
    }
    auto* sheet = freecad_cast<Sheet*>(obj);
    if (!sheet) {
        error = Gui::sceneControlError(id, "NotASheet", objName);
        return nullptr;
    }
    return sheet;
}

/// One cell, as the DOM grid renders it.  Short keys because this is
/// the bulk of the payload: a used range of a few thousand cells is
/// sent whole (docs/SpreadsheetRemote.md sec 3 -- the full form, no
/// delta machinery).
QJsonObject describeCell(const Feed& feed, CellAddress address)
{
    const Cell* cell = feed.sheet->getCell(address);
    QJsonObject c;
    c[QLatin1String("a")] = QString::fromStdString(address.toString());
    c[QLatin1String("r")] = address.row();
    c[QLatin1String("c")] = address.col();

    const QModelIndex index =
        feed.model->index(address.row(), address.col());

    // What the desktop shows in the cell, and what an editor opens with.
    c[QLatin1String("t")] =
        feed.model->data(index, Qt::DisplayRole).toString();
    std::string content;
    if (cell && cell->getStringContent(content))
        c[QLatin1String("f")] = QString::fromStdString(content);

    const QVariant align = feed.model->data(index, Qt::TextAlignmentRole);
    if (align.isValid()) {
        const int flags = align.toInt();
        QString h = QLatin1String("left");
        if (flags & Qt::AlignRight)
            h = QLatin1String("right");
        else if (flags & Qt::AlignHCenter)
            h = QLatin1String("center");
        QString v = QLatin1String("middle");
        if (flags & Qt::AlignTop)
            v = QLatin1String("top");
        else if (flags & Qt::AlignBottom)
            v = QLatin1String("bottom");
        c[QLatin1String("ha")] = h;
        c[QLatin1String("va")] = v;
    }

    const QVariant fg = feed.model->data(index, Qt::ForegroundRole);
    if (fg.isValid())
        c[QLatin1String("fg")] = colorToHex(fg.value<QColor>());
    const QVariant bg = feed.model->data(index, Qt::BackgroundRole);
    if (bg.isValid())
        c[QLatin1String("bg")] = colorToHex(bg.value<QColor>());

    if (cell) {
        std::set<std::string> style;
        if (cell->getStyle(style) && !style.empty()) {
            QJsonArray arr;
            for (const auto& s : style)
                arr.push_back(QString::fromStdString(s));
            c[QLatin1String("st")] = arr;
        }
        int rows = 1, cols = 1;
        if (cell->getSpans(rows, cols) && (rows > 1 || cols > 1)) {
            c[QLatin1String("sr")] = rows;
            c[QLatin1String("sc")] = cols;
        }
        std::string alias;
        if (cell->getAlias(alias))
            c[QLatin1String("alias")] = QString::fromStdString(alias);
        if (cell->hasException())
            c[QLatin1String("err")] =
                QString::fromStdString(cell->getException());
    }
    return c;
}

QJsonObject sheetGet(const QJsonObject& req, const std::string& boundDoc)
{
    QJsonObject error;
    Sheet* sheet = resolveSheet(req, boundDoc, error);
    if (!sheet)
        return error;
    Feed& feed = feedFor(sheet);

    QJsonObject reply;
    reply[QLatin1String("id")] = req.value(QLatin1String("id"));
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("doc")] =
        QString::fromUtf8(sheet->getDocument()->getName());
    reply[QLatin1String("obj")] =
        QString::fromUtf8(sheet->getNameInDocument());
    reply[QLatin1String("label")] = QString::fromUtf8(sheet->Label.getValue());
    reply[QLatin1String("version")] = double(feed.version);

    // Only cells that exist are sent; the grid draws the rest empty.
    QJsonArray cells;
    int maxRow = 0, maxCol = 0;
    for (const auto& addressStr : sheet->getUsedCells()) {
        CellAddress address(addressStr);
        if (!address.isValid())
            continue;
        maxRow = std::max(maxRow, address.row());
        maxCol = std::max(maxCol, address.col());
        cells.push_back(describeCell(feed, address));
    }
    reply[QLatin1String("cells")] = cells;
    reply[QLatin1String("rows")] = maxRow + 1;
    reply[QLatin1String("cols")] = maxCol + 1;

    // Only the non-default sizes travel, keyed by index.
    QJsonObject widths;
    for (const auto& [col, width] : sheet->getColumnWidths())
        widths[QString::number(col)] = width;
    reply[QLatin1String("colW")] = widths;
    QJsonObject heights;
    for (const auto& [row, height] : sheet->getRowHeights())
        heights[QString::number(row)] = height;
    reply[QLatin1String("rowH")] = heights;
    return reply;
}

QJsonObject sheetSet(const QJsonObject& req, const std::string& boundDoc)
{
    QJsonObject error;
    Sheet* sheet = resolveSheet(req, boundDoc, error);
    if (!sheet)
        return error;

    const QJsonValue id = req.value(QLatin1String("id"));
    const QString cellName = req.value(QLatin1String("cell")).toString();
    CellAddress address(cellName.toUtf8().constData());
    if (!address.isValid())
        return Gui::sceneControlError(id, "BadRequest",
                                      QStringLiteral("bad cell address '%1'")
                                          .arg(cellName));
    const QString content = req.value(QLatin1String("content")).toString();

    // The desktop's edit recipe (SheetModel::setData): one transaction
    // named after the cell, then a recompute.  The document recomputed
    // is the SHEET's, not the active one -- a served document need not
    // be the one a window happens to be showing, and on a headless
    // backend there is no active document at all.
    std::ostringstream label;
    label << "Edit cell " << address.toString();
    App::AutoTransaction guard(label.str().c_str());
    try {
        sheet->setCell(address, content.toUtf8().constData());
        sheet->getDocument()->recompute();
    }
    catch (Base::Exception& e) {
        guard.close(true);
        return Gui::sceneControlError(id, "EditFailed",
                                      QString::fromUtf8(e.what()));
    }
    catch (std::exception& e) {
        guard.close(true);
        return Gui::sceneControlError(id, "EditFailed",
                                      QString::fromUtf8(e.what()));
    }

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("version")] = double(feedFor(sheet).version);
    return reply;
}

}  // namespace

void SpreadsheetGui::installSheetControlOps()
{
    Gui::registerSceneControlOp(QLatin1String("sheet.get"), false, sheetGet);
    Gui::registerSceneControlOp(QLatin1String("sheet.set"), true, sheetSet);

    // A feed holds the sheet by raw pointer (and a SheetModel subscribed
    // to its signals), so it has to go when the sheet does -- otherwise
    // the next debounced push, or the next request that finds the stale
    // map entry, reads freed memory.  One application-wide subscription
    // covers every document; it outlives them all, so it is never
    // disconnected.
    static fastsignals::scoped_connection deleted =
        App::GetApplication().signalDeletedObject.connect(
            [](const App::DocumentObject& obj) {
                feeds().erase(&obj);
            });
}
