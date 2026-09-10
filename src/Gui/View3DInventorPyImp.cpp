/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include "PreCompiled.h"

#ifndef __InventorAll__
# include "InventorAll.h"
# include <set>
# include <sstream>
# include <QColor>
# include <QDir>
# include <QFileInfo>
# include <QImage>
# include <Inventor/SbViewVolume.h>
# include <Inventor/nodes/SoCamera.h>
#endif

#include <QtOpenGL.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "Camera.h"
#include "Renderer/CyclesRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneServer.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderExtern.h"
#include "Application.h"
#include "Document.h"
#include "NavigationStyle.h"
#include "SoMouseWheelEvent.h"
#include "SoFCSelectionAction.h"
#include "SoFCOffscreenRenderer.h"
#include "SoFCVectorizeSVGAction.h"
#include "SoFCVectorizeU3DAction.h"
#include "SoFCDB.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "View3DViewerPy.h"
#include "ActiveObjectList.h"
#include "ViewParams.h"
#include "WidgetFactory.h"
#include "PythonWrapper.h"


#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/PlacementPy.h>
#include <Base/Rotation.h>
#include <Base/RotationPy.h>
#include <Base/VectorPy.h>
#include <Base/GeometryPyCXX.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectPy.h>
#include <App/GeoFeature.h>
#include <CXX/Objects.hxx>

#include <Gui/View3DInventorPy.h>
#include <Gui/View3DInventorPy.cpp>

using namespace Gui;

std::string View3DInventorPy::representation(void) const
{
    return "<View3DInventor>";
}

int View3DInventorPy::initialization()
{
    return 1;
}

int View3DInventorPy::finalization() {
    for (std::list<PyObject*>::iterator it = callbacks.begin(); it != callbacks.end(); ++it)
        Py_DECREF(*it);
    return 1;
}


PyObject *View3DInventorPy::getCustomAttributes(const char* attr) const
{
    // see if an active object has the same name
    try {
        if (auto obj = getView3DInventorPtr()->getActiveObject<App::DocumentObject*>(attr))
            return obj->getPyObject();
        return nullptr;
    } PY_CATCH
}

int View3DInventorPy::setCustomAttributes(const char* /*attr*/, PyObject * /*obj*/)
{
    return 0;
}

PyObject* View3DInventorPy::fitAll(PyObject *args)
{
    double factor = 1.0;
    if (!PyArg_ParseTuple(args, "|d", &factor))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->viewAll((float)factor);
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::boxZoom(PyObject *args, PyObject *kwds)
{
    static char* kwds_box[] = {"XMin", "YMin", "XMax", "YMax", NULL};
    short xmin, ymin, xmax, ymax;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "hhhh", kwds_box,
                                     &xmin, &ymin, &xmax, &ymax))
        return nullptr;

    try {
        SbBox2s box(xmin, ymin, xmax, ymax);
        getView3DInventorPtr()->getViewer()->boxZoom(box);
    } PY_CATCH
    Py_Return;
}

/**
 Formulas to get quaternion for axonometric views:

 \code
from math import sqrt, degrees, asin, atan
p1=App.Rotation(App.Vector(1,0,0),90)
p2=App.Rotation(App.Vector(0,0,1),alpha)
p3=App.Rotation(p2.multVec(App.Vector(1,0,0)),beta)
p4=p3.multiply(p2).multiply(p1)

from pivy import coin
c=Gui.ActiveDocument.ActiveView.getCameraNode()
c.orientation.setValue(*p4.Q)
 \endcode

 The angles alpha and beta depend on the type of axonometry
 Isometric:
 \code
alpha=45
beta=degrees(asin(-sqrt(1.0/3.0)))
 \endcode

 Dimetric:
 \code
alpha=degrees(asin(sqrt(1.0/8.0)))
beta=degrees(-asin(1.0/3.0))
 \endcode

 Trimetric:
 \code
alpha=30.0
beta=-35.0
 \endcode

 Verification code that the axonomtries are correct:

 \code
from pivy import coin
c=Gui.ActiveDocument.ActiveView.getCameraNode()
vo=App.Vector(c.getViewVolume().getMatrix().multVecMatrix(coin.SbVec3f(0,0,0)).getValue())
vx=App.Vector(c.getViewVolume().getMatrix().multVecMatrix(coin.SbVec3f(10,0,0)).getValue())
vy=App.Vector(c.getViewVolume().getMatrix().multVecMatrix(coin.SbVec3f(0,10,0)).getValue())
vz=App.Vector(c.getViewVolume().getMatrix().multVecMatrix(coin.SbVec3f(0,0,10)).getValue())
(vx-vo).Length
(vy-vo).Length
(vz-vo).Length

# Projection
vo.z=0
vx.z=0
vy.z=0
vz.z=0

(vx-vo).Length
(vy-vo).Length
(vz-vo).Length
 \endcode

 See also:
 http://www.mathematik.uni-marburg.de/~thormae/lectures/graphics1/graphics_6_2_ger_web.html#1
 http://www.mathematik.uni-marburg.de/~thormae/lectures/graphics1/code_v2/Axonometric/qt/Axonometric.cpp
 https://de.wikipedia.org/wiki/Arkussinus_und_Arkuskosinus
*/

PyObject* View3DInventorPy::viewBottom(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Bottom));
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::viewFront(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Front));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewLeft(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Left));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewRear(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Rear));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewRight(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Right));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewTop(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Top));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewIsometric(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Isometric));
    } PY_CATCH

    Py_Return;
}
PyObject* View3DInventorPy::viewAxonometric(PyObject *args)
{
    return viewIsometric(args);
}

PyObject* View3DInventorPy::viewAxometric(PyObject *args)
{
    return viewIsometric(args);
}

