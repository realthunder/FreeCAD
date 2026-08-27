/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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
# include <string>
# include <QAction>
# include <QApplication>
# include <QKeyEvent>
# include <QEvent>
# include <QDropEvent>
# include <QDragEnterEvent>
# include <QLayout>
# include <QMdiSubWindow>
# include <QMessageBox>
# include <QMimeData>
# include <QPainter>
# include <QPrinter>
# include <QPrintDialog>
# include <QPrintPreviewDialog>
# include <QStackedWidget>
# include <QTimer>
# include <QUrl>
# include <QWindow>
# include <Inventor/actions/SoGetPrimitiveCountAction.h>
# include <Inventor/fields/SoSFString.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
# include <Inventor/nodes/SoSeparator.h>
#endif

#include <atomic>
#include <cctype>


#include <App/DocumentObject.h>
#include <App/Document.h>
#include <sstream>
#include "Inventor/SoFCOwnDisplayModeElement.h"
#include "Renderer/Renderer.h"
#include "SoFCUnifiedSelection.h"
#include <Base/Builder3D.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/Tools.h>

#include "ViewParams.h"
#include "View3DInventor.h"
#include "View3DSettings.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Camera.h"
#include "Document.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "NaviCube.h"
#include "NavigationStyle.h"
#include "SoFCDB.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "SoFCSelectionAction.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "SoFCVectorizeSVGAction.h"
#include "View3DInventorExamples.h"
#include "View3DInventorViewer.h"
#include "ViewArea.h"
#include "View3DInventorPy.h"
#include "ViewProvider.h"
#include "ViewProviderDocumentObject.h"
#include "WaitCursor.h"


using namespace Gui;

void GLOverlayWidget::paintEvent(QPaintEvent*)
{
    QPainter paint(this);
    paint.drawImage(0,0,image);
    paint.end();
}

/* TRANSLATOR Gui::View3DInventor */

PROPERTY_SOURCE_ABSTRACT(Gui::View3DInventor,Gui::MDIView)

View3DInventor::View3DInventor(Gui::Document* pcDocument, QWidget* parent,
                               const QtGLWidget* sharewidget, Qt::WindowFlags wflags)
    : MDIView(pcDocument, parent, wflags), _viewer(nullptr), _viewerPy(nullptr)
{
    ADD_PROPERTY(DrawStyle, (0L));
    DrawStyle.setEnums(drawStyleNames());
    ADD_PROPERTY_TYPE(ShowNaviCube, (false), nullptr, App::Prop_None,
            "Show navigation cube in this view");
    ADD_PROPERTY_TYPE(ThumbnailView, (false), nullptr, App::Prop_None,
            "Mark this view for capturing document thumbnail on saving");
    ADD_PROPERTY_TYPE(ObjectDisplayModes, (), nullptr, App::Prop_Hidden,
            "Per-object display mode overrides of this view.\n"
            "Key: a subname path (one occurrence) or a bare internal\n"
            "name (the object anywhere in this view); value: a display\n"
            "mode name, with 'As Is' pinning the object to its own mode.");

    stack = new QStackedWidget(this);
    // important for highlighting
    setMouseTracking(true);
    // accept drops on the window, get handled in dropEvent, dragEnterEvent
    setAcceptDrops(true);

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

    if (glformat)
        _viewer = new View3DInventorViewer(f, this, sharewidget);
    else
        _viewer = new View3DInventorViewer(this, sharewidget);

    if (smoothing)
        _viewer->getSoRenderManager()->getGLRenderAction()->setSmoothing(true);

    // create the inventor widget and set the defaults
    _viewer->setDocument(this->_pcDocument);
    stack->addWidget(_viewer->getWidget());
    // https://forum.freecad.org/viewtopic.php?f=3&t=6055&sid=150ed90cbefba50f1e2ad4b4e6684eba
    // describes a minor error but trying to fix it leads to a major issue
    // https://forum.freecad.org/viewtopic.php?f=3&t=6085&sid=3f4bcab8007b96aaf31928b564190fd7
    // so the change is commented out
    // By default, the wheel events are processed by the 3d view AND the mdi area.
    //_viewer->getGLWidget()->setAttribute(Qt::WA_NoMousePropagation);
    setCentralWidget(stack);

    ApplySettings applying;

    // apply the user settings
    applySettings();

    auto hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    if (hGrp->GetBool("Perspective", false))
        onMsg("PerspectiveCamera", nullptr);
    else
        onMsg("OrthographicCamera", nullptr);

    stopSpinTimer = new QTimer(this);
    connect(stopSpinTimer, &QTimer::timeout, this, &View3DInventor::stopAnimating);

    camInfo.sensor.setData(this);
    camInfo.sensor.setFunction([](void *arg, SoSensor*) {
        auto self = reinterpret_cast<View3DInventor*>(arg);
        self->onCameraChanged(self->camInfo, self->boundCamInfo);
    });

    boundCamInfo.sensor.setData(this);
    boundCamInfo.sensor.setFunction([](void *arg, SoSensor*) {
        auto self = reinterpret_cast<View3DInventor*>(arg);
        self->onCameraChanged(self->boundCamInfo, self->camInfo);
    });

    setWindowIcon(Gui::BitmapFactory().pixmap("Document"));
}

