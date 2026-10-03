/****************************************************************************
 *   Copyright (c) 2026 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef FC_SOFCVISIBILITYELEMENT_H
#define FC_SOFCVISIBILITYELEMENT_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <FCGlobal.h>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>

class SoAction;
class SoNode;

/** A view's own object visibility, in traversal state.
 *
 * Mirrors the view's ObjectVisibilities map (View3DInventor) and the
 * hide of an edit session, resolved into NODE keys
 * (docs/CoinRetirement.md 5.23): set by the view's SoFCUnifiedSelection
 * for the traversals that must answer per view -- GL render, bounding
 * box, pick and event handling -- and read by SoFCSwitch, which is how
 * an object hidden in one view drops out of that view's picking and
 * fit-all while every other view keeps it.
 *
 * NOT enabled for SoCallbackAction, deliberately. That is the mode-3
 * scene capture, which one traversal shares between every view (and
 * every cell of a unified canvas, and every served client): it stays
 * view-independent, and each view drops what it hides when it DRAWS.
 * Exports go through the callback action too, and are not per view.
 * Nor for SoSearchAction: selection and show-on-top resolve node
 * paths with it, and an object hidden in one view must stay
 * addressable.
 */
class GuiExport SoFCVisibilityElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoFCVisibilityElement);

public:
  /// One entry: a node key and what it says.
  struct Entry {
    /// The ids (SoFCSelectionRoot::getSelNodeId) of the selection roots
    /// on the occurrence's node path, outermost first, ending at the
    /// object's own root. A path entry's key is the roots of
    /// getDetailPath(subname, append) from the top-level object, so its
    /// first node is at the top of the scene; a bare entry's is the
    /// object's root alone. An id is never reused, so a key naming a
    /// deleted node matches nothing, ever.
    std::vector<uint32_t> key;
    /// 0 hidden, 1 shown.
    int8_t visibility = 0;
  };

  /// What the element carries, owned by the viewer and kept alive while
  /// set: the entries by the root they END at, which is also the quick
  /// reject -- no other root can be answered for.
  struct Table {
    /// Each list longest key first, entries of equal length in the
    /// order the view gave them (an edit's hide ahead of the persisted
    /// map).
    std::unordered_map<uint32_t, std::vector<Entry>> byEnd;
    uint32_t version = 0;
    /// What a cache built under this table depends on: the same for two
    /// tables with the same entries, never for two that differ, never
    /// reused (ViewVisibility interns the content). Views with the same
    /// map share the caches of the document's nodes; comparing tables by
    /// address made each view rebuild the ones the last view had built.
    uint64_t identity = 0;

    bool empty() const { return byEnd.empty(); }

    /// THE matcher, the one rule of contextMap2 (SoFCSelectionRoot::
    /// getNodeContext2): a key matches when it is a TAIL of the chain
    /// of roots \a ids[0 .. \a len - 1], and the longest key that
    /// matches decides. 1 shown, 0 hidden, -1 no entry, for the object
    /// whose root is ids[len - 1]. What a traversal asks at an object's
    /// switch, and what the host asks per draw (resolveDraw) -- so the
    /// Coin side and what the backend draws cannot disagree.
    int resolve(const uint32_t *ids, size_t len) const;

    /// A draw's flags (Render::VisibilitySet) from the ids of its key
    /// (Render::ObjectInfo::nodes): hidden when the chain up to some
    /// root on it resolves hidden -- the object's own or a container's,
    /// as a traversal stops at the first hidden switch -- and shown when
    /// some root resolves shown, which is what admits a draw captured
    /// only because some view shows its object.
    uint8_t resolveDraw(const std::vector<uint32_t> &ids) const;
  };

  static void initClass(void);

protected:
  virtual ~SoFCVisibilityElement();

public:
  virtual void init(SoState *state);
  virtual void push(SoState *state);
  virtual SbBool matches(const SoElement *element) const;
  virtual SoElement *copyMatchInfo(void) const;

  /// How a traversal answers (docs/CoinRetirement.md 5.25).
  enum Mode : uint8_t {
    /// By the table: this view's own answer. Fit-all, the scene box.
    Exact,
    /// Not by any view: nothing hidden, and shown what some view shows
    /// (SoFCSwitch::isPerViewShown), so every view gets the same answer
    /// and shares the caches it builds. The auto-clipping bounding box
    /// every view takes on every render, for which a box too large only
    /// loosens the near and far planes.
    Superset,
    /// By the table, and a cache built under Superset is good enough to
    /// cull with: a pick, which only needs its box to hold everything it
    /// can hit.
    Cull,
  };

  static void set(SoState *state, const Table *table, Mode mode = Exact);
  static const Table *get(SoState *state);

  /// This view's answer for the display-mode switch \a node the action
  /// is traversing: 1 shown, 0 hidden, -1 no entry (the switch follows
  /// its own whichChild). Only the object's OWN switch -- a direct
  /// child of the innermost SoFCSelectionRoot -- is answered for;
  /// switches inside a ViewProvider keep their own logic. The chain is
  /// the action's stack of roots, matched by Table::resolve.
  ///
  /// The element is READ, and so becomes a dependency of every cache
  /// open above the switch, only for a root some view's table has an
  /// entry ending at (countOverride). Any other root answers -1 in every
  /// view, and the caches above it -- shared by all the views of the
  /// document -- stay valid whichever view built them.
  static int check(SoAction *action, const SoNode *node);

  /// Count the selection root \a id in (\a add) or out of the roots
  /// that entries of any view's table end at. Returns whether it entered
  /// or left that set, in which case the caller must touch the root's
  /// switch: the caches above it were built without reading the element,
  /// or will now stop reading it.
  static bool countOverride(uint32_t id, bool add);

private:
  const Table *table = nullptr;
  uint64_t identity = 0;
  Mode mode = Exact;
};

#endif // FC_SOFCVISIBILITYELEMENT_H
// vim: noai:ts=2:sw=2
