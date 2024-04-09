/***************************************************************************
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
 *   Copyright (c) 2016 Stefan Tröger <stefantroeger@gmx.net>              *
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
#include <memory>

#ifndef _PreComp_
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <QTimer>
#endif

#include <App/Document.h>
#include <App/DocumentObserver.h>
#include <App/Origin.h>
#include <App/OriginGroupExtension.h>
#include <Base/Console.h>

#include "ViewProviderOriginGroupExtension.h"
#include "ViewProviderDatum.h"
#include "Application.h"
#include "Document.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderOrigin.h"
#include "ViewProviderOriginFeature.h"


using namespace Gui;
namespace sp = std::placeholders;

EXTENSION_PROPERTY_SOURCE(Gui::ViewProviderOriginGroupExtension, Gui::ViewProviderGeoFeatureGroupExtension)

class ViewProviderOriginGroupExtension::Private
    : public std::enable_shared_from_this<ViewProviderOriginGroupExtension::Private>
{
public:
    Private()
    {
        timer.setSingleShot(true);

        QObject::connect(&timer, &QTimer::timeout, [this](){
            if (App::Document::isAnyRestoring()
                    || App::Document::isAnyRecomputing()) {
                timer.start(300);
                return;
            }
            for (auto obj : objs) {
                if (auto vp = Application::Instance->getViewProvider(obj)) {
                    if (auto ext = vp->getExtensionByType<ViewProviderOriginGroupExtension>(true))
                        ext->updateOriginSize();
                }
            }
        });
    }

    void schedule(ViewProviderDocumentObject *vp)
    {
        objs.insert(vp->getObject());
        timer.start(300);
    }

    static std::shared_ptr<ViewProviderOriginGroupExtension::Private> instance()
    {
        static std::weak_ptr<ViewProviderOriginGroupExtension::Private> self;
        auto res = self.lock();
        if (!res) {
            res = std::make_shared<ViewProviderOriginGroupExtension::Private>();
            self = res;
        }
        return res;
    };

    std::set<App::DocumentObject*> objs;
    QTimer timer;
};

ViewProviderOriginGroupExtension::ViewProviderOriginGroupExtension()
{
    pimpl = Private::instance();
    initExtensionType(ViewProviderOriginGroupExtension::getExtensionClassTypeId());
}

ViewProviderOriginGroupExtension::~ViewProviderOriginGroupExtension()
{
}

void ViewProviderOriginGroupExtension::constructChildren (
        std::vector<App::DocumentObject*> &children ) const
{
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::OriginGroupExtension>();
    if(!group)
        return;

    App::DocumentObject *originObj = group->Origin.getValue();

    // Origin must be first
    if (originObj) {
        children.insert(children.begin(),originObj);
    }
}

void ViewProviderOriginGroupExtension::extensionClaimChildren (std::vector<App::DocumentObject *> &children) const {
    ViewProviderGeoFeatureGroupExtension::extensionClaimChildren (children);
    constructChildren ( children );
}

void ViewProviderOriginGroupExtension::extensionClaimChildren3D (std::vector<App::DocumentObject *> &children) const {
    ViewProviderGeoFeatureGroupExtension::extensionClaimChildren3D (children);
    constructChildren ( children );
}

void ViewProviderOriginGroupExtension::extensionAttach(App::DocumentObject *pcObject) {
    ViewProviderGeoFeatureGroupExtension::extensionAttach ( pcObject );
}

void ViewProviderOriginGroupExtension::extensionUpdateData( const App::Property* prop ) {
    auto propName = prop->getName();
    if(propName && (strcmp(propName,"_GroupTouched")==0
                || strcmp(propName,"Group")==0
                || strcmp(propName,"Shape")==0))
    {
        auto owner = getExtendedViewProvider()->getObject();
        if(!App::GetApplication().isRestoring()
                && owner
                && owner->getDocument()
                && !owner->testStatus(App::ObjectStatus::Remove)
                && !owner->getDocument()->isPerformingTransaction())
        {
            pimpl->schedule(getExtendedViewProvider());
        }
    }

    ViewProviderGeoFeatureGroupExtension::extensionUpdateData ( prop );
}

void ViewProviderOriginGroupExtension::updateOriginSize () {
    auto owner = getExtendedViewProvider()->getObject();
    if(!owner || !owner->isAttachedToDocument()
              || owner->testStatus(App::ObjectStatus::Remove)
              || owner->getDocument()->isPerformingTransaction())
        return;

    auto* group = owner->getExtensionByType<App::OriginGroupExtension>();
    if(!group)
        return;

    const auto & model = group->getFullModel ();

    // BBox for Datums is calculated from all visible objects but treating datums as their basepoints only
    SbBox3f bboxDatums = ViewProviderDatum::getRelevantBoundBox ( model );
    // BBox for origin should take into account datums size also
    SbBox3f bboxOrigins(0,0,0,0,0,0);
    bool isDatumEmpty = bboxDatums.isEmpty();
    if(!isDatumEmpty)
        bboxOrigins.extendBy(bboxDatums);

    for(App::DocumentObject* obj : model) {
        if (auto vp = Base::freecad_dynamic_cast<ViewProviderDatum>(
                                        Gui::Application::Instance->getViewProvider(obj)))
        {
            if (!vp || !vp->isVisible()) { continue; }

            if(!isDatumEmpty)
                vp->setExtents ( bboxDatums );

            // Why is the following necessary?
            //
            // if(App::GroupExtension::getGroupOfObject(obj))
            //     continue;

            auto bbox = vp->getBoundingBox();
            if(bbox.IsValid())
                bboxOrigins.extendBy ( SbBox3f(bbox.MinX,bbox.MinY,bbox.MinZ,bbox.MaxX,bbox.MaxY,bbox.MaxZ) );
        }
    }

    // get the bounding box values
    SbVec3f max = bboxOrigins.getMax();
    SbVec3f min = bboxOrigins.getMin();

    // obtain an Origin and it's ViewProvider
    Gui::ViewProviderOrigin* vpOrigin = 0;
    try {
        vpOrigin = Base::freecad_dynamic_cast<ViewProviderOrigin>(
                Application::Instance->getViewProvider(group->getOrigin()));
    } catch (const Base::Exception &e) {
        e.ReportException();
        return;
    }

    // calculate the desired origin size
    Base::Vector3d size;

    for (uint_fast8_t i=0; i<3; i++) {
        size[i] = std::max ( fabs ( max[i] ), fabs ( min[i] ) );
        if (min[i]>max[i] || size[i] < 1e-7 ) {
            size[i] = Gui::ViewProviderOrigin::defaultSize();
        }
    }

    vpOrigin->Size.setValue ( size );
}

bool ViewProviderOriginGroupExtension::extensionCanDragObject(App::DocumentObject *obj) const
{
    if(obj->isDerivedFrom(App::Origin::getClassTypeId()))
        return false;
    return ViewProviderGeoFeatureGroupExtension::extensionCanDragObject(obj);
}

namespace Gui {
EXTENSION_PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderOriginGroupExtensionPython, Gui::ViewProviderOriginGroupExtension)

// explicit template instantiation
template class GuiExport ViewProviderExtensionPythonT<ViewProviderOriginGroupExtension>;
}
