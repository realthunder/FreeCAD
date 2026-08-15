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

#ifndef FC_PBRELEMENT_H
#define FC_PBRELEMENT_H

#include "../InventorBase.h"
#include <Inventor/system/inttypes.h>
#include <Inventor/elements/SoElement.h>

class SoNode;

/** Per-face PBR factors of the shapes that follow, in traversal state
 *
 * The metallic-roughness pair has no Coin material field to ride: the
 * lazy element's per-face arrays are SbColor and SbColor drops the alpha
 * a metallic factor would occupy, and shininess is the Phong quantity
 * the same slot already carries. So the factors travel in their own
 * element, written by SoFCRenderMaterial (the node that carries the
 * uniform pair too) and read by the render-cache traversal
 * (SoFCVertexCache), which bakes them into the per-vertex material
 * stream. Coin's own GL path neither enables nor consumes this element:
 * fixed-function GL has no PBR shading to feed.
 *
 * The arrays are BORROWED from the writing node's fields, exactly like
 * the lazy element borrows a material node's colour arrays: valid for
 * the traversal that set them, guarded by the node id.
 *
 * Indexing follows the material arrays the same node's appearance
 * produced: entry i is face i, and a face past the end reads ENTRY 0 --
 * the padding rule ViewProviderPartExt::setHighlightedFaces uses when a
 * per-face appearance is shorter than the shape's face count. (A clamp
 * to the last entry, which is what the colour arrays do, would paint
 * those faces with a material they were never given.)
 */
class GuiExport SoFCPbrElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoFCPbrElement);

public:
  static void initClass(void);
  static void cleanup(void);

  virtual void init(SoState * state);
  virtual void push(SoState * state);
  virtual SbBool matches(const SoElement * element) const;
  virtual SoElement * copyMatchInfo() const;

  /// The borrowed arrays. Both are set or neither is: a reader that
  /// found only one factor per face could not tell what the other one
  /// means, so the writer states the pair or stays out of the way.
  struct Factors {
    const float * metallic;
    const float * roughness;
    int nummetallic;
    int numroughness;
    SbFCUniqueId nodeid;

    bool isPerFace() const { return nummetallic > 0 && numroughness > 0; }

    /// Entry idx with the pad-with-entry-0 rule above (call only when
    /// isPerFace()).
    float metallicAt(int idx) const
    { return this->metallic[idx < this->nummetallic ? idx : 0]; }
    float roughnessAt(int idx) const
    { return this->roughness[idx < this->numroughness ? idx : 0]; }

    void clear(void) {
      this->metallic = nullptr;
      this->roughness = nullptr;
      this->nummetallic = 0;
      this->numroughness = 0;
      this->nodeid = 0;
    }
  };

  /// Borrow the pair from node's fields; either array empty clears both.
  static void set(SoState * state, const SoNode * node,
                  const float * metallic, int nummetallic,
                  const float * roughness, int numroughness);
  static const Factors & get(SoState * state);

protected:
  Factors factors;
};

#endif //FC_PBRELEMENT_H
// vim: noai:ts=2:sw=2
