/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/nodes/SoNode.h>

#include "SoFCFinishElement.h"

SO_ELEMENT_SOURCE(SoFCFinishElement);

void
SoFCFinishElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCFinishElement, inherited);
  // The render-cache traversal only; Coin's GL path has no procedural
  // finish to shade and never asks for this element.
  SO_ENABLE(SoCallbackAction, SoFCFinishElement);
}

void
SoFCFinishElement::cleanup()
{
}

void
SoFCFinishElement::init(SoState * state)
{
  inherited::init(state);
  this->indices.clear();
}

void
SoFCFinishElement::push(SoState * state)
{
  inherited::push(state);
  const SoFCFinishElement * prev =
    static_cast<const SoFCFinishElement *>(getNextInStack());
  this->indices = prev->indices;
}

SbBool
SoFCFinishElement::matches(const SoElement * element) const
{
  const SoFCFinishElement * other =
    static_cast<const SoFCFinishElement *>(element);
  // The node id identifies the values; the counts distinguish a per-face
  // write from the same node's uniform one, and a framed write from an
  // unframed one -- the frames come and go with the finish that lays
  // them out, and the cached stream has to be rebuilt when they do.
  return other->indices.nodeid == this->indices.nodeid
      && other->indices.numindex == this->indices.numindex
      && other->indices.numframeindex == this->indices.numframeindex;
}

SoElement *
SoFCFinishElement::copyMatchInfo(void) const
{
  assert(getTypeId().canCreateInstance());
  SoFCFinishElement * element =
    static_cast<SoFCFinishElement *>(getTypeId().createInstance());
  element->indices = this->indices;
  return element;
}

void
SoFCFinishElement::set(SoState * state, const SoNode * node,
                       const int32_t * index, int numindex,
                       const int32_t * frameindex, int numframeindex)
{
  SoFCFinishElement * elem = static_cast<SoFCFinishElement *>(
          SoElement::getElement(state, getClassStackIndex()));
  if (!elem)
    return;
  if ((numindex <= 0 || !index) && (numframeindex <= 0 || !frameindex)) {
    // A node that states no per-face finish hides whatever an outer one
    // stated, the way a plain material node hides an outer one's arrays
    elem->indices.clear();
    return;
  }
  elem->indices.index = numindex > 0 ? index : nullptr;
  elem->indices.numindex = numindex > 0 && index ? numindex : 0;
  elem->indices.frameindex = numframeindex > 0 ? frameindex : nullptr;
  elem->indices.numframeindex =
    numframeindex > 0 && frameindex ? numframeindex : 0;
  elem->indices.nodeid = node ? node->getNodeId() : 0;
}

const SoFCFinishElement::Indices &
SoFCFinishElement::get(SoState * state)
{
  static const Indices empty { nullptr, 0, nullptr, 0, 0 };
  // Only the render-cache traversal enables this element, and
  // getConstElement() asserts on a stack slot the action left empty --
  // so an action that does not carry it answers "no per-face finish"
  // rather than aborting a debug build.
  const int stackindex = getClassStackIndex();
  if (!state || !state->isElementEnabled(stackindex))
    return empty;
  const SoFCFinishElement * elem = static_cast<const SoFCFinishElement *>(
          SoElement::getConstElement(state, stackindex));
  return elem ? elem->indices : empty;
}

// vim: noai:ts=2:sw=2
