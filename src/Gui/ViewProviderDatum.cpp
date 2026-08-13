/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinlaender                                   *
 *                                   <jrheinlaender@users.sourceforge.net> *
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
# include <QApplication>
# include <QMessageBox>
# include <QAction>
# include <QMenu>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoShapeHints.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoTransparencyType.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoMarkerSet.h>
# include <Inventor/nodes/SoVertexProperty.h>
# include <Inventor/nodes/SoLineSet.h>
# include <Inventor/nodes/SoFaceSet.h>
# include <Inventor/details/SoLineDetail.h>
# include <Inventor/details/SoFaceDetail.h>
# include <Inventor/details/SoPointDetail.h>
#endif

#include <App/Document.h>
#include <App/DocumentObjectGroup.h>
#include <App/GeoFeatureGroupExtension.h>
#include <Gui/Control.h>
#include <Gui/Command.h>
#include <Gui/Application.h>
#include <Gui/MDIView.h>
#include <Gui/ViewProviderOrigin.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/BitmapFactory.h>
#include <Gui/InventorBase.h>

#include "ViewProviderDatum.h"

using namespace Gui;

PROPERTY_SOURCE_ABSTRACT(Gui::ViewProviderDatum, Gui::ViewProviderGeometryObject)

ViewProviderDatum::ViewProviderDatum()
{
    pShapeSep = new SoSeparator();
    pShapeSep->renderCaching = SoSeparator::OFF;
    pShapeSep->boundingBoxCaching = SoSeparator::OFF;
    pShapeSep->ref();
    pPickStyle = new SoPickStyle();
    pPickStyle->ref();

    DisplayMode.setStatus(App::Property::Hidden, true);

    // set default color for datums (golden yellow with 60% transparency)
    // The parameter is stored in 'PartDesign' for historical reason
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath (
            "User parameter:BaseApp/Preferences/Mod/PartDesign");
    unsigned long shcol = hGrp->GetUnsigned ( "DefaultDatumColor", 0xFFD70066 );

    App::Color col ( (uint32_t) shcol );
    ShapeColor.setValue ( col );

    Transparency.setValue (col.transparency() * 100);
}

ViewProviderDatum::~ViewProviderDatum()
{
    pShapeSep->unref();
    pPickStyle->unref();
}

void ViewProviderDatum::attach(App::DocumentObject *obj)
{
    ViewProviderGeometryObject::attach ( obj );

    // TODO remove this field (2015-09-08, Fat-Zer)
    App::DocumentObject* o = getObject();
    const char *typeName = o->getTypeId().getName();
    if (strstr(typeName, "Plane")) {
        datumType = QStringLiteral("Plane");
        datumText = QObject::tr("Plane");
    }
    else if (strstr(typeName, "Line")) {
        datumType = QStringLiteral("Line");
        datumText = QObject::tr("Line");
    }
    else if (strstr(typeName, "Point")) {
        datumType = QStringLiteral("Point");
        datumText = QObject::tr("Point");
    }
    else if (strstr(typeName, "CoordinateSystem")) {
        datumType = QStringLiteral("CoordinateSystem");
        datumText = QObject::tr("Coordinate System");
    }

    SoShapeHints* hints = new SoShapeHints();
    hints->shapeType.setValue(SoShapeHints::UNKNOWN_SHAPE_TYPE);
    hints->vertexOrdering.setValue(SoShapeHints::COUNTERCLOCKWISE);
    SoDrawStyle* fstyle = new SoDrawStyle();
    fstyle->style = SoDrawStyle::FILLED;
    fstyle->lineWidth = 3;
    fstyle->pointSize = 5;
    pPickStyle->style = SoPickStyle::SHAPE;
    SoMaterialBinding* matBinding = new SoMaterialBinding;
    matBinding->value = SoMaterialBinding::OVERALL;

    SoSeparator* sep = new SoSeparator();
    sep->renderCaching = SoSeparator::OFF;
    sep->boundingBoxCaching = SoSeparator::OFF;
    sep->addChild(hints);
    sep->addChild(fstyle);
    sep->addChild(pPickStyle);
    sep->addChild(matBinding);
    sep->addChild(pcShapeMaterial);
    sep->addChild(pShapeSep);

    addDisplayMaskMode(sep, "Base");
}

bool ViewProviderDatum::onDelete(const std::vector<std::string> &)
{
    // TODO: Ask user what to do about dependent objects, e.g. Sketches that have this feature as their support
    // 1. Delete
    // 2. Suppress
    // 3. Re-route

    return true;
}

std::vector<std::string> ViewProviderDatum::getDisplayModes(void) const
{
    return { "Base" };
}

void ViewProviderDatum::setDisplayMode(const char* ModeName)
{
    if (strcmp(ModeName, "Base") == 0)
        setDisplayMaskMode("Base");
    ViewProviderGeometryObject::setDisplayMode(ModeName);
}

