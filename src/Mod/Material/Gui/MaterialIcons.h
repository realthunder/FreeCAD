// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#pragma once

#include <memory>
#include <deque>
#include <map>

#include <QIcon>
#include <QObject>
#include <QString>

#include <App/Material.h>

class QTimer;

namespace App
{
class PropertyContainer;
}

namespace MatGui
{

class IconScene;

/** Material icons rendered the way the 3D view renders the material
 *
 * Every card in the material tree used to show the icon of the LIBRARY it
 * came from, so twenty-three appearances all looked alike and none of them
 * looked like itself. This renders each one instead -- the same sphere the
 * appearance preview shows, shaded by the same engine under the Realistic
 * (metallic/roughness) model, on a transparent background -- and hands back
 * a multi-resolution QIcon.
 *
 * Three things are worth knowing about how it behaves:
 *
 * - **It renders once, large.** The largest size in sizes() is what
 *   actually goes through the engine; the rest are downsampled from it.
 *   That supersamples the small icons (crisper than rendering 16x16
 *   natively) and guarantees every size shares one framing.
 * - **It never blocks.** GL work has to happen on the GUI thread, so a
 *   request that misses the cache is queued and drained a few per event
 *   loop turn. icon() returns a null icon meanwhile and iconReady() says
 *   when to ask again; a caller shows the library icon until then.
 * - **It may decline.** Rendering needs a GL context and a render backend.
 *   Where there is none -- a console run, a build without the engine --
 *   available() is false, every icon comes back null, and callers keep
 *   whatever they showed before. Nothing here is load bearing.
 *
 * Results are cached in memory and on disk (see cachePath()), keyed by the
 * material's UUID together with a digest of the appearance itself, so an
 * edited material re-renders and an unchanged one costs nothing on the
 * next run. The build pre-warms the standard library into the same cache
 * layout; a card the build did not reach is simply rendered here.
 */
class MaterialIcons: public QObject
{
    Q_OBJECT

public:
    static MaterialIcons& instance();

    /// The sizes a rendered icon carries, LARGEST FIRST. The first entry is
    /// the one rendered; every other is downsampled from it.
    static const QList<int>& sizes();

    /// Whether icons can be rendered at all in this process (a GUI with a
    /// usable GL context and a render backend). False makes every icon()
    /// return null rather than fail loudly.
    bool available() const;

    /** The icon for an appearance, queueing a render if it is not cached
     *
     * \a key identifies the appearance for caching -- the material's UUID
     * where it has one. A null icon means "not ready"; connect to
     * iconReady() and ask again.
     */
    QIcon icon(const QString& key, const App::Material& material);

    /// The icon for a surface finish, shown on a neutral material so the
    /// pattern is what differs between them and not the colour.
    QIcon finishIcon(const App::SurfaceFinish& finish);

    /// Drop everything cached in memory (not on disk) and re-render on
    /// demand -- what a shading change or a preference edit needs.
    void invalidate();

Q_SIGNALS:
    /// A queued render finished and icon() will now answer for \a key.
    void iconReady(const QString& key);

private:
    MaterialIcons();
    ~MaterialIcons() override;

    void drain();
    QIcon build(const QString& key, const App::Material& material,
                const App::SurfaceFinish& finish);
    QIcon fromImage(const QImage& image) const;
    static QString cachePath(const QString& digest);
    static QString digestOf(const App::Material& material,
                            const App::SurfaceFinish& finish);

    struct Request
    {
        QString key;
        App::Material material;
        App::SurfaceFinish finish;
    };

    std::map<QString, QIcon> _cache;
    std::deque<Request> _queue;
    std::unique_ptr<IconScene> _scene;
    QTimer* _timer {nullptr};
    bool _failed {false};
};

}  // namespace MatGui
