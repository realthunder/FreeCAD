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

#include "PreCompiled.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>

#include <Inventor/SoPath.h>
#include <Inventor/actions/SoActions.h>
#include <Inventor/misc/SoState.h>

#include "../InventorBase.h"
#include "../Renderer/Renderer.h"
#include "../SoFCUnifiedSelection.h"
#include "SoFCSwitch.h"
#include "SoFCVisibilityElement.h"

using namespace Gui;

SO_ELEMENT_SOURCE(SoFCVisibilityElement)

namespace {

/// The roots some view's table has an entry ending at, counted per
/// view: selection root id -> count.
std::unordered_map<uint32_t, int> &overrides()
{
  static std::unordered_map<uint32_t, int> counts;
  return counts;
}

/// The first entry of \a list (longest key first) whose key is a tail of
/// \a ids[0 .. len - 1]; every key in it ends at ids[len - 1].
int matchTail(const std::vector<SoFCVisibilityElement::Entry> &list,
              const uint32_t *ids, size_t len)
{
  for (const auto &entry : list) {
    const size_t n = entry.key.size();
    if (n == 0 || n > len)
      continue;
    if (std::equal(entry.key.begin(), entry.key.end(), ids + (len - n)))
      return entry.visibility;
  }
  return -1;
}

} // namespace

bool
SoFCVisibilityElement::countOverride(uint32_t id, bool add)
{
  auto &counts = overrides();
  if (add)
    return ++counts[id] == 1;
  auto it = counts.find(id);
  if (it == counts.end() || --it->second > 0)
    return false;
  counts.erase(it);
  return true;
}

int
SoFCVisibilityElement::Table::resolve(const uint32_t * ids, size_t len) const
{
  if (!len)
    return -1;
  auto it = byEnd.find(ids[len - 1]);
  if (it == byEnd.end())
    return -1;
  return matchTail(it->second, ids, len);
}

uint8_t
SoFCVisibilityElement::Table::resolveDraw(const std::vector<uint32_t> & ids) const
{
  uint8_t flags = 0;
  for (size_t i = 0; i < ids.size(); ++i) {
    auto it = byEnd.find(ids[i]);
    if (it == byEnd.end())
      continue;
    const int r = matchTail(it->second, ids.data(), i + 1);
    if (r == 0)
      flags |= Render::VisibilitySet::Hidden;
    else if (r > 0)
      flags |= Render::VisibilitySet::Shown;
  }
  return flags;
}

void
SoFCVisibilityElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCVisibilityElement, inherited);

  // The per-view traversals only; see the class comment for why the
  // callback (capture, export) and search actions are left out.
  SO_ENABLE(SoGLRenderAction, SoFCVisibilityElement);
  SO_ENABLE(SoGetBoundingBoxAction, SoFCVisibilityElement);
  SO_ENABLE(SoPickAction, SoFCVisibilityElement);
  SO_ENABLE(SoHandleEventAction, SoFCVisibilityElement);
}

SoFCVisibilityElement::~SoFCVisibilityElement()
{
}

void
SoFCVisibilityElement::init(SoState * state)
{
  inherited::init(state);
  this->table = nullptr;
  this->identity = 0;
  this->mode = Exact;
}

void
SoFCVisibilityElement::push(SoState * state)
{
  inherited::push(state);
  auto prev = static_cast<const SoFCVisibilityElement *>(this->getNextInStack());
  this->table = prev->table;
  this->identity = prev->identity;
  this->mode = prev->mode;
}

SbBool
SoFCVisibilityElement::matches(const SoElement * element) const
{
  // This is the copy a cache holds, \a element the current state's.
  auto other = static_cast<const SoFCVisibilityElement *>(element);
  // A superset answer is every view's, and a box that holds everything
  // is all a cull asks of it.
  if (this->mode == Superset)
    return other->mode == Superset || other->mode == Cull;
  if (other->mode == Superset)
    return FALSE;
  // By content, not address: two views with the same entries answer
  // alike.
  return this->identity == other->identity;
}