View3DInventor::~View3DInventor()
{
    if(_pcDocument) {
        SoCamera * Cam = _viewer->getSoRenderManager()->getCamera();
        if (Cam)
            _pcDocument->saveCameraSettings(SoFCDB::writeNodesToString(Cam).c_str());
    }

    viewSettings.reset();

    //If we destroy this viewer by calling 'delete' directly the focus proxy widget which is defined
    //by a widget in SoQtViewer isn't reset. This widget becomes a dangling pointer and makes
    //the application crash. (Probably it's better to destroy this viewer by calling close().)
    //See also Gui::Document::~Document().
    QWidget* foc = qApp->focusWidget();
    if (foc) {
        QWidget* par = foc->parentWidget();
        while (par) {
            if (par == this) {
                foc->setFocusProxy(nullptr);
                foc->clearFocus();
                break;
            }
            par = par->parentWidget();
        }
    }

    if (_viewerPy) {
        Base::PyGILStateLocker lock;
        _viewerPy->setInvalid();
        _viewerPy->DecRef();
    }

    // here is from time to time trouble!!!
    delete _viewer;
}

static int _ApplyingSettings;
View3DInventor::ApplySettings::ApplySettings()
{
    ++_ApplyingSettings;
}

View3DInventor::ApplySettings::~ApplySettings()
{
    --_ApplyingSettings;
}

bool View3DInventor::ApplySettings::isApplying()
{
    return _ApplyingSettings > 0;
}

void View3DInventor::closeEvent(QCloseEvent* e)
{
    MDIView::closeEvent(e);
    // An accepted close means this view is going away (delete on close),
    // but the deletion is deferred and can outlive the document -- a
    // split view cell closed mid-session sits in the deferred-delete
    // queue while the document may be torn down, and the viewer's Coin
    // sensors keep firing until the widget actually dies (deferRedraw
    // reads the document). Decouple the viewer now; null is a supported
    // state (deleteSelf uses the same).
    if (e->isAccepted() && _viewer)
        _viewer->setDocument(nullptr);
}

void View3DInventor::deleteSelf()
{
    _viewer->setSceneGraph(nullptr);
    _viewer->setDocument(nullptr);
    MDIView::deleteSelf();
}

PyObject *View3DInventor::getPyObject()
{
    if (!_viewerPy)
        _viewerPy = new View3DInventorPy(this);

    _viewerPy->IncRef();
    return _viewerPy;
}

void View3DInventor::applySettings()
{
    viewSettings = std::make_unique<View3DSettings>(App::GetApplication().GetParameterGroupByPath
                                   ("User parameter:BaseApp/Preferences/View"), _viewer);
    viewSettings->applySettings();
}

void View3DInventor::onRename(Gui::Document *pDoc)
{
    SoSFString name;
    name.setValue(pDoc->getDocument()->getName());
    SoFCDocumentAction cAct(name);
    cAct.apply(_viewer->getSceneGraph());
}

void View3DInventor::onUpdate()
{
#ifdef FC_LOGUPDATECHAIN
    Base::Console().Log("Acti: Gui::View3DInventor::onUpdate()");
#endif
    update();
    _viewer->redraw();
}

void View3DInventor::viewAll()
{
    _viewer->viewAll();
}

const char *View3DInventor::getName() const
{
    return "View3DInventor";
}

void View3DInventor::print()
{
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setFullPage(true);
    restorePrinterSettings(&printer);

    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted) {
        Gui::WaitCursor wc;
        print(&printer);
        savePrinterSettings(&printer);
    }
}

void View3DInventor::printPdf()
{
    QString filename = FileDialog::getSaveFileName(this, tr("Export PDF"), QString(),
        QStringLiteral("%1 (*.pdf)").arg(tr("PDF file")));
    if (!filename.isEmpty()) {
        Gui::WaitCursor wc;
        QPrinter printer(QPrinter::ScreenResolution);
        // setPdfVersion sets the printied PDF Version to comply with PDF/A-1b, more details under: https://www.kdab.com/creating-pdfa-documents-qt/
        printer.setPdfVersion(QPagedPaintDevice::PdfVersion_A1b);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setPageOrientation(QPageLayout::Landscape);
        printer.setOutputFileName(filename);
        print(&printer);
    }
}

void View3DInventor::printPreview()
{
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setFullPage(true);
    restorePrinterSettings(&printer);

    QPrintPreviewDialog dlg(&printer, this);
    connect(&dlg, &QPrintPreviewDialog::paintRequested,
            this, qOverload<QPrinter*>(&View3DInventor::print));
    dlg.exec();
    savePrinterSettings(&printer);
}

void View3DInventor::print(QPrinter* printer)
{
    QPainter p(printer);
    p.setRenderHints(QPainter::Antialiasing);
    if (!p.isActive() && !printer->outputFileName().isEmpty()) {
        qApp->setOverrideCursor(Qt::ArrowCursor);
        QMessageBox::critical(this, tr("Opening file failed"),
            tr("Can't open file '%1' for writing.").arg(printer->outputFileName()));
        qApp->restoreOverrideCursor();
        return;
    }

    QRect rect = printer->pageLayout().paintRectPixels(printer->resolution());
    QImage img;
    _viewer->imageFromFramebuffer(rect.width(), rect.height(), 8, QColor(255,255,255), img);
    p.drawImage(0,0,img);
    p.end();
}

