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

#ifndef _PreComp_
# include <Inventor/actions/SoActions.h>
#endif

#include "../SoFCUnifiedSelection.h"
#include "../Renderer/Renderer.h"
#include "SoFCOwnDisplayModeElement.h"

using namespace Gui;

SO_ELEMENT_SOURCE(SoFCOwnDisplayModeElement)

// The renderer names the same sentinel; a draw carries the value across
// unchanged, so the two must not drift apart.
static_assert(SoFCOwnDisplayModeElement::Unknown == Render::StyleUnknown,
              "own-display-mode sentinel must match Render::StyleUnknown");

void
SoFCOwnDisplayModeElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCOwnDisplayModeElement, inherited);

  // Enabled for the actions that build or consume the render cache. The
  // cache is filled during GL render traversal and by the callback
  // action; the rest are the traversals that reach a shape at all, and
  // an element the state does not carry reads as "no switch above me",
  // which is the right answer for a shape outside any display-mode
  // switch (the navigation gizmos, most of all).
  SO_ENABLE(SoGLRenderAction, SoFCOwnDisplayModeElement);
  SO_ENABLE(SoCallbackAction, SoFCOwnDisplayModeElement);
  SO_ENABLE(SoGetBoundingBoxAction, SoFCOwnDisplayModeElement);
  SO_ENABLE(SoPickAction, SoFCOwnDisplayModeElement);
  SO_ENABLE(SoGetPrimitiveCountAction, SoFCOwnDisplayModeElement);
}

SoFCOwnDisplayModeElement::~SoFCOwnDisplayModeElement()
{
}

void
SoFCOwnDisplayModeElement::init(SoState * state)
{
  inherited::init(state);
  // No display-mode switch above: nothing to filter against, so the own
  // mask is Unknown (draw as captured) and no style name is registered.
  this->data = pack(Unknown, 0);
}

void
SoFCOwnDisplayModeElement::set(SoState * const state,
                               uint8_t ownmask,
                               uint8_t registeredmask)
{
  inherited::set(classStackIndex, state, pack(ownmask, registeredmask));
}

void
SoFCOwnDisplayModeElement::get(SoState * const state,
                               uint8_t & ownmask,
                               uint8_t & registeredmask)
{
  const int32_t v = inherited::get(classStackIndex, state);
  ownmask = static_cast<uint8_t>(v & 0xff);
  registeredmask = static_cast<uint8_t>((v >> 8) & 0xff);
}

SO_ELEMENT_SOURCE(SoFCModeInterestElement)

void
SoFCModeInterestElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCModeInterestElement, inherited);

  // Same action set as SoFCOwnDisplayModeElement: the capture builders
  // plus the traversals that reach a shape at all; an element the
  // state does not carry reads as "no interest here", the right answer
  // outside any display-mode switch.
  SO_ENABLE(SoGLRenderAction, SoFCModeInterestElement);
  SO_ENABLE(SoCallbackAction, SoFCModeInterestElement);
  SO_ENABLE(SoGetBoundingBoxAction, SoFCModeInterestElement);
  SO_ENABLE(SoPickAction, SoFCModeInterestElement);
  SO_ENABLE(SoGetPrimitiveCountAction, SoFCModeInterestElement);
}

SoFCModeInterestElement::~SoFCModeInterestElement()
{
}

void
SoFCModeInterestElement::init(SoState * state)
{
  inherited::init(state);
  this->data = pack(0, 0);
}

void
SoFCModeInterestElement::set(SoState * const state,
                             uint16_t traversedmode,
                             uint16_t interestbits)
{
  inherited::set(classStackIndex, state, pack(traversedmode, interestbits));
}

void
SoFCModeInterestElement::get(SoState * const state,
                             uint16_t & traversedmode,
                             uint16_t & interestbits)
{
  const uint32_t v = static_cast<uint32_t>(
      inherited::get(classStackIndex, state));
  traversedmode = static_cast<uint16_t>(v >> 16);
  interestbits = static_cast<uint16_t>(v & 0xffff);
}

SO_ELEMENT_SOURCE(SoFCCapturedModeElement)

void
SoFCCapturedModeElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCCapturedModeElement, inherited);

  // Capture builders ONLY (see the class comment): SoFCSwitch gates
  // the additive traversal on this element being enabled.
  SO_ENABLE(SoGLRenderAction, SoFCCapturedModeElement);
  SO_ENABLE(SoCallbackAction, SoFCCapturedModeElement);
}

SoFCCapturedModeElement::~SoFCCapturedModeElement()
{
}

void
SoFCCapturedModeElement::init(SoState * state)
{
  inherited::init(state);
  this->data = 0;
}

void
SoFCCapturedModeElement::set(SoState * const state, uint16_t modeid)
{
  inherited::set(classStackIndex, state, static_cast<int32_t>(modeid));
}

uint16_t
SoFCCapturedModeElement::get(SoState * const state)
{
  return static_cast<uint16_t>(inherited::get(classStackIndex, state));
}

uint8_t
Gui::drawStyleMaskFromModeName(const SbName &mode)
{
  if (mode == SbName::empty())
    return Render::StyleAsIs;
  if (mode == SoFCUnifiedSelection::DisplayModeShaded)
    return Render::StyleShaded;
  if (mode == SoFCUnifiedSelection::DisplayModeFlatLines)
    return Render::StyleFlatLines;
  if (mode == SoFCUnifiedSelection::DisplayModeWireframe)
    return Render::StyleWireframe;
  if (mode == SoFCUnifiedSelection::DisplayModePoints)
    return Render::StylePoints;
  return SoFCOwnDisplayModeElement::Unknown;
}

uint8_t
Gui::drawStyleMaskFromModeName(const char *mode)
{
  if (!mode || !mode[0])
    return Render::StyleAsIs;
  return drawStyleMaskFromModeName(SbName(mode));
}

uint8_t
Gui::styleNameBitOf(const SbName &mode)
{
  if (mode == SoFCUnifiedSelection::DisplayModeShaded)
    return SoFCOwnDisplayModeElement::HasShaded;
  if (mode == SoFCUnifiedSelection::DisplayModeFlatLines)
    return SoFCOwnDisplayModeElement::HasFlatLines;
  if (mode == SoFCUnifiedSelection::DisplayModeWireframe)
    return SoFCOwnDisplayModeElement::HasWireframe;
  if (mode == SoFCUnifiedSelection::DisplayModePoints)
    return SoFCOwnDisplayModeElement::HasPoints;
  return 0;
}

uint8_t
Gui::styleNameBitOf(const char *mode)
{
  if (!mode || !mode[0])
    return 0;
  return styleNameBitOf(SbName(mode));
}

// vim: noai:ts=2:sw=2