PyObject* View3DInventorPy::viewDimetric(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Dimetric));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewTrimetric(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->setCameraOrientation(Camera::rotation(Camera::Trimetric));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewDefaultOrientation(PyObject *args)
{
    char* view = nullptr;
    double scale = -1.0;
    if (!PyArg_ParseTuple(args, "|sd", &view, &scale))
        return nullptr;

    try {
        std::string newDocView;
        SbRotation rot(0,0,0,1);
        if (view) {
            newDocView = view;
        }
        else {
            ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
            newDocView = hGrp->GetASCII("NewDocumentCameraOrientation", "Trimetric");
        }

        if (newDocView == "Top") {
            rot = Camera::rotation(Camera::Top);
        }
        else if (newDocView == "Bottom") {
            rot = Camera::rotation(Camera::Bottom);
        }
        else if (newDocView == "Front") {
            rot = Camera::rotation(Camera::Front);
        }
        else if (newDocView == "Rear") {
            rot = Camera::rotation(Camera::Rear);
        }
        else if (newDocView == "Left") {
            rot = Camera::rotation(Camera::Left);
        }
        else if (newDocView == "Right") {
            rot = Camera::rotation(Camera::Right);
        }
        else if (newDocView == "Isometric") {
            rot = Camera::rotation(Camera::Isometric);
        }
        else if (newDocView == "Dimetric") {
            rot = Camera::rotation(Camera::Dimetric);
        }
        else if (newDocView == "Trimetric") {
            rot = Camera::rotation(Camera::Trimetric);
        }
        else if (newDocView == "Custom") {
            ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View/Custom");
            float q0 = static_cast<float>(hGrp->GetFloat("Q0", 0));
            float q1 = static_cast<float>(hGrp->GetFloat("Q1", 0));
            float q2 = static_cast<float>(hGrp->GetFloat("Q2", 0));
            float q3 = static_cast<float>(hGrp->GetFloat("Q3", 1));
            rot.setValue(q0, q1, q2, q3);
        }

        SoCamera* cam = getView3DInventorPtr()->getViewer()->getCamera();
        cam->orientation = rot;

        if (scale < 0.0){
            scale = ViewParams::getNewDocumentCameraScale();
        }
        if (scale > 1e-7) {
            double f = 0.0; //focal dist
            if (cam->isOfType(SoOrthographicCamera::getClassTypeId())){
                static_cast<SoOrthographicCamera*>(cam)->height = scale;
                f = scale;
            } else if (cam->isOfType(SoPerspectiveCamera::getClassTypeId())){
                //nothing to do
                double ang = static_cast<SoPerspectiveCamera*>(cam)->heightAngle.getValue();
                f = 0.5 * scale / sin(ang * 0.5);
            }
            SbVec3f lookDir;
            rot.multVec(SbVec3f(0,0,-1), lookDir);
            SbVec3f pos = lookDir * -f;
            cam->focalDistance = f;
            cam->position = pos;
        }
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewRotateLeft(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
      SoCamera* cam = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
      SbRotation rot = cam->orientation.getValue();
      SbVec3f vdir(0, 0, -1);
      rot.multVec(vdir, vdir);
      SbRotation nrot(vdir, (float)M_PI/2);
      cam->orientation.setValue(rot*nrot);
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::viewRotateRight(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
      SoCamera* cam = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
      SbRotation rot = cam->orientation.getValue();
      SbVec3f vdir(0, 0, -1);
      rot.multVec(vdir, vdir);
      SbRotation nrot(vdir, (float)-M_PI/2);
      cam->orientation.setValue(rot*nrot);
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::zoomIn(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->navigationStyle()->zoomIn();
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::zoomOut(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        getView3DInventorPtr()->getViewer()->navigationStyle()->zoomOut();
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::setCameraOrientation(PyObject *args)
{
    PyObject* o;
    PyObject* m=Py_False;
    if (!PyArg_ParseTuple(args, "O|O!", &o, &PyBool_Type, &m))
        return nullptr;

    try {
        if (PyTuple_Check(o)) {
            Py::Tuple tuple(o);
            float q0 = (float)Py::Float(tuple[0]);
            float q1 = (float)Py::Float(tuple[1]);
            float q2 = (float)Py::Float(tuple[2]);
            float q3 = (float)Py::Float(tuple[3]);
            getView3DInventorPtr()->getViewer()->setCameraOrientation(SbRotation(q0, q1, q2, q3), PyObject_IsTrue(m));
        }
        else if (PyObject_TypeCheck(o, &Base::RotationPy::Type)) {
            Base::Rotation r = (Base::Rotation)Py::Rotation(o,false);
            double q0, q1, q2, q3;
            r.getValue(q0, q1, q2, q3);
            getView3DInventorPtr()->getViewer()->setCameraOrientation(SbRotation((float)q0, (float)q1, (float)q2, (float)q3), PyObject_IsTrue(m));
        }
        else {
            throw Py::ValueError("Neither tuple nor rotation object");
        }
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::getCameraOrientation(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        SbRotation rot = getView3DInventorPtr()->getViewer()->getCameraOrientation();
        float q0,q1,q2,q3;
        rot.getValue(q0,q1,q2,q3);
        return Py::new_reference_to(Py::Rotation(Base::Rotation(q0,q1,q2,q3)));
    } PY_CATCH
}

PyObject* View3DInventorPy::viewPosition(PyObject *args)
{
    PyObject* p=0;
    int ms = 30;
    if (!PyArg_ParseTuple(args, "|O!i",&Base::PlacementPy::Type,&p,&ms))
        return nullptr;

    try {
        if (p) {
            Base::Placement* plm = static_cast<Base::PlacementPy*>(p)->getPlacementPtr();
            Base::Rotation rot = plm->getRotation();
            Base::Vector3d pos = plm->getPosition();
            double q0,q1,q2,q3;
            rot.getValue(q0,q1,q2,q3);
            getView3DInventorPtr()->getViewer()->moveCameraTo(
                SbRotation((float)q0, (float)q1, (float)q2, (float)q3),
                SbVec3f((float)pos.x, (float)pos.y, (float)pos.z), ms);
        }

        SoCamera* cam = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
        if (!cam) Py_Return;

        SbRotation rot = cam->orientation.getValue();
        SbVec3f pos = cam->position.getValue();
        float q0,q1,q2,q3;
        rot.getValue(q0,q1,q2,q3);
        Base::Placement plm(
            Base::Vector3d(pos[0], pos[1], pos[2]),
            Base::Rotation(q0, q1, q2, q3));
        return Py::new_reference_to(Py::Placement(plm));
    } PY_CATCH
}

PyObject* View3DInventorPy::startAnimating(PyObject *args)
{
    float x,y,z;
    float velocity;
    if (!PyArg_ParseTuple(args, "ffff", &x,&y,&z,&velocity))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->startSpinningAnimation(SbVec3f(x,y,z),velocity);
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::stopAnimating(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->stopAnimating();
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::setAnimationEnabled(PyObject *args)
{
    int ok;
    if (!PyArg_ParseTuple(args, "i", &ok))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->setAnimationEnabled(ok!=0);
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::isAnimationEnabled(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        SbBool ok = getView3DInventorPtr()->getViewer()->isAnimationEnabled();
        return Py::new_reference_to(Py::Boolean(ok ? true : false));
    } PY_CATCH
}

PyObject* View3DInventorPy::saveImage(PyObject *args)
{
    char *cFileName,*cColor="Current",*cComment="$MIBA";
    int w=-1,h=-1;
    int s=View3DInventorViewer::getNumSamples();
    int wait=1;

    if (!PyArg_ParseTuple(args, "et|iissii","utf-8",&cFileName,&w,&h,&cColor,&cComment,&s,&wait))
        return nullptr;

    try {
        std::string encodedName = std::string(cFileName);
        PyMem_Free(cFileName);
        QFileInfo fi(QString::fromUtf8(encodedName.c_str()));

        if (!fi.absoluteDir().exists())
            throw Py::RuntimeError("Directory where to save image doesn't exist");

        QColor bg;
        QString colname = QString::fromUtf8(cColor);
        if (colname.compare(QStringLiteral("Current"), Qt::CaseInsensitive) == 0)
            bg = QColor(); // assign an invalid color here
        else
            bg.setNamedColor(colname);

        QImage img;
        getView3DInventorPtr()->getViewer()->savePicture(w, h, s, bg, img, wait != 0);

        SoFCOffscreenRenderer& renderer = SoFCOffscreenRenderer::instance();
        SoCamera* cam = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
        renderer.writeToImageFile(encodedName.c_str(), cComment, cam->getViewVolume().getMatrix(), img);
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::saveVectorGraphic(PyObject *args)
{
    char* filename;
    int ps=4;
    char* name="white";

    if (!PyArg_ParseTuple(args, "s|is",&filename,&ps,&name))
        return nullptr;

    try {
        std::unique_ptr<SoVectorizeAction> vo;
        Base::FileInfo fi(filename);
        if (fi.hasExtension("ps") || fi.hasExtension("eps")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoVectorizePSAction());
            //vo->setGouraudThreshold(0.0f);
        }
        else if (fi.hasExtension("svg")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoFCVectorizeSVGAction());
        }
        else if (fi.hasExtension("idtf")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoFCVectorizeU3DAction());
        }
        else {
            throw Py::RuntimeError("Not supported vector graphic");
        }

        SoVectorOutput * out = vo->getOutput();
        if (!out || !out->openFile(filename)) {
            std::ostringstream a_out;
            a_out << "Cannot open file '" << filename << "'";
            throw Py::RuntimeError(a_out.str());
        }

        QColor bg;
        QString colname = QString::fromUtf8(name);
        if (colname.compare(QStringLiteral("Current"), Qt::CaseInsensitive) == 0)
            bg = getView3DInventorPtr()->getViewer()->backgroundColor();
        else
            bg.setNamedColor(colname);

        getView3DInventorPtr()->getViewer()->saveGraphic(ps,bg,vo.get());
        out->closeFile();
    } PY_CATCH

    Py_Return;
}

/// JSON value of a view property for the capture sidecar: native for
/// bool/int/float/string properties, str() otherwise.
static QJsonValue propertyJsonValue(App::Property *prop)
{
    Py::Object val(prop->getPyObject(), true);
    if (PyBool_Check(val.ptr()))
        return QJsonValue(val.isTrue());
    if (PyLong_Check(val.ptr()))
        return QJsonValue(double(PyLong_AsLongLong(val.ptr())));
    if (PyFloat_Check(val.ptr()))
        return QJsonValue(PyFloat_AsDouble(val.ptr()));
    Py::Object str(PyObject_Str(val.ptr()), true);
    return QJsonValue(QString::fromUtf8(
                Py::String(str).as_std_string("utf-8").c_str()));
}

/// The reproducibility sidecar of a render dump (docs/RenderDebug.md
/// §4.2): everything needed to re-stage the captured frame — camera,
/// viewport, backend identity and the active render/debug view
/// properties — written as <imagePath>.json.
static void writeRenderDumpSidecar(View3DInventor *view,
                                   const std::string &imagePath,
                                   const char *source, int mode,
                                   const Render::RenderStats *stats)
{
    View3DInventorViewer *viewer = view->getViewer();
    QJsonObject root;

    QJsonObject capture;
    // The BASENAME, not the path it was written to. A golden's
    // sidecar is a shared artefact in a public repository, and the
    // blessing box's own absolute path is both meaningless there and
    // a trap of a shape this tree has already paid for once:
    // Render_PBREnvImage carried a blessing box's absolute path into
    // a golden, and render_verify's relocate() exists to defend the
    // properties against exactly that. Nothing reads this field back
    // -- the verifier derives the image name from the sidecar's own
    // -- so it is descriptive, and a description should not name a
    // directory that exists on one machine.
    capture[QStringLiteral("file")] =
        QFileInfo(QString::fromUtf8(imagePath.c_str())).fileName();
    capture[QStringLiteral("source")] = QString::fromUtf8(source);
    if (mode >= 0)
        capture[QStringLiteral("mode")] = mode;
    capture[QStringLiteral("time")] =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("capture")] = capture;

    // The full Inventor camera definition — setCamera() re-stages it.
    SoOutput out;
    char buffer[2048];
    out.setBuffer(buffer, sizeof(buffer), nullptr);
    SoWriteAction wa(&out);
    if (SoCamera *cam = viewer->getSoRenderManager()->getCamera()) {
        wa.apply(cam);
        root[QStringLiteral("camera")] = QString::fromUtf8(buffer);
    }

    auto glWidget = viewer->getGLWidget();
    if (glWidget) {
        QJsonArray vp;
        vp.append(glWidget->width());
        vp.append(glWidget->height());
        root[QStringLiteral("viewportSize")] = vp;
        root[QStringLiteral("devicePixelRatio")] =
            glWidget->devicePixelRatioF();
    }
    if (auto renderer = viewer->getExternalRenderer()) {
        root[QStringLiteral("backend")] =
            QString::fromUtf8(renderer->type().c_str());
        // Which DEVICE drew it, not just which backend was selected.
        // "bgfx - OpenGL" is what a software rasterizer and a real
        // adapter both report, and a reference image blessed on one
        // device does not compare against another -- so without this a
        // capture carries no way to tell afterwards which it was.
        const std::string device = renderer->deviceName();
        if (!device.empty())
            root[QStringLiteral("device")] =
                QString::fromUtf8(device.c_str());
    }
    root[QStringLiteral("msaa")] = View3DInventorViewer::getNumSamples();

    const auto &config = App::Application::Config();
    auto cfg = [&config](const char *key) {
        auto it = config.find(key);
        return it != config.end()
            ? QString::fromUtf8(it->second.c_str()) : QString();
    };
    QJsonObject build;
    build[QStringLiteral("version")] = cfg("BuildVersionMajor")
        + QStringLiteral(".") + cfg("BuildVersionMinor");
    build[QStringLiteral("revision")] = cfg("BuildRevision");
    root[QStringLiteral("build")] = build;

    if (stats && stats->valid) {
        QJsonObject st;
        st[QStringLiteral("width")] = stats->width;
        st[QStringLiteral("height")] = stats->height;
        st[QStringLiteral("geometryPixels")] =
            double(stats->geometryPixels);
        // Recorded in the sidecar so a blessing can be refused
        // rather than merely regretted: a non-zero count means the
        // shading produced NaN or infinity, and the capture holds
        // arbitrary pixels wherever it did.
        st[QStringLiteral("nonFiniteChannels")] =
            double(stats->nonFiniteChannels);
        QJsonArray avg;
        avg.append(stats->avgColor[0]);
        avg.append(stats->avgColor[1]);
        avg.append(stats->avgColor[2]);
        st[QStringLiteral("avgColor")] = avg;
        root[QStringLiteral("stats")] = st;
    }

    // Every render-engine view property, so the capture carries its
    // full staging state (the harness re-applies these 1:1).
    static const char *prefixes[] = {
        "Render_", "RenderShadow_", "RenderDebug_", "HiddenLine_"};
    QJsonObject props;
    std::map<std::string, App::Property*> propMap;
    view->getPropertyMap(propMap);
    for (const auto &v : propMap) {
        for (const char *prefix : prefixes) {
            if (v.first.compare(0, strlen(prefix), prefix) == 0) {
                props[QString::fromUtf8(v.first.c_str())] =
                    propertyJsonValue(v.second);
                break;
            }
        }
    }
    root[QStringLiteral("properties")] = props;

    // The parameters behind those properties, and the ones that never
    // become properties at all.
    //
    // A Render_* view property outranks its parameter -- _renderParam
    // materializes it once when the renderer is selected and owns it
    // from then on -- so the whole View/Render group is already above,
    // enumerations included (those are materialized by hand beside the
    // _renderParam calls, since the generic helper cannot install the
    // enum strings first).
    //
    // What is NOT: the viewer's own rig, which has no property form at
    // all -- the lights, the scene ambient, the background, the chrome
    // that adds pixels to a capture, and the render cache mode that
    // decides whether a backend draws the frame in the first place.
    // Nor a view that never selected a renderer, which has no Render_*
    // properties to record. Without these a sidecar can describe a
    // capture faithfully and still re-stage into a different picture.
    // The View/Render group goes out beside them because a parameter
    // read against its property is what says whether the property was
    // merely seeded from it or has since been overridden.
    //
    // What is recorded is what the config has SET: a parameter still
    // on its built-in default does not appear, because the default
    // lives in the reading code and not in the group. Restaging into a
    // fresh config (what the verification harness does) is therefore
    // exact; restaging into a config that has set a key this one left
    // alone is best-effort.
    // Keyed by TYPE, not flat. A parameter group is a set of typed
    // maps, and the types are not interchangeable: SetInt on a key
    // files it under Integer while GetUnsigned goes on reading the
    // untouched Unsigned entry (measured -- SetInt 287454020 then
    // GetUnsigned reads 0). JSON cannot tell an int from an unsigned
    // on its own, so a replayer guessing from the value would put
    // every colour below 0x80000000 in the wrong slot; the bucket
    // names the setter instead.
    auto dumpGroup = [](const ParameterGrp::handle &grp,
                        const std::set<std::string> *keep) {
        QJsonObject obj;
        auto want = [keep](const std::string &key) {
            return !keep || keep->count(key) != 0;
        };
        auto bucket = [&obj, &want](const char *type, auto &&values,
                                    auto &&convert) {
            QJsonObject sub;
            for (const auto &v : values) {
                if (want(v.first))
                    sub[QString::fromUtf8(v.first.c_str())] =
                        convert(v.second);
            }
            if (!sub.isEmpty())
                obj[QString::fromUtf8(type)] = sub;
        };
        bucket("bool", grp->GetBoolMap(),
               [](bool v) { return QJsonValue(v); });
        bucket("int", grp->GetIntMap(),
               [](long v) { return QJsonValue(qint64(v)); });
        bucket("unsigned", grp->GetUnsignedMap(),
               [](unsigned long v) { return QJsonValue(qint64(v)); });
        bucket("float", grp->GetFloatMap(),
               [](double v) { return QJsonValue(v); });
        bucket("string", grp->GetASCIIMap(), [](const std::string &v) {
            return QJsonValue(QString::fromUtf8(v.c_str()));
        });
        return obj;
    };
    // The View group is mostly UI (navigation, cursors, dimensions), so
    // only the keys that change what a frame looks like go out: the
    // light rig and its ambient, the background, the chrome drawn over
    // the scene, and the pipeline switches.
    static const std::set<std::string> viewKeys = {
        // AntiAliasing decides whether a silhouette has coverage at all,
        // so a golden restaged without it compares a multisampled capture
        // against an aliased one and diverges along every edge in every
        // stage. It was missing while the default moved from 4x to off
        // (12ad499101), which is exactly when a golden set would have
        // needed it. The `msaa` field above records the resolved count
        // but nothing restages from it.
        "AntiAliasing",
        "RenderCache", "Orthographic", "UseVBO",
        "TransparentObjectRenderType",
        "EnableHeadlight", "HeadlightColor", "HeadlightDirection",
        "HeadlightIntensity",
        "EnableBacklight", "BacklightColor", "BacklightDirection",
        "BacklightIntensity",
        "EnableFillLight", "FillLightColor", "FillLightDirection",
        "FillLightIntensity",
        "AmbientLightColor", "AmbientLightIntensity",
        "Gradient", "RadialGradient", "UseBackgroundColorMid",
        "BackgroundColor", "BackgroundColor2", "BackgroundColor3",
        "BackgroundColor4",
        "ShowNaviCube", "CornerNaviCube", "CornerCoordSystem",
        "CornerCoordSystemSize", "ShowAxisCross", "ShowFPS",
    };
    auto viewGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
    QJsonObject prefs;
    prefs[QStringLiteral("View")] = dumpGroup(viewGrp, &viewKeys);
    prefs[QStringLiteral("View/Render")] =
        dumpGroup(viewGrp->GetGroup("Render"), nullptr);
    root[QStringLiteral("preferences")] = prefs;

    QFile file(QString::fromUtf8((imagePath + ".json").c_str()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw Py::RuntimeError("Cannot write sidecar JSON: "
                               + imagePath + ".json");
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

PyObject* View3DInventorPy::cyclesRender(PyObject *args, PyObject *kwds)
{
    char *cPath;
    int width = 640;
    int height = 480;
    int samples = 64;
    const char *device = "CPU";
    PyObject *waitObj = Py_True;
    static char *kwlist[] = {"path", "width", "height", "samples", "device", "wait", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "et|iiisO", kwlist,
                                     "utf-8", &cPath, &width, &height, &samples, &device,
                                     &waitObj))
        return nullptr;
    std::string path(cPath);
    PyMem_Free(cPath);

    View3DInventorViewer *viewer = getView3DInventorPtr()->getViewer();
    std::string error;
    Render::Cycles::RenderReport report;
    bool ok = false;
    // The scene traced is the render cache as it stands, and a publish
    // that deferred shapes under its capture budget has not finished
    // stating it: wait for a complete frame first, with the GIL held
    // like saveRenderDump's pump (the event loop may run script
    // timers). A timeout is reported and the render goes ahead -- a
    // slow machine gets a picture, not a refusal.
    if (PyObject_IsTrue(waitObj) > 0 && !viewer->waitFrameComplete())
        Base::Console().Warning("cyclesRender: the scene did not reach a "
                                "complete frame in time; tracing it as it "
                                "stands\n");
    // The render blocks for as long as the samples take and touches no
    // Python; the snapshot before it is Coin, not Python, either.
    Py_BEGIN_ALLOW_THREADS
    ok = viewer->renderWithCycles(path, width, height, samples, device, &error, &report);
    Py_END_ALLOW_THREADS
    if (!ok) {
        PyErr_SetString(PyExc_RuntimeError, error.c_str());
        return nullptr;
    }
    Py::Dict dict;
    dict.setItem("meshes", Py::Long(report.meshes));
    dict.setItem("objects", Py::Long(report.objects));
    dict.setItem("shaders", Py::Long(report.shaders));
    dict.setItem("triangles", Py::Long(report.triangles));
    dict.setItem("images", Py::Long(report.images));
    dict.setItem("skipped", Py::Long(report.skipped));
    dict.setItem("seconds", Py::Float(report.seconds));
    return Py::new_reference_to(dict);
}

PyObject* View3DInventorPy::cyclesViewport(PyObject *args, PyObject *kwds)
{
    PyObject *enableObj = Py_True;
    const char *device = "CPU";
    int samples = 256;
    double timeLimit = 0.0;
    PyObject *denoiseObj = Py_True;
    int pixelSize = 1;
    static char *kwlist[] = {"enable", "device", "samples", "timeLimit", "denoise", "pixelSize",
                             nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OsidOi", kwlist,
                                     &enableObj, &device, &samples, &timeLimit, &denoiseObj,
                                     &pixelSize))
        return nullptr;

    View3DInventorViewer *viewer = getView3DInventorPtr()->getViewer();
    std::string error;
    bool ok;
    if (PyObject_IsTrue(enableObj) > 0) {
        Render::Cycles::ViewportOptions options;
        options.device = device;
        options.samples = samples;
        options.timeLimit = timeLimit;
        options.denoise = PyObject_IsTrue(denoiseObj) > 0;
        options.pixelSize = pixelSize;
        ok = viewer->setCyclesViewport(&options, &error);
    }
    else {
        ok = viewer->setCyclesViewport(nullptr, &error);
    }
    if (!ok) {
        PyErr_SetString(PyExc_RuntimeError, error.c_str());
        return nullptr;
    }
    Py_Return;
}

PyObject* View3DInventorPy::cyclesViewportStatus(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    Render::Cycles::ViewportStatus status;
    if (!getView3DInventorPtr()->getViewer()->cyclesViewportStatus(status))
        Py_Return;
    Py::Dict dict;
    dict.setItem("running", Py::Boolean(status.running));
    dict.setItem("progress", Py::Float(status.progress));
    dict.setItem("complete", Py::Boolean(status.complete));
    dict.setItem("status", Py::String(status.status));
    dict.setItem("error", Py::String(status.error));
    dict.setItem("meshes", Py::Long(status.report.meshes));
    dict.setItem("objects", Py::Long(status.report.objects));
    dict.setItem("shaders", Py::Long(status.report.shaders));
    dict.setItem("triangles", Py::Long(status.report.triangles));
    dict.setItem("images", Py::Long(status.report.images));
    dict.setItem("skipped", Py::Long(status.report.skipped));
    dict.setItem("added", Py::Long(status.report.added));
    dict.setItem("removed", Py::Long(status.report.removed));
    dict.setItem("restated", Py::Long(status.report.restated));
    dict.setItem("built", Py::Long(status.report.built));
    dict.setItem("released", Py::Long(status.report.released));
    dict.setItem("sessions", Py::Long(status.sessions));
    dict.setItem("updates", Py::Long(status.updates));
    return Py::new_reference_to(dict);
}

PyObject* View3DInventorPy::waitFrameComplete(PyObject *args, PyObject *kwds)
{
    int timeout = 120000;
    static char *kwlist[] = {"timeout", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist, &timeout))
        return nullptr;
    try {
        View3DInventorViewer *viewer = getView3DInventorPtr()->getViewer();
        return Py::new_reference_to(Py::Boolean(viewer->waitFrameComplete(timeout)));
    } PY_CATCH
}

PyObject* View3DInventorPy::isFrameComplete(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        View3DInventorViewer *viewer = getView3DInventorPtr()->getViewer();
        Render::Renderer *renderer = viewer->getExternalRenderer();
        return Py::new_reference_to(
            Py::Boolean(!renderer || renderer->frameComplete()));
    } PY_CATCH
}

PyObject* View3DInventorPy::saveRenderDump(PyObject *args, PyObject *kwds)
{
    char *cPath;
    char *cSource = "renderer";
    PyObject *modeObj = Py_None;
    PyObject *metaObj = Py_True;
    PyObject *waitObj = Py_True;
    static char *kwlist[] = {"path", "source", "mode", "metadata", "wait", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "et|sOOO", kwlist,
                                     "utf-8", &cPath, &cSource,
                                     &modeObj, &metaObj, &waitObj))
        return nullptr;
    const bool wait = PyObject_IsTrue(waitObj) > 0;

    std::string path(cPath);
    PyMem_Free(cPath);
    std::string source(cSource);
    int mode = -1;
    if (modeObj != Py_None) {
        mode = int(PyLong_AsLong(modeObj));
        if (PyErr_Occurred())
            return nullptr;
    }
    bool metadata = PyObject_IsTrue(metaObj) > 0;

    try {
        QFileInfo fi(QString::fromUtf8(path.c_str()));
        if (!fi.absoluteDir().exists())
            throw Py::RuntimeError("Directory where to save image doesn't exist");

        View3DInventor *view = getView3DInventorPtr();
        View3DInventorViewer *viewer = view->getViewer();

        if (source == "renderer") {
            Render::Renderer *renderer = viewer->getExternalRenderer();
            if (!renderer)
                throw Py::RuntimeError("No external renderer active on this view");
            Render::FrameDumpRequest req;
            req.path = path;
            req.mode = mode;
            req.waitComplete = wait;
            if (!renderer->requestFrameDump(req))
                throw Py::RuntimeError("Render backend has no frame capture");
            if (!viewer->pumpFrameDump(renderer))
                throw Py::RuntimeError("Frame capture timed out");
            Render::RenderStats stats;
            renderer->getRenderStats(stats);
            if (metadata)
                writeRenderDumpSidecar(view, path, "renderer", mode,
                                       &stats);
            return Py::new_reference_to(Py::String(path));
        }

        if (source == "framebuffer") {
            if (mode >= 0)
                throw Py::ValueError("mode override is not supported for "
                                     "source='framebuffer' (it captures the "
                                     "composited frame as staged)");
            // savePicture's framebuffer path sizes an FBO from these
            // (no -1 = current-size convention there).
            auto glWidget = viewer->getGLWidget();
            if (!glWidget)
                throw Py::RuntimeError("View has no GL widget");
            const qreal dpr = glWidget->devicePixelRatioF();
            QImage img;
            // The composited frame is the backend's frame too, so the
            // same wait applies before it is read.
            if (wait)
                viewer->waitFrameComplete();
            // The composited GL frame specifically -- not savePicture,
            // which sends a backend-rendered view to source='renderer'
            // and would make these two sources the same capture.
            viewer->imageFromFramebuffer(int(glWidget->width() * dpr),
                                         int(glWidget->height() * dpr),
                                         View3DInventorViewer::getNumSamples(),
                                         QColor(), img);
            if (img.isNull()
                    || !img.save(QString::fromUtf8(path.c_str())))
                throw Py::RuntimeError("Cannot write image: " + path);
            if (metadata)
                writeRenderDumpSidecar(view, path, "framebuffer", -1,
                                       nullptr);
            return Py::new_reference_to(Py::String(path));
        }

        if (source == "viewer") {
            auto &server = Render::SceneStreamServer::instance();
            if (!server.running())
                throw Py::RuntimeError("Scene-streaming server not running "
                                       "(FC_BGFX_SERVE_SCENE)");
            std::vector<Render::ViewerFrameDump> dumps;
            Py_BEGIN_ALLOW_THREADS
            server.requestFrameDumps(mode, 10000, dumps);
            Py_END_ALLOW_THREADS
            if (dumps.empty())
                throw Py::RuntimeError("No connected viewer answered the "
                                       "dumpFrame request");

            // One image per connected viewer: the single-viewer capture
            // keeps the given path, more get a -<n> suffix; the sidecar
            // is the viewer's own metadata (its GPU, canvas, applied
            // debug state) plus the request context.
            Py::List paths;
            auto dot = path.rfind('.');
            std::string base = dot == std::string::npos
                ? path : path.substr(0, dot);
            std::string ext = dot == std::string::npos
                ? std::string() : path.substr(dot);
            for (size_t i = 0; i < dumps.size(); ++i) {
                const auto &dump = dumps[i];
                std::string file = dumps.size() == 1 ? path
                    : base + "-" + std::to_string(i) + ext;
                QImage img(dump.rgba.data(), dump.width, dump.height,
                           dump.width * 4, QImage::Format_RGBA8888);
                // WebGL readback rows are bottom-up like desktop GL.
                if (!img.mirrored().save(QString::fromUtf8(file.c_str())))
                    throw Py::RuntimeError("Cannot write image: " + file);
                if (metadata) {
                    QJsonObject root;
                    if (!dump.meta.empty()) {
                        QJsonDocument doc = QJsonDocument::fromJson(
                            QByteArray(dump.meta.data(),
                                       int(dump.meta.size())));
                        if (doc.isObject())
                            root = doc.object();
                    }
                    QJsonObject capture;
                    // The basename, as above.
                    capture[QStringLiteral("file")] =
                        QFileInfo(QString::fromUtf8(file.c_str()))
                            .fileName();
                    capture[QStringLiteral("source")] =
                        QStringLiteral("viewer");
                    if (mode >= 0)
                        capture[QStringLiteral("mode")] = mode;
                    capture[QStringLiteral("time")] =
                        QDateTime::currentDateTime().toString(Qt::ISODate);
                    root[QStringLiteral("capture")] = capture;
                    QFile sidecar(QString::fromUtf8(
                                (file + ".json").c_str()));
                    if (!sidecar.open(QIODevice::WriteOnly
                                      | QIODevice::Truncate))
                        throw Py::RuntimeError("Cannot write sidecar JSON: "
                                               + file + ".json");
                    sidecar.write(QJsonDocument(root).toJson(
                                QJsonDocument::Indented));
                }
                paths.append(Py::String(file));
            }
            return Py::new_reference_to(paths);
        }

        throw Py::ValueError("source must be 'renderer', 'framebuffer' "
                             "or 'viewer'");
    } PY_CATCH
}

PyObject* View3DInventorPy::reloadShaders(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        View3DInventorViewer *viewer = getView3DInventorPtr()->getViewer();
        Render::Renderer *renderer = viewer->getExternalRenderer();
        if (!renderer)
            throw Py::RuntimeError("No external renderer active on this view");
        if (!renderer->reloadShaders())
            throw Py::RuntimeError("Render backend has no shader reload");
        if (auto rm = viewer->getSoRenderManager())
            rm->scheduleRedraw();
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::getRenderStats(PyObject *args, PyObject *kwds)
{
    PyObject *waitObj = Py_True;
    static char *kwlist[] = {"wait", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &waitObj))
        return nullptr;
    try {
        View3DInventorViewer *viewer = getView3DInventorPtr()->getViewer();
        Render::Renderer *renderer = viewer->getExternalRenderer();
        if (!renderer)
            throw Py::RuntimeError("No external renderer active on this view");
        Render::FrameDumpRequest req;    // stats-only readback, no file
        req.waitComplete = PyObject_IsTrue(waitObj) > 0;
        if (!renderer->requestFrameDump(req))
            throw Py::RuntimeError("Render backend has no frame capture");
        if (!viewer->pumpFrameDump(renderer))
            throw Py::RuntimeError("Frame capture timed out");
        Render::RenderStats stats;
        if (!renderer->getRenderStats(stats))
            throw Py::RuntimeError("No readback statistics available");
        Py::Dict dict;
        dict.setItem("width", Py::Long(stats.width));
        dict.setItem("height", Py::Long(stats.height));
        dict.setItem("geometryPixels",
                     Py::Long(long(stats.geometryPixels)));
        dict.setItem("nonFiniteChannels",
                     Py::Long(long(stats.nonFiniteChannels)));
        Py::Tuple avg(3);
        avg.setItem(0, Py::Float(stats.avgColor[0]));
        avg.setItem(1, Py::Float(stats.avgColor[1]));
        avg.setItem(2, Py::Float(stats.avgColor[2]));
        dict.setItem("avgColor", avg);
        // Backend handle pools, process-wide (see Render::RenderStats):
        // what is left across every open 3D view, not this one's share.
        dict.setItem("frameBuffers", Py::Long(stats.numFrameBuffers));
        dict.setItem("maxFrameBuffers", Py::Long(stats.maxFrameBuffers));
        dict.setItem("textures", Py::Long(stats.numTextures));
        dict.setItem("maxTextures", Py::Long(stats.maxTextures));
        dict.setItem("views", Py::Long(stats.numViews));
        dict.setItem("maxViews", Py::Long(stats.maxViews));
        dict.setItem("textureMemory",
                     Py::Long(long(stats.textureMemory)));
        dict.setItem("renderTargetMemory",
                     Py::Long(long(stats.renderTargetMemory)));
        dict.setItem("gpuMemoryUsed",
                     Py::Long(long(stats.gpuMemoryUsed)));
        dict.setItem("gpuMemoryMax",
                     Py::Long(long(stats.gpuMemoryMax)));
        dict.setItem("temporalSamples", Py::Long(stats.temporalSamples));
        return Py::new_reference_to(dict);
    } PY_CATCH
}

PyObject* View3DInventorPy::getCameraNode(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        SoNode* camera = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
        PyObject* proxy = 0;
        std::string type;
        type = "So"; // seems that So prefix is missing in camera node
        type += camera->getTypeId().getName().getString();
        type += " *";
        proxy = Base::Interpreter().createSWIGPointerObj("pivy.coin", type.c_str(), (void*)camera, 1);
        camera->ref();
        return proxy;
    } PY_CATCH
}

PyObject* View3DInventorPy::getCamera(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    SoOutput out;
    char buffer[512];
    out.setBuffer(buffer, 512, 0);

    try {
        SoWriteAction wa(&out);
        SoCamera * cam = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
        if (cam) wa.apply(cam);
        else buffer[0] = '\0';
        return Py::new_reference_to(Py::String(buffer));
    } PY_CATCH
}

PyObject* View3DInventorPy::getViewDirection(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        SbVec3f dvec = getView3DInventorPtr()->getViewer()->getViewDirection();
        return Py::new_reference_to(Py::Vector(Base::Vector3f(dvec[0], dvec[1], dvec[2])));
    } PY_CATCH
}

PyObject* View3DInventorPy::setViewDirection(PyObject *args)
{
    PyObject* object;
    if (!PyArg_ParseTuple(args, "O", &object))
        return nullptr;

    try {
        if (PyTuple_Check(object)) {
            Py::Tuple tuple(object);
            Py::Float x(tuple.getItem(0));
            Py::Float y(tuple.getItem(1));
            Py::Float z(tuple.getItem(2));
            SbVec3f dir;
            dir.setValue((float)x, (float)y, (float)z);
            if (dir.length() < 0.001f)
                throw Py::ValueError("Null vector cannot be used to set direction");
            getView3DInventorPtr()->getViewer()->setViewDirection(dir);
        }
        Py_Return;
    } PY_CATCH
}


PyObject* View3DInventorPy::setCamera(PyObject *args)
{
    char* buffer;
    if (!PyArg_ParseTuple(args, "s", &buffer))
        return nullptr;

    try {
        getView3DInventorPtr()->setCamera(buffer);
        Py_Return;
    } PY_CATCH
}

//FIXME: Once View3DInventor inherits from PropertyContainer we can use PropertyEnumeration.
const char* CameraTypeEnums[]= {"Orthographic","Perspective",NULL};

PyObject* View3DInventorPy::getCameraType(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        SoCamera* cam = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getCamera();
        if (!cam) {
            throw Py::RuntimeError("No camera set!");
        }
        else if (cam->getTypeId() == SoOrthographicCamera::getClassTypeId()) {
            return Py::new_reference_to(Py::String(CameraTypeEnums[0]));
        }
        else if (cam->getTypeId() == SoPerspectiveCamera::getClassTypeId()) {
            return Py::new_reference_to(Py::String(CameraTypeEnums[1]));
        }
        else {
            throw Py::TypeError("Unknown camera type");
        }
    } PY_CATCH
}

PyObject* View3DInventorPy::setCameraType(PyObject *args)
{
    int cameratype=-1;
    try {
        if (!PyArg_ParseTuple(args, "i", &cameratype)) {    // convert args: Python->C
            char* modename;
            PyErr_Clear();
            if (!PyArg_ParseTuple(args, "s", &modename))
                return nullptr;
            for (int i=0; i<2; i++ ) {
                if (strncmp(CameraTypeEnums[i],modename,20) == 0 ) {
                    cameratype = i;
                    break;
                }
            }

            if (cameratype < 0) {
                std::string s;
                std::ostringstream s_out;
                s_out << "Unknown camera type '" << modename << "'";
                throw Py::NameError(s_out.str());
            }
        }

        if (cameratype < 0 || cameratype > 1)
            throw Py::IndexError("Out of range");
        if (cameratype==0)
            getView3DInventorPtr()->getViewer()->setCameraType(SoOrthographicCamera::getClassTypeId());
        else
            getView3DInventorPtr()->getViewer()->setCameraType(SoPerspectiveCamera::getClassTypeId());
        getView3DInventorPtr()->bindCamera(getView3DInventorPtr()->boundCamera());
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::listCameraTypes(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        Py::List list(2);
        for (int i=0; i<2; i++) {
            list[i] = Py::String(CameraTypeEnums[i]);
        }
        return Py::new_reference_to(list);
    } PY_CATCH
}

PyObject* View3DInventorPy::dump(PyObject *args)
{
    char* filename;
    PyObject *onlyVisible = Py_False;
    if (!PyArg_ParseTuple(args, "s|O", &filename, &onlyVisible))
        return nullptr;

    try {
        getView3DInventorPtr()->dump(filename, PyObject_IsTrue(onlyVisible));
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::dumpNode(PyObject *args)
{
    PyObject* object;
    if (!PyArg_ParseTuple(args, "O", &object))     // convert args: Python->C
        return nullptr;

    void* ptr = 0;
    try {
        Base::Interpreter().convertSWIGPointerObj("pivy.coin", "SoNode *", object, &ptr, 0);
    } PY_CATCH
    SoNode* node = reinterpret_cast<SoNode*>(ptr);
    return Py::new_reference_to(Py::String(SoFCDB::writeNodesToString(node)));
}

//FIXME: Once View3DInventor inherits from PropertyContainer we can use PropertyEnumeration.
const char* StereoTypeEnums[]= {"None","Anaglyph","QuadBuffer","InterleavedRows","InterleavedColumns",NULL};

PyObject* View3DInventorPy::setStereoType(PyObject *args)
{
    int stereomode=-1;
    try {
        if (!PyArg_ParseTuple(args, "i", &stereomode)) {
            char* modename;
            PyErr_Clear();
            if (!PyArg_ParseTuple(args, "s", &modename))
                return nullptr;
            for (int i=0; i<5; i++) {
                if (strncmp(StereoTypeEnums[i],modename,20) == 0) {
                    stereomode = i;
                    break;
                }
            }

            if (stereomode < 0) {
                std::string s;
                std::ostringstream s_out;
                s_out << "Unknown stereo type '" << modename << "'";
                throw Py::NameError(s_out.str());
            }
        }

        if (stereomode < 0 || stereomode > 4)
            throw Py::IndexError("Out of range");
        Quarter::SoQTQuarterAdaptor::StereoMode mode = Quarter::SoQTQuarterAdaptor::StereoMode(stereomode);
        getView3DInventorPtr()->getViewer()->setStereoMode(mode);
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::getStereoType(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        int mode = (int)(getView3DInventorPtr()->getViewer()->stereoMode());
        return Py::new_reference_to(Py::String(StereoTypeEnums[mode]));
    } PY_CATCH
}

PyObject* View3DInventorPy::listStereoTypes(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        Py::List list(5);
        for (int i=0; i<5; i++) {
            list[i] = Py::String(StereoTypeEnums[i]);
        }

        return Py::new_reference_to(list);
    } PY_CATCH
}

PyObject* View3DInventorPy::getCursorPos(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {

        QPoint pos = getView3DInventorPtr()->mapFromGlobal(QCursor::pos());
        SbVec2s vec = getView3DInventorPtr()->getViewer()->fromQPoint(pos);
        Py::Tuple tuple(2);
        tuple.setItem(0, Py::Int(vec[0]));
        tuple.setItem(1, Py::Int(vec[1]));
        return Py::new_reference_to(tuple);
    } PY_CATCH
}

PyObject* View3DInventorPy::getObjectInfo(PyObject *args)
{
    PyObject* object;
    float r = getView3DInventorPtr()->getViewer()->getPickRadius();
    if (!PyArg_ParseTuple(args, "O|f", &object, &r))
        return nullptr;

    try {
        //Note: For gcc (4.2) we need the 'const' keyword to avoid the compiler error:
        //conversion from 'Py::seqref<Py::Object>' to non-scalar type 'Py::Int' requested
        //We should report this problem to the PyCXX project as in the documentation an
        //example without the 'const' keyword is used.
        //Or we can also write Py::Int x(tuple[0]);
        const Py::Tuple tuple(object);
        Py::Int x(tuple[0]);
        Py::Int y(tuple[1]);

        // As this method could be called during a SoHandleEventAction scene
        // graph traversal we must not use a second SoHandleEventAction as
        // we will get Coin warnings because of multiple scene graph traversals
        // which is regarded as error-prone.
        SoRayPickAction action(getView3DInventorPtr()->getViewer()->getSoRenderManager()->getViewportRegion());
        action.setPoint(SbVec2s((long)x,(long)y));
        action.setRadius(r);
        action.apply(getView3DInventorPtr()->getViewer()->getSoRenderManager()->getSceneGraph());
        SoPickedPoint *Point = action.getPickedPoint();

        Py::Object ret = Py::None();
        if (Point) {
            Py::Dict dict;
            SbVec3f pt = Point->getPoint();
            dict.setItem("x", Py::Float(pt[0]));
            dict.setItem("y", Py::Float(pt[1]));
            dict.setItem("z", Py::Float(pt[2]));

            ViewProvider *vp = getView3DInventorPtr()->getViewer()->getViewProviderByPath(Point->getPath());
            if (vp && vp->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
                if (!vp->isSelectable())
                    return Py::new_reference_to(ret);
                ViewProviderDocumentObject* vpd = static_cast<ViewProviderDocumentObject*>(vp);
                if (vp->useNewSelectionModel()) {
                    std::string subname;
                    if (!vp->getElementPicked(Point,subname))
                        return Py::new_reference_to(ret);
                    auto obj = vpd->getObject();
                    if (!obj)
                        return Py::new_reference_to(ret);
                    if (subname.size()) {
                        std::pair<std::string,std::string> elementName;
                        auto sobj = App::GeoFeature::resolveElement(obj,subname.c_str(),elementName);
                        if (!sobj)
                            return Py::new_reference_to(ret);
                        if (sobj != obj) {
                            dict.setItem("ParentObject",Py::Object(obj->getPyObject(),true));
                            dict.setItem("SubName",Py::String(subname));
                            obj = sobj;
                        }
                        subname = elementName.second.size()?elementName.second:elementName.first;
                    }
                    dict.setItem("Document",
                        Py::String(obj->getDocument()->getName()));
                    dict.setItem("Object",
                        Py::String(obj->getNameInDocument()));
                    dict.setItem("Component",Py::String(subname));
                }
                else {
                    dict.setItem("Document",
                        Py::String(vpd->getObject()->getDocument()->getName()));
                    dict.setItem("Object",
                        Py::String(vpd->getObject()->getNameInDocument()));
                    // search for a SoFCSelection node
                    SoFCDocumentObjectAction objaction;
                    objaction.apply(Point->getPath());
                    if (objaction.isHandled()) {
                        dict.setItem("Component",
                            Py::String(objaction.componentName.getString()));
                    }
                }

                // ok, found the node of interest
                ret = dict;
            }
            else {
                // custom nodes not in a VP: search for a SoFCSelection node
                SoFCDocumentObjectAction objaction;
                objaction.apply(Point->getPath());
                if (objaction.isHandled()) {
                    dict.setItem("Document",
                        Py::String(objaction.documentName.getString()));
                    dict.setItem("Object",
                        Py::String(objaction.objectName.getString()));
                    dict.setItem("Component",
                        Py::String(objaction.componentName.getString()));
                    // ok, found the node of interest
                    ret = dict;
                }
            }
        }

        return Py::new_reference_to(ret);
    } PY_CATCH
}

PyObject* View3DInventorPy::getObjectsInfo(PyObject *args)
{
    PyObject* object;
    float r = getView3DInventorPtr()->getViewer()->getPickRadius();
    if (!PyArg_ParseTuple(args, "O|f", &object, &r))
        return nullptr;

    try {
        //Note: For gcc (4.2) we need the 'const' keyword to avoid the compiler error:
        //conversion from 'Py::seqref<Py::Object>' to non-scalar type 'Py::Int' requested
        //We should report this problem to the PyCXX project as in the documentation an
        //example without the 'const' keyword is used.
        //Or we can also write Py::Int x(tuple[0]);
        const Py::Tuple tuple(object);
        Py::Int x(tuple[0]);
        Py::Int y(tuple[1]);

        // As this method could be called during a SoHandleEventAction scene
        // graph traversal we must not use a second SoHandleEventAction as
        // we will get Coin warnings because of multiple scene graph traversals
        // which is regarded as error-prone.
        SoRayPickAction action(getView3DInventorPtr()->getViewer()->getSoRenderManager()->getViewportRegion());
        action.setPickAll(true);
        action.setRadius(r);
        action.setPoint(SbVec2s((long)x,(long)y));
        action.apply(getView3DInventorPtr()->getViewer()->getSoRenderManager()->getSceneGraph());
        const SoPickedPointList& pp = action.getPickedPointList();

        Py::Object ret = Py::None();
        if (pp.getLength() > 0) {
            Py::List list;
            for (int i=0; i<pp.getLength(); i++) {
                Py::Dict dict;
                SoPickedPoint* point = static_cast<SoPickedPoint*>(pp.get(i));
                SbVec3f pt = point->getPoint();
                dict.setItem("x", Py::Float(pt[0]));
                dict.setItem("y", Py::Float(pt[1]));
                dict.setItem("z", Py::Float(pt[2]));

                ViewProvider *vp = getView3DInventorPtr()->getViewer()->getViewProviderByPath(point->getPath());
                if(vp && vp->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
                    if(!vp->isSelectable())
                        continue;
                    ViewProviderDocumentObject* vpd = static_cast<ViewProviderDocumentObject*>(vp);
                    if (vp->useNewSelectionModel()) {
                        std::string subname;
                        if (!vp->getElementPicked(point,subname))
                            continue;
                        auto obj = vpd->getObject();
                        if (!obj)
                            continue;
                        if (subname.size()) {
                            std::pair<std::string,std::string> elementName;
                            auto sobj = App::GeoFeature::resolveElement(obj,subname.c_str(),elementName);
                            if (!sobj)
                                continue;
                            if (sobj != obj) {
                                dict.setItem("ParentObject",Py::Object(obj->getPyObject(),true));
                                dict.setItem("SubName",Py::String(subname));
                                obj = sobj;
                            }
                            subname = elementName.second.size()?elementName.second:elementName.first;
                        }
                        dict.setItem("Document",
                            Py::String(obj->getDocument()->getName()));
                        dict.setItem("Object",
                            Py::String(obj->getNameInDocument()));
                        dict.setItem("Component",Py::String(subname));
                    }
                    else {
                        dict.setItem("Document",
                            Py::String(vpd->getObject()->getDocument()->getName()));
                        dict.setItem("Object",
                            Py::String(vpd->getObject()->getNameInDocument()));
                        // search for a SoFCSelection node
                        SoFCDocumentObjectAction objaction;
                        objaction.apply(point->getPath());
                        if (objaction.isHandled()) {
                            dict.setItem("Component",
                                Py::String(objaction.componentName.getString()));
                        }
                    }
                    // ok, found the node of interest
                    list.append(dict);
                }
                else {
                    // custom nodes not in a VP: search for a SoFCSelection node
                    SoFCDocumentObjectAction objaction;
                    objaction.apply(point->getPath());
                    if (objaction.isHandled()) {
                        dict.setItem("Document",
                            Py::String(objaction.documentName.getString()));
                        dict.setItem("Object",
                            Py::String(objaction.objectName.getString()));
                        dict.setItem("Component",
                            Py::String(objaction.componentName.getString()));
                        // ok, found the node of interest
                        ret = dict;
                    }
                }
            }

            ret = list;
        }

        return Py::new_reference_to(ret);
    } PY_CATCH
}

PyObject* View3DInventorPy::getSize(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        SbVec2s size = getView3DInventorPtr()->getViewer()->getSoRenderManager()->getSize();
        Py::Tuple tuple(2);
        tuple.setItem(0, Py::Int(size[0]));
        tuple.setItem(1, Py::Int(size[1]));
        return Py::new_reference_to(tuple);
    } PY_CATCH
}

PyObject* View3DInventorPy::getPoint(PyObject *args)
{
    return getPointOnFocalPlane(args);
}

PyObject *View3DInventorPy::getPointOnFocalPlane(PyObject *args)
{
    try {
        short x,y;
        if (!PyArg_ParseTuple(args, "hh", &x, &y)) {
            PyErr_Clear();
            PyObject *pyobj;
            if (!PyArg_ParseTuple(args, "O", &pyobj))
                return nullptr;
            if (!PySequence_Check(pyobj))
                throw Py::TypeError("");
            Py::Sequence t(pyobj);
            if (t.size() != 2)
                throw Py::TypeError("");
            x = (int)Py::Int(t[0]);
            y = (int)Py::Int(t[1]);
        }
        SbVec3f pt = getView3DInventorPtr()->getViewer()->getPointOnFocalPlane(SbVec2s(x,y));
        return Py::new_reference_to(Py::Vector(Base::Vector3f(pt[0], pt[1], pt[2])));
    } PY_CATCH
}

PyObject* View3DInventorPy::getPointOnScreen(PyObject *args)
{
    return getPointOnViewport(args);
}

PyObject* View3DInventorPy::getPointOnViewport(PyObject *args)
{
    PyObject* v;
    double vx,vy,vz;
    try {
        if (PyArg_ParseTuple(args, "O!", &Base::VectorPy::Type, &v)) {
            Base::Vector3d* vec = static_cast<Base::VectorPy*>(v)->getVectorPtr();
            vx = vec->x;
            vy = vec->y;
            vz = vec->z;
        }
        else {
            PyErr_Clear();
            if (!PyArg_ParseTuple(args, "ddd", &vx,&vy,&vz)) {
                throw Py::TypeError("Wrong argument, Vector or three floats expected expected");
            }
        }

        SbVec2s pt = getView3DInventorPtr()->getViewer()->getPointOnViewport(SbVec3f(vx,vy,vz));
        Py::Tuple tuple(2);
        tuple.setItem(0, Py::Int(pt[0]));
        tuple.setItem(1, Py::Int(pt[1]));

        return Py::new_reference_to(tuple);
    } PY_CATCH
}

PyObject* View3DInventorPy::listNavigationTypes(PyObject *)
{
    std::vector<Base::Type> types;
    try {
        Py::List styles;
        Base::Type::getAllDerivedFrom(UserNavigationStyle::getClassTypeId(), types);
        for (std::vector<Base::Type>::iterator it = types.begin()+1; it != types.end(); ++it) {
            styles.append(Py::String(it->getName()));
        }
        return Py::new_reference_to(styles);
    } PY_CATCH
}

PyObject* View3DInventorPy::getNavigationType(PyObject *)
{
    try {
        std::string name = getView3DInventorPtr()->getViewer()->navigationStyle()->getTypeId().getName();
        return Py::new_reference_to(Py::String(name));
    } PY_CATCH
}

PyObject* View3DInventorPy::setNavigationType(PyObject *args)
{
    char* style;
    if (!PyArg_ParseTuple(args, "s", &style))
        return nullptr;
    try {
        Base::Type type = Base::Type::fromName(style);
        getView3DInventorPtr()->getViewer()->setNavigationType(type);
        Py_Return;
    } PY_CATCH
}

void View3DInventorPy::eventCallback(void * ud, SoEventCallback * n)
{
    Base::PyGILStateLocker lock;
    try {
        Py::Dict dict;
        const SoEvent* e = n->getEvent();
        if (!e) return; // invalid event
        // Type
        dict.setItem("Type", Py::String(std::string(e->getTypeId().getName().getString())));
        // Time
        dict.setItem("Time", Py::String(std::string(e->getTime().formatDate().getString())));
        SbVec2s p = n->getEvent()->getPosition();
        Py::Tuple pos(2);
        pos.setItem(0, Py::Int(p[0]));
        pos.setItem(1, Py::Int(p[1]));
        // Position
        dict.setItem("Position", pos);
        // Shift, Ctrl, Alt down
        dict.setItem("ShiftDown", Py::Object((e->wasShiftDown() ? Py_True : Py_False)));
        dict.setItem("CtrlDown",  Py::Object((e->wasCtrlDown()  ? Py_True : Py_False)));
        dict.setItem("AltDown",   Py::Object((e->wasAltDown()   ? Py_True : Py_False)));
        if (e->isOfType(SoButtonEvent::getClassTypeId())) {
            std::string state;
            const SoButtonEvent* be = static_cast<const SoButtonEvent*>(e);
            switch (be->getState()) {
                case SoButtonEvent::UP:
                    state = "UP";
                    break;
                case SoButtonEvent::DOWN:
                    state = "DOWN";
                    break;
                default:
                    state = "UNKNOWN";
                    break;
            }

            dict.setItem("State", Py::String(state));
        }
        if (e->isOfType(SoKeyboardEvent::getClassTypeId())) {
            const SoKeyboardEvent* ke = static_cast<const SoKeyboardEvent*>(e);
            switch (ke->getKey()) {
                case SoKeyboardEvent::ANY:
                    dict.setItem("Key", Py::String("ANY"));
                    break;
                case SoKeyboardEvent::UNDEFINED:
                    dict.setItem("Key", Py::String("UNDEFINED"));
                    break;
                case SoKeyboardEvent::LEFT_SHIFT:
                case SoKeyboardEvent::RIGHT_SHIFT:
                    dict.setItem("Key", Py::String("SHIFT"));
                    break;
                case SoKeyboardEvent::LEFT_CONTROL:
                case SoKeyboardEvent::RIGHT_CONTROL:
                    dict.setItem("Key", Py::String("CONTROL"));
                    break;
                case SoKeyboardEvent::LEFT_ALT:
                case SoKeyboardEvent::RIGHT_ALT:
                    dict.setItem("Key", Py::String("ALT"));
                    break;
                case SoKeyboardEvent::HOME:
                    dict.setItem("Key", Py::String("HOME"));
                    break;
                case SoKeyboardEvent::LEFT_ARROW:
                    dict.setItem("Key", Py::String("LEFT_ARROW"));
                    break;
                case SoKeyboardEvent::UP_ARROW:
                    dict.setItem("Key", Py::String("UP_ARROW"));
                    break;
                case SoKeyboardEvent::RIGHT_ARROW:
                    dict.setItem("Key", Py::String("RIGHT_ARROW"));
                    break;
                case SoKeyboardEvent::DOWN_ARROW:
                    dict.setItem("Key", Py::String("DOWN_ARROW"));
                    break;
                case SoKeyboardEvent::PAGE_UP:
                    dict.setItem("Key", Py::String("PAGE_UP"));
                    break;
                case SoKeyboardEvent::PAGE_DOWN:
                    dict.setItem("Key", Py::String("PAGE_DOWN"));
                    break;
                case SoKeyboardEvent::END:
                    dict.setItem("Key", Py::String("END"));
                    break;
                case SoKeyboardEvent::PAD_ENTER:
                    dict.setItem("Key", Py::String("PAD_ENTER"));
                    break;
                case SoKeyboardEvent::PAD_F1:
                    dict.setItem("Key", Py::String("PAD_F1"));
                    break;
                case SoKeyboardEvent::PAD_F2:
                    dict.setItem("Key", Py::String("PAD_F2"));
                    break;
                case SoKeyboardEvent::PAD_F3:
                    dict.setItem("Key", Py::String("PAD_F3"));
                    break;
                case SoKeyboardEvent::PAD_F4:
                    dict.setItem("Key", Py::String("PAD_F4"));
                    break;
                case SoKeyboardEvent::PAD_0:
                    dict.setItem("Key", Py::String("PAD_0"));
                    break;
                case SoKeyboardEvent::PAD_1:
                    dict.setItem("Key", Py::String("PAD_1"));
                    break;
                case SoKeyboardEvent::PAD_2:
                    dict.setItem("Key", Py::String("PAD_2"));
                    break;
                case SoKeyboardEvent::PAD_3:
                    dict.setItem("Key", Py::String("PAD_3"));
                    break;
                case SoKeyboardEvent::PAD_4:
                    dict.setItem("Key", Py::String("PAD_4"));
                    break;
                case SoKeyboardEvent::PAD_5:
                    dict.setItem("Key", Py::String("PAD_5"));
                    break;
                case SoKeyboardEvent::PAD_6:
                    dict.setItem("Key", Py::String("PAD_6"));
                    break;
                case SoKeyboardEvent::PAD_7:
                    dict.setItem("Key", Py::String("PAD_7"));
                    break;
                case SoKeyboardEvent::PAD_8:
                    dict.setItem("Key", Py::String("PAD_8"));
                    break;
                case SoKeyboardEvent::PAD_9:
                    dict.setItem("Key", Py::String("PAD_9"));
                    break;
                case SoKeyboardEvent::PAD_ADD:
                    dict.setItem("Key", Py::String("PAD_ADD"));
                    break;
                case SoKeyboardEvent::PAD_SUBTRACT:
                    dict.setItem("Key", Py::String("PAD_SUBTRACT"));
                    break;
                case SoKeyboardEvent::PAD_MULTIPLY:
                    dict.setItem("Key", Py::String("PAD_MULTIPLY"));
                    break;
                case SoKeyboardEvent::PAD_DIVIDE:
                    dict.setItem("Key", Py::String("PAD_DIVIDE"));
                    break;
                case SoKeyboardEvent::PAD_TAB:
                    dict.setItem("Key", Py::String("PAD_TAB"));
                    break;
                case SoKeyboardEvent::PAD_DELETE:
                    dict.setItem("Key", Py::String("PAD_DELETE"));
                    break;
                case SoKeyboardEvent::F1:
                    dict.setItem("Key", Py::String("F1"));
                    break;
                case SoKeyboardEvent::F2:
                    dict.setItem("Key", Py::String("F2"));
                    break;
                case SoKeyboardEvent::F3:
                    dict.setItem("Key", Py::String("F3"));
                    break;
                case SoKeyboardEvent::F4:
                    dict.setItem("Key", Py::String("F4"));
                    break;
                case SoKeyboardEvent::F5:
                    dict.setItem("Key", Py::String("F5"));
                    break;
                case SoKeyboardEvent::F6:
                    dict.setItem("Key", Py::String("F6"));
                    break;
                case SoKeyboardEvent::F7:
                    dict.setItem("Key", Py::String("F7"));
                    break;
                case SoKeyboardEvent::F8:
                    dict.setItem("Key", Py::String("F8"));
                    break;
                case SoKeyboardEvent::F9:
                    dict.setItem("Key", Py::String("F9"));
                    break;
                case SoKeyboardEvent::F10:
                    dict.setItem("Key", Py::String("F10"));
                    break;
                case SoKeyboardEvent::F11:
                    dict.setItem("Key", Py::String("F11"));
                    break;
                case SoKeyboardEvent::F12:
                    dict.setItem("Key", Py::String("F12"));
                    break;
                case SoKeyboardEvent::BACKSPACE:
                    dict.setItem("Key", Py::String("BACKSPACE"));
                    break;
                case SoKeyboardEvent::TAB:
                    dict.setItem("Key", Py::String("TAB"));
                    break;
                case SoKeyboardEvent::RETURN:
                    dict.setItem("Key", Py::String("RETURN"));
                    break;
                case SoKeyboardEvent::PAUSE:
                    dict.setItem("Key", Py::String("PAUSE"));
                    break;
                case SoKeyboardEvent::SCROLL_LOCK:
                    dict.setItem("Key", Py::String("SCROLL_LOCK"));
                    break;
                case SoKeyboardEvent::ESCAPE:
                    dict.setItem("Key", Py::String("ESCAPE"));
                    break;
                case SoKeyboardEvent::KEY_DELETE:
                    dict.setItem("Key", Py::String("DELETE"));
                    break;
                case SoKeyboardEvent::PRINT:
                    dict.setItem("Key", Py::String("PRINT"));
                    break;
                case SoKeyboardEvent::INSERT:
                    dict.setItem("Key", Py::String("INSERT"));
                    break;
                case SoKeyboardEvent::NUM_LOCK:
                    dict.setItem("Key", Py::String("NUM_LOCK"));
                    break;
                case SoKeyboardEvent::CAPS_LOCK:
                    dict.setItem("Key", Py::String("CAPS_LOCK"));
                    break;
                case SoKeyboardEvent::SHIFT_LOCK:
                    dict.setItem("Key", Py::String("SHIFT_LOCK"));
                    break;
                case SoKeyboardEvent::SPACE:
                    dict.setItem("Key", Py::String("SPACE"));
                    break;
                case SoKeyboardEvent::APOSTROPHE:
                    dict.setItem("Key", Py::String("APOSTROPHE"));
                    break;
                case SoKeyboardEvent::COMMA:
                    dict.setItem("Key", Py::String("COMMA"));
                    break;
                case SoKeyboardEvent::MINUS:
                    dict.setItem("Key", Py::String("MINUS"));
                    break;
                case SoKeyboardEvent::PERIOD:
                    dict.setItem("Key", Py::String("PERIOD"));
                    break;
                case SoKeyboardEvent::SLASH:
                    dict.setItem("Key", Py::String("SLASH"));
                    break;
                case SoKeyboardEvent::SEMICOLON:
                    dict.setItem("Key", Py::String("SEMICOLON"));
                    break;
                case SoKeyboardEvent::EQUAL:
                    dict.setItem("Key", Py::String("EQUAL"));
                    break;
                case SoKeyboardEvent::BRACKETLEFT:
                    dict.setItem("Key", Py::String("BRACKETLEFT"));
                    break;
                case SoKeyboardEvent::BACKSLASH:
                    dict.setItem("Key", Py::String("BACKSLASH"));
                    break;
                case SoKeyboardEvent::BRACKETRIGHT:
                    dict.setItem("Key", Py::String("BRACKETRIGHT"));
                    break;
                case SoKeyboardEvent::GRAVE:
                    dict.setItem("Key", Py::String("GRAVE"));
                    break;
                default:
                    dict.setItem("Key", Py::Char(ke->getPrintableCharacter()));
                    break;
            }
        }
        if (e->isOfType(SoMouseButtonEvent::getClassTypeId())) {
            const SoMouseButtonEvent* mbe = static_cast<const SoMouseButtonEvent*>(e);
            std::string button;
            switch (mbe->getButton()) {
                case SoMouseButtonEvent::BUTTON1:
                    button = "BUTTON1";
                    break;
                case SoMouseButtonEvent::BUTTON2:
                    button = "BUTTON2";
                    break;
                case SoMouseButtonEvent::BUTTON3:
                    button = "BUTTON3";
                    break;
                case SoMouseButtonEvent::BUTTON4:
                    button = "BUTTON4";
                    break;
                case SoMouseButtonEvent::BUTTON5:
                    button = "BUTTON5";
                    break;
                default:
                    button = "ANY";
                    break;
            }

            dict.setItem("Button", Py::String(button));
        }
        if (e->isOfType(SoMouseWheelEvent::getClassTypeId())){
            const SoMouseWheelEvent* mwe = static_cast<const SoMouseWheelEvent*>(e);
            dict.setItem("Delta", Py::Long(mwe->getDelta()));
        }
        if (e->isOfType(SoSpaceballButtonEvent::getClassTypeId())) {
            const SoSpaceballButtonEvent* sbe = static_cast<const SoSpaceballButtonEvent*>(e);
            std::string button;
            switch (sbe->getButton()) {
                case SoSpaceballButtonEvent::BUTTON1:
                    button = "BUTTON1";
                    break;
                case SoSpaceballButtonEvent::BUTTON2:
                    button = "BUTTON2";
                    break;
                case SoSpaceballButtonEvent::BUTTON3:
                    button = "BUTTON3";
                    break;
                case SoSpaceballButtonEvent::BUTTON4:
                    button = "BUTTON4";
                    break;
                case SoSpaceballButtonEvent::BUTTON5:
                    button = "BUTTON5";
                    break;
                case SoSpaceballButtonEvent::BUTTON6:
                    button = "BUTTON6";
                    break;
                case SoSpaceballButtonEvent::BUTTON7:
                    button = "BUTTON7";
                    break;
                default:
                    button = "ANY";
                    break;
            }

            dict.setItem("Button", Py::String(button));
        }
        if (e->isOfType(SoMotion3Event::getClassTypeId())) {
            const SoMotion3Event* me = static_cast<const SoMotion3Event*>(e);
            const SbVec3f& m = me->getTranslation();
            const SbRotation& r = me->getRotation();
            Py::Tuple mov(3);
            mov.setItem(0, Py::Float(m[0]));
            mov.setItem(1, Py::Float(m[1]));
            mov.setItem(2, Py::Float(m[2]));
            dict.setItem("Translation", mov);
            Py::Tuple rot(4);
            rot.setItem(0, Py::Float(r.getValue()[0]));
            rot.setItem(1, Py::Float(r.getValue()[1]));
            rot.setItem(2, Py::Float(r.getValue()[2]));
            rot.setItem(3, Py::Float(r.getValue()[3]));
            dict.setItem("Rotation", rot);
        }

        // now run the method
        Py::Callable method(reinterpret_cast<PyObject*>(ud));
        Py::Tuple args(1);
        args.setItem(0, dict);
        method.apply(args);
    } _PY_CATCH(return)
}

PyObject* View3DInventorPy::addEventCallback(PyObject *args)
{
    char* eventtype;
    PyObject* method;
    if (!PyArg_ParseTuple(args, "sO", &eventtype, &method))
        return nullptr;
    try {
        if (PyCallable_Check(method) == 0) {
            throw Py::TypeError("object is not callable");
        }
        SoType eventId = SoType::fromName(eventtype);
        if (eventId.isBad() || !eventId.isDerivedFrom(SoEvent::getClassTypeId())) {
            std::string s;
            std::ostringstream s_out;
            s_out << eventtype << " is not a valid event type";
            throw Py::TypeError(s_out.str());
        }

        if (std::find(callbacks.begin(), callbacks.end(), method) != callbacks.end())
            throw Py::RuntimeError("callback already added");

        getView3DInventorPtr()->getViewer()->addEventCallback(eventId, eventCallback, method);
        callbacks.push_back(method);
        Py_INCREF(method); // reference to keep method in callbacks
        Py_INCREF(method); // reference for return
        return method;
    } PY_CATCH
}

PyObject* View3DInventorPy::removeEventCallback(PyObject *args)
{
    char* eventtype;
    PyObject* method;
    if (!PyArg_ParseTuple(args, "sO", &eventtype, &method))
        return nullptr;
    try {
        if (PyCallable_Check(method) == 0) {
            throw Py::RuntimeError("object is not callable");
        }
        SoType eventId = SoType::fromName(eventtype);
        if (eventId.isBad() || !eventId.isDerivedFrom(SoEvent::getClassTypeId())) {
            std::string s;
            std::ostringstream s_out;
            s_out << eventtype << " is not a valid event type";
            throw Py::TypeError(s_out.str());
        }

        auto it = std::find(callbacks.begin(), callbacks.end(), method);
        if (it == callbacks.end())
            throw Py::RuntimeError("callback not found");

        getView3DInventorPtr()->getViewer()->removeEventCallback(eventId, eventCallback, method);
        callbacks.erase(it);
        Py_DECREF(method);
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::setAnnotation(PyObject *args)
{
    char *psAnnoName,*psBuffer;
    if (!PyArg_ParseTuple(args, "ss", &psAnnoName, &psBuffer))
        return nullptr;
    ViewProviderExtern* view = 0;
    try {
        view = new ViewProviderExtern();
        view->setModeByString(psAnnoName, psBuffer);
        getView3DInventorPtr()->getGuiDocument()->setAnnotationViewProvider(psAnnoName, view);
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::removeAnnotation(PyObject *args)
{
    char *psAnnoName;
    if (!PyArg_ParseTuple(args, "s", &psAnnoName))
        return nullptr;
    try {
        ViewProvider* view = 0;
        view = getView3DInventorPtr()->getGuiDocument()->getAnnotationViewProvider(psAnnoName);
        if (view) {
            getView3DInventorPtr()->getGuiDocument()->removeAnnotationViewProvider(psAnnoName);
            Py_Return;
        }
        else {
            std::string s;
            std::ostringstream s_out;
            s_out << "No such annotation '" << psAnnoName << "'";
            throw Py::KeyError(s_out.str());
        }
    } PY_CATCH
}

PyObject* View3DInventorPy::getSceneGraph(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        SoNode* scene = getView3DInventorPtr()->getViewer()->getSceneGraph();
        PyObject* proxy = 0;
        proxy = Base::Interpreter().createSWIGPointerObj("pivy.coin", "SoSeparator *", (void*)scene, 1);
        scene->ref();
        return proxy;
    } PY_CATCH
}

PyObject* View3DInventorPy::getAuxSceneGraph(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        SoNode* scene = getView3DInventorPtr()->getViewer()->getAuxSceneGraph();
        PyObject* proxy = 0;
        proxy = Base::Interpreter().createSWIGPointerObj("pivy.coin", "SoGroup *", (void*)scene, 1);
        scene->ref();
        return proxy;
    } PY_CATCH
}

PyObject* View3DInventorPy::getViewer(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        return getView3DInventorPtr()->getViewer()->getPyObject();
    } PY_CATCH
}

void View3DInventorPy::eventCallbackPivy(void * ud, SoEventCallback * n)
{
    Base::PyGILStateLocker lock;
    const SoEvent* e = n->getEvent();
    std::string type = e->getTypeId().getName().getString();
    type += " *";

    PyObject* proxy = 0;
    try {
        proxy = Base::Interpreter().createSWIGPointerObj("pivy.coin", type.c_str(), (void*)e, 0);
        // now run the method
        Py::Object event(proxy,true);
        Py::Callable method(reinterpret_cast<PyObject*>(ud));
        Py::Tuple args(1);
        args.setItem(0, event);
        method.apply(args);
    } _PY_CATCH(return)
}

void View3DInventorPy::eventCallbackPivyEx(void * ud, SoEventCallback * n)
{
    Base::PyGILStateLocker lock;
    std::string type = "SoEventCallback *";

    PyObject* proxy = 0;
    try {
        proxy = Base::Interpreter().createSWIGPointerObj("pivy.coin", type.c_str(), (void*)n, 0);
        // now run the method
        Py::Object event(proxy,true);
        Py::Callable method(reinterpret_cast<PyObject*>(ud));
        Py::Tuple args(1);
        args.setItem(0, event);
        method.apply(args);
    } _PY_CATCH(return)
    
}

PyObject* View3DInventorPy::addEventCallbackSWIG(PyObject *args)
{
    return addEventCallbackPivy(args);
}

PyObject* View3DInventorPy::addEventCallbackPivy(PyObject *args)
{
    PyObject* proxy;
    PyObject* method;
    int ex=1; // if 1, use eventCallbackPivyEx
    if (!PyArg_ParseTuple(args, "OO|i", &proxy, &method,&ex))
        return nullptr;

    void* ptr = 0;
    try {
        Base::Interpreter().convertSWIGPointerObj("pivy.coin", "SoType *", proxy, &ptr, 0);

        SoType* eventId = reinterpret_cast<SoType*>(ptr);
        if (eventId->isBad() || !eventId->isDerivedFrom(SoEvent::getClassTypeId())) {
            std::string s;
            std::ostringstream s_out;
            s_out << eventId->getName().getString() << "is not a valid event type";
            throw Py::TypeError(s_out.str());
        }

        if (PyCallable_Check(method) == 0) {
            throw Py::TypeError("object is not callable");
        }

        SoEventCallbackCB* callback = (ex == 1 ?  eventCallbackPivyEx : eventCallbackPivy);
        getView3DInventorPtr()->getViewer()->addEventCallback(*eventId, callback, method);
        callbacks.push_back(method);
        Py_INCREF(method); // reference to keep method in callbacks
        Py_INCREF(method); // reference for return
        return method;
    } PY_CATCH
}

PyObject* View3DInventorPy::removeEventCallbackSWIG(PyObject *args)
{
    return removeEventCallbackPivy(args);
}

PyObject* View3DInventorPy::removeEventCallbackPivy(PyObject *args)
{
    PyObject* proxy;
    PyObject* method;
    int ex=1; // if 1, use eventCallbackPivyEx
    if (!PyArg_ParseTuple(args, "OO|i", &proxy, &method,&ex))
        return nullptr;

    void* ptr = 0;
    try {
        Base::Interpreter().convertSWIGPointerObj("pivy.coin", "SoType *", proxy, &ptr, 0);

        SoType* eventId = reinterpret_cast<SoType*>(ptr);
        if (eventId->isBad() || !eventId->isDerivedFrom(SoEvent::getClassTypeId())) {
            std::string s;
            std::ostringstream s_out;
            s_out << eventId->getName().getString() << "is not a valid event type";
            throw Py::TypeError(s_out.str());
        }

        auto it = std::find(callbacks.begin(), callbacks.end(), method);
        if (it == callbacks.end())
            throw Py::RuntimeError("callback not found");

        SoEventCallbackCB* callback = (ex == 1 ?  eventCallbackPivyEx : eventCallbackPivy);
        getView3DInventorPtr()->getViewer()->removeEventCallback(*eventId, callback, method);
        callbacks.erase(it);
        Py_DECREF(method);
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::setAxisCross(PyObject *args)
{
    int ok;
    if (!PyArg_ParseTuple(args, "i", &ok))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->setAxisCross(ok!=0);
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::hasAxisCross(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        SbBool ok = getView3DInventorPtr()->getViewer()->hasAxisCross();
        return Py::new_reference_to(Py::Boolean(ok ? true : false));
    } PY_CATCH
}

void View3DInventorPy::draggerCallback(void * ud, SoDragger* n)
{
    Base::PyGILStateLocker lock;
    PyObject* proxy = 0;
    try {
        proxy = Base::Interpreter().createSWIGPointerObj("pivy.coin", "SoDragger *", (void*)n, 0);
        //call the method
        Py::Object dragger(proxy,true);
        Py::Callable method(reinterpret_cast<PyObject*>(ud));
        Py::Tuple args(1);
        args.setItem(0, dragger);
        method.apply(args);
    } _PY_CATCH(return)
}

PyObject* View3DInventorPy::addDraggerCallback(PyObject *args)
{
    PyObject* dragger;
    char* type;
    PyObject* method;
    if (!PyArg_ParseTuple(args, "OsO", &dragger,&type, &method))
        return nullptr;

    try {
        //Check if dragger is a SoDragger object and cast
        void* ptr = 0;
        try {
            Base::Interpreter().convertSWIGPointerObj("pivy.coin", "SoDragger *", dragger, &ptr, 0);
        }
        catch (const Base::Exception&) {
            throw Py::TypeError("The first argument must be of type SoDragger");
        }
        SoDragger* drag = reinterpret_cast<SoDragger*>(ptr);

        //Check if method is callable
        if (PyCallable_Check(method) == 0) {
            throw Py::TypeError("the method is not callable");
        }

        if (strcmp(type,"addFinishCallback")==0) {
            drag->addFinishCallback(draggerCallback,method);
        }
        else if (strcmp(type,"addStartCallback")==0) {
            drag->addStartCallback(draggerCallback,method);
        }
        else if (strcmp(type,"addMotionCallback")==0) {
            drag->addMotionCallback(draggerCallback,method);
        }
        else if (strcmp(type,"addValueChangedCallback")==0) {
            drag->addValueChangedCallback(draggerCallback,method);
        }
        else {
            std::string s;
            std::ostringstream s_out;
            s_out << type << " is not a valid dragger callback type";
            throw Py::TypeError(s_out.str());
        }

        callbacks.push_back(method);
        Py_INCREF(method); // reference to keep method in callbacks
        Py_INCREF(method); // reference for return
        return method;
    } PY_CATCH
}

PyObject* View3DInventorPy::removeDraggerCallback(PyObject *args)
{
    PyObject* dragger;
    char* type;
    PyObject* method;
    if (!PyArg_ParseTuple(args, "OsO", &dragger, &type, &method))
        return nullptr;

    try {
        //Check if dragger is a SoDragger object and cast
        void* ptr = 0;
        try {
            Base::Interpreter().convertSWIGPointerObj("pivy.coin", "SoDragger *", dragger, &ptr, 0);
        }
        catch (const Base::Exception&) {
            throw Py::TypeError("The first argument must be of type SoDragger");
        }

        auto it = std::find(callbacks.begin(), callbacks.end(), method);
        if (it == callbacks.end())
            throw Py::RuntimeError("callback not found");

        SoDragger* drag = reinterpret_cast<SoDragger*>(ptr);
        if (strcmp(type, "addFinishCallback") == 0) {
            drag->removeFinishCallback(draggerCallback, method);
        }
        else if (strcmp(type, "addStartCallback") == 0) {
            drag->removeStartCallback(draggerCallback, method);
        }
        else if (strcmp(type, "addMotionCallback") == 0) {
            drag->removeMotionCallback(draggerCallback, method);
        }
        else if (strcmp(type, "addValueChangedCallback") == 0) {
            drag->removeValueChangedCallback(draggerCallback, method);
        }
        else {
            std::string s;
            std::ostringstream s_out;
            s_out << type << " is not a valid dragger callback type";
            throw Py::TypeError(s_out.str());
        }

        callbacks.erase(it);
        Py_DECREF(method);
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::getViewProvidersOfType(PyObject *args)
{
    char* name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;

    try {
        std::vector<ViewProvider*> vps = getView3DInventorPtr()->getViewer()->getViewProvidersOfType(Base::Type::fromName(name));
        Py::List list;
        for (std::vector<ViewProvider*>::iterator it = vps.begin(); it != vps.end(); ++it) {
            list.append(Py::asObject((*it)->getPyObject()));
        }
        return Py::new_reference_to(list);
    } PY_CATCH
}

PyObject* View3DInventorPy::redraw(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->redraw();
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::setName(PyObject *args)
{
    char* buffer;
    if (!PyArg_ParseTuple(args, "s", &buffer))
        return nullptr;

    try {
        getView3DInventorPtr()->setWindowTitle(QString::fromUtf8(buffer));
        Py_Return;
    } PY_CATCH
}

PyObject* View3DInventorPy::getName(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        App::PropertyString tmp;
        tmp.setValue(getView3DInventorPtr()->windowTitle().toUtf8().constData());
        return tmp.getPyObject();
    } PY_CATCH
}

PyObject* View3DInventorPy::toggleClippingPlane(PyObject *args, PyObject *kwds)
{
    static char* keywords[] = {"toggle", "beforeEditing", "noManip", "pla", NULL};
    int toggle = -1;
    PyObject *beforeEditing = Py_False;
    PyObject *noManip = Py_True;
    PyObject *pyPla = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iOOO!", keywords,
                    &toggle, &beforeEditing, &noManip, &Base::PlacementPy::Type,&pyPla))
        return nullptr;

    try {
        Base::Placement pla;
        if(pyPla!=Py_None)
            pla = *static_cast<Base::PlacementPy*>(pyPla)->getPlacementPtr();
        getView3DInventorPtr()->getViewer()->toggleClippingPlane(toggle,PyObject_IsTrue(beforeEditing),
                PyObject_IsTrue(noManip),pla);
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::hasClippingPlane(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        return Py::new_reference_to(Py::Boolean(getView3DInventorPtr()->getViewer()->hasClippingPlane()));
    } PY_CATCH
}

PyObject* View3DInventorPy::addObjectOnTop(PyObject * args) {
    PyObject *pyObj;
    const char *subname = 0;
    if (!PyArg_ParseTuple(args, "O!|s",&App::DocumentObjectPy::Type,&pyObj,&subname))
        return nullptr;
    try {
        App::DocumentObject *obj = static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr();
        std::string sub;
        if(!subname) {
            SelectionSingleton::checkTopParent(obj,sub);
            subname = sub.c_str();
        }
        if(obj && obj->getDocument() && obj->getNameInDocument()) {
            getView3DInventorPtr()->getViewer()->checkGroupOnTop(SelectionChanges(SelectionChanges::AddSelection,
                        obj->getDocument()->getName(),obj->getNameInDocument(),subname),true);
            return Py::new_reference_to(Py::TupleN(Py::asObject(obj->getPyObject()),Py::String(subname)));
        }
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::removeObjectOnTop(PyObject * args) {
    PyObject *pyObj = Py_None;
    const char *subname = 0;
    if (!PyArg_ParseTuple(args, "|Os",&pyObj,&subname))
        return nullptr;
    try {
        App::DocumentObject *obj = 0;
        if(pyObj!=Py_None) {
            if(!PyObject_TypeCheck(pyObj,&App::DocumentObjectPy::Type))
                throw Py::TypeError("Expect document object");
            obj = static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr();
        }
        if(!obj) {
            getView3DInventorPtr()->getViewer()->clearGroupOnTop(true);
            Py_Return;
        }

        std::string sub;
        if(obj && !subname) {
            SelectionSingleton::checkTopParent(obj,sub);
            subname = sub.c_str();
        }
        if(obj && obj->getDocument() && obj->getNameInDocument()) {
            getView3DInventorPtr()->getViewer()->checkGroupOnTop(SelectionChanges(SelectionChanges::RmvSelection,
                        obj->getDocument()->getName(),obj->getNameInDocument(),subname),true);
            return Py::new_reference_to(Py::TupleN(Py::asObject(obj->getPyObject()),Py::String(subname)));
        }
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::isObjectOnTop(PyObject * args) {
    PyObject *pyObj;
    const char *subname = 0;
    if (!PyArg_ParseTuple(args, "O!|s",&App::DocumentObjectPy::Type,&pyObj,&subname))
        return nullptr;
    try {
        App::DocumentObject *obj = static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr();
        std::string sub;
        if(!subname) {
            SelectionSingleton::checkTopParent(obj,sub);
            subname = sub.c_str();
        }
        if(obj && obj->getDocument() && obj->getNameInDocument()) {
            if(getView3DInventorPtr()->getViewer()->isInGroupOnTop(App::SubObjectT(obj, subname)))
                return Py::new_reference_to(Py::TupleN(Py::asObject(obj->getPyObject()),Py::String(subname)));
        }
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::getObjectsOnTop(PyObject * args) {
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        Py::List list;
        for (const auto &sobjT : getView3DInventorPtr()->getViewer()->getObjectsOnTop()) {
            auto obj = sobjT.getObject();
            if (!obj)
                continue;
            list.append(Py::TupleN(Py::asObject(obj->getPyObject()),
                                Py::String(sobjT.getSubName())));
        }
        return Py::new_reference_to(list);
    } PY_CATCH
}

PyObject* View3DInventorPy::bindCamera(PyObject * args) {
    PyObject *pyObj;
    PyObject *sync = Py_False;
    if (!PyArg_ParseTuple(args, "O|O",&pyObj,&sync))
        return nullptr;
    if(pyObj == Py_None) {
        getView3DInventorPtr()->bindCamera(nullptr);
        Py_Return;
    }
    void* ptr = 0;
    try {
        Base::Interpreter().convertSWIGPointerObj("pivy.coin","_p_SoCamera", pyObj, &ptr, 0);
        getView3DInventorPtr()->bindCamera(reinterpret_cast<SoCamera*>(ptr), PyObject_IsTrue(sync));
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::bindView(PyObject * args) {
    PyObject *pyObj;
    PyObject *sync = Py_False;
    if (!PyArg_ParseTuple(args, "O|O",&pyObj,&sync))
        return nullptr;
    try {
        if(pyObj == Py_None) {
            getView3DInventorPtr()->bindView(QString());
            Py_Return;
        }
        QString title;
        if(PyObject_TypeCheck(pyObj, &View3DInventorPy::Type))
            title = static_cast<View3DInventorPy*>(pyObj)->getView3DInventorPtr()->windowTitle();
        else {
            const char *s;
            if (!PyArg_ParseTuple(args, "s|O",&s,&sync))
                return nullptr;
            title = QString::fromUtf8(s);
        }
        getView3DInventorPtr()->bindView(title, PyObject_IsTrue(sync));
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::unbindView(PyObject * args) {
    PyObject *pyObj = Py_None;
    if (!PyArg_ParseTuple(args, "|O",&pyObj))
        return nullptr;
    try {
        if(pyObj == Py_None) {
            getView3DInventorPtr()->unbindView();
            Py_Return;
        }
        QString title;
        if(PyObject_TypeCheck(pyObj, &View3DInventorPy::Type))
            title = static_cast<View3DInventorPy*>(pyObj)->getView3DInventorPtr()->windowTitle();
        else {
            const char *s;
            if (!PyArg_ParseTuple(args, "s",&s))
                return nullptr;
            title = QString::fromUtf8(s);
        }
        getView3DInventorPtr()->unbindView(title);
    } PY_CATCH

    Py_Return;
}

PyObject* View3DInventorPy::boundView(PyObject * args) {
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        auto view = getView3DInventorPtr()->boundView();
        if(!view)
            Py_Return;
        return view->getPyObject();
    } PY_CATCH
}

PyObject* View3DInventorPy::boundViews(PyObject * args) {
    PyObject *recursive = Py_False;
    if (!PyArg_ParseTuple(args, "|O", &recursive))
        return nullptr;

    try {
        Py::List list;
        for(auto view : getView3DInventorPtr()->boundViews(PyObject_IsTrue(recursive)))
            list.append(Py::asObject(view->getPyObject()));
        return Py::new_reference_to(list);
    } PY_CATCH
}

PyObject* View3DInventorPy::graphicsView(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        PythonWrapper wrap;
        wrap.loadWidgetsModule();
        return Py::new_reference_to(wrap.fromQWidget(getView3DInventorPtr()->getViewer(), "QGraphicsView"));
    } PY_CATCH
}

PyObject* View3DInventorPy::setCornerCrossVisible(PyObject *args)
{
    int ok;
    if (!PyArg_ParseTuple(args, "i", &ok))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->setFeedbackVisibility(ok!=0);
        getView3DInventorPtr()->getViewer()->redraw(); // added because isViewing() returns False when focus is in Python Console
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::isCornerCrossVisible(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    try {
        SbBool ok = getView3DInventorPtr()->getViewer()->isFeedbackVisible();
        return Py::new_reference_to(Py::Boolean(ok ? true : false));
    } PY_CATCH
}

PyObject* View3DInventorPy::setCornerCrossSize(PyObject *args)
{
    int size=0;
    if (!PyArg_ParseTuple(args, "i", &size))
        return nullptr;
    try {
        getView3DInventorPtr()->getViewer()->setFeedbackSize(size);
        getView3DInventorPtr()->getViewer()->redraw(); // added because isViewing() returns False when focus is in Python Console
    } PY_CATCH
    Py_Return;
}

PyObject* View3DInventorPy::getCornerCrossSize(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    int size = getView3DInventorPtr()->getViewer()->getFeedbackSize();
    return Py::new_reference_to(Py::Int(size));
}
