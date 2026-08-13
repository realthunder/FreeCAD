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

#include "SoFCPbrElement.h"

SO_ELEMENT_SOURCE(SoFCPbrElement);

void
SoFCPbrElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCPbrElement, inherited);
  // The render-cache traversal only; Coin's GL path has no PBR shading
  // to feed and never asks for this element.
  SO_ENABLE(SoCallbackAction, SoFCPbrElement);
}

void
SoFCPbrElement::cleanup()
{
}

void
SoFCPbrElement::init(SoState * state)
{
  inherited::init(state);
  this->factors.clear();
}

void
SoFCPbrElement::push(SoState * state)
{
  inherited::push(state);
  const SoFCPbrElement * prev =
    static_cast<const SoFCPbrElement *>(getNextInStack());
  this->factors = prev->factors;
}

SbBool
SoFCPbrElement::matches(const SoElement * element) const
{
  const SoFCPbrElement * other =
    static_cast<const SoFCPbrElement *>(element);
  // The node id identifies the values; the counts distinguish a per-face
  // write from the same node's uniform one.
  return other->factors.nodeid == this->factors.nodeid
      && other->factors.nummetallic == this->factors.nummetallic
      && other->factors.numroughness == this->factors.numroughness;
}

SoElement *
SoFCPbrElement::copyMatchInfo(void) const
{
  assert(getTypeId().canCreateInstance());
  SoFCPbrElement * element =
    static_cast<SoFCPbrElement *>(getTypeId().createInstance());
  element->factors = this->factors;
  return element;
}

void
SoFCPbrElement::set(SoState * state, const SoNode * node,
                    const float * metallic, int nummetallic,
                    const float * roughness, int numroughness)
{
  SoFCPbrElement * elem = static_cast<SoFCPbrElement *>(
          SoElement::getElement(state, getClassStackIndex()));
  if (!elem)
    return;
  if (nummetallic <= 0 || numroughness <= 0 || !metallic || !roughness) {
    // A node that states no per-face pair hides whatever an outer one
    // stated, the way a plain material node hides an outer one's arrays
    elem->factors.clear();
    return;
  }
  elem->factors.metallic = metallic;
  elem->factors.roughness = roughness;
  elem->factors.nummetallic = nummetallic;
  elem->factors.numroughness = numroughness;
  elem->factors.nodeid = node ? node->getNodeId() : 0;
}

const SoFCPbrElement::Factors &
SoFCPbrElement::get(SoState * state)
{
  static const Factors empty { nullptr, nullptr, 0, 0, 0 };
  // Only the render-cache traversal enables this element, and
  // getConstElement() asserts on a stack slot the action left empty --
  // so an action that does not carry it answers "no per-face factors"
  // rather than aborting a debug build.
  const int stackindex = getClassStackIndex();
  if (!state || !state->isElementEnabled(stackindex))
    return empty;
  const SoFCPbrElement * elem = static_cast<const SoFCPbrElement *>(
          SoElement::getConstElement(state, stackindex));
  return elem ? elem->factors : empty;
}

// vim: noai:ts=2:sw=2
