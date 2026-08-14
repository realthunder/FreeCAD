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

#ifndef FC_FINISHELEMENT_H
#define FC_FINISHELEMENT_H

#include "../InventorBase.h"
#include <Inventor/system/inttypes.h>
#include <Inventor/elements/SoElement.h>

class SoNode;

/** Per-face surface finish of the shapes that follow, in traversal state
 *
 * A finish is four numbers (pattern, pitch, depth, lay angle), and a
 * per-face one is therefore four arrays -- which is three more than any
 * per-vertex stream should carry. So the finishes an appearance holds are
 * reduced to a PALETTE of the distinct ones (SoFCRenderMaterial's
 * finishPalette, captured into the draw material), and what travels per
 * face is a single index into it. This element carries that index array
 * from the node down to the render-cache traversal (SoFCVertexCache),
 * which bakes it into the per-vertex material stream; the palette itself
 * never needs to reach the shape, because the backend resolves it per
 * draw.
 *
 * This follows SoFCPbrElement exactly, for the same reason: the quantity
 * has no Coin material field to ride, and Coin's own GL path neither
 * enables nor consumes the element -- fixed-function GL has no procedural
 * finish to shade.
 *
 * The array is BORROWED from the writing node's field: valid for the
 * traversal that set it, guarded by the node id.
 *
 * Indexing follows the material arrays the same node's appearance
 * produced: entry i is face i, and a face past the end reads ENTRY 0 --
 * the padding rule SoFCPbrElement documents.
 */
class GuiExport SoFCFinishElement : public SoElement {
  typedef SoElement inherited;

  SO_ELEMENT_HEADER(SoFCFinishElement);

public:
  static void initClass(void);
  static void cleanup(void);

  virtual void init(SoState * state);
  virtual void push(SoState * state);
  virtual SbBool matches(const SoElement * element) const;
  virtual SoElement * copyMatchInfo() const;

  /// The borrowed index arrays: one into the finish palette, one into
  /// the palette of PROJECTION FRAMES the finish is laid out in. The
  /// second is geometry rather than appearance -- the plane's own axes,
  /// or the axis a cylinder was turned about -- and the writing node
  /// states it only while a finish exists to lay out, so a shape nobody
  /// finished never pays for the stream. Either array may be absent.
  struct Indices {
    const int32_t * index;
    int numindex;
    const int32_t * frameindex;
    int numframeindex;
    SbFCUniqueId nodeid;

    bool isPerFace() const { return numindex > 0; }
    bool hasFrames() const { return numframeindex > 0; }

    /// Entry idx with the pad-with-entry-0 rule above (call only when
    /// isPerFace()). Clamped into a byte, which is what the stream slot
    /// holds and what the palette cap allows.
    uint8_t indexAt(int idx) const
    {
      int32_t value = this->index[idx < this->numindex ? idx : 0];
      return value > 0 && value < 256 ? static_cast<uint8_t>(value) : 0;
    }

    /// The frame index of face idx, same rule (call only when
    /// hasFrames()).
    uint8_t frameAt(int idx) const
    {
      int32_t value = this->frameindex[idx < this->numframeindex ? idx : 0];
      return value > 0 && value < 256 ? static_cast<uint8_t>(value) : 0;
    }

    void clear(void) {
      this->index = nullptr;
      this->numindex = 0;
      this->frameindex = nullptr;
      this->numframeindex = 0;
      this->nodeid = 0;
    }
  };

  /// Borrow the index arrays from node's fields; empty arrays clear
  /// them, and either may be empty on its own.
  static void set(SoState * state, const SoNode * node,
                  const int32_t * index, int numindex,
                  const int32_t * frameindex = nullptr,
                  int numframeindex = 0);
  static const Indices & get(SoState * state);

protected:
  Indices indices;
};

#endif //FC_FINISHELEMENT_H
// vim: noai:ts=2:sw=2
