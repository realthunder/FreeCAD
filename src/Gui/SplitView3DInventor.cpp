/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QSplitter>
# include <Inventor/nodes/SoDirectionalLight.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
#endif

#include <Base/Builder3D.h>
#include <Base/Interpreter.h>

#include "SplitView3DInventor.h"

#include "AbstractSplitViewPy.h"
#include "Application.h"
#include "Camera.h"
#include "Document.h"
#include "NavigationStyle.h"
#include "SoFCSelectionAction.h"
#include "View3DInventorViewer.h"
#include "View3DInventorPy.h"
#include "View3DSettings.h"


using namespace Gui;

TYPESYSTEM_SOURCE_ABSTRACT(Gui::AbstractSplitView,Gui::MDIView)

AbstractSplitView::AbstractSplitView(Gui::Document* pcDocument, QWidget* parent, Qt::WindowFlags wflags)
  : MDIView(pcDocument,parent, wflags)
{
    _viewerPy = nullptr;
    // important for highlighting
    setMouseTracking(true);
}

AbstractSplitView::~AbstractSplitView()
{
    for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
        delete *it;
    }
    if (_viewerPy) {
        Base::PyGILStateLocker lock;
        Py_DECREF(_viewerPy);
    }
}

void AbstractSplitView::deleteSelf()
{
    for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
        (*it)->setSceneGraph(nullptr);
    }
    MDIView::deleteSelf();
}

void AbstractSplitView::setDocumentOfViewers(Gui::Document* document)
{
    for (auto view : _viewer) {
        view->setDocument(document);
    }
}

void AbstractSplitView::viewAll()
{
    for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it)
        (*it)->viewAll();
}

bool AbstractSplitView::containsViewProvider(const ViewProvider* vp) const
{
    for (auto it = _viewer.begin(); it != _viewer.end(); ++it) {
        if ((*it)->containsViewProvider(vp))
            return true;
    }

    return false;
}

void AbstractSplitView::setupSettings()
{
    viewSettings = std::make_unique<View3DSettings>(App::GetApplication().GetParameterGroupByPath
                                   ("User parameter:BaseApp/Preferences/View"), _viewer);
    // tmp. disabled will be activated after redesign of 3d viewer
    // check whether the simple or the Full Mouse model is used
    viewSettings->ignoreNavigationStyle = true;
    // Disable VBO for split screen as this leads to random crashes
    viewSettings->ignoreVBO = true;
    viewSettings->ignoreTransparent = true;
    viewSettings->ignoreRenderCache = true;
    viewSettings->ignoreDimensions = true;
    viewSettings->applySettings();
}

View3DInventorViewer* AbstractSplitView::getViewer(unsigned int n) const
{
    return (_viewer.size() > n ? _viewer[n] : nullptr);
}

void AbstractSplitView::onUpdate()
{
    update();
}

const char *AbstractSplitView::getName() const
{
    return "SplitView3DInventor";
}

bool AbstractSplitView::onMsg(const char* pMsg, const char**)
{
    if (strcmp("ViewFit",pMsg) == 0 ) {
        viewAll();
        return true;
    }
    else if (strcmp("ViewBottom",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Bottom));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }
    else if (strcmp("ViewFront",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Front));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }
    else if (strcmp("ViewLeft",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Left));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }
    else if (strcmp("ViewRear",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Rear));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }
    else if (strcmp("ViewRight",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Right));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }
    else if (strcmp("ViewTop",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Top));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }
    else if (strcmp("ViewAxo",pMsg) == 0) {
        SbRotation rot(Camera::rotation(Camera::Isometric));
        for (std::vector<View3DInventorViewer*>::iterator it = _viewer.begin(); it != _viewer.end(); ++it) {
            SoCamera* cam = (*it)->getSoRenderManager()->getCamera();
            cam->orientation.setValue(rot);
            (*it)->viewAll();
        }
        return true;
    }

    return false;
}

bool AbstractSplitView::onHasMsg(const char* pMsg) const
{
    if (strcmp("CanPan",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewFit",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewBottom",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewFront",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewLeft",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewRear",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewRight",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewTop",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewAxo",pMsg) == 0) {
        return true;
    }
    return false;
}

void AbstractSplitView::setOverrideCursor(const QCursor& aCursor)
{
    Q_UNUSED(aCursor);
    //_viewer->getWidget()->setCursor(aCursor);
}

PyObject *AbstractSplitView::getPyObject()
{
    if (!_viewerPy)
        _viewerPy = new AbstractSplitViewPy(this);
    Py_INCREF(_viewerPy);
    return _viewerPy;
}

void AbstractSplitView::setPyObject(PyObject *)
{
    throw Base::AttributeError("Attribute is read-only");
}

int AbstractSplitView::getSize()
{
    return static_cast<int>(_viewer.size());
}

// ------------------------------------------------------

TYPESYSTEM_SOURCE_ABSTRACT(Gui::SplitView3DInventor, Gui::AbstractSplitView)

SplitView3DInventor::SplitView3DInventor(int views, Gui::Document* pcDocument, QWidget* parent, Qt::WindowFlags wflags)
  : AbstractSplitView(pcDocument,parent, wflags)
{
    //anti-aliasing settings
    bool smoothing = false;
    bool glformat = false;
    int samples = View3DInventorViewer::getNumSamples();
    QtGLFormat f;

    if (samples > 1) {
        glformat = true;
        f.setSamples(samples);
    }
    else if (samples > 0) {
        smoothing = true;
    }

    // minimal 2 views
    while (views < 2)
        views ++;

    QSplitter* mainSplitter = nullptr;

    // if views < 3 show them as a row
    if (views <= 3) {
        mainSplitter = new QSplitter(Qt::Horizontal, this);
        for (int i=0; i < views; i++) {
            if (glformat)
                _viewer.push_back(new View3DInventorViewer(f, mainSplitter));
            else
                _viewer.push_back(new View3DInventorViewer(mainSplitter));
        }
    }
    else {
        mainSplitter = new QSplitter(Qt::Vertical, this);
        auto topSplitter = new QSplitter(Qt::Horizontal, mainSplitter);
        auto botSplitter = new QSplitter(Qt::Horizontal, mainSplitter);

        if (glformat) {
            _viewer.push_back(new View3DInventorViewer(f, topSplitter));
            _viewer.push_back(new View3DInventorViewer(f, topSplitter));
        }
        else {
            _viewer.push_back(new View3DInventorViewer(topSplitter));
            _viewer.push_back(new View3DInventorViewer(topSplitter));
        }

        for (int i=2;i<views;i++) {
            if (glformat)
                _viewer.push_back(new View3DInventorViewer(f, botSplitter));
            else
                _viewer.push_back(new View3DInventorViewer(botSplitter));
        }

        topSplitter->setOpaqueResize(true);
        botSplitter->setOpaqueResize(true);
    }

    if (smoothing) {
        for (std::vector<int>::size_type i = 0; i != _viewer.size(); i++)
            _viewer[i]->getSoRenderManager()->getGLRenderAction()->setSmoothing(true);
    }

    mainSplitter->show();
    setCentralWidget(mainSplitter);

    setDocumentOfViewers(pcDocument);

    // apply the user settings
    setupSettings();
}

SplitView3DInventor::~SplitView3DInventor() = default;

#include "moc_SplitView3DInventor.cpp"
