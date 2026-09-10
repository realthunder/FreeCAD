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

#ifndef GUI_FW_IMAGE_H
#define GUI_FW_IMAGE_H

/* Images a mirrored widget carries by id (docs/Sandbox.md 7.19 M2): a
 * custom-painted leaf's picture, a button's icon, an item cell's
 * decoration -- none of which has a name a client could fetch through
 * `widgets.icon`.  The bytes are registered here once, keyed by their
 * content (`img:<sha1 of the PNG>`), the id travels in the bag or the
 * cell, and a client fetches the PNG through `widgets.image {name}`
 * when it first sees the id.  A picture re-grabbed after a repaint gets
 * the same id when nothing changed, so nothing is re-sent.
 *
 * Bounded: the newest `capacity` images stay (a client that asks for
 * an evicted one is answered UnknownImage and re-fetches on the next
 * id it sees); an icon is registered once per QIcon cache key and
 * size.
 */

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSize>
#include <QString>

#include <FCGlobal.h>

class QIcon;
class QImage;
class QPixmap;

namespace Gui
{
namespace Fw
{

class GuiExport ImageStore
{
public:
    static ImageStore& instance();

    /// The id (`img:<sha1>`) an image is filed under; empty for a null
    /// image.
    QString add(const QImage& image);
    QString add(const QPixmap& pixmap);
    /// An icon's normal pixmap at `size`, cached by the icon's cache key.
    QString ofIcon(const QIcon& icon, const QSize& size);
    /// A pixmap's id, cached by its cache key (a label's pixmap re-read
    /// on every repaint is not re-encoded).
    QString ofPixmap(const QPixmap& pixmap);
    /// The PNG of an id, empty when unknown (or evicted); `width` and
    /// `height` the image's, when asked.
    QByteArray png(const QString& id, int* width = nullptr, int* height = nullptr) const;
    bool contains(const QString& id) const
    {
        return _images.contains(id);
    }
    int count() const
    {
        return _images.size();
    }
    void setCapacity(int n);
    int capacity() const
    {
        return _capacity;
    }
    /// How many images were encoded (tests).
    int encoded() const
    {
        return _encoded;
    }
    void clear();

    static QString prefix()
    {
        return QStringLiteral("img:");
    }
    static bool isImageId(const QString& s)
    {
        return s.startsWith(prefix());
    }

private:
    ImageStore() = default;
    struct Entry
    {
        QByteArray png;
        int width = 0;
        int height = 0;
    };
    QString file(const QByteArray& png, int width, int height);
    void touch(const QString& id);

    QHash<QString, Entry> _images;
    QList<QString> _order;  ///< oldest first
    QHash<quint64, QString> _icons;
    int _capacity = 512;
    int _encoded = 0;
};

}  // namespace Fw
}  // namespace Gui

#endif  // GUI_FW_IMAGE_H
