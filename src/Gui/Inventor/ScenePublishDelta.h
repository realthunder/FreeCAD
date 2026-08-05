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

#ifndef FC_SCENEPUBLISHDELTA_H
#define FC_SCENEPUBLISHDELTA_H

#include "SoFCRenderCache.h"

namespace Gui
{

/** What one publish changed, cache by cache.
 *
 * A publish rebuilds the scene cache whenever anything below the scene
 * root moves, but the traversal doing the rebuilding prunes at every
 * separator whose cache is still valid and hands the existing cache back
 * to the parent. Nearly every child a rebuilt cache ends up holding is
 * therefore the very object its predecessor held
 * (docs/IncrementalPublish.md §3). This class is what notices: every
 * rebuilt cache is matched against the cache it replaced, so a publish
 * can state what it changed rather than only what it contains.
 *
 * It is deliberately not just the scene root. An imported assembly hangs
 * under a single container, so the scene cache has one child and reports
 * it replaced on every publish however little moved; the change set that
 * means anything is the one down the hierarchy, and the traversal's
 * pruning keeps it to the paths that actually changed (§4a).
 *
 * The flatten is the first thing to consume the answer: with
 * RenderCacheIncremental on it copies a child's slice of the flattened
 * map wherever lastMatch() says the child is one the previous publish
 * already produced (§5). Everything else a publish does — the draw
 * entries, the backend draw calls and the backend's bookkeeping — is
 * still rebuilt in full (§8).
 */
class GuiExport ScenePublishDelta
{
public:
    typedef SoFCRenderCache::CacheEntry CacheEntry;

    /// Whether two children contribute identically, and so whether the
    /// newer one may keep whatever the older one produced. This is the
    /// test the flatten needs before reusing a child's slice of the map
    /// rather than deriving it again, and the reason the flatten is told
    /// the answer (lastMatch()) instead of working it out for itself.
    static bool sameEntry(const CacheEntry& a, const CacheEntry& b);

    /// Begin recording a publish, before the traversal runs.
    void begin();

    /// Record what one rebuilt cache changed against the cache it
    /// replaced, as that cache closes. Where the change set actually
    /// lives: a scene root usually holds a single container child, so
    /// only the diffs down the hierarchy say what moved.
    void updateCache(SoFCRenderCache* cache, SoFCRenderCache* prev);

    /// Which child of the previous cache each child of the last cache
    /// passed to updateCache() is, by index into that previous cache's
    /// children, or -1 for a child the previous publish did not hold.
    ///
    /// The flatten needs the same answer about the same pair, to decide
    /// whose slice of the map it may copy (docs/IncrementalPublish.md
    /// §5). It is a hash of the previous children plus a material
    /// comparison per child, and asking it twice in one publish costs
    /// what asking it once does. Valid until the next updateCache().
    const SbFCVector<int>& lastMatch() const
    {
        return scratchmatch;
    }

    /// Match the finished scene cache against the one from the previous
    /// publish, and keep both alive. Costs one hash lookup and at most a
    /// material comparison per child.
    void update(SoFCRenderCache* scene);

    /// Forget the previous publish; the next one reports every child as
    /// added. For a scene that was torn down rather than changed.
    void clear();

    /// Children of the current scene cache that the previous publish did
    /// not hold, as indices into scene()->getChildCaches().
    const SbFCVector<int>& added() const
    {
        return addedidx;
    }
    /// Children of the previous scene cache that this one dropped, as
    /// indices into prevScene()->getChildCaches(). That cache is retained
    /// for exactly as long as this delta describes it, so the entries
    /// stay valid to read.
    const SbFCVector<int>& removed() const
    {
        return removedidx;
    }
    /// How many children came through the publish unchanged.
    int kept() const
    {
        return keptcount;
    }

    /// Totals over every cache rebuilt by this publish, the scene cache
    /// included: children added, children dropped, children reused, and
    /// how many caches were diffed to arrive at them.
    int totalAdded() const
    {
        return addedtotal;
    }
    int totalRemoved() const
    {
        return removedtotal;
    }
    int totalKept() const
    {
        return kepttotal;
    }
    int diffedCaches() const
    {
        return diffedcaches;
    }

    SoFCRenderCache* scene() const
    {
        return curscene;
    }
    SoFCRenderCache* prevScene() const
    {
        return prevscene;
    }

    /// Count one separator visited by the traversal, \a reused if it
    /// handed back an existing cache instead of building one. Reuse as
    /// the traversal sees it, against reuse as the diffs see it: the two
    /// bracket the change set, and their disagreement is what says how
    /// much of a publish is spent on parts that never moved.
    void countSeparator(bool reused);

    static bool logging();
    /// Set from the RenderDebug_Delta property of the rendering view.
    static void setLogging(bool on);
    /// One summary line, at most once a second, if logging is on.
    void report();

private:
    /// Match \a cur's children against \a prev's, appending the unmatched
    /// ones to \a added and \a removed and returning how many matched.
    /// With \a matchout, also writes which child of \a prev each child of
    /// \a cur turned out to be, -1 where none.
    int diff(SoFCRenderCache* prev,
             SoFCRenderCache* cur,
             SbFCVector<int>& added,
             SbFCVector<int>& removed,
             SbFCVector<int>* matchout = nullptr);

    CoinPtr<SoFCRenderCache> curscene;
    CoinPtr<SoFCRenderCache> prevscene;
    SbFCVector<int> addedidx;
    SbFCVector<int> removedidx;
    int keptcount {0};
    int reusedseps {0};
    int rebuiltseps {0};
    int addedtotal {0};
    int removedtotal {0};
    int kepttotal {0};
    int diffedcaches {0};
    // Scratch for the per-cache diffs, kept so a publish that rebuilds
    // many caches does not allocate once per cache.
    SbFCVector<int> scratchadded;
    SbFCVector<int> scratchremoved;
    SbFCVector<int> scratchmatch;

    // Accumulated over the reporting window.
    int logpublishes {0};
    int logadded {0};
    int logremoved {0};
    int logkept {0};
    int logdiffed {0};
    int logsceneadded {0};
    int logsceneremoved {0};
    int logreused {0};
    int logrebuilt {0};
    long long reportedAt {0};
};

}  // namespace Gui

#endif  // FC_SCENEPUBLISHDELTA_H
