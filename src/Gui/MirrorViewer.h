/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef GUI_MIRRORVIEWER_H
#define GUI_MIRRORVIEWER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ViewerContext.h"

class SoCamera;
class SoEvent;

namespace Gui
{

class SelectionSingleton;

/** One connected client's view, mirrored in a process with no display.
 *
 * A serving process publishes a scene and every viewer navigates it with a
 * camera of its own (docs/HeadlessServe.md section 3). That is fine until the
 * client asks the server to do something *in* its view -- pick this point,
 * drag this curve -- because the server has no idea which view that is. Its
 * one synthetic camera is a framing for a joining viewer to adopt, not a
 * framing anybody is actually looking through.
 *
 * So this is the client's view as the server keeps it: the camera and the
 * canvas it last stated over the wire (docs/ThinClient.md section 8.5, the
 * `'C'` frame), and nothing else. It is a ViewerContext, which is the type
 * the edit path takes, and it holds no Qt widget and no GL context -- Coin
 * separates the camera and the viewport region from drawing, and the mirror
 * only ever uses that half.
 *
 * What it buys immediately is the pick. Coin gives a ray set with
 * SoRayPickAction::setRay a radius of essentially zero and ignores
 * setRadius() entirely, so a served click had to hit geometry exactly; an
 * edge or a vertex was unpickable from a browser. Resolved through a mirror
 * the click goes back to the viewport point it came from, and the client's
 * pick radius in pixels applies as it does on the desktop.
 */
class GuiExport MirrorViewer: public ViewerContext
{
public:
    /** What a client says about the view it is looking through.
     *
     * The camera, the canvas, and the two numbers that describe its input
     * device rather than either. Sizes and \a pickRadius are in device
     * pixels, which is what the client renders at and what the desktop
     * viewer's own pick radius is measured against.
     */
    struct Camera
    {
        bool perspective = false;
        SbVec3f position {0, 0, 0};
        SbRotation orientation {0, 0, 0, 1};
        /// Orthographic height, or perspective height angle in radians.
        float heightOrAngle = 1;
        float nearDistance = 0.1F;
        float farDistance = 100.0F;
        float aspectRatio = 1;
        SbVec2s sizePixels {0, 0};
        float devicePixelRatio = 1;
        float pickRadius = 5;
    };

    /** Mirror a client of \a doc.
     *
     * \a scene is the graph the server publishes -- shared, never owned --
     * and \a cacheManager and \a renderer are the document's, for the edit
     * modes that ask a view for them. Everything else is this client's.
     */
    MirrorViewer(Document* doc, SoNode* scene, SoFCRenderCacheManager* cacheManager,
                 Render::Renderer* renderer);
    ~MirrorViewer() override;

    /// Adopt what the client last stated.
    void setCamera(const Camera& camera);
    /** Whether a client has stated a camera yet.
     *
     * Until it has, the mirror has no framing and nothing may be resolved
     * through it -- the caller falls back to whatever it did before.
     */
    bool hasCamera() const;

    /** Where a world ray computed by this client lands on its viewport.
     *
     * The inverse of what the client did to make the ray, through the same
     * camera it stated, so the round trip is exact to within the float noise
     * the pick radius swamps. False when there is no camera yet, when the
     * ray is degenerate, or when the view volume is empty. \a normPoint is
     * in [0 1] over the viewport, the space SoRayPickAction picks in.
     */
    bool rayToNormPoint(const SbVec3f& origin, const SbVec3f& dir, SbVec2f& normPoint) const;

    /** Pick this mirror's scene along a world ray the client computed.
     *
     * The ray is turned back into the viewport point it came from and picked
     * from there, so the client's pick radius applies. The caller owns the
     * returned point; null when nothing was hit, or when there is no camera
     * to resolve against.
     */
    SoPickedPoint* pickRay(const SbVec3f& origin, const SbVec3f& dir) const;

