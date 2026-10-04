/***************************************************************************
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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

#ifndef _PreComp_
# include <Inventor/nodes/SoAsciiText.h>
# include <Inventor/nodes/SoAnnotation.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoFont.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoMaterialBinding.h>
# include <Inventor/nodes/SoLightModel.h>
# include <Inventor/nodes/SoScale.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoSwitch.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/OriginFeature.h>
#include <App/Origin.h>

#include "ViewProviderDatum.h"
#include "SoFCSelection.h"
#include "SoFCUnifiedSelection.h"
#include "ViewParams.h"
#include "ViewProviderCoordinateSystem.h"
#include "BitmapFactory.h"
#include "Inventor/SoAutoZoomTranslation.h"


using namespace Gui;

namespace {

// ViewParams' group; ParamHandlers takes the path below the user root
const char *DatumParamPath = "BaseApp/Preferences/View";

// One screen unit, the unit upstream lays datums out in, as a scale factor of
// SoAutoZoomTranslation, under which a unit is scaleFactor/50 of the view
// height. 0.05 makes a unit a thousandth of the view height: a pixel of a
// view 1000 pixels high. Upstream's SoShapeScale counts real pixels; the
// fork's autozoom, which every render path and the browser viewer implement,
// scales with the view instead, so a datum keeps its share of the view as
// the window is resized.
constexpr float ScreenUnit = 0.05f;

// Label height in screen units (upstream's plane label font)
constexpr float ScreenFontSize = 10.0f;

} // namespace

PROPERTY_SOURCE(Gui::ViewProviderDatum, Gui::ViewProviderGeometryObject)

ViewProviderDatum::ViewProviderDatum () {
    ADD_PROPERTY_TYPE ( Size, (ViewProviderCoordinateSystem::defaultSize()), 0, App::Prop_ReadOnly,
    QT_TRANSLATE_NOOP("App::Property", "Visual size of the feature"));

    ShapeColor.setValue ( ViewProviderCoordinateSystem::defaultColor ); // Set default color for origin (light-blue)
    BoundingBox.setStatus(App::Property::Hidden, true); // Hide Boundingbox from the user due to it doesn't make sense

    // Create node for scaling the origin
    pScale = new SoScale ();
    pScale->ref ();

    // Create the separator filled by inherited classes
    pOriginFeatureRoot = new SoSeparator();
    pOriginFeatureRoot->ref ();
    pOriginFeatureRoot->renderCaching = SoSeparator::OFF;

    // Create the node for a constant size on screen
    pZoom = new SoAutoZoomTranslation();
    pZoom->ref();

    pFont = new SoFont();
    pFont->ref();

    // Create the Label node
    pLabel = new SoAsciiText();
    pLabel->ref();
    pLabel->width.setValue(-1);

    pLabelSwitch = new SoSwitch();
    pLabelSwitch->ref();
    pLabelSwitch->addChild(pLabel);
    pLabelSwitch->whichChild = SO_SWITCH_ALL;

    ShadowStyle.setValue(3);

    pHighlight = new SoFCSelection ();
    pHighlight->ref();
}


ViewProviderDatum::~ViewProviderDatum () {
    pScale->unref ();
    pZoom->unref ();
    pFont->unref ();
    pOriginFeatureRoot->unref ();
    pLabelSwitch->unref ();
    pLabel->unref ();
    pHighlight->unref();
}

bool ViewProviderDatum::isScreenSize()
{
    return ViewParams::getDatumScreenSize();
}

float ViewProviderDatum::screenPlaneSize()
{
    return static_cast<float>(ViewParams::getDatumPlaneSize() * ViewParams::getDatumScale() / 100.0);
}

float ViewProviderDatum::screenLineSize()
{
    return static_cast<float>(ViewParams::getDatumLineSize() * ViewParams::getDatumScale() / 100.0);
}

std::string ViewProviderDatum::getRole() const
{
    // The Role of an App::DatumElement may not be set yet when attaching:
    // the name tells as well, the way upstream reads it
    const char* name = pcObject ? pcObject->getNameInDocument() : nullptr;
    if (!name)
        return {};
    for (auto roles : {App::LocalCoordinateSystem::AxisRoles, App::LocalCoordinateSystem::PlaneRoles}) {
        for (int i = 0; i < 3; ++i) {
            if (strncmp(name, roles[i], strlen(roles[i])) == 0)
                return roles[i];
        }
    }
    auto point = App::LocalCoordinateSystem::PointRoles[0];
    if (strncmp(name, point, strlen(point)) == 0)
        return point;
    return {};
}

std::string ViewProviderDatum::screenLabel() const
{
    auto role = getRole();
    if (role.size() > 6 && role.compare(role.size() - 6, 6, "_Plane") == 0)
        return role.substr(0, role.size() - 6);
    if (role.size() > 5 && role.compare(role.size() - 5, 5, "_Axis") == 0)
        return role.substr(0, role.size() - 5);
    return getObject()->Label.getValue();
}

void ViewProviderDatum::applySizeModel()
{
    screenSize = isScreenSize();

    SoNode* wanted = screenSize ? static_cast<SoNode*>(pZoom) : static_cast<SoNode*>(pScale);
    SoNode* other = screenSize ? static_cast<SoNode*>(pScale) : static_cast<SoNode*>(pZoom);
    int index = pHighlight->findChild(other);
    if (index >= 0)
        pHighlight->replaceChild(index, wanted);

    float lcsSize = static_cast<float>(
            ViewParams::getHandle()->GetFloat("LocalCoordinateSystemSize", 1.0));
    pZoom->scaleFactor = static_cast<float>(ScreenUnit * lcsSize * temporaryScale);

    float fontRatio = 10.0f;
    if (pcObject && pcObject->is<App::Line>())
        // keep font size on axes equal to font size on planes
        fontRatio *= ViewProviderCoordinateSystem::axesScaling;
    pFont->size = screenSize ? ScreenFontSize
                             : ViewProviderCoordinateSystem::baseSize() / fontRatio;
}

void ViewProviderDatum::updateLabel()
{
    if (!pcObject)
        return;
    std::string text = screenSize ? screenLabel() : std::string(pcObject->Label.getValue());
    pLabel->string.setValue(SbString(text.c_str()));
    bool show = !screenSize || labelForced || showLabelOnScreen();
    pLabelSwitch->whichChild = show ? SO_SWITCH_ALL : SO_SWITCH_NONE;
}

void ViewProviderDatum::updateDatumSize()
{
}

void ViewProviderDatum::setTemporaryScale(double factor)
{
    temporaryScale = factor;
    applySizeModel();
}

void ViewProviderDatum::resetTemporarySize()
{
    setTemporaryScale(1.0);
}

void ViewProviderDatum::setLabelVisibility(bool visible)
{
    labelForced = visible;
    updateLabel();
}

// Separator node that alters OpenGL depth function. Coin3d's SoDepthBuffer
// does not account for user code change of depth function.
class DepthSeparator : public SoSeparator {
    typedef SoSeparator inherited;

public:
    DepthSeparator(int32_t func)
        :func(func)
    {}

    virtual void GLRenderBelowPath(SoGLRenderAction * action) {
        render(action, false);
    }

    virtual void GLRenderInPath(SoGLRenderAction * action) {
        render(action, true);
    }

    void render(SoGLRenderAction *action, bool inpath) {
        Gui::FCDepthFunc guard(func);
        // SoState *state = action->getState();
        // float t = SoLazyElement::getTransparency(state, 0);
        // if (t != 0.0f) {
        //     state->push();
        //     float trans = 0.0f;
        //     SoLazyElement::setTransparency(state, this, 1, &trans, &packer);
        // }
        if (inpath)
            inherited::GLRenderInPath(action);
        else
            inherited::GLRenderBelowPath(action);
        // if (t != 0.0f)
        //     state->pop();
    }


private:
    int32_t func;
    SoColorPacker packer;
};

void ViewProviderDatum::attach(App::DocumentObject* pcObject)
{
    ViewProviderGeometryObject::attach(pcObject);

    float defaultSz = ViewProviderCoordinateSystem::baseSize();
    float sz = Size.getValue () / defaultSz;

    // Create an external separator
    auto sep = new DepthSeparator(GL_LEQUAL);
    sep->renderCaching = SoSeparator::OFF;

    // Add material from the base class
    sep->addChild(pcShapeMaterial);

    // Bind same material to all part
    auto matBinding = new SoMaterialBinding;
    matBinding->value = SoMaterialBinding::OVERALL;
    sep->addChild(matBinding);

    // Bind same material to all part
    SoLightModel * lightmodel = new SoLightModel;
    lightmodel->model = SoLightModel::BASE_COLOR;
    sep->addChild(lightmodel);

    // Scale feature to the given size, or keep it constant on screen: the
    // size model swaps pZoom in for pScale (applySizeModel)
    pScale->scaleFactor = SbVec3f (sz, sz, sz);
    // sep->addChild (pScale);
    pHighlight->addChild (pScale);

    if ( pcObject->is<App::Line>() ) {
        const char* axisName = pcObject->getNameInDocument();
        auto axisRoles = App::Origin::AxisRoles;
        if ( strncmp(axisName, axisRoles[0], strlen(axisRoles[0]) ) == 0 ) {
            // X-axis: red
            ShapeColor.setValue ( 0xFF0000FF );
        } else if ( strncmp(axisName, axisRoles[1], strlen(axisRoles[1]) ) == 0 ) {
            // Y-axis: green
            ShapeColor.setValue ( 0x00FF00FF );
        } else if ( strncmp(axisName, axisRoles[2], strlen(axisRoles[2]) ) == 0 ) {
            // Z-axis: blue
            ShapeColor.setValue ( 0x0000FFFF );
        }
    }

    // Adding font node under SoFCSelection node is crucial for its bounding
    // box rendering to work. Because SoGetBoundingBoxAction is applied on the
    // node itself, instead of a path. Without the font, SoAsciiText will
    // report incorrect bounds.
    //
    // sep->addChild ( font );
    pHighlight->addChild (pFont);

    // Create the selection node
    auto highlight = pHighlight;
    highlight->applySettings ();
    if ( !Selectable.getValue() ) {
        highlight->selectionMode = Gui::SoFCSelection::SEL_OFF;
    }
    highlight->objectName    = getObject()->getNameInDocument();
    highlight->documentName  = getObject()->getDocument()->getName();
    highlight->style = SoFCSelection::EMISSIVE_DIFFUSE;

    // Style for normal (visible) lines
    auto style = new SoDrawStyle ();
    style->lineWidth = 2.0f;
    highlight->addChild ( style );

    // Visible lines
    highlight->addChild ( pOriginFeatureRoot );

    // Hidden features
    auto hidden = new SoFCPathAnnotation;
    hidden->priority = -1;
    hidden->renderCaching = SoSeparator::OFF;

    // Style for hidden lines
    style = new SoDrawStyle ();
    style->lineWidth = 2.0f;
    style->linePattern.setValue ( 0xF000 ); // (dash-skip-skip-skip)
    hidden->addChild ( style );

    // Hidden lines
    hidden->addChild ( pOriginFeatureRoot );

    highlight->addChild ( hidden );

    sep->addChild ( highlight );

    applySizeModel();
    updateLabel();

    addDisplayMaskMode ( sep, "Base" );

    // A subclass lays its geometry out at the end of its attach(); these
    // redo it when the size parameters change
    handlers.addDelayedHandler(DatumParamPath,
            {"DatumScreenSize", "DatumScale", "DatumPlaneSize", "DatumLineSize",
             "LocalCoordinateSystemSize"},
            [this](ParameterGrp::handle) {
                applySizeModel();
                updateDatumSize();
                updateLabel();
            });
}

void ViewProviderDatum::updateData ( const App::Property* prop ) {
    if (prop == &getObject()->Label) {
        updateLabel();
    }
    ViewProviderGeometryObject::updateData(prop);
}

void ViewProviderDatum::onChanged ( const App::Property* prop ) {
    if (prop == &Size) {
        float sz = Size.getValue () / ViewProviderCoordinateSystem::baseSize();
        pScale->scaleFactor = SbVec3f (sz, sz, sz);
    }
    ViewProviderGeometryObject::onChanged(prop);
}

std::vector<std::string> ViewProviderDatum::getDisplayModes () const
{
    // add modes
    std::vector<std::string> StrList;
    StrList.emplace_back("Base");
    return StrList;
}

void ViewProviderDatum::setDisplayMode (const char* ModeName)
{
    if (strcmp(ModeName, "Base") == 0)
        setDisplayMaskMode("Base");
    ViewProviderGeometryObject::setDisplayMode(ModeName);
}

bool ViewProviderDatum::onDelete(const std::vector<std::string> &) {
    auto feat = static_cast <App::DatumElement *> ( getObject() );
    // Forbid deletion if there is a coordinate system this feature belongs to

    if ( feat->getLCS () ) {
        return false;
    } else {
        return true;
    }
}

QIcon ViewProviderDatum::getIcon() const
{
    App::OriginFeature *feat = static_cast <App::OriginFeature *> ( getObject() );
    const char *pixmap;
    if (feat->Role.getStrValue() == "X_Axis")
        pixmap = "Std_AxisX";
    else if (feat->Role.getStrValue() == "Y_Axis")
        pixmap = "Std_AxisY";
    else if (feat->Role.getStrValue() == "Z_Axis")
        pixmap = "Std_AxisZ";
    else if (feat->Role.getStrValue() == "XY_Plane")
        pixmap = "Std_PlaneXY";
    else if (feat->Role.getStrValue() == "XZ_Plane")
        pixmap = "Std_PlaneXZ";
    else if (feat->Role.getStrValue() == "YZ_Plane")
        pixmap = "Std_PlaneYZ";
    else
        return ViewProviderGeometryObject::getIcon();
    return Gui::BitmapFactory().iconFromTheme(pixmap);
}
