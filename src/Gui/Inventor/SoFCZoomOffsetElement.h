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

#ifndef FC_ZOOMOFFSETELEMENT_H
#define FC_ZOOMOFFSETELEMENT_H

#include "../InventorBase.h"
#include <Inventor/SbVec2f.h>
#include <Inventor/elements/SoElement.h>

/// Accumulated screen-space translation, in SoAutoZoomTranslation scale
/// units, along the current render-cache capture path. Sketcher-style zoom
/// translations (SketcherGui::SoZoomTranslation) recompute their offset
/// from the view every GL frame; a capture would bake the offset at the
/// capture-time zoom into the static caches, where it then scales WITH the
/// camera. During capture (see isCapturing()) they deposit the zoom-scaled
/// part here instead — separator push/pop scopes it for free — and the
/// SoImage capture companion folds the accumulated value into its quad as
/// a constant pixel offset (one scale unit = viewportHeight/50 pixels).
class GuiExport SoFCZoomOffsetElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoFCZoomOffsetElement);
public:
  static void initClass(void);
  static void cleanup(void);

  virtual void init(SoState * state);
  virtual void push(SoState * state);
  virtual SbBool matches(const SoElement * element) const;
  virtual SoElement * copyMatchInfo() const;

  /// Accumulate a zoom-scaled x/y offset (consecutive zoom translations
  /// chain, mirroring their model-matrix accumulation on the GL path).
  static void add(SoState * state, const SbVec2f & offset);
  static SbVec2f get(SoState * state);

  /// True while a SoFCRenderCacheManager capture traversal runs on this
  /// thread — the zoom translations divert their scaled part here only
  /// then, every other action keeps the classic behaviour.
  static bool isCapturing();
  static void setCapturing(bool);

protected:
  SbVec2f offset;
};

#endif // FC_ZOOMOFFSETELEMENT_H
// vim: noai:ts=2:sw=2