bool View3DInventor::containsViewProvider(const ViewProvider* vp) const
{
    return _viewer->containsViewProvider(vp);
}

// **********************************************************************************

static const char *_OverrideModePrefix = "## overrideMode: ";
static bool _ViewSyncing;

void View3DInventor::syncBoundViews(const char *pMsg)
{
    if(_ViewSyncing || 
            QApplication::queryKeyboardModifiers() == Qt::ControlModifier)
    {
        if (strcmp("ViewFit",pMsg) == 0) {
            _viewer->viewAll();
        } else if (strcmp("ViewSelection", pMsg) == 0) {
            _viewer->viewSelection();
        } else if (strcmp("ViewSelectionExtend", pMsg) == 0) {
            _viewer->viewSelection(true);
        }
        return;
    }

    Base::StateLocker guard(_ViewSyncing);
    auto views = boundViews();
    if(views.empty()) {
        const char *dummy;
        onMsg(pMsg, &dummy);
        return;
    }
    views.insert(this);
    for(auto view : views) {
        const char *dummy;
        bool enabled = view->_viewer->isAnimationEnabled();
        view->_viewer->setAnimationEnabled(false);
        view->onMsg(pMsg, &dummy);
        view->_viewer->setAnimationEnabled(enabled);
    }
    for(auto view : views) {
        view->camInfo.sync();
        view->boundCamInfo.sync();
    }
}

bool View3DInventor::onMsg(const char* pMsg, const char** ppReturn)
{
    if (strcmp("ViewFit",pMsg) == 0) {
        syncBoundViews(pMsg);
        return true;
    }
    else if (strcmp("ViewVR",pMsg) == 0) {
        // call the VR portion of the viewer
        _viewer->viewVR();
        return true;
    }
    else if(strcmp("ViewSelection",pMsg) == 0) {
        syncBoundViews(pMsg);
        return true;
    }
    else if(strcmp("ViewSelectionExtend",pMsg) == 0) {
        syncBoundViews(pMsg);
        return true;
    }
    else if (strcmp("ViewSelectionFront", pMsg) == 0) {
        _viewer->viewSelectionNormal(false);
        return true;
    }
    else if (strcmp("ViewSelectionBack", pMsg) == 0) {
        _viewer->viewSelectionNormal(true);
        return true;
    }
    else if (strcmp("RotationCenterSelection",pMsg) == 0) {
        _viewer->setRotationCenterSelection();
        return true;
    } else if(strcmp("SetStereoRedGreen",pMsg) == 0 ) {
        _viewer->setStereoMode(Quarter::SoQTQuarterAdaptor::ANAGLYPH);
        return true;
    }
    else if(strcmp("SetStereoQuadBuff",pMsg) == 0 ) {
        _viewer->setStereoMode(Quarter::SoQTQuarterAdaptor::QUAD_BUFFER );
        return true;
    }
    else if(strcmp("SetStereoInterleavedRows",pMsg) == 0 ) {
        _viewer->setStereoMode(Quarter::SoQTQuarterAdaptor::INTERLEAVED_ROWS );
        return true;
    }
    else if(strcmp("SetStereoInterleavedColumns",pMsg) == 0 ) {
        _viewer->setStereoMode(Quarter::SoQTQuarterAdaptor::INTERLEAVED_COLUMNS  );
        return true;
    }
    else if(strcmp("SetStereoOff",pMsg) == 0 ) {
        _viewer->setStereoMode(Quarter::SoQTQuarterAdaptor::MONO );
        return true;
    }
    else if(strcmp("Example1",pMsg) == 0 ) {
        auto root = new SoSeparator;
        Texture3D(root);
        _viewer->setSceneGraph(root);
        return true;
    }
    else if(strcmp("Example2",pMsg) == 0 ) {
        auto root = new SoSeparator;
        LightManip(root);
        _viewer->setSceneGraph(root);
        return true;
    }
    else if(strcmp("Example3",pMsg) == 0 ) {
        auto root = new SoSeparator;
        AnimationTexture(root);
        _viewer->setSceneGraph(root);
        return true;
    }
    else if(strcmp("GetCamera",pMsg) == 0 ) {
        SoCamera * Cam = _viewer->getSoRenderManager()->getCamera();
        if (!Cam)
            return false;
        std::ostringstream ss;
        ss << SoFCDB::writeNodesToString(Cam)
           << _OverrideModePrefix << _viewer->getOverrideMode();
        static std::string ret;
        ret = ss.str();
        *ppReturn = ret.c_str();
        return true;
    }
    else if(strncmp("SetCamera",pMsg,9) == 0 ) {
        return setCamera(pMsg+10);
    }
    else if(strncmp("Dump",pMsg,4) == 0 ) {
        dump(pMsg+5);
        return true;
    }
    else if(strcmp("ViewBottom",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Bottom));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("ViewFront",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Front));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("ViewLeft",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Left));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("ViewRear",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Rear));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("ViewRight",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Right));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("ViewTop",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Top));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("ViewAxo",pMsg) == 0 ) {
        _viewer->setCameraOrientation(Camera::rotation(Camera::Isometric));
        _viewer->viewAll();
        return true;
    }
    else if(strcmp("OrthographicCamera",pMsg) == 0 ) {
        _viewer->setCameraType(SoOrthographicCamera::getClassTypeId());
        bindCamera(boundCamera());
        return true;
    }
    else if(strcmp("PerspectiveCamera",pMsg) == 0 ) {
        _viewer->setCameraType(SoPerspectiveCamera::getClassTypeId());
        bindCamera(boundCamera());
        return true;
    }
    else if (strcmp("ZoomIn", pMsg) == 0) {
        View3DInventorViewer* viewer = getViewer();
        viewer->navigationStyle()->zoomIn();
        return true;
    }
    else if (strcmp("ZoomOut", pMsg) == 0) {
        View3DInventorViewer* viewer = getViewer();
        viewer->navigationStyle()->zoomOut();
        return true;
    }

    return MDIView::onMsg(pMsg,ppReturn);
}

