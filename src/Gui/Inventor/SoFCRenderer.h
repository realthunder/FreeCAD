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

#ifndef GUI_SOFCRENDERER_H
#define GUI_SOFCRENDERER_H

#include "../InventorBase.h"
#include "SoFCRenderCache.h"

class SoGLRenderAction;
class SoGroup;
class SoFCRenderCache;
class SoPath;
class SoFCRendererP;

namespace Render {
class Renderer;
}

class GuiExport SoFCRenderer {
public:
  SoFCRenderer();
  virtual ~SoFCRenderer();

  void clear();

  /// Attach an optional external render backend. When set, the scene,
  /// selection and highlight feeds are mirrored to it in translated
  /// (backend-neutral) form, and render() skips the internal fixed-function
  /// GL pass while the backend reports canSkipInternal(). Pass null to
  /// detach. Set env FC_RENDERER_PARALLEL_GL=1 to keep the GL pass drawing
  /// on top for comparison.
  void setExternalRenderer(Render::Renderer * renderer);

  void render(SoGLRenderAction * action);

  void setScene(const Gui::CoinPtr<SoFCRenderCache> & cache);

  typedef SoFCRenderCache::VertexCacheMap VertexCacheMap;

  void setHighlight(VertexCacheMap && caches, bool wholeontop);
  void clearHighlight();

  enum SelIdBits {
    SelIdImplicit   = 0x01000000,
    SelIdAlt        = 0x02000000,
    SelIdFull       = 0x04000000,
    SelIdPartial    = 0x08000000,
    SelIdMask       = 0x0fffffff,
    SelIdSelected = (SelIdPartial|SelIdFull),
  };
  void addSelection(int id, const VertexCacheMap & caches);
  void removeSelection(int id);

  void getBoundingBox(SbBox3f & bbox) const;

  void setHatchImage(const void *dataptr, int nc, int width, int height);

  const char * getStatistics() const;

private:
  friend class SoFCRenderCacheP;
  SoFCRendererP * pimpl;
};

#endif //GUI_SOFCRENDERER_H
// vim: noai:ts=2:sw=2