std::string ViewProviderDatum::getElement(const SoDetail* detail) const
{
    if (detail) {
        int element = 1;

        if (detail->getTypeId() == SoLineDetail::getClassTypeId()) {
            const SoLineDetail* line_detail = static_cast<const SoLineDetail*>(detail);
            element = line_detail->getLineIndex();
        } else if (detail->getTypeId() == SoFaceDetail::getClassTypeId()) {
            const SoFaceDetail* face_detail = static_cast<const SoFaceDetail*>(detail);
            element = face_detail->getPartIndex();
        } else if (detail->getTypeId() == SoPointDetail::getClassTypeId()) {
            const SoPointDetail* point_detail = static_cast<const SoPointDetail*>(detail);
            element = point_detail->getCoordinateIndex();
        }

        if (element == 0)
            return datumType.toStdString();
    }

    return std::string("");
}

SoDetail* ViewProviderDatum::getDetail(const char* subelement) const
{
    QString subelem = QString::fromUtf8(subelement);

    if (subelem == QObject::tr("Line")) {
         SoLineDetail* detail = new SoLineDetail();
         detail->setPartIndex(0);
         return detail;
    } else if (subelem == QObject::tr("Plane")) {
        SoFaceDetail* detail = new SoFaceDetail();
        detail->setPartIndex(0);
        return detail;
   } else if (subelem == QObject::tr("Point")) {
        SoPointDetail* detail = new SoPointDetail();
        detail->setCoordinateIndex(0);
        return detail;
   }

    return NULL;
}

void ViewProviderDatum::updateExtents () {
    setExtents ( getRelevantBoundBox () );
}

void ViewProviderDatum::setExtents (const SbBox3f &bbox) {
    if(!Gui::isValidBBox(bbox))
        return;
    const SbVec3f & min = bbox.getMin ();
    const SbVec3f & max = bbox.getMax ();
    setExtents ( Base::BoundBox3d ( min.getValue()[0], min.getValue()[1], min.getValue()[2],
                                    max.getValue()[0], max.getValue()[1], max.getValue()[2] ) );
}

SbBox3f ViewProviderDatum::getRelevantBoundBox () const {
    std::vector<App::DocumentObject *> objs;

    // Probe body first
    auto group = App::GeoFeatureGroupExtension::getGroupOfObject(this->getObject());
    if (group) {
        if (auto ext = group->getExtensionByType<App::GeoFeatureGroupExtension>())
            objs = ext->getFullModel();
    } else {
        // Probe if we belongs to some group
        App::DocumentObject* group =  App::DocumentObjectGroup::getGroupOfObject ( this->getObject () );

        if(group) {
            auto* ext = group->getExtensionByType<App::GroupExtension>();
            if(ext)
                objs = ext->getFullModel ();
        } else {
            // Fallback to whole document
            objs = this->getObject ()->getDocument ()->getObjects ();
            for(auto it=objs.begin();it!=objs.end();) {
                auto obj = *it;
                if(!obj->Visibility.getValue()
                        || App::GeoFeatureGroupExtension::getGroupOfObject(obj))
                {
                    it = objs.erase(it);
                    continue;
                }
                auto vp = Gui::Application::Instance->getViewProvider(obj);
                if(!vp || !vp->canAddToSceneGraph()) {
                    it = objs.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }
    return getRelevantBoundBox (objs);
}

SbBox3f ViewProviderDatum::getRelevantBoundBox (
        const std::vector <App::DocumentObject *> &objs )
{
    Base::BoundBox3d bbox;

    // Adds the bbox of given feature to the output
    for (auto obj :objs) {
        if(!obj || !obj->getNameInDocument())
            continue;

        ViewProvider *vp = Gui::Application::Instance->getViewProvider(obj);
        if (!vp) { continue; }
        if (!vp->isVisible ()) { continue; }

        if (auto vpDatum = Base::freecad_dynamic_cast<ViewProviderDatum>(vp)) {
            // Treat datums only as their basepoint
            bbox.Add(vpDatum->getBasePoint());
        } else {
            bbox.Add(vp->getBoundingBox());
        }
    }

    if(!bbox.IsValid())
        return SbBox3f();

    return SbBox3f(bbox.MinX,bbox.MinY,bbox.MinZ,bbox.MaxX,bbox.MaxY,bbox.MaxZ);
}

SbBox3f ViewProviderDatum::defaultBoundBox () {
    float s = defaultSize();
    return SbBox3f (-s, -s, -s, s, s, s);
}

double ViewProviderDatum::defaultSize() {
    return Gui::ViewProviderOrigin::defaultSize();
}

bool ViewProviderDatum::isPickable() {

    return bool(pPickStyle->style.getValue() == SoPickStyle::SHAPE);
}

void ViewProviderDatum::setPickable(bool val) {

    if(val)
        pPickStyle->style = SoPickStyle::SHAPE;
    else
        pPickStyle->style = SoPickStyle::UNPICKABLE;
}