    /** One input event as the client sent it (docs/ThinClient.md sec 8.5,
     * the `'E'` frame).
     *
     * Client coordinates throughout: device pixels with the origin at the
     * TOP left, which is what a canvas reports and what a browser has. The
     * flip to Coin's bottom-up convention happens here, where the canvas
     * height is the one the mirror is resolving everything else against --
     * so a client that resized between frames cannot make the two
     * disagree.
     */
    struct Input
    {
        enum Kind
        {
            Move,
            Press,
            Release,
            Wheel,
            KeyDown,
            KeyUp
        };
        Kind kind = Move;
        /// Canvas position in device pixels, origin top left.
        int x = 0;
        int y = 0;
        /// 1 left, 2 middle, 3 right for the button kinds; a Coin key code
        /// (SoKeyboardEvent::Key) for the key kinds; unused otherwise.
        int code = 0;
        /// Wheel movement; 120 units is one notch, as everywhere else.
        /// For the key kinds this slot carries the printable character the
        /// key produced instead (a Unicode code point, 0 for none) -- a
        /// wheel has no meaning there, and the case a keysym folds away is
        /// exactly what an entry box needs to insert.
        int delta = 0;
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
        /// The client's own clock, in seconds. A replayed event with no
        /// time would make every gesture instantaneous, which is what the
        /// double-click and drag thresholds read.
        double time = 0;
    };

    /** Replay \a input against this mirror's scene.
     *
     * The event is built with this client's camera and canvas, this
     * client's modifier and button state, and delivered through an
     * SoHandleEventAction over the mirror's own event root -- which is the
     * served graph with this client's camera and event callback in front of
     * it, so one client's events reach nobody else's callbacks. The view
     * is made current for the extent of the call (Gui::ViewerScope), which
     * is how Gui::Document::setEdit learns which view an edit started in.
     *
     * Returns whether anything claimed the event. False when the client has
     * stated no camera yet: there is nothing to resolve a pointer position
     * against, and guessing one would put the pick somewhere plausible and
     * wrong.
     */
    bool handleInput(const Input& input);

    /** @name On-view parameters (docs/ThinClient.md section 8.7)
     *
     * The entry boxes an edit mode has open here. Built unshown, driven by
     * replayed keys, and streamed to the client to be drawn -- so this is
     * the whole of what a client is told, and every decision about the text
     * inside one is taken on this side.
     */
    //@{
    struct OnViewParam
    {
        /// Where the box belongs, in world coordinates: the client projects
        /// it with the camera of the frame it is drawing, which is the only
        /// camera that is never behind the picture (section 8.7).
        SbVec3f anchor {0, 0, 0};
        /// The text a desktop user would read in the box, units included.
        std::string text;
        /// What is selected in it, so a client shows the same highlight.
        int selStart = 0;
        int selLength = 0;
        /// Whether this is the box taking the keys.
        bool focus = false;
        /// Whether the value has been fixed by the user rather than driven
        /// by the pointer -- the desktop says it in the label colour.
        bool set = false;
    };
    std::vector<OnViewParam> onViewParameters() const;
    /// Told when any of that changes, so the connection can restate it.
    void setOnViewParametersCallback(std::function<void()> callback);
    /** Give one box the keys, at the client's asking.
     *
     * The client asks by index into the set above; a stale index from a
     * client whose set has since changed is refused rather than applied to
     * whichever box now sits there.
     */
    bool focusOnViewParameter(int index);
    void addOnViewParameter(EditableDatumLabel* label) override;
    void removeOnViewParameter(EditableDatumLabel* label) override;
    void onViewParameterFocused(EditableDatumLabel* label) override;
    void onViewParametersChanged() override;
    bool sendKeyEvent(QKeyEvent* event) override;
    //@}

    /** @name ViewerContext -- scene, camera and viewport */
    //@{
    SoNode* getSceneGraph() const override;
    SoRenderManager* getSoRenderManager() const override;
    SoEventManager* getSoEventManager() const override;
    const SbViewportRegion& getViewportRegion() const override;
    Gui::Document* getDocument() override;
    SoFCRenderCacheManager* getRenderCacheManager() const override;
    Render::Renderer* getExternalRenderer() const override;
    //@}

