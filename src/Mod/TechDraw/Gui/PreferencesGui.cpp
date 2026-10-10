/***************************************************************************
 *   Copyright (c) 2020 WandererFan <wandererfan@gmail.com>                *
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

#include "PreCompiled.h"

#include <Mod/TechDraw/App/TechDrawParams.h>

#ifndef _PreComp_
# include <string>
# include <QColor>
# include <QFont>
# include <QString>
#endif

#include <App/Application.h>
#include <App/MaterialAppearance.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Gui/ViewParams.h>
#include <Mod/TechDraw/App/Preferences.h>
#include <Mod/TechDraw/App/LineGenerator.h>


#include "PreferencesGui.h"
#include "Rez.h"


//getters for parameters used in multiple places.
//ensure this is in sync with preference page user interfaces

using namespace TechDrawGui;
using namespace TechDraw;

QFont PreferencesGui::labelFontQFont()
{
    QString name = Preferences::labelFontQString();
    return QFont(name);
}

int PreferencesGui::labelFontSizePX()
{
    return (int) (Rez::guiX(Preferences::labelFontSizeMM()) + 0.5);
}

int PreferencesGui::dimFontSizePX()
{
    return (int) (Rez::guiX(Preferences::dimFontSizeMM()) + 0.5);
}

QColor PreferencesGui::normalQColor()
{
    App::Color fcColor = Preferences::normalColor();
    return fcColor.asValue<QColor>();
}

// TechDraw's own colour once one is set. 0, which is what the setting is
// while it is not, follows the 3D view's: the view's setting, with the
// view's default (it used to be read here with a default of TechDraw's).
static QColor ownOrTheViews(unsigned long own, unsigned long views)
{
    App::Color fcColor;
    fcColor.setPackedValue(static_cast<uint32_t>(own != 0 ? own : views));
    return fcColor.asValue<QColor>();
}

QColor PreferencesGui::selectQColor()
{
    return ownOrTheViews(TechDraw::TechDrawParams::getSelectColor(),
                         Gui::ViewParams::getSelectionColor());
}

QColor PreferencesGui::preselectQColor()
{
    return ownOrTheViews(TechDraw::TechDrawParams::getPreSelectColor(),
                         Gui::ViewParams::getHighlightColor());
}

App::Color PreferencesGui::sectionLineColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Decorations")->GetUnsigned("SectionColor", TechDraw::TechDrawParams::defaultSectionColor()));
    return fcColor;
}

QColor PreferencesGui::sectionLineQColor()
{
//if the App::Color version has already lightened the color, we don't want to do it again
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Decorations")->GetUnsigned("SectionColor", TechDraw::TechDrawParams::defaultSectionColor()));
    return fcColor.asValue<QColor>();
}

App::Color PreferencesGui::breaklineColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Decorations")->GetUnsigned("BreaklineColor", TechDraw::TechDrawParams::defaultBreaklineColor()));
    return fcColor;
}

QColor PreferencesGui::breaklineQColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Decorations")->GetUnsigned("BreaklineColor", TechDraw::TechDrawParams::defaultBreaklineColor()));
    return fcColor.asValue<QColor>();
}

App::Color PreferencesGui::centerColor()
{
    return App::Color((uint32_t) Preferences::getPreferenceGroup("Decorations")->GetUnsigned("CenterColor", TechDraw::TechDrawParams::defaultCenterColor()));
}

QColor PreferencesGui::centerQColor()
{
    App::Color fcColor = App::Color((uint32_t) Preferences::getPreferenceGroup("Decorations")->GetUnsigned("CenterColor", TechDraw::TechDrawParams::defaultCenterColor()));
    return fcColor.asValue<QColor>();
}

QColor PreferencesGui::vertexQColor()
{
    return Preferences::vertexColor().asValue<QColor>();
}

App::Color PreferencesGui::dimColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Dimensions")->GetUnsigned("Color", TechDraw::TechDrawParams::defaultDimensionColor()));
    return fcColor;
}

QColor PreferencesGui::dimQColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Dimensions")->GetUnsigned("Color", TechDraw::TechDrawParams::defaultDimensionColor()));
    return fcColor.asValue<QColor>();
}

//! The colour of leader lines. The Colors page stored its "Leaderline" colour
//! as Markups/Color, which nothing read, while this read LeaderLine/Color,
//! which nothing stored. The page stores LeaderLine/Color now; a colour it
//! stored before is still honoured while that is not stored.
static unsigned long leaderPackedColor()
{
    const unsigned long before = Preferences::getPreferenceGroup("Markups")->GetUnsigned(
        "Color", TechDraw::TechDrawParams::defaultLeaderLineColor());
    return Preferences::getPreferenceGroup("LeaderLine")->GetUnsigned("Color", before);
}

App::Color PreferencesGui::leaderColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(leaderPackedColor());
    return fcColor;
}

QColor PreferencesGui::leaderQColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(leaderPackedColor());
    return fcColor.asValue<QColor>();
}

int PreferencesGui::dimArrowStyle()
{
    return Preferences::getPreferenceGroup("Dimensions")->GetInt("ArrowStyle", TechDraw::TechDrawParams::defaultArrowStyle());
}

double PreferencesGui::dimArrowSize()
{
    return Preferences::getPreferenceGroup("Dimensions")->GetFloat("ArrowSize", TechDraw::TechDrawParams::defaultArrowSize());
}


double PreferencesGui::edgeFuzz()
{
    return Preferences::getPreferenceGroup("General")->GetFloat("EdgeFuzz", TechDraw::TechDrawParams::defaultEdgeFuzz());
}


bool PreferencesGui::sectionLineMarks()
{
    return Preferences::getPreferenceGroup("Decorations")->GetBool("SectionLineMarks", TechDraw::TechDrawParams::defaultSectionLineMarks());
}

QString PreferencesGui::weldingDirectory()
{
    std::string defaultDir = App::Application::getResourceDir() + "Mod/TechDraw/Symbols/Welding/AWS/";

    std::string symbolDir = Preferences::getPreferenceGroup("Files")->GetASCII("WeldingDir", TechDraw::TechDrawParams::defaultWeldingDir().c_str());
    if (symbolDir.empty()) {
        symbolDir = defaultDir;
    }
    QString qSymbolDir = QString::fromUtf8(symbolDir.c_str());
    Base::FileInfo fi(symbolDir);
    if (!fi.isReadable()) {
        Base::Console().Warning("Welding Directory: %s is not readable\n", symbolDir.c_str());
        qSymbolDir = QString::fromUtf8(defaultDir.c_str());
    }
    return qSymbolDir;
}

App::Color PreferencesGui::gridColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Colors")->GetUnsigned("gridColor", TechDraw::TechDrawParams::defaultgridColor()));  //#000000 black
    return fcColor;
}

QColor PreferencesGui::gridQColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Colors")->GetUnsigned("gridColor", TechDraw::TechDrawParams::defaultgridColor()));  //#000000 black
    return fcColor.asValue<QColor>();
}

double PreferencesGui::gridSpacing()
{
    return Preferences::getPreferenceGroup("General")->GetFloat("gridSpacing", TechDraw::TechDrawParams::defaultgridSpacing());
}

bool PreferencesGui::showGrid()
{
    return Preferences::getPreferenceGroup("General")->GetBool("showGrid", TechDraw::TechDrawParams::defaultshowGrid());
}

bool PreferencesGui::multiSelection()
{
  return Preferences::getPreferenceGroup("General")->GetBool("multiSelection", TechDraw::TechDrawParams::defaultmultiSelection());
}

App::Color PreferencesGui::pageColor()
{
    App::Color result;
    result.setPackedValue(Preferences::getPreferenceGroup("Colors")->GetUnsigned("PageColor", TechDraw::TechDrawParams::defaultPageColor()));  //#FFFFFFFF white
    return result;
}

QColor PreferencesGui::pageQColor()
{
    return PreferencesGui::pageColor().asValue<QColor>();
}

QColor PreferencesGui::getAccessibleQColor(QColor orig)
{
    if (Preferences::lightOnDark() && Preferences::monochrome()) {
        return lightTextQColor();
    }
    if (Preferences::lightOnDark()) {
        return lightenColor(orig);
    }
    return orig;
}

QColor PreferencesGui::lightTextQColor()
{
    return Preferences::lightTextColor().asValue<QColor>();
}

QColor PreferencesGui::reverseColor(QColor orig)
{
    int revRed = 255 - orig.red();
    int revBlue = 255 - orig.blue();
    int revGreen = 255 - orig.green();
    return QColor(revRed, revGreen, revBlue);
}

// largely based on code from https://invent.kde.org/graphics/okular and
// https://en.wikipedia.org/wiki/HSL_and_HSV#HSL_to_RGB
QColor PreferencesGui::lightenColor(QColor orig)
{
    // get component colours on [0, 255]
    uchar red = orig.red();
    uchar blue = orig.blue();
    uchar green = orig.green();
    uchar alpha = orig.alpha();

    // shift color values
    uchar m = std::min( {red, blue, green} );
    red -= m;
    blue -= m;
    green -= m;

    // calculate chroma (colour range)
    uchar chroma = std::max( {red, blue, green} );

    // calculate lightened colour value
    uchar newm = 255 - chroma - m;
    red += newm;
    green += newm;
    blue += newm;

    return QColor(red, green, blue, alpha);
}


double PreferencesGui::templateClickBoxSize()
{
    return Preferences::getPreferenceGroup("General")->GetFloat("TemplateDotSize", TechDraw::TechDrawParams::defaultTemplateDotSize());
}


QColor PreferencesGui::templateClickBoxColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Colors")->GetUnsigned("TemplateUnderlineColor", TechDraw::TechDrawParams::defaultTemplateUnderlineColor()));  //#0000FF blue
    return fcColor.asValue<QColor>();
}
