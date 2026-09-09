/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef GUI_VIEWERCONTEXT_H
#define GUI_VIEWERCONTEXT_H

#include <FCGlobal.h>

#include <vector>

#include <QtCore/qnamespace.h>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoType.h>
#include <Inventor/nodes/SoEventCallback.h>

class SoNode;
class SoPath;
class SoPickedPoint;
class SoRenderManager;
class SoEventManager;
class SoEventCallback;  // NOLINT
class SoFCRenderCacheManager;

class QWidget;
class QCursor;

namespace Base {
class Matrix4D;
class Placement;
}  // namespace Base

namespace Render {
class Renderer;
}

namespace Gui {

class Document;
class ViewProvider;
class GLGraphicsItem;

/** What an edit mode is allowed to ask of the view it is running in.
 *
 * The interaction code -- the sketcher's tools, the draggers, every
 * ViewProvider edit mode -- was written against View3DInventorViewer, which
 * is a QuarterWidget: a Qt widget owning a GL context. Nothing an edit mode
 * actually needs from it requires either. It needs a camera, a viewport, a
 * scene graph to pick against, the editing root to hang its geometry on, and
 * a few numbers the input device supplies. Coin already separates those from
 * drawing: SoRenderManager holds the camera and the viewport region and
 * touches GL only inside render(), and SoEventManager runs
 * SoHandleEventAction with no framebuffer anywhere.
 *
 * So this is that subset, named, with View3DInventorViewer as its desktop
 * implementation and a per-client offscreen mirror as the other one
 * (docs/ThinClient.md section 8.3). The edit entry points take this type,
 * which is the whole point: an edit mode that runs here runs in a server
 * process with no display.
 *
 * The last group is the residue -- the widget and GL calls the edit path
 * still makes. They are declared here rather than left out so that call sites
 * compile unchanged, and they answer null or do nothing by default. An edit
 * mode that cannot work without a widget is then found by that null, in a
 * place that can say so, rather than by a crash in a headless process.
 */
class GuiExport ViewerContext
{
public:
    virtual ~ViewerContext();

    /** @name Scene, camera and viewport */
    //@{
    virtual SoNode* getSceneGraph() const = 0;
    virtual SoRenderManager* getSoRenderManager() const = 0;
    virtual SoEventManager* getSoEventManager() const = 0;
    virtual const SbViewportRegion& getViewportRegion() const = 0;
    virtual Gui::Document* getDocument() = 0;
    virtual SoFCRenderCacheManager* getRenderCacheManager() const = 0;
    virtual Render::Renderer* getExternalRenderer() const = 0;
    //@}

    /** @name Values the input device supplies
     *
     * Not global state: a touch client wants a larger pick radius than a
     * mouse, and the device pixel ratio is the client's, so both are asked of
     * the context the event arrived through.
     */
    //@{
    virtual float getPickRadius() const = 0;
    virtual double devicePixelRatio() const = 0;
    /// The mouse buttons this view last saw held down.
    virtual Qt::MouseButtons mouseButtons() const = 0;
    /// Screen density, for the edit modes that size things in millimetres.
    virtual double logicalDotsPerInchX() const = 0;
    /// Whether any of them is.
    bool isMouseButtonDown() const
    {
        return mouseButtons() != Qt::NoButton;
    }
    //@}

    /** @name Camera math -- all of it view-less */
    //@{
    virtual SbVec3f getViewDirection() const = 0;
    virtual SbVec3f getCenterPointOnFocalPlane() const = 0;
    virtual SbVec3f getPointOnFocalPlane(const SbVec2s& pnt) const = 0;
    virtual SbVec3f getPointOnXYPlaneOfPlacement(const SbVec2s& pnt,
                                                 const Base::Placement& plc) const = 0;
    virtual SbVec3f getPointOnLine(const SbVec2s& pnt, const SbVec3f& axisCenter,
                                   const SbVec3f& axis) const = 0;
    virtual SbVec2s getPointOnViewport(const SbVec3f& pnt) const = 0;
    virtual SbVec2f screenCoordsOfPath(SoPath* path) const = 0;
    virtual void getNearPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const = 0;
    virtual float getMaxDimension() const = 0;
    virtual bool getSceneBoundBox(SbBox3f& box) const = 0;
    virtual void setCameraOrientation(const SbRotation& orientation,
                                      bool moveToCenter = false) = 0;
    /** Viewport points as dimensionless [0 1] screen coordinates.
     *
     * Named for GL by history only -- it is the viewport region and an aspect
     * ratio, and no context of any kind is needed to compute it.
     */
    virtual std::vector<SbVec2f> getGLPolygon(const std::vector<SbVec2s>& pnts) const = 0;
    //@}