    /** @name ViewerContext -- what the input device supplies */
    //@{
    float getPickRadius() const override;
    double devicePixelRatio() const override;
    Qt::MouseButtons mouseButtons() const override;
    double logicalDotsPerInchX() const override;
    //@}

    /** @name ViewerContext -- camera math */
    //@{
    SbVec3f getViewDirection() const override;
    SbVec3f getCenterPointOnFocalPlane() const override;
    SbVec3f getPointOnFocalPlane(const SbVec2s& pnt) const override;
    SbVec3f getPointOnXYPlaneOfPlacement(const SbVec2s& pnt,
                                         const Base::Placement& plc) const override;
    SbVec3f getPointOnLine(const SbVec2s& pnt, const SbVec3f& axisCenter,
                           const SbVec3f& axis) const override;
    SbVec2s getPointOnViewport(const SbVec3f& pnt) const override;
    SbVec2f screenCoordsOfPath(SoPath* path) const override;
    void getNearPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const override;
    float getMaxDimension() const override;
    bool getSceneBoundBox(SbBox3f& box) const override;
    void setCameraOrientation(const SbRotation& orientation, bool moveToCenter = false) override;
    std::vector<SbVec2f> getGLPolygon(const std::vector<SbVec2s>& pnts) const override;
    //@}

    /** @name ViewerContext -- picking */
    //@{
    SoPickedPoint* getPointOnRay(const SbVec2s& pos, const ViewProvider* vp) const override;
    SoPickedPoint* getPointOnRay(const SbVec3f& pos, const SbVec3f& dir,
                                 const ViewProvider* vp) const override;
    void appendDetailPath(SoPath* path, ViewProvider* vp) override;
    //@}

    /** @name ViewerContext -- edit mode
     *
     * The editing root and what is done to it are ViewerContext's. What is
     * this mirror's is where that root hangs: in the graph the server
     * publishes, because the change-driven traversal is the only thing here
     * that plays the part a redraw plays on the desktop, and it only sees
     * what is in that graph (docs/ThinClient.md section 8.5). It is hung
     * there for the duration of an edit and taken out again after, so a
     * connected client that is not editing does not put an empty separator
     * in everybody's scene.
     */
    //@{
    void setEditing(bool edit) override;
    bool isEditing() const override;
    void setEditingViewProvider(Gui::ViewProvider* vp, int ModNum) override;
    void resetEditingViewProvider() override;
    //@}

    /** This client's own selection instance.
     *
     * Not the room's: what a browser picks while it is editing is that
     * browser's, and the room is what every viewer and every panel agrees
     * on (docs/ThinClient.md section 8.4). Made current by ViewerScope,
     * so everything replayed through this view selects here without a
     * single one of the call sites behind Gui::Selection() being touched.
     */
    SelectionSingleton* selectionInstance() const override;

    /** @name ViewerContext -- event delivery and selection mode */
    //@{
    void addEventCallback(SoType eventtype, SoEventCallbackCB* cb,
                          void* userdata = nullptr) override;
    void removeEventCallback(SoType eventtype, SoEventCallbackCB* cb,
                             void* userdata = nullptr) override;
    void setRedirectToSceneGraph(bool redirect) override;
    void setSelectionEnabled(bool enable) override;
    bool isSelectionEnabled() const override;
    bool isSelecting() const override;
    //@}

private:
    /// Deliver one built event through this mirror's event manager, with
    /// this view current for the extent of the call.
    bool replay(SoEvent& event);
    /// The same, for a key frame: built into the Coin event it describes.
    bool replayKey(const Input& input);
    /// Hand a key frame to the focused entry box as the Qt event it is.
    bool routeKeyToParameter(const Input& input);

    class Private;
    std::unique_ptr<Private> pimpl;
};

}  // namespace Gui

#endif  // GUI_MIRRORVIEWER_H