bool View3DInventor::onHasMsg(const char* pMsg) const
{
    if (strcmp("AllowsOverlayOnHover",pMsg) == 0)
        return true;
    if (strcmp("CanPan", pMsg) == 0) {
        return true;
    }
    else if (strcmp("Print",pMsg) == 0) {
        return true;
    }
    else if (strcmp("PrintPreview",pMsg) == 0) {
        return true;
    }
    else if (strcmp("PrintPdf",pMsg) == 0) {
        return true;
    }
    else if(strcmp("SetStereoRedGreen",pMsg) == 0) {
        return true;
    }
    else if(strcmp("SetStereoQuadBuff",pMsg) == 0) {
        return true;
    }
    else if(strcmp("SetStereoInterleavedRows",pMsg) == 0) {
        return true;
    }
    else if(strcmp("SetStereoInterleavedColumns",pMsg) == 0) {
        return true;
    }
    else if(strcmp("SetStereoOff",pMsg) == 0) {
        return true;
    }
    else if(strcmp("Example1",pMsg) == 0) {
        return true;
    }
    else if(strcmp("Example2",pMsg) == 0) {
        return true;
    }
    else if(strcmp("Example3",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewFit",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewVR",pMsg) == 0) {
#ifdef BUILD_VR
        return true;
#else
        return false;
#endif
    }
    else if(strcmp("ViewSelection",pMsg) == 0) {
        return true;
    }
    else if (strcmp("ViewSelectionExtend", pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewSelectionFront",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewSelectionBack",pMsg) == 0) {
        return true;
    }
    else if (strcmp("RotationCenterSelection", pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewBottom",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewFront",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewLeft",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewRear",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewRight",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewTop",pMsg) == 0) {
        return true;
    }
    else if(strcmp("ViewAxo",pMsg) == 0) {
        return true;
    }
    else if(strcmp("GetCamera",pMsg) == 0) {
        return true;
    }
    else if(strncmp("SetCamera",pMsg,9) == 0) {
        return true;
    }
    else if(strncmp("Dump",pMsg,4) == 0) {
        return true;
    }
    else if (strcmp("ZoomIn", pMsg) == 0) {
        return true;
    }
    else if (strcmp("ZoomOut", pMsg) == 0) {
        return true;
    }

    return MDIView::onHasMsg(pMsg);
}

bool View3DInventor::setCamera(const char* pCamera, int animateDuration)
{
    // The camera application itself lives on the viewer (so viewers
    // without an MDI view can use it); this wrapper keeps the
    // view-level tail: camera binding and the saved draw-style.
    _viewer->setCamera(pCamera, animateDuration);

    bindCamera(boundCamera());

    const char *pos = std::strstr(pCamera,_OverrideModePrefix);
    if(pos) {
        size_t start = strlen(_OverrideModePrefix);
        for(;pos[start] && std::isspace(static_cast<unsigned char>(pos[start]));++start);
        size_t end = start;
        for(;pos[end] && pos[end]!='\n'; ++end);
        for(;end!=start && std::isspace(static_cast<unsigned char>(pos[end-1]));--end);
        std::string mode(pos+start, pos+end);
        // A camera saved with the Shadow draw style asks for something
        // that is no longer a style (docs/CoinRetirement.md 4d): the
        // renderer's light and map go on, and the display style stays
        // the view's own -- the migration has already put the one the
        // Shadow style wrapped there.
        if (mode == "Shadow")
            applyLegacyShadowStyle(this);
        else
            DrawStyle.setValue(mode.c_str());
    }

    return true;
}

void View3DInventor::bindCamera(SoCamera *pcCamera, bool sync)
{
    SoCamera * cam = _viewer->getSoRenderManager()->getCamera();
    if(camInfo.sensor.getAttachedNode() != cam) {
        camInfo.sensor.detach();
        if(cam)
            camInfo.sensor.attach(cam);
    }
    camInfo.sync();

    if(pcCamera == getCamera())
        return;

    if(!pcCamera) {
        boundCamInfo.sensor.detach();
        return;
    }

    if(pcCamera != boundCamInfo.sensor.getAttachedNode()) {
        boundCamInfo.sensor.detach();
        boundCamInfo.sensor.attach(pcCamera);
    }
    boundCamInfo.sync();

    if(sync) {
        camInfo.sync(pcCamera);
        camInfo.apply();
    }
}

