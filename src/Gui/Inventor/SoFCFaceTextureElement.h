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

#ifndef FC_FACETEXTUREELEMENT_H
#define FC_FACETEXTUREELEMENT_H

#include "../InventorBase.h"
#include <Inventor/system/inttypes.h>
#include <Inventor/elements/SoElement.h>

class SoNode;

/** Per-face texture layer of the shapes that follow, in traversal state
 *
 * A face can carry an image of its own, and a draw binds one sampler --
 * so the images a shape uses become a PALETTE the draw material holds
 * (SoFCRenderMaterial's face texture nodes, uploaded by the backend as
 * the layers of one array texture), and what travels per face is a
 * single index into it. This element carries that index array from the
 * node down to the render-cache traversal (SoFCVertexCache), which bakes
 * it into the per-vertex material stream; the images themselves never
 * need to reach the shape, because the backend resolves them per draw.
 *
 * LAYER 0 IS THE UNTEXTURED FACE. It has no palette entry: it samples
 * opaque white, which leaves the face exactly as it looked before any of
 * this existed -- and it is what an out-of-range index, an unbound
 * attribute and a face the palette had no room for all read.
 *
 * This follows SoFCPbrElement and SoFCFinishElement exactly, for the same
 * reason: the quantity has no Coin material field to ride, and Coin's own
 * GL path neither enables nor consumes the element -- fixed-function GL
 * binds one texture per draw and has nowhere to put a second.
 *
 * The array is BORROWED from the writing node's field: valid for the
 * traversal that set it, guarded by the node id.
 *
 * Indexing follows the FACE (the shape's part index), not the material
 * index: an image is put on a face, and a shape may well paint two faces
 * that share one colour differently. A face past the end reads entry 0.
 */
class GuiExport SoFCFaceTextureElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoFCFaceTextureElement);

public:
  static void initClass(void);
  static void cleanup(void);

  virtual void init(SoState * state);
  virtual void push(SoState * state);
  virtual SbBool matches(const SoElement * element) const;
  virtual SoElement * copyMatchInfo() const;

  /// The borrowed layer index array.
  struct Layers {
    const int32_t * index;
    int numindex;
    SbFCUniqueId nodeid;

    bool isPerFace() const { return numindex > 0; }

    /// The layer of face idx with the pad-with-entry-0 rule above (call
    /// only when isPerFace()). Clamped into a byte, which is what the
    /// stream slot holds and what the palette cap allows.
    uint8_t layerAt(int idx) const
    {
      int32_t value = this->index[idx < this->numindex ? idx : 0];
      return value > 0 && value < 256 ? static_cast<uint8_t>(value) : 0;
    }

    void clear(void) {
      this->index = nullptr;
      this->numindex = 0;
      this->nodeid = 0;
    }
  };

  /// Borrow the layer array from node's field; an empty array clears it.
  static void set(SoState * state, const SoNode * node,
                  const int32_t * index, int numindex);
  static const Layers & get(SoState * state);

protected:
  Layers layers;
};

#endif //FC_FACETEXTUREELEMENT_H
// vim: noai:ts=2:sw=2
