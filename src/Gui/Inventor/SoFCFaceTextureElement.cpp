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

#include "SoFCFaceTextureElement.h"

SO_ELEMENT_SOURCE(SoFCFaceTextureElement);

void
SoFCFaceTextureElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCFaceTextureElement, inherited);
  // The render-cache traversal only; Coin's GL path binds one texture
  // per draw and has nowhere to put a per-face palette.
  SO_ENABLE(SoCallbackAction, SoFCFaceTextureElement);
}

void
SoFCFaceTextureElement::cleanup()
{
}

void
SoFCFaceTextureElement::init(SoState * state)
{
  inherited::init(state);
  this->layers.clear();
}

void
SoFCFaceTextureElement::push(SoState * state)
{
  inherited::push(state);
  const SoFCFaceTextureElement * prev =
    static_cast<const SoFCFaceTextureElement *>(getNextInStack());
  this->layers = prev->layers;
}

SbBool
SoFCFaceTextureElement::matches(const SoElement * element) const
{
  const SoFCFaceTextureElement * other =
    static_cast<const SoFCFaceTextureElement *>(element);
  // The node id identifies the values; the count distinguishes a
  // per-face write from the same node's uniform one.
  return other->layers.nodeid == this->layers.nodeid
      && other->layers.numindex == this->layers.numindex;
}

SoElement *
SoFCFaceTextureElement::copyMatchInfo(void) const
{
  assert(getTypeId().canCreateInstance());
  SoFCFaceTextureElement * element =
    static_cast<SoFCFaceTextureElement *>(getTypeId().createInstance());
  element->layers = this->layers;
  return element;
}

void
SoFCFaceTextureElement::set(SoState * state, const SoNode * node,
                            const int32_t * index, int numindex)
{
  SoFCFaceTextureElement * elem = static_cast<SoFCFaceTextureElement *>(
          SoElement::getElement(state, getClassStackIndex()));
  if (!elem)
    return;
  if (numindex <= 0 || !index) {
    // A node that states no per-face layer hides whatever an outer one
    // stated, the way a plain material node hides an outer one's arrays
    elem->layers.clear();
    return;
  }
  elem->layers.index = index;
  elem->layers.numindex = numindex;
  elem->layers.nodeid = node ? node->getNodeId() : 0;
}

const SoFCFaceTextureElement::Layers &
SoFCFaceTextureElement::get(SoState * state)
{
  static const Layers empty { nullptr, 0, 0 };
  // Only the render-cache traversal enables this element, and
  // getConstElement() asserts on a stack slot the action left empty --
  // so an action that does not carry it answers "no per-face layers"
  // rather than aborting a debug build.
  const int stackindex = getClassStackIndex();
  if (!state || !state->isElementEnabled(stackindex))
    return empty;
  const SoFCFaceTextureElement * elem =
    static_cast<const SoFCFaceTextureElement *>(
          SoElement::getConstElement(state, stackindex));
  return elem ? elem->layers : empty;
}

// vim: noai:ts=2:sw=2
