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

#include <App/MaterialAppearance.h>

class QImage;
class QTimer;

namespace App
{
class PropertyContainer;
}

namespace MatGui
{

class IconScene;

/** What an icon is a picture OF
 *
 * An appearance is shown on a sphere and a surface finish on a
 * cylinder, because they answer different questions and because the
 * finish shader needs a frame the geometry states -- a sphere has none.
 * The reasoning is on MatGui::IconScene.
 */
enum class IconShape
{
    Sphere,
    Cylinder
};

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
 * The standard presets and the surface finishes do not go through any of
 * that: they ship as ordinary named icons (see resourceName()), found on
 * the "icons:" search path the way every other icon in FreeCAD is found,
 * which is what makes them overridable -- a file of the same name in the
 * user's icon directory, or an IconSet entry, wins over the bundled one.
 * They are also the only icons available at all where nothing can be
 * rendered.
 *
 * Anything not bundled -- a material the user wrote -- is rendered here
 * and cached in memory and on disk (see cachePath()), keyed by the
 * material's UUID together with a digest of the appearance itself, so an
 * edited material re-renders and an unchanged one costs nothing on the
 * next run.
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
     * where it has one. \a name is the material's name, under which a
     * bundled or user-supplied icon is looked for before anything is
     * rendered; a card with no name simply renders. A null icon means
     * "not ready"; connect to iconReady() and ask again.
     */
    QIcon icon(const QString& key, const App::MaterialAppearance& material,
               const QString& name = {},
               const App::MaterialRenderProperties& render = {});

    /// The icon for a surface finish, shown on a neutral material so the
    /// pattern is what differs between them and not the colour.
    QIcon finishIcon(const App::SurfaceFinish& finish);

    /// The material a finish icon is rendered on: one polished metal for
    /// every pattern, so what differs is the finish and not the colour.
    /// Its gloss is the one thing that does vary -- see finishRoughness.
    static App::MaterialAppearance finishMaterial(uint8_t pattern);

    /** How glossy the metal under \a pattern is
     *
     * Matte is not a pattern, it is a gloss level, and a gloss level is
     * fine relief. The shader turns relief it cannot draw into roughness
     * by itself; the icons draw their relief far coarser than reality so
     * that it reads at 32 pixels, which bypasses that entirely. This
     * puts it back, from what each process really leaves.
     */
    static float finishRoughness(uint8_t pattern);

    /** The finish a bundled pattern icon is rendered at
     *
     * A pattern says nothing on its own -- a knurl is its pitch and depth
     * as much as it is a diamond -- so an icon has to state a scale, and
     * the bundled one states this one. A finish the user has since given
     * a pitch of its own no longer matches the bundled digest and is
     * rendered instead.
     */
    static App::SurfaceFinish defaultFinish(uint8_t pattern);

    /// The name an appearance icon is bundled and looked up under, and
    /// the same for a surface finish pattern. Both are ordinary icon
    /// names: whatever the "icons:" search path resolves first wins.
    static QString resourceName(const QString& materialName);
    static QString finishResourceName(uint8_t pattern);

    /** The name a look is bundled under when several cards share it
     *
     * Named by the appearance digest rather than by any one card,
     * because that IS what the cards have in common: 219 bundled
     * materials resolve to 28 distinct looks, 102 of them to the one
     * steel. Bundling per card would ship the same picture a hundred
     * times over, so a card falls back to this name when nothing is
     * bundled under its own -- which is also what keeps the per-card
     * name working as the override it is meant to be.
     */
    static QString sharedResourceName(const QString& digest);

    /** The bundled swatch for a hatch pattern card, by its name
     *
     * A hatch is not a surface and cannot be rendered as one, so these
     * are drawn flat and ahead of time (scripts/pattern-icons.py) and
     * only ever looked up -- there is no render to fall back to, and a
     * null icon means the caller should keep whatever it had.
     */
    QIcon patternIcon(const QString& key, const QString& materialName);
    static QString patternResourceName(const QString& materialName);

    /// The digest of a material's appearance, as the bundled icons are
    /// named by. Public for the icon generator (MatGui.appearanceDigest),
    /// which has to group cards by look before it renders anything.
    static QString digestOf(const App::MaterialAppearance& material,
                            const App::SurfaceFinish& finish,
                            const App::MaterialRenderProperties& render = {});

    /** Render an appearance straight to a PNG file, digest and all
     *
     * What the icon generator calls (see MatGui.renderMaterialIcon); the
     * digest it writes into the file is what later lets a stale bundled
     * icon be recognised as stale. False if nothing was rendered.
     */
    bool renderToFile(const App::MaterialAppearance& material,
                      const App::SurfaceFinish& finish, const QString& path,
                      const App::MaterialRenderProperties& render = {},
                      IconShape shape = IconShape::Sphere);

    /** Where the icon for \a key came from
     *
     * A file on disk, or a path into the bundled resources. Rich text
     * takes a path and not a QIcon, so this is what lets a tooltip show
     * the same render larger than the tree can. Empty until icon() has
     * answered for that key.
     */
    QString iconPath(const QString& key) const;

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
    QIcon build(const QString& key, const App::MaterialAppearance& material,
                const App::SurfaceFinish& finish,
                const App::MaterialRenderProperties& render = {},
                IconShape shape = IconShape::Sphere);
    QImage render(const App::MaterialAppearance& material, const App::SurfaceFinish& finish,
                  const App::MaterialRenderProperties& props, IconShape shape);
    /// A bundled or user-supplied icon, or a null icon where there is
    /// none and where the one there is has gone stale.
    QIcon fromResource(const QString& key, const QString& file,
                       const QString& digest);
    /// The first \a file on the shared icon search path, resolved to a
    /// concrete path -- the user's own directory before the bundled
    /// resource, which is what makes a bundled icon overridable.
    static QString resourcePath(const QString& file);
    QIcon fromImage(const QImage& image) const;
    static QString cachePath(const QString& digest);

    struct Request
    {
        QString key;
        App::MaterialAppearance material;
        App::SurfaceFinish finish;
        App::MaterialRenderProperties render;
    };

    std::map<QString, QIcon> _cache;
    /// Where each cached icon was read from or written to, for callers
    /// that need a path rather than the icon itself.
    std::map<QString, QString> _paths;
    std::deque<Request> _queue;
    std::unique_ptr<IconScene> _scene;
    QTimer* _timer {nullptr};
    bool _failed {false};
    /// Set once the application has begun to quit. The scene is a
    /// QWidget owning a GL context, and Qt requires every widget to be
    /// gone before QApplication is -- so it is dropped at aboutToQuit
    /// and never built again after that.
    bool _quitting {false};
};

}  // namespace MatGui
