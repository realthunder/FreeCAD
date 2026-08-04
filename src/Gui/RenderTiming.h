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

#ifndef GUI_RENDERTIMING_H
#define GUI_RENDERTIMING_H

#include <FCGlobal.h>

namespace Gui
{

/** Where the time of a rendered frame goes, by pipeline stage.
 *
 * Publishing a scene runs through several stages that live in different
 * classes, and which of them dominates decides what is worth optimizing.
 * The answer is not guessable: at 6000 objects a frame costs ~100ms, but
 * that could be the Coin traversal, the flattening of the vertex caches,
 * the draw-entry build, the translation to the backend or the backend's
 * own bookkeeping (docs/IncrementalPublish.md §2).
 *
 * Enabled by the RenderDebug_Timing view property, off by default. While
 * on, a summary line is logged once a second, which over a progressive
 * import gives the growth of each stage against the object count rather
 * than a single average. This is deliberately permanent instrumentation:
 * the last profile of this path had to be retracted for having been taken
 * against a debug build of Coin, and improvising the measurement again is
 * how that repeats.
 *
 * Stages are timed *exclusively* — a stage nested inside another (the
 * flatten inside the draw-entry build, say) is subtracted from its
 * parent, so the totals add up to the frame instead of double counting.
 */
class GuiExport RenderTiming
{
public:
    enum Stage {
        /// Coin traversal building the scene render cache
        Traverse,
        /// matching the rebuilt scene cache against the previous publish
        Delta,
        /// flattening child caches into the scene's vertex-cache map
        Flatten,
        /// the same, below the top level: what a nested cache spends
        /// building its own map before its parent copies it up again
        FlattenSub,
        /// building SoFCRenderer draw entries and the sorted lists
        Entries,
        /// translating the vertex-cache map into backend draw calls
        Translate,
        /// the backend taking the new scene (instance groups, bbox, plan)
        Backend,
        /// drawing the frame from the state above
        Submit,
        StageCount
    };

    static bool enabled();
    /// Set from the RenderDebug_Timing property of the rendering view.
    static void setEnabled(bool on);

    /// Scoped exclusive timer for one stage; costs nothing while disabled.
    class GuiExport Scope
    {
    public:
        explicit Scope(Stage stage);
        ~Scope();

        /// End the stage before the scope does; further calls do nothing.
        /// For a stage that ends partway through a function, where the
        /// alternative is wrapping a long block in braces for the timer's
        /// sake.
        void stop();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

    private:
        Stage stage;
        bool active;
        long long start;
        long long childNs;
        Scope* parent;
    };

    /// Call once per rendered frame; logs a summary at most once a second.
    static void frameDone();

    /// Milliseconds and call counts accumulated since the last report.
    static void totals(double ms[StageCount], int counts[StageCount]);
    static void reset();
    static const char* stageName(Stage stage);
};

}  // namespace Gui

#endif  // GUI_RENDERTIMING_H
