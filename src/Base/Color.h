/***************************************************************************
 *   Copyright (c) 2005 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#ifndef BASE_COLOR_H
#define BASE_COLOR_H

#ifdef __GNUC__
# include <cstdint>
#endif
#include <cmath>
#include <string>

#include <FCGlobal.h>

namespace Base
{

/** Adapter over a foreign colour type, e.g. QColor
 *
 * Lets code convert to and from a colour class Base cannot name -- Base
 * carries no Qt -- by going through the accessors every such class has.
 */
template<class color_type>
struct color_traits
{
    color_traits() = default;
    explicit color_traits(const color_type& ct)
        : ct(ct)
    {}
    float redF() const
    {
        return static_cast<float>(ct.redF());
    }
    float greenF() const
    {
        return static_cast<float>(ct.greenF());
    }
    float blueF() const
    {
        return static_cast<float>(ct.blueF());
    }
    float alphaF() const
    {
        return static_cast<float>(ct.alphaF());
    }
    int red() const
    {
        return ct.red();
    }
    int green() const
    {
        return ct.green();
    }
    int blue() const
    {
        return ct.blue();
    }
    int alpha() const
    {
        return ct.alpha();
    }
    static color_type makeColor(int red, int green, int blue, int alpha = 255)
    {
        return color_type {red, green, blue, alpha};
    }

private:
    color_type ct;
};

/** Color class
 *
 * THE ALPHA COMPONENT IS AN ALPHA: 1 is fully opaque, 0 is invisible, and a
 * transparency is 1 - a (which is what transparency() returns). This is
 * upstream's meaning, adopted deliberately; the fork used to mean the
 * opposite, where the component held transparency and 0 was opaque.
 *
 * ## Why the fork changed
 *
 * The two meanings share one type, so a Base::Color carries no marker saying
 * which it is in and the compiler cannot tell them apart. While both were in
 * the tree -- fork code writing transparency, merged upstream code writing
 * opacity -- every value's meaning depended on which module produced it, and
 * that is not a thing a reviewer can check. One meaning everywhere is the
 * only version of this that stays correct.
 *
 * ## What is in which convention
 *
 * In memory, everywhere, always: OPACITY. No exceptions, no per-module rules.
 *
 * On disk, two surfaces still hold the older meaning and convert at their
 * boundary rather than in memory:
 *
 *   - **Documents.** Every .FCStd written before upstream 1.1 stores
 *     transparency in the alpha, and so does every .FCStd this fork writes,
 *     because the fork still calls itself 0.22 and a file is read by the
 *     version it states. So colours are converted on the way in AND on the
 *     way out, in both encodings, by the four colour properties in
 *     App/PropertyStandard.cpp. See Base/ProgramVersion.h: alphaIsOpacity()
 *     asks it of a file being read, writerAlphaIsOpacity() of this build, and
 *     the two move together the day PACKAGE_VERSION reaches 1.1.
 *   - **The DiffuseColor compatibility element.** A view provider still
 *     writes the pre-ShapeAppearance name with its values, so an older
 *     FreeCAD opens the document with its face colours -- but only while the
 *     appearance varies nothing but the diffuse colour, since a colour list
 *     cannot carry the rest (PropertyAppearanceList::variesOnlyInDiffuse).
 *     Its alpha is a transparency, like the rest of the document.
 *
 * Preferences are NOT one of them. user.cfg moved to opacity with the release
 * that renamed the vendor, so its file is a new file and a colour read from
 * it is already an alpha.
 *
 * Anywhere else, a site that inverts an alpha is a bug. That is the whole
 * point of having one meaning: the inversions are enumerable and they all
 * live at a named boundary.
 *
 * ## Two things upstream got wrong here, which this fork does not copy
 *
 *   - Their LineColor and PointColor were never converted and still hold
 *     transparency, inconsistent with DiffuseColor beside them (upstream
 *     issue #20213, closed as not planned). Here they are opacity like
 *     everything else.
 *   - Their STEP import lost transparency for a release because the same
 *     conversion existed in two files and the two drifted (upstream issue
 *     #18575, fixed by moving it into one shared helper). Hence the rule
 *     above: one conversion, at one named boundary, never a second copy.
 */
class BaseExport Color
{
public:
    /**
     * Defines the color as (R,G,B,A) whereas all values are in the range [0,1].
     * \a A defines the alpha value, 1 means fully opaque and 0 invisible.
     */
    explicit Color(float R=0.0,float G=0.0, float B=0.0, float A=1.0);

    /**
     * Does basically the same as the constructor above unless that (R,G,B,A) is
     * encoded as an unsigned int.
     */
    explicit Color(uint32_t rgba);

    /** Copy constructor. */
    Color(const Color& c) = default;
    Color(Color&&) = default;