SoCamera *View3DInventor::boundCamera() const
{
    return static_cast<SoCamera*>(boundCamInfo.sensor.getAttachedNode());
}

SoCamera *View3DInventor::getCamera() const
{
    return static_cast<SoCamera*>(camInfo.sensor.getAttachedNode());
}

View3DInventor *View3DInventor::boundView() const
{
    auto camera = boundCamera();
    if(!camera)
        return nullptr;

    for(auto doc : App::GetApplication().getDocuments()) {
        auto gdoc = Application::Instance->getDocument(doc);
        if(!gdoc)
            continue;
        for(auto v : gdoc->getViews()) {
            auto view = Base::freecad_dynamic_cast<View3DInventor>(v);
            if(view && view->getCamera() == camera)
                return view;
        }
    }
    return nullptr;
}

void View3DInventor::syncCamera(View3DInventor *view)
{
    auto camera = getCamera();
    if(!camera)
        return;
    if(!view) {
        if(boundCamera())
            bindCamera(boundCamera(), true);
        return;
    }
    auto cam = view->getCamera();
    if(!cam)
        return;
    if (camera == view->boundCamera()) {
        view->boundCamInfo.sync(cam);
        view->boundCamInfo.apply();
    } else {
        camInfo.sync(cam);
        camInfo.apply();
    }
}

bool View3DInventor::unbindView(const QString &text)
{
    if(text.isEmpty()) {
        for(auto view : boundViews())
            view->bindCamera(nullptr);
        bindCamera(nullptr);
        return true;
    }

    for(auto view : boundViews()) {
        if(view->windowTitle() == text) {
            if(boundCamera() == view->getCamera()) {
                bindCamera(nullptr);
                return true;
            }
            if(view->boundCamera() == getCamera()) {
                view->bindCamera(nullptr);
                return true;
            }
            break;
        }
    }
    return false;
}

void View3DInventor::boundViews(std::set<View3DInventor*> &views, bool recursive) const
{
    auto view = boundView();
    if(view) {
        if(views.insert(view).second && recursive)
            view->boundViews(views, true);
    }
    for(auto doc : App::GetApplication().getDocuments()) {
        auto gdoc = Application::Instance->getDocument(doc);
        if(!gdoc)
            continue;
        for(auto v : gdoc->getViews()) {
            auto view = Base::freecad_dynamic_cast<View3DInventor>(v);
            if(view && view->boundView() == this) {
                if(views.insert(view).second && recursive)
                    view->boundViews(views,true);
            }
        }
    }
}

std::set<View3DInventor*> View3DInventor::boundViews(bool recursive) const
{
    std::set<View3DInventor*> views;
    boundViews(views,recursive);
    return views;
}

View3DInventor *View3DInventor::bindView(const QString &title, bool sync)
{
    bindCamera(nullptr);
    if(title.isEmpty())
        return nullptr;

    for(auto doc : App::GetApplication().getDocuments()) {
        auto gdoc = Application::Instance->getDocument(doc);
        if(!gdoc)
            continue;
        for(auto v : gdoc->getViews()) {
            auto view = Base::freecad_dynamic_cast<View3DInventor>(v);
            if(!view || view == this || view->windowTitle()!=title)
                continue;

            if(view->boundViews(true).count(this))
                return nullptr;

            bindCamera(view->getCamera(), sync);
            return view;
        }
    }
    return nullptr;
}

bool View3DInventor::CameraInfo::sync(SoCamera *camera)
{
    auto myCamera = static_cast<SoCamera*>(sensor.getAttachedNode());
    if(!camera)
        camera = myCamera;
    if(!camera)
        return false;
    bool touched = false;
    if(position != camera->position.getValue()) {
        touched = true;
        position = camera->position.getValue();
    }
    if(orientation != camera->orientation.getValue()) {
        touched = true;
        orientation = camera->orientation.getValue();
    }
    if (camera->isOfType(SoOrthographicCamera::getClassTypeId())) {
        auto cam = static_cast<SoOrthographicCamera*>(camera);
        if(myCamera->isOfType(SoOrthographicCamera::getClassTypeId())) {
            if(height != cam->height.getValue()) {
                touched = true;
                height = cam->height.getValue();
            }
        } else if (focal != cam->height.getValue()) {
            touched = true;
            focal = cam->height.getValue();
        }
    } else if (myCamera->isOfType(SoOrthographicCamera::getClassTypeId())) {
        if(height != camera->focalDistance.getValue()) {
            touched = true;
            height = camera->focalDistance.getValue();
        }
    } else if (focal != camera->focalDistance.getValue()) {
        touched = true;
        focal = camera->focalDistance.getValue();
    }
    return touched;
}

void View3DInventor::CameraInfo::apply(SoCamera *camera)
{
    if(!camera)
        camera = static_cast<SoCamera*>(sensor.getAttachedNode());
    if(!camera)
        return;
    camera->position = position;
    camera->orientation = orientation;
    if(camera->isOfType(SoOrthographicCamera::getClassTypeId()))
        static_cast<SoOrthographicCamera*>(camera)->height = height;
    else
        camera->focalDistance = focal;
}

