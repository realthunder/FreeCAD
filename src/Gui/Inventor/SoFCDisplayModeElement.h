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

#ifndef FC_SOFCDISPLAYMODEELEMENT_H
#define FC_SOFCDISPLAYMODEELEMENT_H

#include <Inventor/SbColor.h>
#include <Inventor/SbString.h>
#include <Inventor/SbName.h>
#include <Inventor/elements/SoReplacedElement.h>

#include <cstdint>
#include <utility>
#include <vector>

class GuiExport SoFCDisplayModeElement: public SoReplacedElement {
    typedef SoReplacedElement inherited;

    SO_ELEMENT_HEADER(SoFCDisplayModeElement);

public:
    static void initClass(void);
protected:
    virtual ~SoFCDisplayModeElement();

public:
    virtual void init(SoState *state);

    /** The capture's additive-mode interest set (docs/CoinRetirement.md
     * 5.9 "Non-standard modes"): the display mode NAMES, outside the
     * four Class-A styles, that any per-object override of any view
     * sharing this capture wants, each with its interned id
     * (Render::internModeName). SoFCSwitch traverses a child named
     * here IN ADDITION to the normal flow, tagged with the id.
     *
     * The producer owns the storage and keeps it alive while it is on
     * the element; the ORDER is the interestBits bit assignment, so
     * the list handed to Renderer::setCaptureInterest must be this
     * one. At most 16 entries -- the bit budget; the producer drops
     * the excess. version bumps on every content change and is what
     * the element's matches() compares, which is what re-captures the
     * scene when the interest moves: the caches that read the element
     * mismatch on the next traversal.
     */
    struct CaptureInterest {
        std::vector<std::pair<SbName, uint16_t>> modes;
        uint32_t version = 0;

        /// The interned id of \a name when listed, else 0, with the
        /// interestBits bit it owns in \a bit. SbName-interned pointer
        /// compares (the 5.8 strcmp trap).
        uint16_t idOf(const SbName &name, uint16_t *bit = nullptr) const {
            for (size_t i = 0; i < modes.size(); ++i) {
                if (modes[i].first == name) {
                    if (bit)
                        *bit = uint16_t(1u << i);
                    return modes[i].second;
                }
            }
            if (bit)
                *bit = 0;
            return 0;
        }
    };

    struct HiddenLineConfig {
      SbBool shaded;
      float lineWidth;
      float pointSize;
      SbBool outline;
      SbBool perFaceOutline;
      SbBool hideVertex;
      SbBool hideFace;
      SbBool hideSeam;
      SbBool sceneOutline;
      float outlineWidth;
      SbBool hasFaceColor;
      uint32_t faceColor;
      SbBool hasLineColor;
      uint32_t lineColor;
      SbBool hasTransparency;
      float transparency;

      void reset();
    };

    static void set(SoState * const state, SoNode * const node,
            const SbName &mode, SbBool hiddenLines, const HiddenLineConfig *config=nullptr,
            const CaptureInterest *interest=nullptr);

    static const SbName &get(SoState * const state);
    /// The capture's interest set, or null. Read by SoFCSwitch to
    /// decide the additive traversal; reading it during a cache build
    /// records the dependency that re-captures on an interest change.
    static const CaptureInterest *getCaptureInterest(SoState * const state);
    static SbBool showHiddenLines(SoState * const state, HiddenLineConfig *config=nullptr);
    static void setColors(SoState * const state, SoNode * const node,
            const SbColor *faceColor, const SbColor *lineColor, float transp);
    static SbBool showHiddenLines(SoState * const state, SbBool *outline);
    static const SbColor *getFaceColor(SoState * const state);
    static const SbColor *getLineColor(SoState * const state);
    static float getTransparency(SoState * const state);

    static SoFCDisplayModeElement * getInstance(SoState *state);

    const SbName &get() const;

    SbBool showHiddenLines() const;
    const HiddenLineConfig &getHiddenLineConfig() const;

    const SbColor *getFaceColor() const;
    const SbColor *getLineColor() const;
    float getTransparency() const;

    virtual SbBool matches(const SoElement * element) const;
    virtual SoElement *copyMatchInfo(void) const;

protected:
    SbName displayMode;
    SbBool hiddenLines;
    SbColor lineColor;
    SbColor faceColor;
    HiddenLineConfig hiddenLineConfig;
    /// The interest set (owned by the producer) and the version it had
    /// when set. The version is stored beside the pointer because
    /// matches() runs against a copyMatchInfo() snapshot taken earlier:
    /// the pointer alone would compare equal after the producer edited
    /// the list in place.
    const CaptureInterest *captureInterest = nullptr;
    uint32_t captureInterestVersion = 0;
};

#endif // FC_SOFCDISPLAYMODEELEMENT_H
// vim: noai:ts=2:sw=2
