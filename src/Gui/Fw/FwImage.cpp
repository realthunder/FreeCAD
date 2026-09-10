/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#include "PreCompiled.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QIcon>
#include <QImage>
#include <QPixmap>

#include "Fw/FwImage.h"

using namespace Gui::Fw;

ImageStore& ImageStore::instance()
{
    static ImageStore store;
    return store;
}

QString ImageStore::file(const QByteArray& png, int width, int height)
{
    const QString id = prefix()
        + QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex());
    if (_images.contains(id)) {
        touch(id);
        return id;
    }
    Entry e;
    e.png = png;
    e.width = width;
    e.height = height;
    _images.insert(id, e);
    _order.append(id);
    while (_order.size() > _capacity) {
        _images.remove(_order.takeFirst());
    }
    return id;
}

void ImageStore::touch(const QString& id)
{
    // recently used goes to the back; a linear take is fine at this size
    if (!_order.isEmpty() && _order.last() == id)
        return;
    _order.removeOne(id);
    _order.append(id);
}

QString ImageStore::add(const QImage& image)
{
    if (image.isNull())
        return QString();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    ++_encoded;
    return file(png, image.width(), image.height());
}

QString ImageStore::add(const QPixmap& pixmap)
{
    if (pixmap.isNull())
        return QString();
    return add(pixmap.toImage());
}

QString ImageStore::ofIcon(const QIcon& icon, const QSize& size)
{
    if (icon.isNull())
        return QString();
    const QSize s = size.isValid() ? size : QSize(16, 16);
    // the cache key changes whenever the icon's data does; the size
    // folded in keeps a button's 24 px apart from a cell's 16 px
    const quint64 key = static_cast<quint64>(icon.cacheKey()) * 1000003ULL
        + static_cast<quint64>(s.width() * 4096 + s.height());
    auto it = _icons.constFind(key);
    if (it != _icons.constEnd() && _images.contains(*it)) {
        touch(*it);
        return *it;
    }
    if (_icons.size() > 4096)
        _icons.clear();
    const QString id = add(icon.pixmap(s));
    if (!id.isEmpty())
        _icons.insert(key, id);
    return id;
}

QString ImageStore::ofPixmap(const QPixmap& pixmap)
{
    if (pixmap.isNull())
        return QString();
    const quint64 key = static_cast<quint64>(pixmap.cacheKey()) * 1000003ULL + 1;
    auto it = _icons.constFind(key);
    if (it != _icons.constEnd() && _images.contains(*it)) {
        touch(*it);
        return *it;
    }
    if (_icons.size() > 4096)
        _icons.clear();
    const QString id = add(pixmap);
    if (!id.isEmpty())
        _icons.insert(key, id);
    return id;
}

QByteArray ImageStore::png(const QString& id, int* width, int* height) const
{
    auto it = _images.constFind(id);
    if (it == _images.constEnd())
        return QByteArray();
    if (width)
        *width = it->width;
    if (height)
        *height = it->height;
    return it->png;
}

void ImageStore::setCapacity(int n)
{
    _capacity = std::max(n, 1);
    while (_order.size() > _capacity)
        _images.remove(_order.takeFirst());
}

void ImageStore::clear()
{
    _images.clear();
    _order.clear();
    _icons.clear();
}
