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

#include <cstring>

#include <Inventor/SoPath.h>
#include <Inventor/actions/SoActions.h>
#include <Inventor/misc/SoState.h>

#include "../InventorBase.h"
#include "../Renderer/Renderer.h"
#include "../SoFCUnifiedSelection.h"
#include "SoFCVisibilityElement.h"

using namespace Gui;

SO_ELEMENT_SOURCE(SoFCVisibilityElement)

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
  this->version = 0;
}

void
SoFCVisibilityElement::push(SoState * state)
{
  inherited::push(state);
  auto prev = static_cast<const SoFCVisibilityElement *>(this->getNextInStack());
  this->table = prev->table;
  this->version = prev->version;
}

SbBool
SoFCVisibilityElement::matches(const SoElement * element) const
{
  auto other = static_cast<const SoFCVisibilityElement *>(element);
  return this->table == other->table && this->version == other->version;
}

SoElement *
SoFCVisibilityElement::copyMatchInfo(void) const
{
  auto elem = static_cast<SoFCVisibilityElement *>(this->getTypeId().createInstance());
  elem->table = this->table;
  elem->version = this->version;
  return elem;
}

void
SoFCVisibilityElement::set(SoState * state, const Table * table)
{
  auto elem = static_cast<SoFCVisibilityElement *>(
      state->getElement(classStackIndex));
  if (!elem)
    return;
  elem->table = (table && table->table) ? table : nullptr;
  elem->version = elem->table ? table->version : 0;
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

void
SoFCVisibilityElement::Table::update(const Render::VisibilityOverrideTable *t)
{
  this->table = t;
  this->leaves.clear();
  this->version = t ? t->version : 0;
  if (!t)
    return;
  for (const auto &ov : t->entries) {
    if (!ov.path.empty())
      this->leaves.insert(ov.path.back().obj);
  }
}

int
SoFCVisibilityElement::check(SoAction * action, const SoNode * node)
{
  SoState *state = action->getState();
  if (!state->isElementEnabled(classStackIndex))
    return -1;
  const Table *table = get(state);
  if (!table || !table->table)
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
  const char *doc, *obj;
  if (!root->getRenderedObject(doc, obj))
    return -1;
  if (!table->leaves.count(std::string_view(obj)))
    return -1;
  static FC_COIN_THREAD_LOCAL Chain chain;
  static FC_COIN_THREAD_LOCAL std::vector<Render::ObjectRef> refs;
  SoFCSelectionRoot::getActionObjectChain(action, chain);
  refs.resize(chain.size());
  for (size_t i = 0; i < chain.size(); ++i) {
    refs[i].doc = chain[i].first;
    refs[i].obj = chain[i].second;
  }
  return Render::resolveVisibility(*table->table, refs, refs.size());
}

// vim: noai:ts=2:sw=2
