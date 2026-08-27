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

#ifndef GUI_VIEW3DINVENTOR_H
#define GUI_VIEW3DINVENTOR_H

#include <memory>

#include <QImage>
#include <QtOpenGL.h>

#include <Base/Parameter.h>
#include <App/PropertyStandard.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbRotation.h>

#include "InventorBase.h"
#include "MDIView.h"

class SoNode;
class SoCamera;
class SoDragger;
class QPrinter;
class QStackedWidget;

namespace Gui {

class Document;
class View3DInventorViewer;
class View3DInventorPy;

class View3DSettings;

class GuiExport GLOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GLOverlayWidget(QWidget* parent=nullptr) : QWidget(parent)
    {}
    ~GLOverlayWidget() override = default;
    virtual void setImage(const QImage& img)
    { image = img; }
    void paintEvent(QPaintEvent*) override;

protected:
    QImage image;
};

/** The 3D view window
 *  It consists out of the 3D view
 *  \author Juergen Riegel
 */
class GuiExport View3DInventor : public MDIView
{
    Q_OBJECT

    PROPERTY_HEADER_WITH_OVERRIDE(Gui::View3DInventor);

public:
    /// Helper class to apply user parameter settings to view properties
    class ApplySettings {
    public:
        ApplySettings();
        ~ApplySettings();
        static bool isApplying();
    private:
        /// Private new operator to prevent heap allocation
        void* operator new(size_t size);
    };
    
    App::PropertyEnumeration DrawStyle;
    App::PropertyBool ShowNaviCube;
    App::PropertyBool ThumbnailView;
    /// Per-object display mode overrides of THIS view
    /// (docs/CoinRetirement.md 5.9). Key: a subname path rooted at a
    /// top-level object of the view's document ("Asm.Sub.Part.", one
    /// occurrence -- the same shape show-on-top uses), or a bare
    /// internal name ("Part", optionally "Doc#Part" for an object of
    /// another document shown through a link) meaning the object
    /// wherever it appears in this view. Value: a display mode name;
    /// "As Is" pins the object to its own mode, escaping the view
    /// style. Rides the view's Save/Restore into GuiDocument.xml like
    /// DrawStyle; stays Hidden -- the property editor edits it through
    /// the ViewProvider's DisplayModeInView row instead.
    App::PropertyMap ObjectDisplayModes;
    /// The objects this view is showing ON TOP -- drawn over
    /// everything else, whatever occludes them (Std_ToggleShowOnTop).
    /// One entry per object, as "<internal name>.<subname path>" with
    /// the trailing dot the subname always carries and no element
    /// name; the subname may be empty, which is the common case of a
    /// whole top-level object.
    ///
    /// Rides the view's Save/Restore into GuiDocument.xml, like
    /// ObjectDisplayModes and DrawStyle. It used to be a dynamic
    /// PropertyStringList on the APP document, keyed by a view id that
    /// the next session renumbers from a counter
    /// (docs/CoinRetirement.md 5.12); old files are still read from
    /// there and migrated on restore. Stays Hidden: the on-top set is
    /// edited from the 3D view and the tree, not from a property row.
    App::PropertyStringList OnTopObjects;

    View3DInventor(Gui::Document* pcDocument, QWidget* parent, const QtGLWidget* sharewidget = nullptr, Qt::WindowFlags wflags=Qt::WindowFlags());
    ~View3DInventor() override;

    /// Message handler
    bool onMsg(const char* pMsg, const char** ppReturn) override;
    bool onHasMsg(const char* pMsg) const override;
    void deleteSelf() override;
    /// get called when the document is updated
    void onRename(Gui::Document *pDoc) override;
    void onUpdate(void) override;
    void viewAll() override;
    const char *getName(void) const override;
    void onChanged(const App::Property *prop) override;
    void Restore(Base::XMLReader &reader) override;
    /// True while the view's properties are being restored: property
    /// changes are then the file's own content, not user edits, so
    /// handlers that would write back to the document must stand down.
    bool isRestoring() const { return _restoring; }

