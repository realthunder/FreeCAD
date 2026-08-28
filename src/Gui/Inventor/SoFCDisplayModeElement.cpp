/****************************************************************************
 *   Copyright (c) 2020 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <Inventor/actions/SoActions.h>
#include "../SoFCUnifiedSelection.h"
#include "../SoFCSelectionAction.h"
#include "SoFCDisplayModeElement.h"

using namespace Gui;

SO_ELEMENT_SOURCE(SoFCDisplayModeElement)

void
SoFCDisplayModeElement::initClass(void)
{
  SO_ELEMENT_INIT_CLASS(SoFCDisplayModeElement, inherited);

  SO_ENABLE(SoGLRenderAction, SoFCDisplayModeElement);
  SO_ENABLE(SoGetBoundingBoxAction, SoFCDisplayModeElement);
  SO_ENABLE(SoAudioRenderAction, SoFCDisplayModeElement);
  SO_ENABLE(SoSearchAction, SoFCDisplayModeElement);
  SO_ENABLE(SoGetMatrixAction, SoFCDisplayModeElement);
  SO_ENABLE(SoPickAction, SoFCDisplayModeElement);
  SO_ENABLE(SoCallbackAction, SoFCDisplayModeElement);
  SO_ENABLE(SoGetPrimitiveCountAction, SoFCDisplayModeElement);
  SO_ENABLE(SoHandleEventAction, SoFCDisplayModeElement);
  SO_ENABLE(SoSelectionElementAction, SoFCDisplayModeElement);
  SO_ENABLE(SoHighlightElementAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCEnableSelectionAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCEnableHighlightAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCSelectionColorAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCHighlightColorAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCSelectionAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCHighlightAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCDocumentAction, SoFCDisplayModeElement);
  SO_ENABLE(SoFCDocumentObjectAction, SoFCDisplayModeElement);
  SO_ENABLE(SoGLSelectAction, SoFCDisplayModeElement);
  SO_ENABLE(SoVisibleFaceAction, SoFCDisplayModeElement);
  SO_ENABLE(SoUpdateVBOAction, SoFCDisplayModeElement);
  SO_ENABLE(SoVRMLAction, SoFCDisplayModeElement);
}

SoFCDisplayModeElement::~SoFCDisplayModeElement()
{
}

void
SoFCDisplayModeElement::HiddenLineConfig::reset()
{
  memset(this, 0, sizeof(HiddenLineConfig));
}

void
SoFCDisplayModeElement::set(SoState * const state,
                            SoNode * const node,
                            const SbName &mode,
                            SbBool hiddenLines,
                            const HiddenLineConfig *pConfig,
                            const CaptureInterest *interest)
{
  auto element = static_cast<SoFCDisplayModeElement*>(
      inherited::getElement(state, classStackIndex, node));

  if (element) {
    element->displayMode = mode;
    if ((element->hiddenLines = hiddenLines) && pConfig) {
      element->hiddenLineConfig = *pConfig;
    } else
      element->hiddenLineConfig.reset();
    float tmp;
    element->faceColor.setPackedValue(element->hiddenLineConfig.faceColor, tmp);
    element->lineColor.setPackedValue(element->hiddenLineConfig.lineColor, tmp);
    element->captureInterest =
        (interest && !interest->modes.empty()) ? interest : nullptr;
    element->captureInterestVersion =
        element->captureInterest ? interest->version : 0;
  }
}

const SoFCDisplayModeElement::CaptureInterest *
SoFCDisplayModeElement::getCaptureInterest(SoState * const state)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  return element->captureInterest;
}

SoFCDisplayModeElement *
SoFCDisplayModeElement::getInstance(SoState *state)
{
  return static_cast<SoFCDisplayModeElement*>(
      state->getElementNoPush(classStackIndex));
}

void
SoFCDisplayModeElement::setColors(SoState * const state,
                                  SoNode * const node,
                                  const SbColor *faceColor,
                                  const SbColor *lineColor,
                                  float transp)
{
  auto element = static_cast<SoFCDisplayModeElement*>(
      inherited::getElement(state, classStackIndex, node));

  if (element) {
    if ((element->hiddenLineConfig.hasFaceColor = (faceColor != nullptr))) {
      element->hiddenLineConfig.faceColor = faceColor->getPackedValue(1.0);
      element->faceColor = *faceColor;
    }
    if ((element->hiddenLineConfig.hasLineColor = (lineColor != nullptr))) {
      element->hiddenLineConfig.lineColor = lineColor->getPackedValue(1.0);
      element->lineColor = *lineColor;
    }
    if ((element->hiddenLineConfig.hasTransparency = transp>0.0))
      element->hiddenLineConfig.transparency = transp;
  }
}

const SbName &
SoFCDisplayModeElement::get(SoState * const state)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  return element->displayMode;
}

const SbName &
SoFCDisplayModeElement::get() const
{
  return displayMode;
}

SbBool
SoFCDisplayModeElement::showHiddenLines(SoState * const state, HiddenLineConfig *config)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  if (config)
    *config = element->hiddenLineConfig;
  return element->hiddenLines;
}

SbBool
SoFCDisplayModeElement::showHiddenLines(SoState * const state, SbBool *outline)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  if (outline) *outline = element->hiddenLineConfig.outline;
  return element->hiddenLines;
}

SbBool
SoFCDisplayModeElement::showHiddenLines() const
{
  return hiddenLines;
}

const SoFCDisplayModeElement::HiddenLineConfig &
SoFCDisplayModeElement::getHiddenLineConfig() const
{
  return hiddenLineConfig;
}

const SbColor *
SoFCDisplayModeElement::getFaceColor(SoState * const state)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  return element->getFaceColor();
}

const SbColor *
SoFCDisplayModeElement::getFaceColor() const
{
  return hiddenLineConfig.hasFaceColor ? &faceColor : nullptr;
}

const SbColor *
SoFCDisplayModeElement::getLineColor(SoState * const state)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  return element->getLineColor();
}

const SbColor *
SoFCDisplayModeElement::getLineColor() const
{
  return hiddenLineConfig.hasLineColor ? &lineColor : nullptr;
}

float
SoFCDisplayModeElement::getTransparency(SoState * const state)
{
  auto element = static_cast<const SoFCDisplayModeElement*>(
      inherited::getConstElement(state, classStackIndex));
  return element->getTransparency();
}

float
SoFCDisplayModeElement::getTransparency() const
{
  if (hiddenLineConfig.hasTransparency)
    return hiddenLineConfig.transparency;
  return -1.0f;
}

SbBool
SoFCDisplayModeElement::matches(const SoElement * element) const
{
  if (this == element)
    return TRUE;
  if (element->getTypeId() != SoFCDisplayModeElement::getClassTypeId())
    return FALSE;
  auto other = static_cast<const SoFCDisplayModeElement *>(element);
  if (this->displayMode != other->displayMode
      || this->captureInterest != other->captureInterest
      || this->captureInterestVersion != other->captureInterestVersion
      || this->hiddenLines != other->hiddenLines
      || this->faceColor != other->lineColor
      || this->lineColor != other->lineColor
      || memcmp(&this->hiddenLineConfig, &other->hiddenLineConfig, sizeof(HiddenLineConfig))!=0)
    return FALSE;
  return TRUE;
}

SoElement *
SoFCDisplayModeElement::copyMatchInfo(void) const
{
  auto element = static_cast<SoFCDisplayModeElement *>(
      SoFCDisplayModeElement::getClassTypeId().createInstance());

  element->displayMode = this->displayMode;
  element->captureInterest = this->captureInterest;
  element->captureInterestVersion = this->captureInterestVersion;
  element->hiddenLines = this->hiddenLines;
  element->hiddenLineConfig = this->hiddenLineConfig;
  element->faceColor = this->faceColor;
  element->lineColor = this->lineColor;
  element->nodeId = this->nodeId;
  return element;
}

void
SoFCDisplayModeElement::init(SoState * state)
{
  inherited::init(state);
  this->displayMode = SbName::empty();
  this->hiddenLines = FALSE;
  this->hiddenLineConfig.reset();
  this->captureInterest = nullptr;
  this->captureInterestVersion = 0;
}

// vim: noai:ts=2:sw=2