    /** @name Picking */
    //@{
    virtual SoPickedPoint* getPointOnRay(const SbVec2s& pos, const ViewProvider* vp) const = 0;
    virtual SoPickedPoint* getPointOnRay(const SbVec3f& pos, const SbVec3f& dir,
                                         const ViewProvider* vp) const = 0;
    virtual void appendDetailPath(SoPath* path, ViewProvider* vp) = 0;
    //@}

    /** @name Edit mode */
    //@{
    virtual void setEditing(bool edit) = 0;
    virtual bool isEditing() const = 0;
    virtual void setEditingViewProvider(Gui::ViewProvider* vp, int ModNum) = 0;
    virtual bool isEditingViewProvider() const = 0;
    virtual void resetEditingViewProvider() = 0;
    virtual void setupEditingRoot(SoNode* node = nullptr,
                                  const Base::Matrix4D* mat = nullptr) = 0;
    virtual void resetEditingRoot(bool updateLinks = true) = 0;
    virtual void setEditingTransform(const Base::Matrix4D& mat) = 0;
    /** Where this view hangs the geometry of the mode editing in it.
     *
     * Gui::Document remembers it so that a traversal arriving at that node
     * knows it is looking at the edit, not at the document. Null where a
     * view keeps no such root.
     */
    virtual SoNode* getEditRootNode() const
    {
        return nullptr;
    }
    //@}

    /** @name Event delivery and selection mode */
    //@{
    virtual void addEventCallback(SoType eventtype, SoEventCallbackCB* cb,
                                  void* userdata = nullptr) = 0;
    virtual void removeEventCallback(SoType eventtype, SoEventCallbackCB* cb,
                                     void* userdata = nullptr) = 0;
    virtual void setRedirectToSceneGraph(bool redirect) = 0;
    virtual void setSelectionEnabled(bool enable) = 0;
    virtual bool isSelectionEnabled() const = 0;
    virtual bool isSelecting() const = 0;
    //@}

    /** @name The widget residue
     *
     * No answer without a widget, and none needed: these draw or focus, and a
     * mirror does neither. The DOM layer takes over these surfaces
     * (docs/ThinClient.md section 8.7).
     */
    //@{
    virtual QWidget* getWidget() const
    {
        return nullptr;
    }
    virtual QWidget* getGLWidget() const
    {
        return nullptr;
    }
    virtual void redraw(bool force = false)
    {
        (void)force;
    }
    virtual void addGraphicsItem(GLGraphicsItem*)
    {}
    virtual void removeGraphicsItem(GLGraphicsItem*)
    {}
    virtual void setEditingCursor(const QCursor&)
    {}
    virtual void setFocusToView()
    {}
    /** How the view gets its pixels onto the screen.
     *
     * Lives here rather than on the viewer only so that the edit modes that
     * switch it while dragging keep naming one type. Without a framebuffer it
     * means nothing, so the default does nothing.
     */
    enum RenderType
    {
        Native,
        Framebuffer,
        Image
    };
    virtual void setRenderType(RenderType type)
    {
        (void)type;
    }
    /// The Python face of this view. None where there is not one.
    virtual PyObject* getPyObject();
    //@}

    /** The context an event callback node was installed by.
     *
     * The node's user data is always a ViewerContext, never a derived
     * pointer: a pointer to a multiply-inherited object is not the same
     * address as a pointer to its base, so storing one and casting to the
     * other through void* reads the wrong bytes. Everything that wants the
     * desktop viewer back asks View3DInventorViewer::fromEventCallback,
     * which is a checked cast from this one.
     */
    static ViewerContext* fromEventCallback(const SoEventCallback* node)
    {
        return node ? static_cast<ViewerContext*>(node->getUserData()) : nullptr;
    }

    /** The view whose input is being handled right now, if one said so.
     *
     * An edit mode is entered from wherever the click that entered it
     * landed, and the code that starts one is nowhere near the code that
     * knows which view that was: Gui::Document::setEdit reaches
     * MainWindow::activeWindow() for it, which in a serving process names
     * either nothing or somebody else's window. So a replayed client event
     * says which view it is (ViewerScope), and setEdit asks here first.
     *
     * Null on the desktop, where nothing opens a scope, so every caller
     * falls back to the active window exactly as it did before. This is the
     * same shape as Gui::SelectionScope (docs/ThinClient.md sec 8.4): one
     * replayed event, one dynamic extent, and no call site retyped.
     */
    static ViewerContext* current();
};

/** Make \a context the current view for this scope's dynamic extent.
 *
 * Scopes nest and unwind innermost first. The scope does not own the
 * context and must not outlive it -- it is opened around the handling of
 * one event by the view that received it.
 */
class GuiExport ViewerScope
{
public:
    explicit ViewerScope(ViewerContext* context);
    ~ViewerScope();
    ViewerScope(const ViewerScope&) = delete;
    ViewerScope& operator=(const ViewerScope&) = delete;

private:
    ViewerContext* previous;
};

}  // namespace Gui

#endif  // GUI_VIEWERCONTEXT_H