    /** Returns true if both colors are equal. Therefore all components must be equal. */
    bool operator==(const Color& c) const;
    bool operator!=(const Color& c) const;
    /**
     * Defines the color as (R,G,B,A) whereas all values are in the range [0,1].
     * \a A defines the alpha value, 1 means fully opaque and 0 invisible.
     */
    void set(float R,float G, float B, float A=1.0);
    Color& operator=(const Color& c) = default;
    Color& operator=(Color&& c) = default;
    /**
     * Sets the color value as a 32 bit combined red/green/blue/alpha value.
     * Each component is 8 bit wide (i.e. from 0x00 to 0xff), and the red
     * value should be stored leftmost, like this: 0xRRGGBBAA.
     *
     * \sa getPackedValue().
     */
    Color& setPackedValue(uint32_t rgba);
    /// This colour's transparency, the complement of its alpha component
    float transparency() const {return 1.0F - a;}
    /// Set the alpha component so that the colour has the given transparency
    void setTransparency(float value) {a = 1.0F - value;}
    /**
     * Returns color as a 32 bit packed unsigned int in the form 0xRRGGBBAA.
     *
     *  \sa setPackedValue().
     */
    uint32_t getPackedValue() const;
    /**
     * Returns color as a 32 bit packed unsigned int in the form 0xRRGGBB00.
     * The alpha channel is not represented at all, so this is lossy for any
     * colour that is not opaque; use getPackedValue() to keep it.
     */
    uint32_t getPackedRGB() const;
    /**
     * Sets color as a 32 bit packed unsigned int in the form 0xRRGGBB00.
     * The argument carries no alpha, and this resets it to fully opaque
     * rather than preserving the previous value.
     */
    void setPackedRGB(uint32_t);
    /**
     * Returns color as a 32 bit packed unsigned int in the form 0xAARRGGBB.
     */
    uint32_t getPackedARGB() const;
    /**
     * Sets color as a 32 bit packed unsigned int in the form 0xAARRGGBB.
     */
    void setPackedARGB(uint32_t);

    template <typename T>
    static uint32_t asPackedRGBA(const T& color) {
        return (color.red() << 24) | (color.green() << 16) | (color.blue() << 8) | color.alpha();
    }

    template <typename T>
    static T fromPackedRGBA(uint32_t color) {
        return T((color >> 24) & 0xff, (color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
    }

    template <typename T>
    static uint32_t asPackedRGB(const T& color) {
        return (color.red() << 24) | (color.green() << 16) | (color.blue() << 8);
    }

    template <typename T>
    static T fromPackedRGB(uint32_t color) {
        return T((color >> 24) & 0xff, (color >> 16) & 0xff, (color >> 8) & 0xff);
    }
    /**
     * creates FC Color from template type, e.g. Qt QColor
     */
    template <typename T>
    void setValue(const T& q) {
        color_traits<T> ct {q};
        set(ct.redF(), ct.greenF(), ct.blueF(), ct.alphaF());
    }
    /**
     * creates FC Color from template type, e.g. Qt QColor
     * (upstream's factory twin of setValue; StyleParameters uses it)
     */
    template <typename T>
    static Color fromValue(const T& q) {
        Color c;
        c.setValue(q);
        return c;
    }
    /**
     * returns a template type e.g. Qt color equivalent to FC color
     *
     */
    template <typename T>
    inline T asValue() const {
        return color_traits<T>::makeColor(int(std::lround(r * 255.0F)),
                                         int(std::lround(g * 255.0F)),
                                         int(std::lround(b * 255.0F)),
                                         int(std::lround(a * 255.0F)));
    }
    /**
     * returns color as hex color "#RRGGBB"
     *
     */
    std::string asHexString() const;

    /**
     * gets color from hex color "#RRGGBB"
     *
     */
    bool fromHexString(const std::string& hex);

    /// color values, public accessible. \a a is an opacity: 1 is opaque.
    float r,g,b,a;
};

/// Specialization for Color itself, so generic code can adapt it like any other
template<>
struct color_traits<Base::Color>
{
    using color_type = Base::Color;
    color_traits() = default;
    explicit color_traits(const color_type& ct)
        : ct(ct)
    {}
    float redF() const
    {
        return ct.r;
    }
    float greenF() const
    {
        return ct.g;
    }
    float blueF() const
    {
        return ct.b;
    }
    float alphaF() const
    {
        return ct.a;
    }
    void setRedF(float red)
    {
        ct.r = red;
    }
    void setGreenF(float green)
    {
        ct.g = green;
    }
    void setBlueF(float blue)
    {
        ct.b = blue;
    }
    void setAlphaF(float alpha)
    {
        ct.a = alpha;
    }
    int red() const
    {
        return int(std::lround(ct.r * 255.0F));
    }
    int green() const
    {
        return int(std::lround(ct.g * 255.0F));
    }
    int blue() const
    {
        return int(std::lround(ct.b * 255.0F));
    }
    int alpha() const
    {
        return int(std::lround(ct.a * 255.0F));
    }
    void setRed(int red)
    {
        ct.r = static_cast<float>(red) / 255.0F;
    }
    void setGreen(int green)
    {
        ct.g = static_cast<float>(green) / 255.0F;
    }
    void setBlue(int blue)
    {
        ct.b = static_cast<float>(blue) / 255.0F;
    }
    void setAlpha(int alpha)
    {
        ct.a = static_cast<float>(alpha) / 255.0F;
    }
    static color_type makeColor(int red, int green, int blue, int alpha = 255)
    {
        return color_type {static_cast<float>(red) / 255.0F,
                           static_cast<float>(green) / 255.0F,
                           static_cast<float>(blue) / 255.0F,
                           static_cast<float>(alpha) / 255.0F};
    }

private:
    color_type ct;
};

} //namespace Base

#endif // BASE_COLOR_H