SoElement *
SoFCVisibilityElement::copyMatchInfo(void) const
{
  auto elem = static_cast<SoFCVisibilityElement *>(this->getTypeId().createInstance());
  elem->table = this->table;
  elem->identity = this->identity;
  elem->mode = this->mode;
  return elem;
}

void
SoFCVisibilityElement::set(SoState * state, const Table * table, Mode mode)
{
  auto elem = static_cast<SoFCVisibilityElement *>(
      state->getElement(classStackIndex));
  if (!elem)
    return;
  elem->mode = mode;
  elem->table = (mode != Superset && table && !table->empty()) ? table : nullptr;
  elem->identity = elem->table ? table->identity : 0;
}

const SoFCVisibilityElement::Table *
SoFCVisibilityElement::get(SoState * state)
{
  // Through SoElement::getConstElement, which records the read in every
  // open cache (a separator's bounding box cache most of all), so a table
  // change re-validates them. SoState::getConstElement would not.
  auto elem = static_cast<const SoFCVisibilityElement *>(
      SoElement::getConstElement(state, classStackIndex));
  return elem ? elem->table : nullptr;
}

int
SoFCVisibilityElement::check(SoAction * action, const SoNode * node)
{
  SoState *state = action->getState();
  const auto &counts = overrides();
  if (counts.empty() || !state->isElementEnabled(classStackIndex))
    return -1;
  // The object's own display-mode switch only: a direct child of the
  // innermost selection root.
  SoFCSelectionRoot *root = SoFCSelectionRoot::getInnermostRoot(action);
  if (!root)
    return -1;
  const SoPath *path = action->getCurPath();
  if (!path || path->getLength() < 2 || path->getNodeFromTail(0) != node
      || path->getNodeFromTail(1) != root)
    return -1;
  // No view has an entry ending at this root: every view answers the
  // same, so the element is not read and records no dependency.
  const uint32_t id = root->getSelNodeId();
  if (!counts.count(id))
    return -1;
  // Read (and recorded) even when THIS view has no table: a cache built
  // here must not be reused by a view whose table does hide the object.
  auto elem = static_cast<const SoFCVisibilityElement *>(
      SoElement::getConstElement(state, classStackIndex));
  if (!elem)
    return -1;
  // Every view's answer: hidden nowhere, shown where some view shows it.
  // The same in every occurrence, so no cache needs spoiling for it.
  if (elem->mode == Superset)
    return node->isOfType(SoFCSwitch::getClassTypeId())
        && SoFCSwitch::isPerViewShown(static_cast<const SoFCSwitch *>(node)) ? 1 : -1;
  const Table *table = elem->table;
  if (!table)
    return -1;
  auto it = table->byEnd.find(id);
  if (it == table->byEnd.end())
    return -1;
  // A key longer than the root itself answers per occurrence, and one
  // node can be several: an object's root sits under every group and
  // link that shows it. A cache open above this switch -- that root's
  // own bounding box, which is what culls a pick -- would be reused
  // through the other occurrences with this one's answer. So none is
  // kept inside the root the longest key starts at, the same span
  // checkSecondaryCache() spoils for a tail context. The caches outside
  // it stay: every other object's, and that root's own and those ABOVE
  // it, which hold the whole chain in their subtree and so get the same
  // answer however they are reached. A one-node key -- a bare entry --
  // spoils nothing: its answer is the root's own, wherever it is.
  const size_t longest = it->second.front().key.size();
  if (longest > 1)
    SoFCSelectionRoot::invalidateKeyCaches(action, longest);
  static FC_COIN_THREAD_LOCAL std::vector<uint32_t> ids;
  SoFCSelectionRoot::getActionRootIds(action, ids);
  return table->resolve(ids.data(), ids.size());
}

// vim: noai:ts=2:sw=2