void View3DInventor::onCameraChanged(CameraInfo &src, CameraInfo &dst)
{
    if(!src.attached() || !dst.attached())
        return;

    SbMatrix mat;
    SbVec3f s(1,1,1);
    mat.setTransform(src.position,src.orientation,s);
    float focal = src.focal;
    float height = src.height;

    if(!src.sync())
        return;

    if(QApplication::queryKeyboardModifiers() == Qt::ControlModifier)
        return;

    SbMatrix mat2;
    mat2.setTransform(src.position,src.orientation,s);

    SbMatrix mat3;
    mat3.setTransform(dst.position,dst.orientation,s);

    mat3 *= mat.inverse() * mat2;
    SbRotation so;
    mat3.getTransform(dst.position,dst.orientation,s,so);

    if(src.sensor.getAttachedNode()->isOfType(
                SoPerspectiveCamera::getClassTypeId()))
    {
        if(src.focal != focal && std::abs(focal) > 1e-10) {
            float m = src.focal/focal;
            dst.focal *= m;
            dst.height *= m;
        }
    } else if (src.height != height && std::abs(height) > 1e-10) {
        float m = src.height/height;
        if (dst.sensor.getAttachedNode()->isOfType(
                    SoOrthographicCamera::getClassTypeId()))
        {
            dst.height *= m;
        } else {
            float newfocal = dst.focal * m;
            SbVec3f direction;
            dst.orientation.multVec(SbVec3f(0, 0, -1), direction);
            SbVec3f newpos = dst.position + (newfocal - dst.focal) * -direction;
            if(newpos.length() <= float(sqrt(FLT_MAX))) {
                dst.position = newpos;
                dst.focal = newfocal;
            }
        }
    }
    dst.apply();
}

void View3DInventor::toggleClippingPlane()
{
    _viewer->toggleClippingPlane();
}

bool View3DInventor::hasClippingPlane() const
{
    return _viewer->hasClippingPlane();
}

void View3DInventor::setOverlayWidget(QWidget* widget)
{
    removeOverlayWidget();
    stack->addWidget(widget);
    stack->setCurrentIndex(1);
}

void View3DInventor::removeOverlayWidget()
{
    stack->setCurrentIndex(0);
    QWidget* overlay = stack->widget(1);
    if (overlay) stack->removeWidget(overlay);
}

void View3DInventor::setOverrideCursor(const QCursor& aCursor)
{
    _viewer->getWidget()->setCursor(aCursor);
}

void View3DInventor::restoreOverrideCursor()
{
    _viewer->getWidget()->setCursor(QCursor(Qt::ArrowCursor));
}

void View3DInventor::dump(const char* filename, bool onlyVisible)
{
    _viewer->dump(filename, onlyVisible);
}

void View3DInventor::windowStateChanged(QWidget* view)
{
    bool canStartTimer = false;
    if (this != view) {
        // If both views are child widgets of the workspace and view is maximized this view
        // must be hidden, hence we can start the timer.
        // Note: If view is top-level or fullscreen it doesn't necessarily hide the other view
        // e.g. if it is on a second monitor.
        canStartTimer = (!this->isWindow() && !view->isWindow() && view->isMaximized());
    } else if (isMinimized()) {
        // I am the active view but minimized
        canStartTimer = true;
    }

    if (canStartTimer) {
        int msecs = viewSettings->stopAnimatingIfDeactivated();
        if (!stopSpinTimer->isActive() && msecs >= 0) { // if < 0 do not stop rotation
            stopSpinTimer->setSingleShot(true);
            stopSpinTimer->start(msecs);
        }
    } else if (stopSpinTimer->isActive()) {
        // If this view may be visible again we can stop the timer
        stopSpinTimer->stop();
    }

    // The same news answers a second question -- whether this view still
    // has anyone looking at it -- and the render engine gives its targets
    // back when the answer is no. Deliberately re-asked as a question
    // rather than derived from this event: a tab switch changes the state
    // of two views and emits for each, so which emission arrives last is
    // not something to depend on.
    if (_viewer)
        _viewer->armBackgroundRelease();
}

bool View3DInventor::isBackgroundView() const
{
    // A cell of a split view area is never maximized itself and would
    // wrongly see its own (maximized) container as "somebody maximized
    // over me". The container's status answers for its visible cells; a
    // cell hidden inside the container answers for itself.
    if (auto area = ViewArea::areaOf(this))
        return !isVisible() || area->isBackgroundView();
    return MDIView::isBackgroundView();
}

void View3DInventor::stopAnimating()
{
    _viewer->stopAnimating();
}

/**
 * Drops the event \a e and writes the right Python command.
 */
void View3DInventor::dropEvent (QDropEvent * e)
{
    const QMimeData* data = e->mimeData();
    if (data->hasUrls()) {
        getMainWindow()->loadUrls(getAppDocument(), data->urls());
    }
    else {
        MDIView::dropEvent(e);
    }
}

void View3DInventor::dragEnterEvent (QDragEnterEvent * e)
{
    // Here we must allow uri drags and check them in dropEvent
    const QMimeData* data = e->mimeData();
    if (data->hasUrls())
        e->accept();
    else
        e->ignore();
}

