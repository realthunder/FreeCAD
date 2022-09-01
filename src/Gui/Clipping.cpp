/***************************************************************************
 *   Copyright (c) 2013 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <cmath>
# include <climits>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/nodes/SoClipPlane.h>
# include <Inventor/nodes/SoGroup.h>
# include <Inventor/sensors/SoTimerSensor.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoIndexedLineSet.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <QDockWidget>
# include <QPointer>
# include <QScrollArea>
# include <QAction>
# include <QMessageBox>
# include <cmath>
#endif

#include <Inventor/SmSwitchboard.h>
#include <Inventor/nodekits/SoBaseKit.h>
#include <QStackedWidget>

#include <Base/Tools.h>
#include <App/SavedView.h>
#include "Clipping.h"
#include "ui_Clipping.h"
#include "Application.h"
#include "DockWindowManager.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderSavedView.h"
#include "ViewParams.h"
#include "MainWindow.h"
#include "SoFCCSysDragger.h"
#include "SoFCUnifiedSelection.h"

using namespace Gui::Dialog;
using namespace Gui;

SO_NODE_SOURCE(ClipDragger);

ClipDragger::ClipDragger(SoClipPlane *clip, bool custom)
{
    SO_NODE_CONSTRUCTOR(ClipDragger);
    SO_NODE_ADD_FIELD(plane, (SbPlane(SbVec3f(0,0,1), 0)));
    SO_NODE_ADD_FIELD(planeSize, (ViewParams::getClipPlaneSize()));
    SO_NODE_ADD_FIELD(lineWidth, (ViewParams::getClipPlaneLineWidth()));
    QColor c(ViewParams::getClipPlaneColor().c_str());
    if (!c.isValid())
        c = QColor(ViewParams::defaultClipPlaneColor().c_str());
    SO_NODE_ADD_FIELD(planeColor, (c.redF(),c.greenF(),c.blueF()));

    init(clip, custom);
}

ClipDragger::~ClipDragger()
{
}
    
void ClipDragger::init(SoClipPlane *clip, bool custom)
{
    if (this->clip || !clip)
        return;

    this->clip = clip;
    // hideTranslationX();
    // hideTranslationY();
    hideRotationZ();
    if (!custom) {
        hideRotationX();
        hideRotationY();
    }

    SoSeparator *sep = new SoSeparator;
    auto pickStyle = new SoPickStyle;
    pickStyle->style.setValue(SoPickStyle::UNPICKABLE);
    sep->addChild(pickStyle);

    this->color = new SoBaseColor;
    this->color->rgb.setValue(this->planeColor.getValue());
    sep->addChild(this->color);

    this->drawStyle = new SoDrawStyle;
    this->drawStyle->lineWidth.setValue(this->lineWidth.getValue());
    sep->addChild(this->drawStyle);

    sep->renderCaching = SoSeparator::OFF;
    coords = new SoCoordinate3;
    updateSize();
    sep->addChild(coords);

    static const int32_t lines[6] = { 0, 1, 2, 3, 0, -1 };
    auto lineset = new SoIndexedLineSet;
    lineset->coordIndex.setNum(6);
    lineset->coordIndex.setValues(0, 6, lines);
    sep->addChild (lineset);

    auto root = SO_GET_ANY_PART(this, "annotation", SoFCPathAnnotation);
    root->addChild(sep);

    addStartCallback([](void *ctx, SoDragger*){reinterpret_cast<ClipDragger*>(ctx)->onDragStart();}, this);
    addFinishCallback([](void *ctx, SoDragger*){reinterpret_cast<ClipDragger*>(ctx)->onDragFinish();}, this);
    addMotionCallback([](void *ctx, SoDragger*){reinterpret_cast<ClipDragger*>(ctx)->onDragMotion();}, this);

    this->plane.setValue(clip->plane.getValue());
    this->plane.connectFrom(&clip->plane);
}

void ClipDragger::onDragStart()
{
}

void ClipDragger::onDragFinish()
{
    onDragMotion();
    if (dragDone)
        dragDone(this);
}

void ClipDragger::onDragMotion()
{
    Base::StateLocker lock(busy);
    SbVec3f n, t;
    auto r = this->rotation.getValue();
    r.multVec(SbVec3f(0, 0, 1), n);
    r.inverse().multVec(this->translation.getValue(), t);
    clip->plane.setValue(SbPlane(n, t[2]));
}

void ClipDragger::updateSize()
{
    float size = planeSize.getValue() * 0.5f;
    SbVec3f verts[4] = {
        SbVec3f(size,size,0),   SbVec3f(size,-size,0),
        SbVec3f(-size,-size,0), SbVec3f(-size,size,0),
    };
    coords->point.setNum(4);
    coords->point.setValues(0, 4, verts);
}

void ClipDragger::notify(SoNotList * l)
{
    SoField * f = l->getLastField();
    if (f == &this->planeSize)
        updateSize();
    else if (f == &this->plane) {
        if (busy) return;
        Base::StateLocker lock(busy);
        SbVec3f t;
        this->rotation.getValue().inverse().multVec(this->translation.getValue(), t);

        SbRotation rot(SbVec3f(0,0,1), plane.getValue().getNormal());
        this->rotation.setValue(rot);

        t[2] = plane.getValue().getDistanceFromOrigin();
        rot.multVec(t, t);
        this->translation.setValue(t);
    }
    else if (f == &this->planeColor)
        this->color->rgb.setValue(this->planeColor.getValue());
    else if (f == &this->lineWidth)
        this->drawStyle->lineWidth.setValue(this->lineWidth.getValue());
    else
        inherited::notify(l);
}

void ClipDragger::initClass(void)
{
    SO_NODE_INIT_CLASS(ClipDragger,SoFCCSysDragger,"ClipDragger");
}

class Clipping::Private {
public:
    Ui_Clipping ui;
    QPointer<Gui::View3DInventor> view;
    CoinPtr<SoGroup> node;
    CoinPtr<SoGroup> auxNode;
    CoinPtr<SmSwitchboard> clipSwitch;
    CoinPtr<SoClipPlane> clipX;
    CoinPtr<SoClipPlane> clipY;
    CoinPtr<SoClipPlane> clipZ;
    CoinPtr<SoClipPlane> clipView;
    CoinPtr<ClipDragger> draggerX;
    CoinPtr<ClipDragger> draggerY;
    CoinPtr<ClipDragger> draggerZ;
    CoinPtr<ClipDragger> draggerCustom;
    CoinPtr<SoPickStyle> pickStyle;
    bool flipX;
    bool flipY;
    bool flipZ;
    SoTimerSensor* sensor;
    bool busy = false;

    void initClip(CoinPtr<SoClipPlane> &clip,
                  CoinPtr<ClipDragger> &dragger,
                  PrefDoubleSpinBox *spinBox,
                  SoCamera *camera,
                  float x, float y, float z)
    {
        clip = new SoClipPlane;
        clip->on.setValue(false);
        clip->plane.setValue(SbPlane(SbVec3f(x, y, z), 0));
        this->node->insertChild(clip, 0);

        dragger = new ClipDragger(clip, spinBox == ui.clipView);
        clipSwitch->addChild(dragger);

        dragger->dragDone = [this, spinBox](ClipDragger *dragger) {
            Base::StateLocker guard(this->busy);
            const auto &pln = dragger->getClipPlane()->plane.getValue();
            SbVec3f n = pln.getNormal();
            double d = pln.getDistanceFromOrigin();
            if (spinBox == ui.clipView) {
                ui.dirX->setValue(n[0]);
                ui.dirY->setValue(n[1]);
                ui.dirZ->setValue(n[2]);

                double alpha, beta, gamma;
                Base::Rotation rot(Base::Vector3d(0, 0, 1), Base::Vector3d(n[0], n[1], n[2]));
                rot.getEulerAngles(Base::Rotation::Intrinsic_XYZ, alpha, beta, gamma);
                ui.angleX->setValue(alpha);
                ui.angleY->setValue(beta);
            }
            spinBox->setValue(d);
        };

        dragger->draggerSize.setValue(0.02);
        dragger->setUpAutoScale(camera);
    }

    void onDirectionChanged()
    {
        if (busy) return;
        Base::StateLocker guard(busy);
        double x = ui.dirX->value();
        double y = ui.dirY->value();
        double z = ui.dirZ->value();
        SbPlane pln = clipView->plane.getValue();
        SbVec3f normal(x,y,z);
        if (normal.sqrLength() > 0.0f) {
            clipView->plane.setValue(SbPlane(normal,pln.getDistanceFromOrigin()));

            Base::Rotation rot(Base::Vector3d(0, 0, 1), Base::Vector3d(normal[0], normal[1], normal[2]));
            double alpha, beta, gamma;
            rot.getEulerAngles(Base::Rotation::Intrinsic_XYZ, alpha, beta, gamma);
            ui.angleX->setValue(alpha);
            ui.angleY->setValue(beta);
        }
    }

    void onAngleChanged()
    {
        if (busy) return;
        Base::StateLocker guard(busy);
        Base::Rotation rot;
        rot.setEulerAngles(Base::Rotation::Intrinsic_XYZ,
                           ui.angleX->value(),
                           ui.angleY->value(),
                           0);

        auto normal = rot.multVec(Base::Vector3d(0, 0, 1));
        ui.dirX->setValue(normal.x);
        ui.dirY->setValue(normal.y);
        ui.dirZ->setValue(normal.z);
        SbPlane pln = clipView->plane.getValue();
        clipView->plane.setValue(SbPlane(SbVec3f(normal.x, normal.y, normal.z),
                                         pln.getDistanceFromOrigin()));
    }

    void initView(View3DInventor *view)
    {
        this->view = view;
        View3DInventorViewer* viewer = view->getViewer();
        this->node = static_cast<SoGroup*>(viewer->getSceneGraph());
        auto camera = viewer->getSoRenderManager()->getCamera();

        this->pickStyle = new SoPickStyle;
        this->node->insertChild(this->pickStyle, 0);

        initClip(this->clipX, this->draggerX, ui.clipX, camera, 1, 0, 0);
        initClip(this->clipY, this->draggerY, ui.clipY, camera, 0, 1, 0);
        initClip(this->clipZ, this->draggerZ, ui.clipZ, camera, 0, 0, 1);
        initClip(this->clipView, this->draggerCustom, ui.clipView, camera, 0, 0, 1);

        this->auxNode = static_cast<SoGroup*>(viewer->getAuxSceneGraph());
        // this->auxNode = static_cast<SoGroup*>(viewer->getSceneGraph());
        this->auxNode->insertChild(this->clipSwitch, 0);
    }

    Private()
        : clipSwitch(new SmSwitchboard)
        , flipX(false), flipY(false), flipZ(false)
    {
        sensor = new SoTimerSensor(moveCallback, this);
    }
    ~Private()
    {
        draggerX->dragDone = nullptr;
        draggerY->dragDone = nullptr;
        draggerZ->dragDone = nullptr;
        draggerCustom->dragDone = nullptr;
        delete sensor;
    }
    static void moveCallback(void * data, SoSensor * sensor)
    {
        Q_UNUSED(sensor);
        Private* self = reinterpret_cast<Private*>(data);
        if (self->view) {
            Gui::View3DInventorViewer* view = self->view->getViewer();
            SoClipPlane* clip = self->clipView;
            SbPlane pln = clip->plane.getValue();
            clip->plane.setValue(SbPlane(view->getViewDirection(),pln.getDistanceFromOrigin()));
        }
    }

    void updateSwitch()
    {
        SbBool sw[4];
        bool show = ui.checkBoxShowPlane->isChecked();
        sw[0] = show && clipX->on.getValue();
        sw[1] = show && clipY->on.getValue();
        sw[2] = show && clipZ->on.getValue();
        sw[3] = show && clipView->on.getValue();
        clipSwitch->enable.setValues(0, 4, sw);
        if (sw[0] || sw[1] || sw[2] || sw[3])
            pickStyle->style.setValue(SoPickStyle::UNPICKABLE);
        else
            pickStyle->style.setValue(SoPickStyle::SHAPE);
    }
};

/* TRANSLATOR Gui::Dialog::Clipping */

