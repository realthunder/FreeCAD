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

#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <FCGlobal.h>
#include <Inventor/elements/SoElement.h>

class SoAction;
class SoNode;

namespace Render {
struct VisibilityOverrideTable;
}

/** A view's own object visibility, in traversal state.
 *
 * Mirrors the view's ObjectVisibilities map (View3DInventor): set by
 * the view's SoFCUnifiedSelection for the traversals that must answer
 * per view -- GL render, bounding box, pick and event handling -- and
 * read by SoFCSwitch, which is how an object hidden in one view drops
 * out of that view's picking and fit-all while every other view keeps
 * it.
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
  /// What the element carries: the view's parsed table plus a quick
  /// reject set, owned by the viewer and kept alive while set.
  struct Table {
    const Render::VisibilityOverrideTable *table = nullptr;
    /// Internal names of the objects the entries END at -- the only
    /// objects a lookup can answer for -- viewing the table's strings.
    std::unordered_set<std::string_view> leaves;
    uint32_t version = 0;

    /// Rebuild \c leaves from \c table and take its version.
    void update(const Render::VisibilityOverrideTable *t);
  };

  static void initClass(void);

protected:
  virtual ~SoFCVisibilityElement();

public:
  virtual void init(SoState *state);
  virtual void push(SoState *state);
  virtual SbBool matches(const SoElement *element) const;
  virtual SoElement *copyMatchInfo(void) const;

  static void set(SoState *state, const Table *table);
  static const Table *get(SoState *state);

  /// This view's answer for the display-mode switch \a node the action
  /// is traversing: 1 shown, 0 hidden, -1 no entry (the switch follows
  /// its own whichChild). Only the object's OWN switch -- a direct
  /// child of the innermost SoFCSelectionRoot -- is answered for;
  /// switches inside a ViewProvider keep their own logic.
  ///
  /// The element is READ, and so becomes a dependency of every cache
  /// open above the switch, only for an object some view's table has an
  /// entry ending at (countOverride). Any other object answers -1 in
  /// every view, and the caches above it -- shared by all the views of
  /// the document -- stay valid whichever view built them.
  static int check(SoAction *action, const SoNode *node);

  /// Count the object \a doc#\a obj in (\a add) or out of the objects
  /// that entries of any view's table end at. Returns whether it entered
  /// or left that set, in which case the caller must touch the object's
  /// switch: the caches above it were built without reading the element,
  /// or will now stop reading it.
  static bool countOverride(const char *doc, const char *obj, bool add);

  /// An object chain, outermost first, as {document, object} internal
  /// names.
  typedef std::vector<std::pair<const char *, const char *>> Chain;

private:
  const Table *table = nullptr;
  uint32_t version = 0;
};

#endif // FC_SOFCVISIBILITYELEMENT_H
// vim: noai:ts=2:sw=2