void View3DInventor::setCurrentViewMode(ViewMode newmode)
{
    ViewMode oldmode = MDIView::currentViewMode();
    if (oldmode == newmode)
        return;

    if (newmode == Child) {
        // Fix in two steps:
        // The mdi view got a QWindow when it became a top-level widget and when resetting it to a child widget
        // the QWindow must be deleted because it has an impact on resize events and may break the layout of
        // mdi view inside the QMdiSubWindow.
        // In the second step below the layout must be invalidated after it's again a child widget to make sure
        // the mdi view fits into the QMdiSubWindow.
        QWindow* winHandle = this->windowHandle();
        if (winHandle)
            winHandle->destroy();
    }

    MDIView::setCurrentViewMode(newmode);

    // Internally the QOpenGLWidget switches of the multi-sampling and there is no
    // way to switch it on again. So as a workaround we just re-create a new viewport
    // The method is private but defined as slot to avoid to call it by accident.
    //int index = _viewer->metaObject()->indexOfMethod("replaceViewport()");
    //if (index >= 0) {
    //    _viewer->qt_metacall(QMetaObject::InvokeMetaMethod, index, 0);
    //}

    // This widget becomes the focus proxy of the embedded GL widget if we leave
    // the 'Child' mode. If we reenter 'Child' mode the focus proxy is reset to 0.
    // If we change from 'TopLevel' mode to 'Fullscreen' mode or vice versa nothing
    // happens.
    // Grabbing keyboard when leaving 'Child' mode (as done in a recent version) should
    // be avoided because when two or more windows are either in 'TopLevel' or 'Fullscreen'
    // mode only the last window gets all key event even if it is not the active one.
    //
    // It is important to set the focus proxy to get all key events otherwise we would lose
    // control after redirecting the first key event to the GL widget.
    if (oldmode == Child) {
        // To make a global shortcut working from this window we need to add
        // all existing actions from the mainwindow and its sub-widgets
        QList<QAction*> acts = getMainWindow()->findChildren<QAction*>();
        this->addActions(acts);
        _viewer->getGLWidget()->setFocusProxy(this);
        // To be notfified for new actions
        qApp->installEventFilter(this);
    }
    else if (newmode == Child) {
        _viewer->getGLWidget()->setFocusProxy(nullptr);
        qApp->removeEventFilter(this);
        QList<QAction*> acts = this->actions();
        for (QAction* it : acts)
            this->removeAction(it);

        // Step two
        auto mdi = qobject_cast<QMdiSubWindow*>(parentWidget());
        if (mdi && mdi->layout())
            mdi->layout()->invalidate();
    }
}

bool View3DInventor::eventFilter(QObject* watched, QEvent* e)
{
    // As long as this widget is a top-level window (either in 'TopLevel' or 'FullScreen' mode) we
    // need to be notified when an action is added to a widget. This action must also be added to
    // this window to allow to make use of its shortcut (if defined).
    // Note: We don't need to care about removing an action if its parent widget gets destroyed.
    // This does the action itself for us.
    if (watched != this && e->type() == QEvent::ActionAdded) {
        auto a = static_cast<QActionEvent*>(e);
        QAction* action = a->action();

        if (!action->isSeparator()) {
            QList<QAction*> actions = this->actions();
            if (!actions.contains(action))
                this->addAction(action);
        }
    }

    return false;
}

void View3DInventor::keyPressEvent (QKeyEvent* e)
{
    // See StdViewDockUndockFullscreen::activated()
    // With Qt5 one cannot directly use 'setCurrentViewMode'
    // of an MDI view because it causes rendering problems.
    // The only reliable solution is to clone the MDI view,
    // set its view mode and close the original MDI view.

    QMainWindow::keyPressEvent(e);
}

void View3DInventor::keyReleaseEvent (QKeyEvent* e)
{
    QMainWindow::keyReleaseEvent(e);
}

void View3DInventor::focusInEvent (QFocusEvent *)
{
    _viewer->getGLWidget()->setFocus();
}

void View3DInventor::contextMenuEvent (QContextMenuEvent*e)
{
    MDIView::contextMenuEvent(e);
}

void View3DInventor::customEvent(QEvent * e)
{
    if (e->type() == QEvent::User) {
        auto se = static_cast<NavigationStyleEvent*>(e);
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath
            ("User parameter:BaseApp/Preferences/View");
        if (hGrp->GetBool("SameStyleForAllViews", true))
            hGrp->SetASCII("NavigationStyle", se->style().getName());
        else
            _viewer->setNavigationType(se->style());
    }
}

void View3DInventor::Restore(Base::XMLReader &reader)
{
    Base::StateLocker guard(_restoring);
    MDIView::Restore(reader);
    // A Light_* property exists only where the author overrode the rig, so
    // whatever the document carried has to reach the light nodes now. The
    // keys it did not carry keep following this installation's preference.
    if (_viewer)
        _viewer->syncLightProperties();
    // Old documents saved per-view copies of render properties that have
    // since been retired to global RenderParams (debug switches, ladder
    // tuning, the GPU budget). They restore fine as dynamic properties,
    // but nothing reads them anymore and their values used to shadow the
    // globals -- strip them so the dead surface does not linger or get
    // re-saved.
    stripLegacyRenderProperties(this);
    reseedLocalRenderProperties(this);
    // A document written before the Shadow draw style became the
    // renderer's light, map and ground (docs/CoinRetirement.md 4d)
    // carries its settings as Shadow_* and its style as an enum value
    // that is on its way out; move both onto what reads them now.
    migrateShadowProperties(this);
}

