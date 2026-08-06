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

#include <Inventor/actions/SoCallbackAction.h>
#include "SoFCZoomOffsetElement.h"

SO_ELEMENT_SOURCE(SoFCZoomOffsetElement);

static FC_COIN_THREAD_LOCAL bool _capturing = false;

void
SoFCZoomOffsetElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCZoomOffsetElement, inherited);
  SO_ENABLE(SoCallbackAction, SoFCZoomOffsetElement);
}

void
SoFCZoomOffsetElement::cleanup()
{
}

void
SoFCZoomOffsetElement::init(SoState * state)
{
  inherited::init(state);
  this->offset = SbVec2f(0.f, 0.f);
}

void
SoFCZoomOffsetElement::push(SoState * state)
{
  inherited::init(state);
  SoFCZoomOffsetElement *elem =
      static_cast<SoFCZoomOffsetElement*>(getNextInStack());
  this->offset = elem->offset;
}

SbBool
SoFCZoomOffsetElement::matches(const SoElement * element) const
{
  const SoFCZoomOffsetElement *other =
      static_cast<const SoFCZoomOffsetElement *>(element);
  return other->offset == this->offset;
}

SoElement *
SoFCZoomOffsetElement::copyMatchInfo(void) const
{
  assert(getTypeId().canCreateInstance());
  SoFCZoomOffsetElement * element =
      static_cast<SoFCZoomOffsetElement *>(getTypeId().createInstance());
  element->offset = this->offset;
  return element;
}

void
SoFCZoomOffsetElement::add(SoState * state, const SbVec2f & offset)
{
  SoFCZoomOffsetElement * elem = static_cast<SoFCZoomOffsetElement *>(
      SoElement::getElement(state, getClassStackIndex()));
  if (elem)
    elem->offset += offset;
}

SbVec2f
SoFCZoomOffsetElement::get(SoState * state)
{
  const SoFCZoomOffsetElement * elem =
      static_cast<const SoFCZoomOffsetElement*>(
          SoElement::getConstElement(state, getClassStackIndex()));
  if (!elem)
    return SbVec2f(0.f, 0.f);
  return elem->offset;
}

bool
SoFCZoomOffsetElement::isCapturing()
{
  return _capturing;
}

void
SoFCZoomOffsetElement::setCapturing(bool enable)
{
  _capturing = enable;
}

// vim: noai:ts=2:sw=2
