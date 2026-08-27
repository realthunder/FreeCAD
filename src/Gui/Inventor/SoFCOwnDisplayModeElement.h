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

#ifndef FC_SOFCOWNDISPLAYMODEELEMENT_H
#define FC_SOFCOWNDISPLAYMODEELEMENT_H

#include <FCGlobal.h>
#include <Inventor/elements/SoInt32Element.h>

/** The OWN display mode of the shapes that follow, in traversal state.
 *
 * SoFCDisplayModeElement carries the mode a VIEW is asking for;
 * this one carries the mode the OBJECT is in, which is a different
 * question and is answered by a different node -- the SoFCSwitch whose
 * children are the object's display modes.
 *
 * Why the renderer needs it (docs/CoinRetirement.md 5.8). A display
 * style reaches the pixels by traversing the style-NAMED child of every
 * object's switch, so it is traversal state, and one traversal can
 * therefore produce only one style. That is what stops the split-view
 * unified canvas -- N cells fed by ONE traversal -- from giving its
 * cells different styles. The way out is the one Rhino, SolidWorks and
 * Blender all take: resolve the style per object per view at DRAW time,
 * from the view's style and the object's own mode together. The
 * traversal then captures the SUPERSET child once and each cell filters
 * it, and this element is how the object's own mode reaches the draw.
 *
 * Two bit masks, both Render::DrawStyleMask (bit i = Material::Type i),
 * packed into the int32 this element inherits:
 *
 * - \c ownMask -- what the object's OWN mode draws, so a cell showing
 *   "As Is" can filter the superset capture back down to it.
 * - \c registeredMask -- which of the four style names the switch has a
 *   child for. A style whose name the switch does not carry does not
 *   apply to that object: SoFCSwitch falls through to whichChild, which
 *   is why Mesh's "Point" is untouched by a "Points" override. The
 *   renderer has to reproduce that, so it has to know it.
 *
 * A mask of \c Unknown means the object's own mode is not one of the
 * four (Mesh's "Point", FEM's "Faces & Wireframe"): its buckets cannot
 * be named, so nothing may filter it and it is drawn as captured.
 */
class GuiExport SoFCOwnDisplayModeElement : public SoInt32Element {
  typedef SoInt32Element inherited;

  SO_ELEMENT_HEADER(SoFCOwnDisplayModeElement);

public:
  /// Own mode that no style mask describes; see the class comment.
  static const uint8_t Unknown = 0xff;

  static void initClass(void);

protected:
  virtual ~SoFCOwnDisplayModeElement();

public:
  virtual void init(SoState * state);

  static void set(SoState * const state, uint8_t ownmask, uint8_t registeredmask);
  static void get(SoState * const state, uint8_t & ownmask, uint8_t & registeredmask);

  /// Pack/unpack, exposed because the render cache stores the pair as
  /// one value and the backend unpacks it again.
  static int32_t pack(uint8_t ownmask, uint8_t registeredmask)
  { return (static_cast<int32_t>(registeredmask) << 8) | ownmask; }

  /// One bit per Class-A style NAME, for \c registeredMask.
  ///
  /// A name needs its own bit because the bucket masks overlap and so
  /// cannot spell a SET of names: Shaded is the faces bit, Points the
  /// points bit, and Wireframe and Flat Lines are unions of those, so
  /// OR-ing bucket masks together loses which names were in it.
  enum StyleNameBit : uint8_t {
    HasShaded    = 1 << 0,
    HasFlatLines = 1 << 1,
    HasWireframe = 1 << 2,
    HasPoints    = 1 << 3,
  };
};

namespace Gui {

/// The primitive buckets a Class-A display mode NAME draws, as a
/// Render::DrawStyleMask.
///
/// Render::StyleAsIs for an empty name (no style at all), and
/// SoFCOwnDisplayModeElement::Unknown for a name that is not one of the
/// four -- Mesh's "Point", FEM's "Faces & Wireframe". Those two answers
/// are NOT interchangeable: "no style" admits every draw, an unnameable
/// mode admits every draw *and* forbids anything from filtering it.
///
/// The single home of this mapping; View3DInventorViewer's
/// drawStyleMaskFromName() delegates here.
GuiExport uint8_t drawStyleMaskFromModeName(const char *mode);

/// The SoFCOwnDisplayModeElement::StyleNameBit of a Class-A style name,
/// or 0 for any other name.
GuiExport uint8_t styleNameBitOf(const char *mode);

} // namespace Gui

#endif // FC_SOFCOWNDISPLAYMODEELEMENT_H
// vim: noai:ts=2:sw=2