namespace {
/// Parse the ObjectDisplayModes property (docs/CoinRetirement.md 5.9)
/// into backend override entries, resolving every path element to its
/// true {document, object} pair so the backend never touches a
/// document. An unresolvable key -- its object was deleted -- is INERT
/// rather than an error: the entry stays in the map and revives when
/// the property is next touched with the object back.
Render::StyleOverrideTable parseObjectDisplayModes(
        const std::map<std::string, std::string> &modes,
        App::Document *doc)
{
    Render::StyleOverrideTable table;
    if (!doc)
        return table;
    for (const auto &kv : modes) {
        const std::string &key = kv.first;
        const std::string &mode = kv.second;
        if (key.empty() || mode.empty())
            continue;
        Render::StyleOverride ov;
        if (SoFCUnifiedSelection::DisplayModeAsIs == mode.c_str()) {
            // Pin to the object's own mode: escapes the view style
            // (SolidWorks' "Default Display").
            ov.pin = true;
        }
        else {
            // EVERY named mode resolves by additive capture first
            // (5.9 "Non-standard modes"): the feed captures the named
            // child tagged with this interned id and the entry admits
            // exactly the draws so tagged -- the mode's own subgraph,
            // not a mask approximation of it. That is the only way a
            // mode can reach an object whose superset child does not
            // contain its buckets (Mesh's "Points": the Flat Lines
            // child has no point rendering at all). A mode no object
            // registers -- a stale value, a viewer-level name like
            // "Hidden Line" -- stays inert through the same fallback
            // a Class-A style takes: no child of that name, the
            // object keeps its own mode.
            ov.modeId = Render::internModeName(mode.c_str());
            // The Class-A mask stays as the fallback for a switch the
            // additive capture has not covered (an interest list past
            // its 16-entry budget).
            ov.nameBit = Gui::styleNameBitOf(mode.c_str());
            if (ov.nameBit)
                ov.mask =
                    View3DInventorViewer::drawStyleMaskFromName(mode.c_str());
        }
        if (key.find('.') == std::string::npos) {
            // Bare form: the object wherever it appears in this view.
            // "Doc#Obj" names an object of another document shown here
            // through a link; a plain name is of this view's document.
            ov.rooted = false;
            auto sep = key.find('#');
            if (sep != std::string::npos)
                ov.path.push_back({key.substr(0, sep),
                                   key.substr(sep + 1)});
            else
                ov.path.push_back({doc->getName(), key});
        }
        else {
            // Path form: one occurrence, resolved token by token so
            // every element carries its true document -- getSubObject
            // follows links across documents the same way the scene
            // graph does.
            ov.rooted = true;
            std::istringstream iss(key);
            std::string tok;
            App::DocumentObject *cur = nullptr;
            bool ok = true;
            while (std::getline(iss, tok, '.')) {
                if (tok.empty())
                    continue;
                if (!cur)
                    cur = doc->getObject(tok.c_str());
                else
                    cur = cur->getSubObject((tok + ".").c_str());
                if (!cur || !cur->isAttachedToDocument()) {
                    ok = false;
                    break;
                }
                ov.path.push_back({cur->getDocument()->getName(),
                                   cur->getNameInDocument()});
            }
            if (!ok || ov.path.empty())
                continue;
        }
        table.entries.push_back(std::move(ov));
    }
    return table;
}
} // namespace

void View3DInventor::onChanged(const App::Property *prop)
{
    if (_viewer) {
        if (prop == &DrawStyle) {
            if (!DrawStyle.testStatus(App::Property::User1)) {
                Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                        App::Property::User1, &DrawStyle);
                _viewer->setOverrideMode(DrawStyle.getValueAsString());
            }
        }
        else if (prop == &ObjectDisplayModes) {
            _viewer->setObjectStyleOverrides(parseObjectDisplayModes(
                    ObjectDisplayModes.getValues(),
                    getGuiDocument() ? getGuiDocument()->getDocument()
                                     : nullptr));
            // Keep the DisplayModeInView rows honest while this is the
            // view they present (docs/CoinRetirement.md 5.9): a table
            // change from anywhere -- the rows themselves, the context
            // command, undo, restore -- re-reads them.
            if (Application::Instance->activeView() == this) {
                if (auto gdoc = getGuiDocument()) {
                    for (auto obj : gdoc->getDocument()->getObjects()) {
                        if (auto vp = Base::freecad_dynamic_cast<
                                ViewProviderDocumentObject>(
                                    gdoc->getViewProvider(obj)))
                            vp->syncDisplayModeInView(this);
                    }
                }
            }
        }
        _viewer->onViewPropertyChanged(*prop);
    }
    MDIView::onChanged(prop);
}

#include "moc_View3DInventor.cpp"