    void bindCamera(SoCamera *camera, bool sync=false);
    void syncCamera(View3DInventor *view);
    View3DInventor *bindView(const QString &title, bool sync=false);
    bool unbindView(const QString &title = QString());
    SoCamera *boundCamera() const;
    View3DInventor *boundView() const;
    std::set<View3DInventor*> boundViews(bool recursive=false) const;
    void boundViews(std::set<View3DInventor*> &views, bool recursive=false) const;
    SoCamera *getCamera() const;
    void syncBoundViews(const char *pMsg);

    /// print function of the view
    void print() override;
    void printPdf() override;
    void printPreview() override;
    void print(QPrinter*) override;

    PyObject *getPyObject() override;
    /**
     * If \a b is set to \a FullScreen the MDI view is displayed in full screen mode, if \a b
     * is set to \a TopLevel then it is displayed as an own top-level window, otherwise (\a Normal)
     * as tabbed window.
     * This method is reimplemented from MDIView to set the this widget as the proxy of the embedded
     * GL widget to get all key events in \a TopLevel or \a Fullscreen mode.
     */
    void setCurrentViewMode(ViewMode b) override;
    bool setCamera(const char* pCamera, int animateDuration=0);
    void toggleClippingPlane();
    bool hasClippingPlane() const;

    void setOverlayWidget(QWidget*);
    void removeOverlayWidget();

    View3DInventorViewer *getViewer() const {return _viewer;}
    /// Whether nobody can currently see this view: hidden, minimized, or
    /// -- the MDI case, which no Qt visibility flag reports -- sitting in
    /// the tab stack behind a maximized sibling. Asked (not remembered)
    /// whenever the render engine reconsiders releasing this view's
    /// targets; see View3DInventorViewer::armBackgroundRelease.
    bool isBackgroundView() const override;
    bool containsViewProvider(const ViewProvider*) const override;

    template<class PropT, class ValueT, class CallbackT>
    ValueT getProperty(const char *_name, const char *_docu, const char *group, const ValueT &def, CallbackT cb) {
        char name[128];
        snprintf(name,sizeof(name)-1,"%s_%s",group,_name);
        auto prop = this->getPropertyByName(name);
        if(prop && !prop->isDerivedFrom(PropT::getClassTypeId()))
            return def;
        if(!prop) {
            prop = this->addDynamicProperty(PropT::getClassTypeId().getName(), name, group, _docu);
            static_cast<PropT*>(prop)->setValue(def);
        }
        cb(*static_cast<PropT*>(prop));
        return static_cast<PropT*>(prop)->getValue();
    }

public Q_SLOTS:
    /// override the cursor in this view
    void setOverrideCursor(const QCursor&) override;
    void restoreOverrideCursor() override;

    void dump(const char* filename, bool onlyVisible=false);

protected Q_SLOTS:
    void stopAnimating();

public:
    bool eventFilter(QObject*, QEvent* ) override;

private:
    void applySettings();
    /// Put the viewer's on-top group where OnTopObjects says: what
    /// makes the property the storage rather than a record of it.
    void applyOnTopObjects();

protected:
    void closeEvent(QCloseEvent* e) override;
    void windowStateChanged(QWidget* view) override;
    void dropEvent        (QDropEvent      * e) override;
    void dragEnterEvent   (QDragEnterEvent * e) override;
    void keyPressEvent    (QKeyEvent       * e) override;
    void keyReleaseEvent  (QKeyEvent       * e) override;
    void focusInEvent     (QFocusEvent     * e) override;
    void customEvent      (QEvent          * e) override;
    void contextMenuEvent (QContextMenuEvent*e) override;

    struct CameraInfo {
        SoNodeSensor sensor;
        SbVec3f position;
        SbRotation orientation;
        float focal;
        float height;

        bool sync(SoCamera *cam = 0);
        void apply(SoCamera *cam = 0);

        bool attached() const {
            return sensor.getAttachedNode() != nullptr;
        }
    };

    void onCameraChanged(CameraInfo &src, CameraInfo &dst);

private:
    View3DInventorViewer * _viewer;
    View3DInventorPy *_viewerPy;
    bool _restoring = false;
    QTimer * stopSpinTimer;
    QStackedWidget* stack;
    std::unique_ptr<View3DSettings> viewSettings;

    CameraInfo camInfo;
    CameraInfo boundCamInfo;

    // friends
    friend class View3DInventorPy;
};

} // namespace Gui

#endif  // GUI_VIEW3DINVENTOR_H