Clipping::Clipping(Gui::View3DInventor* view, QWidget* parent)
  : QDialog(parent)
  , d(new Private)
{
    connect(view, SIGNAL(destroyed(QObject*)), this, SLOT(onViewDestroyed(QObject*)));

    // create widgets
    d->ui.setupUi(this);
    d->initView(view);

    // Install event filter to suppress wheel focus
    for(auto child : this->findChildren<QWidget*>()) {
        if (child->focusPolicy() == Qt::WheelFocus) {
            child->setFocusPolicy(Qt::StrongFocus);
            child->installEventFilter(this);
        }
    }

    d->ui.checkBoxFill->setChecked(ViewParams::getSectionFill() && ViewParams::isUsingRenderer());
    d->ui.checkBoxInvert->initAutoSave();
    d->ui.checkBoxConcave->initAutoSave();
    d->ui.checkBoxInvert->initAutoSave();
    d->ui.checkBoxOnTop->initAutoSave();
    d->ui.checkBoxHatch->initAutoSave();
    d->ui.spinBoxHatchScale->initAutoSave();
    d->ui.checkBoxGroupRendering->initAutoSave();
    d->ui.checkBoxBacklight->initAutoSave();
    d->ui.backlightColor->initAutoSave();
    d->ui.sliderIntensity->initAutoSave();
    d->ui.checkBoxShowPlane->initAutoSave();
    d->ui.spinBoxPlaneSize->initAutoSave();
    d->ui.editHatchTexture->setFileName(
            QString::fromUtf8(ViewParams::getSectionHatchTexture().c_str()));

    d->ui.checkBoxOnTop->setDisabled(ViewParams::getSectionConcave());
    d->ui.checkBoxGroupRendering->setDisabled(ViewParams::getSectionConcave());

    if (!d->ui.checkBoxFill->isChecked()) {
        d->ui.checkBoxInvert->setDisabled(true);
        d->ui.checkBoxConcave->setDisabled(true);
        d->ui.checkBoxOnTop->setDisabled(true);
        d->ui.checkBoxHatch->setDisabled(true);
        d->ui.editHatchTexture->setDisabled(true);
        d->ui.spinBoxHatchScale->setDisabled(true);
    }

    QObject::connect(d->ui.spinBoxPlaneSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        [this](double size) {
            d->draggerX->planeSize.setValue(size);
            d->draggerY->planeSize.setValue(size);
            d->draggerZ->planeSize.setValue(size);
            d->draggerCustom->planeSize.setValue(size);
        });

    auto setupSpinBox = [](PrefDoubleSpinBox *spinbox, const char *name, bool subentry) {
        spinbox->setParamGrpPath("View/Clipping");
        spinbox->setEntryName(name);
        if (!subentry) {
            // Sub entries (decimals, single step) are now exposed as dedicated
            // spinbox. So remove them from context menu.
            spinbox->setSubEntries(spinbox, {});
        }
        spinbox->onRestore();
    };

    d->ui.clipView->setRange(-INT_MAX,INT_MAX);
    d->ui.clipView->setSingleStep(0.1f);
    d->ui.clipX->setRange(-INT_MAX,INT_MAX);
    d->ui.clipX->setSingleStep(0.1f);
    d->ui.clipY->setRange(-INT_MAX,INT_MAX);
    d->ui.clipY->setSingleStep(0.1f);
    d->ui.clipZ->setRange(-INT_MAX,INT_MAX);
    d->ui.clipZ->setSingleStep(0.1f);

    d->ui.dirX->setRange(-INT_MAX,INT_MAX);
    d->ui.dirX->setSingleStep(0.1f);
    d->ui.dirY->setRange(-INT_MAX,INT_MAX);
    d->ui.dirY->setSingleStep(0.1f);
    d->ui.dirZ->setRange(-INT_MAX,INT_MAX);
    d->ui.dirZ->setSingleStep(0.1f);
    d->ui.dirZ->setValue(1.0f);

    d->ui.angleX->setSingleStep(1.0f);
    d->ui.angleX->setRange(-180,180);
    d->ui.angleY->setSingleStep(1.0f);
    d->ui.angleY->setRange(-180,180);

    setupSpinBox(d->ui.clipView, "ClipView", false);
    setupSpinBox(d->ui.clipX, "ClipX", false);
    setupSpinBox(d->ui.clipY, "ClipY", false);
    setupSpinBox(d->ui.clipZ, "ClipZ", false);
    setupSpinBox(d->ui.dirX, "DirX", false);
    setupSpinBox(d->ui.dirY, "DirY", false);
    setupSpinBox(d->ui.dirZ, "DirZ", false);
    setupSpinBox(d->ui.angleX, "AngleX", false);
    setupSpinBox(d->ui.angleY, "AngleY", false);

    d->ui.spinBoxTranslation->signalSubEntryChanged.connect(
        [this](QObject *o, const PrefWidget::SubEntry *entry) {
            auto v = o->property(entry->name);
            d->ui.clipView->setProperty(entry->name, v);
            d->ui.clipX->setProperty(entry->name, v);
            d->ui.clipY->setProperty(entry->name, v);
            d->ui.clipZ->setProperty(entry->name, v);
            d->ui.dirX->setProperty(entry->name, v);
            d->ui.dirY->setProperty(entry->name, v);
            d->ui.dirZ->setProperty(entry->name, v);
            if (entry->name == "singleStep") {
                double step = v.toReal();
                d->draggerX->translationIncrement.setValue(step);
                d->draggerY->translationIncrement.setValue(step);
                d->draggerZ->translationIncrement.setValue(step);
                d->draggerCustom->translationIncrement.setValue(step);
            }
        });

    d->ui.spinBoxRotation->signalSubEntryChanged.connect(
        [this](QObject *o, const PrefWidget::SubEntry *entry) {
            auto v = o->property(entry->name);
            d->ui.angleX->setProperty(entry->name, v);
            d->ui.angleY->setProperty(entry->name, v);
            if (entry->name == "singleStep") {
                double step = v.toReal() * D_PI/180.0;
                d->draggerX->rotationIncrement.setValue(step);
                d->draggerY->rotationIncrement.setValue(step);
                d->draggerZ->rotationIncrement.setValue(step);
                d->draggerCustom->rotationIncrement.setValue(step);
            }
        });

    setupSpinBox(d->ui.spinBoxTranslation, "Translation", true);
    setupSpinBox(d->ui.spinBoxRotation, "Rotation", true);

    QObject::connect(d->ui.buttonBox, &QDialogButtonBox::clicked,
        [this](QAbstractButton *button) {
            if (!d->view || !d->view->getAppDocument())
                return;
            ViewProviderSavedView::CaptureOptions options;
            if (button == d->ui.buttonBox->button(QDialogButtonBox::Save))
                options = ViewProviderSavedView::CaptureOption::Clippings;
            else if (button == d->ui.buttonBox->button(QDialogButtonBox::SaveAll))
                options = ViewProviderSavedView::CaptureOption::Default;
            else
                return;
            auto doc = d->view->getAppDocument();
            auto sels = Gui::Selection().getObjectsOfType(App::SavedView::getClassTypeId(), doc->getName());
            ViewProviderSavedView *vp = nullptr;
            if (sels.size())
                vp = Base::freecad_dynamic_cast<ViewProviderSavedView>(
                        Application::Instance->getViewProvider(sels[0]));
            else 
                vp = Base::freecad_dynamic_cast<ViewProviderSavedView>(
                        Application::Instance->getViewProvider(doc->addObject("App::SavedView", "SavedView")));
            if (vp)
                vp->capture(options);

        });
}

static QPointer<QDockWidget> _DockWidget;
static QPointer<QStackedWidget> _StackedWidget;
static std::map<QObject*, QPointer<QScrollArea> > _Clippings;
static bool _Inited = false;

void Clipping::onViewDestroyed(QObject *o)
{
    _Clippings.erase(o);
    auto parent = parentWidget();
    if (parent) {
        parent = parent->parentWidget();
        if (parent)
            parent->deleteLater();
    }
}

static QWidget *bindView(Gui::View3DInventor *view)
{
    if (!_StackedWidget)
        return nullptr;
    auto &scrollArea = _Clippings[view];
    if (!scrollArea) {
        scrollArea = new QScrollArea(nullptr);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto clipping = new Clipping(view, scrollArea);
        scrollArea->setWidget(clipping);
        _StackedWidget->addWidget(scrollArea);
    }
    _StackedWidget->setCurrentWidget(scrollArea);
    return scrollArea;
}

void Clipping::toggle(View3DInventor *view)
{
    if (!view) {
        view = qobject_cast<Gui::View3DInventor*>(Application::Instance->activeView());
        if (!view)
            return;
    }

    if (!_Inited) {
        _Inited = true;
        Application::Instance->signalActivateView.connect(
            [](const Gui::MDIView *view) {
                auto view3d = qobject_cast<const Gui::View3DInventor*>(view);
                if (!view3d || !_DockWidget)
                    return;
                bindView(const_cast<Gui::View3DInventor*>(view3d));
            });
    }

    bool doToggle = true;
    if (!_DockWidget || !_StackedWidget) {
        if (_DockWidget)
            _DockWidget->deleteLater();
        doToggle = false;
        _StackedWidget = new QStackedWidget(nullptr);
        Gui::DockWindowManager* pDockMgr = Gui::DockWindowManager::instance();
        _DockWidget = pDockMgr->addDockWindow("Clipping", _StackedWidget, Qt::LeftDockWidgetArea);
        _DockWidget->setFeatures(QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        _DockWidget->show();
    }

    auto widget = bindView(view);
    if (widget && doToggle)
        _DockWidget->toggleViewAction()->activate(QAction::Trigger);
}

/** Destroys the object and frees any allocated resources */
Clipping::~Clipping()
{
    if (d->view) {
        d->auxNode->removeChild(d->clipSwitch);
        d->node->removeChild(d->pickStyle);
        d->node->removeChild(d->clipX);
        d->node->removeChild(d->clipY);
        d->node->removeChild(d->clipZ);
        d->node->removeChild(d->clipView);
    }
    d->ui.clipX->onSave();
    d->ui.clipY->onSave();
    d->ui.clipZ->onSave();
    d->ui.dirX->onSave();
    d->ui.dirY->onSave();
    d->ui.dirZ->onSave();
    delete d;
}

void Clipping::done(int r)
{
    if (_DockWidget)
        _DockWidget->deleteLater();
    _Clippings.clear();
    QDialog::done(r);
}

void Clipping::on_groupBoxX_toggled(bool on)
{
    if (on) {
        d->ui.groupBoxView->setChecked(false);
    }

    d->clipX->on.setValue(on);
    d->updateSwitch();
}

void Clipping::on_groupBoxY_toggled(bool on)
{
    if (on) {
        d->ui.groupBoxView->setChecked(false);
    }

    d->clipY->on.setValue(on);
    d->updateSwitch();
}

void Clipping::on_groupBoxZ_toggled(bool on)
{
    if (on) {
        d->ui.groupBoxView->setChecked(false);
    }

    d->clipZ->on.setValue(on);
    d->updateSwitch();
}

void Clipping::on_checkBoxFill_toggled(bool on)
{
    if (on && !ViewParams::isUsingRenderer()) {
        int res = QMessageBox::question(Gui::getMainWindow(), tr("Clipping"),
                tr("Cross section fill only works with 'Experiemental' render cache"
                   " (Preferences -> Display -> Render cache).\n\n"
                   "Do you want to enable it?"),
                QMessageBox::Yes, QMessageBox::No|QMessageBox::No);
        if (res == QMessageBox::No) {
            d->ui.checkBoxFill->setChecked(false);
            return;
        }
        ViewParams::useRenderer(true);
    }
    ViewParams::setSectionFill(on);
    d->ui.checkBoxInvert->setEnabled(on);
    d->ui.checkBoxConcave->setEnabled(on);
    d->ui.checkBoxOnTop->setEnabled(on && !ViewParams::getSectionConcave());
    d->ui.checkBoxHatch->setEnabled(on);
    d->ui.editHatchTexture->setEnabled(on);
    d->ui.spinBoxHatchScale->setEnabled(on);
    d->ui.checkBoxGroupRendering->setEnabled(on && !ViewParams::getSectionConcave());
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::on_checkBoxInvert_toggled(bool)
{
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::on_checkBoxConcave_toggled(bool on)
{
    d->ui.checkBoxOnTop->setDisabled(on);
    d->ui.checkBoxGroupRendering->setDisabled(on);
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::on_checkBoxOnTop_toggled(bool)
{
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::on_clipX_valueChanged(double val)
{
    if (d->busy) return;
    SbPlane pln = d->clipX->plane.getValue();
    d->clipX->plane.setValue(SbPlane(pln.getNormal(),d->flipX ? -val : val));
}

void Clipping::on_clipY_valueChanged(double val)
{
    if (d->busy) return;
    SbPlane pln = d->clipY->plane.getValue();
    d->clipY->plane.setValue(SbPlane(pln.getNormal(),d->flipY ? -val : val));
}

void Clipping::on_clipZ_valueChanged(double val)
{
    if (d->busy) return;
    SbPlane pln = d->clipZ->plane.getValue();
    d->clipZ->plane.setValue(SbPlane(pln.getNormal(),d->flipZ ? -val : val));
}

void Clipping::on_flipClipX_clicked()
{
    d->flipX = !d->flipX;
    SbPlane pln = d->clipX->plane.getValue();
    d->clipX->plane.setValue(SbPlane(-pln.getNormal(),-pln.getDistanceFromOrigin()));
}

void Clipping::on_flipClipY_clicked()
{
    d->flipY = !d->flipY;
    SbPlane pln = d->clipY->plane.getValue();
    d->clipY->plane.setValue(SbPlane(-pln.getNormal(),-pln.getDistanceFromOrigin()));
}

void Clipping::on_flipClipZ_clicked()
{
    d->flipZ = !d->flipZ;
    SbPlane pln = d->clipZ->plane.getValue();
    d->clipZ->plane.setValue(SbPlane(-pln.getNormal(),-pln.getDistanceFromOrigin()));
}

void Clipping::on_groupBoxView_toggled(bool on)
{
    if (on) {
        d->ui.groupBoxX->setChecked(false);
        d->ui.groupBoxY->setChecked(false);
        d->ui.groupBoxZ->setChecked(false);
    }

    d->clipView->on.setValue(on);
    d->updateSwitch();
}

void Clipping::on_checkBoxShowPlane_toggled(bool)
{
    d->updateSwitch();
}

void Clipping::on_clipView_valueChanged(double val)
{
    if (d->busy) return;
    SbPlane pln = d->clipView->plane.getValue();
    d->clipView->plane.setValue(SbPlane(pln.getNormal(),val));
}

void Clipping::on_fromView_clicked()
{
    if (d->view) {
        Gui::View3DInventorViewer* view = d->view->getViewer();
        SbVec3f dir = view->getViewDirection();
        SbPlane pln = d->clipView->plane.getValue();
        d->clipView->plane.setValue(SbPlane(dir,pln.getDistanceFromOrigin()));
    }
}

void Clipping::on_adjustViewdirection_toggled(bool on)
{
    d->ui.dirX->setDisabled(on);
    d->ui.dirY->setDisabled(on);
    d->ui.dirZ->setDisabled(on);
    d->ui.fromView->setDisabled(on);

    if (on)
        d->sensor->schedule();
    else
        d->sensor->unschedule();
}

void Clipping::on_dirX_valueChanged(double)
{
    d->onDirectionChanged();
}

void Clipping::on_dirY_valueChanged(double)
{
    d->onDirectionChanged();
}

void Clipping::on_dirZ_valueChanged(double)
{
    d->onDirectionChanged();
}

void Clipping::on_angleX_valueChanged(double)
{
    d->onAngleChanged();
}

void Clipping::on_angleY_valueChanged(double)
{
    d->onAngleChanged();
}

void Clipping::on_spinBoxHatchScale_valueChanged(double)
{
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::on_checkBoxHatch_toggled(bool)
{
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::on_editHatchTexture_fileNameSelected(const QString &filename)
{
    if (filename.isEmpty())
        ViewParams::removeSectionHatchTexture();
    else
        ViewParams::setSectionHatchTexture(filename.toUtf8().constData());
    if (d->view)
        d->view->getViewer()->redraw();
}

void Clipping::restoreClipPlanes(View3DInventor *view,
                                 const Base::Vector3d &posX, bool enableX,
                                 const Base::Vector3d &posY, bool enableY,
                                 const Base::Vector3d &posZ, bool enableZ,
                                 const Base::Placement &plaCustom, bool enableCustom)
{
    if (!view)
        return;
    if (!enableX && !enableY && !enableZ && !enableCustom)
        return;

    auto it = _Clippings.find(view);
    if (it == _Clippings.end() || !it->second) {
        toggle(view);
        it = _Clippings.find(view);
        if (it  == _Clippings.end() || !it->second)
            return;
    }
    auto clipping = qobject_cast<Clipping*>(it->second->widget());
    if (!clipping)
        return;
    auto d = clipping->d;
    d->ui.groupBoxX->setChecked(enableX);
    d->ui.groupBoxY->setChecked(enableY);
    d->ui.groupBoxZ->setChecked(enableZ);
    d->ui.groupBoxView->setChecked(enableCustom);
    d->draggerX->translation.setValue(SbVec3f(posX.x, posX.y, posX.z));
    d->draggerY->translation.setValue(SbVec3f(posY.x, posY.y, posY.z));
    d->draggerZ->translation.setValue(SbVec3f(posZ.x, posZ.y, posZ.z));
    const auto &r = plaCustom.getRotation();
    d->draggerCustom->rotation.setValue(SbRotation(r[0], r[1], r[2], r[3]));
    const auto &t = plaCustom.getPosition();
    d->draggerCustom->translation.setValue(SbVec3f(t[0], t[1], t[2]));

    d->draggerX->onDragFinish();
    d->draggerY->onDragFinish();
    d->draggerZ->onDragFinish();
    d->draggerCustom->onDragFinish();
}

void Clipping::getClipPlanes(View3DInventor *view,
                             Base::Vector3d &posX, bool &enableX,
                             Base::Vector3d &posY, bool &enableY,
                             Base::Vector3d &posZ, bool &enableZ,
                             Base::Placement &plaCustom, bool &enableCustom)
{
    auto it = _Clippings.find(view);
    if (it == _Clippings.end() || !it->second) {
        enableX = enableY = enableZ = enableCustom = false;
        return;
    }
    auto clipping = qobject_cast<Clipping*>(it->second->widget());
    if (!clipping)
        return;
    auto d = clipping->d;
    enableX = d->ui.groupBoxX->isChecked();
    enableY = d->ui.groupBoxY->isChecked();
    enableZ = d->ui.groupBoxZ->isChecked();
    enableCustom = d->ui.groupBoxView->isChecked();
    SbVec3f t;
    t = d->draggerX->translation.getValue();
    posX = Base::Vector3d(t[0], t[1], t[2]);
    t = d->draggerY->translation.getValue();
    posY = Base::Vector3d(t[0], t[1], t[2]);
    t = d->draggerZ->translation.getValue();
    posZ = Base::Vector3d(t[0], t[1], t[2]);
    t = d->draggerCustom->translation.getValue();
    const auto &r = d->draggerCustom->rotation.getValue();
    plaCustom = Base::Placement(Base::Vector3d(t[0], t[1], t[2]),
                                Base::Rotation(r[0], r[1], r[2], r[3]));
}

bool Clipping::eventFilter(QObject *o, QEvent *ev)
{
    if (o->isWidgetType()) {
        auto widget = static_cast<QWidget*>(o);
        switch(ev->type()) {
        case QEvent::Wheel:
            if (!widget->hasFocus()) {
                ev->setAccepted(false);
                return true;
            }
            break;
        case QEvent::FocusIn:
            widget->setFocusPolicy(Qt::WheelFocus);
            break;
        case QEvent::FocusOut:
            widget->setFocusPolicy(Qt::StrongFocus);
            break;
        default:
            break;
        }
    }
    return QDialog::eventFilter(o, ev);
}

#include "moc_Clipping.cpp"
