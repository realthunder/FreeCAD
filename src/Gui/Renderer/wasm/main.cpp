// Browser entry point of the standalone bgfx renderer build.
//
// Initializes the FreeCAD bgfx renderer on the #canvas element (bgfx
// creates the WebGL2 context), loads a scene snapshot captured on the
// desktop (FC_BGFX_DUMP_SCENE -> preloaded at /scene.fcsd) and renders
// it with an orbit camera (drag = orbit, shift/right-drag = pan,
// wheel = zoom).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>

#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#include <emscripten/websocket.h>

#include <bx/math.h>

#include "BGFXRenderer.h"
#include "SceneDump.h"
#include "StandalonePlatform.h"

static std::unique_ptr<Render::Renderer> s_renderer;
static Render::SceneSnapshot s_snap;
static bool s_haveScene = false;
static std::set<int> s_selIds;
static std::set<int> s_overlayIds;

// Live streaming (?scene=<http://host:port> page parameter): connect a
// WebSocket to the desktop scene server (FC_BGFX_SERVE_SCENE) and
// re-apply the feeds on every pushed version; fall back to HTTP polling
// where WebSocket fails.
static std::string s_sceneUrl;
static uint64_t s_sceneVersion = 0;
static EMSCRIPTEN_WEBSOCKET_T s_ws = 0;
static bool s_wsOpen = false;
static bool s_polling = false;

EM_JS(char *, fcviewer_scene_param, (), {
    var p = new URLSearchParams(window.location.search).get('scene');
    if (!p)
        return 0;
    var len = lengthBytesUTF8(p) + 1;
    var s = _malloc(len);
    stringToUTF8(p, s, len);
    return s;
});

// ?cam=<yaw,pitch,dist,cx,cy,cz,panX,panY> reproduces an exact viewport
// (the string the 'v' key prints); null when absent.
EM_JS(char *, fcviewer_cam_param, (), {
    var p = new URLSearchParams(window.location.search).get('cam');
    if (!p)
        return 0;
    var len = lengthBytesUTF8(p) + 1;
    var s = _malloc(len);
    stringToUTF8(p, s, len);
    return s;
});

// Log the current camera string to the console (and copy to the clipboard
// when available) so it can be pasted back as a ?cam= parameter.
EM_JS(void, fcviewer_report_cam, (const char *s), {
    var str = UTF8ToString(s);
    console.log('fcviewer: cam=' + str);
    try { if (navigator.clipboard) navigator.clipboard.writeText(str); } catch (e) {}
});

// On-screen debug HUD: a fixed div over the canvas top-left showing live
// state (mouse, camera, hover). Passing null hides it.
EM_JS(void, fcviewer_hud, (const char *s), {
    var el = document.getElementById('__hud');
    if (!s) { if (el) el.style.display = 'none'; return; }
    if (!el) {
        el = document.createElement('div');
        el.id = '__hud';
        el.style.cssText = 'position:fixed;left:10px;top:96px;z-index:99999;'
            + 'font:12px/1.45 monospace;color:#0f0;background:rgba(0,0,0,.62);'
            + 'padding:6px 9px;white-space:pre;pointer-events:none;'
            + 'border-radius:4px';
        document.body.appendChild(el);
    }
    el.style.display = 'block';
    el.textContent = UTF8ToString(s);
});

// Canvas position in client coordinates (mouse events are registered
// on the document; picking needs canvas-relative pixels).
EM_JS(void, fcviewer_canvas_origin, (double *out), {
    var r = document.getElementById('canvas').getBoundingClientRect();
    HEAPF64[out >> 3] = r.left;
    HEAPF64[(out >> 3) + 1] = r.top;
});

static int s_width = 1024;    // drawing-buffer size, device px (css * dpr)
static int s_height = 768;
static float s_dpr = 1.0f;    // devicePixelRatio applied to the buffer

// Orbit camera state around the scene bounds.
static float s_center[3] = {0.0f, 0.0f, 0.0f};
static float s_panX = 0.0f, s_panY = 0.0f;  // pan in camera plane
static float s_yaw = 0.785f;
static float s_pitch = 0.5f;
static float s_dist = 10.0f;
static float s_diag = 10.0f;
// A ?cam= parameter overriding the initial fit (applied after the first
// fitCamera so the scene bounds / near-far are still derived).
static bool s_haveCamParam = false;
static float s_camParam[8] = {0.0f};

// On-screen debug HUD state.
static bool s_hudOn = false;
static float s_mouseX = 0.0f, s_mouseY = 0.0f;   // last cursor, canvas px
static char s_hoverDesc[128] = "none";
static char s_camMsg[96] = "";                    // last 'v' capture
static bool s_dragging = false;
static bool s_panning = false;
static int s_lastX = 0, s_lastY = 0;

// Progressive refinement (the Fusion 360 pattern): camera interaction
// drops MSAA and SSAO for cheap frames, idle restores them. The MSAA
// flip re-creates the render targets on the next render()
// (BGFXRenderer::setMSAASamples).
static const double kRefineDelayMs = 300.0;
static const int kFullMSAA = 4;
static double s_lastInteract = -1e9;
static bool s_degraded = false;
// Set once the user drives the camera (orbit/pan/zoom/NaviCube/?cam). Until
// then the camera is the auto fit, so a viewport/orientation change re-fits.
static bool s_userCam = false;

static void interact()
{
    s_lastInteract = emscripten_get_now();
    s_userCam = true;
}

static void updateQuality()
{
    const bool moving =
        emscripten_get_now() - s_lastInteract < kRefineDelayMs;
    if (moving == s_degraded)
        return;
    s_degraded = moving;
    Render::BGFXRenderer::setMSAASamples(moving ? 0 : kFullMSAA);
    Render::AOConfig ao = s_snap.aoconf;
    if (moving)
        ao.enabled = false;
    s_renderer->setAOConfig(ao);
}

static const float kFovY = 45.0f;

/// Orbit camera frame shared by rendering and picking.
struct CamFrame {
    bx::Vec3 eye = bx::InitZero;
    bx::Vec3 at = bx::InitZero;
    bx::Vec3 right = bx::InitZero;
    bx::Vec3 up = bx::InitZero;
};

static CamFrame camFrame()
{
    const float cp = std::cos(s_pitch), sp = std::sin(s_pitch);
    const float cy = std::cos(s_yaw), sy = std::sin(s_yaw);
    // Z-up spherical orbit (FreeCAD convention).
    bx::Vec3 dir(cp * cy, cp * sy, sp);
    bx::Vec3 up(0.0f, 0.0f, 1.0f);
    CamFrame f;
    f.right = bx::normalize(bx::cross(dir, up));
    f.up = bx::normalize(bx::cross(f.right, dir));
    f.at = bx::Vec3(s_center[0], s_center[1], s_center[2]);
    f.at = bx::add(f.at, bx::add(bx::mul(f.right, s_panX),
                                 bx::mul(f.up, s_panY)));
    f.eye = bx::add(f.at, bx::mul(dir, s_dist));
    return f;
}

/// World-space ray through a canvas pixel of the current camera.
static void screenRay(float px, float py, bx::Vec3 &orig, bx::Vec3 &rdir)
{
    CamFrame f = camFrame();
    const float aspect = s_height > 0
        ? float(s_width) / float(s_height) : 1.0f;
    const float th = std::tan(0.5f * kFovY * bx::kPi / 180.0f);
    const float nx = s_width > 0 ? 2.0f * px / float(s_width) - 1.0f : 0.0f;
    const float ny = s_height > 0 ? 1.0f - 2.0f * py / float(s_height) : 0.0f;
    bx::Vec3 fwd = bx::normalize(bx::sub(f.at, f.eye));
    // The camera's right axis is cross(forward, up) — CamFrame::right
    // is its negation (the orbit frame kept the historical pan
    // convention), so the horizontal ray term flips sign.
    bx::Vec3 rightCam = bx::normalize(bx::cross(fwd, bx::Vec3(0.0f, 0.0f, 1.0f)));
    orig = f.eye;
    rdir = bx::normalize(bx::add(fwd,
        bx::add(bx::mul(rightCam, nx * th * aspect),
                bx::mul(f.up, ny * th))));
}

static void buildCamera(float *viewMtx, float *projMtx)
{
    CamFrame f = camFrame();
    bx::Vec3 up(0.0f, 0.0f, 1.0f);
    // Right-handed like the Coin camera the renderer's shading assumes
    // (camera forward = -z in view space; bx defaults to left-handed).
    bx::mtxLookAt(viewMtx, f.eye, f.at, up, bx::Handedness::Right);

    const float aspect = s_height > 0
        ? float(s_width) / float(s_height) : 1.0f;
    const float neard = bx::max(0.001f * s_diag, s_dist - 4.0f * s_diag);
    const float fard = s_dist + 4.0f * s_diag;
    // WebGL keeps the GL clip conventions (homogeneous depth); bgfx is
    // not initialized yet when the first frame builds its camera, so
    // don't ask getCaps().
    bx::mtxProj(projMtx, kFovY, aspect, neard, fard, true,
                bx::Handedness::Right);
}

//////////////////////////////////////////////////////////////////////
// Local preselection: CPU raycast against the snapshot meshes

struct PickHit {
    int draw = -1;          ///< index into s_snap.scene
    int triOffset = -1;     ///< first index of the hit triangle in the
                            ///< mesh triangle-index buffer
    float t = 1e30f;        ///< world-space ray parameter
};

/// Ray/AABB slab test in world space.
static bool rayHitsBBox(const float *bmin, const float *bmax,
                        const bx::Vec3 &o, const bx::Vec3 &d, float tmax)
{
    float t0 = 0.0f, t1 = tmax;
    const float *ov = &o.x;
    const float *dv = &d.x;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dv[i]) < 1e-20f) {
            if (ov[i] < bmin[i] || ov[i] > bmax[i])
                return false;
            continue;
        }
        float inv = 1.0f / dv[i];
        float ta = (bmin[i] - ov[i]) * inv;
        float tb = (bmax[i] - ov[i]) * inv;
        if (ta > tb)
            std::swap(ta, tb);
        t0 = bx::max(t0, ta);
        t1 = bx::min(t1, tb);
        if (t0 > t1)
            return false;
    }
    return true;
}

/// Möller-Trumbore, two-sided; \a t is the model-space ray parameter.
static bool rayHitsTriangle(const bx::Vec3 &o, const bx::Vec3 &d,
                            const float *v0, const float *v1,
                            const float *v2, float &t)
{
    bx::Vec3 a(v0[0], v0[1], v0[2]);
    bx::Vec3 e1(v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]);
    bx::Vec3 e2(v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]);
    bx::Vec3 p = bx::cross(d, e2);
    float det = bx::dot(e1, p);
    if (std::fabs(det) < 1e-12f)
        return false;
    float inv = 1.0f / det;
    bx::Vec3 s = bx::sub(o, a);
    float u = bx::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f)
        return false;
    bx::Vec3 q = bx::cross(s, e1);
    float v = bx::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    float tt = bx::dot(e2, q) * inv;
    if (tt <= 0.0f)
        return false;
    t = tt;
    return true;
}

/// Closest triangle-draw hit of the ray through canvas pixel (px, py).
static PickHit pickScene(float px, float py)
{
    PickHit hit;
    bx::Vec3 orig(bx::InitZero), rdir(bx::InitZero);
    screenRay(px, py, orig, rdir);
    for (size_t di = 0; di < s_snap.scene.size(); ++di) {
        const auto &dc = s_snap.scene[di];
        if (dc.material.type != Render::Material::Triangle || !dc.mesh
                || !dc.mesh->triangleIndices || !dc.mesh->positions)
            continue;
        if (dc.bboxMin[0] <= dc.bboxMax[0]
                && !rayHitsBBox(dc.bboxMin, dc.bboxMax, orig, rdir, hit.t))
            continue;

        // Cast in model space (transform the ray by the inverse model
        // matrix); the hit converts back to a world-space t so draws
        // stay comparable.
        bx::Vec3 mo = orig, md = rdir;
        float model[16], inv[16];
        if (!dc.identity) {
            std::memcpy(model, dc.model, sizeof(model));
            bx::mtxInverse(inv, model);
            mo = bx::mul(orig, inv);        // point transform
            md = bx::mulXyz0(rdir, inv);    // vector transform
        }

        const int total = dc.mesh->numTriangleIndices;
        int start = dc.indexStart;
        int count = dc.indexCount ? dc.indexCount : total - start;
        if (start < 0 || start + count > total)
            continue;
        const int32_t *idx = dc.mesh->triangleIndices;
        const float *pos = dc.mesh->positions;
        for (int i = start; i + 2 < start + count; i += 3) {
            float t;
            if (!rayHitsTriangle(mo, md, pos + 3 * idx[i],
                                 pos + 3 * idx[i + 1],
                                 pos + 3 * idx[i + 2], t))
                continue;
            // Model-space hit point -> world t (handles scaled models).
            float tw = t;
            if (!dc.identity) {
                bx::Vec3 mp = bx::add(mo, bx::mul(md, t));
                bx::Vec3 wp = bx::mul(mp, model);
                tw = bx::dot(bx::sub(wp, orig), rdir);
            }
            if (tw > 0.0f && tw < hit.t) {
                hit.t = tw;
                hit.draw = int(di);
                hit.triOffset = i;
            }
        }
    }
    return hit;
}

//////////////////////////////////////////////////////////////////////
// NaviCube click-to-orient: browser-local raycast against the cube
// overlay feed, then snap the orbit camera to the picked face / edge /
// corner view. The desktop keeps its own GL pick pass; this makes the
// streamed (and snapshot-only) cube interactive in the browser.

static const int kNaviCubeOverlayId = 5;  // View3DInventorViewer OverlayNaviCube
// Browser-local NaviCube hover highlight: a tinted copy of the hovered
// cube face, fed as its own overlay (a high id so it draws after the cube
// and buttons) anchored to the cube's viewport. Not part of the streamed
// snapshot, so the snapshot replay never clears it.
static const int kCubeHiliteOverlayId = 20;
static int s_cubeHiliteDraw = -2;         // cube draw index currently tinted

/// Overlay viewport rect in top-left canvas pixels, mirroring
/// BGFXRenderer's per-frame anchor placement (corner + margins).
static void overlayRect(const Render::OverlayAnchor &a,
                        int &rx, int &ry, int &rw, int &rh)
{
    if (a.corner == Render::OverlayAnchor::FullViewport) {
        rx = ry = 0;
        rw = s_width;
        rh = s_height;
        return;
    }
    int edge = int(std::max(1.0f,
        a.sizeFraction * float(std::min(s_width, s_height))));
    rw = rh = edge;
    const bool right = a.corner == Render::OverlayAnchor::BottomRight
        || a.corner == Render::OverlayAnchor::TopRight;
    const bool top = a.corner == Render::OverlayAnchor::TopLeft
        || a.corner == Render::OverlayAnchor::TopRight;
    const int mx = int(a.marginX), my = int(a.marginY);
    rx = std::max(0, right ? s_width - edge - mx : mx);
    ry = std::max(0, top ? my : s_height - edge - my);
}

/// If canvas pixel (px, py) lands on the NaviCube overlay, return in
/// \a dirOut the snapped world-space direction (the eye offset from the
/// scene center) for the picked face / edge / corner view.
static bool pickNaviCube(float px, float py, bx::Vec3 &dirOut)
{
    const Render::SceneSnapshot::Overlay *cube = nullptr;
    for (const auto &ov : s_snap.overlays) {
        if (ov.id == kNaviCubeOverlayId) {
            cube = &ov;
            break;
        }
    }
    if (!cube || cube->draws.empty())
        return false;

    const Render::OverlayAnchor &a = cube->anchor;
    int rx, ry, rw, rh;
    overlayRect(a, rx, ry, rw, rh);
    const float lx = px - float(rx), ly = py - float(ry);
    if (rw <= 0 || rh <= 0 || lx < 0.0f || ly < 0.0f
            || lx > float(rw) || ly > float(rh))
        return false;

    // Overlay view matrix: the orientFromScene rotation of the current
    // orbit view, translated back by the anchor camera distance — the
    // same matrix BGFXRenderer builds to draw the cube.
    float viewMtx[16], projMtx[16];
    buildCamera(viewMtx, projMtx);
    float ovView[16];
    bx::mtxIdentity(ovView);
    if (a.orientFromScene) {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                ovView[c * 4 + r] = viewMtx[c * 4 + r];
    }
    ovView[14] = -a.cameraDistance;

    // Perspective ray in overlay view space (RH, camera looks down -z);
    // matches bx::mtxProj's mapping used to render the overlay.
    const float aspect = float(rw) / float(rh);
    const float th = std::tan(0.5f * a.fovDeg * bx::kPi / 180.0f);
    const float nx = 2.0f * lx / float(rw) - 1.0f;
    const float ny = 1.0f - 2.0f * ly / float(rh);
    const bx::Vec3 dv =
        bx::normalize(bx::Vec3(nx * th * aspect, ny * th, -1.0f));

    float invV[16];
    bx::mtxInverse(invV, ovView);
    const bx::Vec3 orig = bx::mul(bx::Vec3(0.0f, 0.0f, 0.0f), invV);
    const bx::Vec3 rdir = bx::normalize(bx::mulXyz0(dv, invV));

    // Closest triangle hit across the cube overlay's face draws.
    float bestT = 1e30f;
    for (const auto &dc : cube->draws) {
        if (dc.material.type != Render::Material::Triangle || !dc.mesh
                || !dc.mesh->triangleIndices || !dc.mesh->positions)
            continue;
        bx::Vec3 mo = orig, md = rdir;
        float model[16], inv[16];
        if (!dc.identity) {
            std::memcpy(model, dc.model, sizeof(model));
            bx::mtxInverse(inv, model);
            mo = bx::mul(orig, inv);
            md = bx::mulXyz0(rdir, inv);
        }
        const int total = dc.mesh->numTriangleIndices;
        int start = dc.indexStart;
        int count = dc.indexCount ? dc.indexCount : total - start;
        if (start < 0 || start + count > total)
            continue;
        const int32_t *idx = dc.mesh->triangleIndices;
        const float *pos = dc.mesh->positions;
        for (int i = start; i + 2 < start + count; i += 3) {
            float t;
            if (!rayHitsTriangle(mo, md, pos + 3 * idx[i],
                                 pos + 3 * idx[i + 1],
                                 pos + 3 * idx[i + 2], t))
                continue;
            float tw = t;
            if (!dc.identity) {
                bx::Vec3 mp = bx::add(mo, bx::mul(md, t));
                bx::Vec3 wp = bx::mul(mp, model);
                tw = bx::dot(bx::sub(wp, orig), rdir);
            }
            if (tw > 0.0f && tw < bestT)
                bestT = tw;
        }
    }
    if (bestT >= 1e29f)
        return false;

    // Hit point in the cube's own frame — which equals the world frame,
    // since orientFromScene applied exactly the orbit camera's rotation.
    // Snap to the NaviCube 3x3 face grid: the outer third of each face
    // edge counts toward the neighbouring edge / corner view. The
    // resulting axis-signed direction is where the eye goes so that the
    // picked feature faces the camera.
    const bx::Vec3 hp = bx::add(orig, bx::mul(rdir, bestT));
    const float axv = std::fabs(hp.x), ayv = std::fabs(hp.y),
                azv = std::fabs(hp.z);
    const float m = bx::max(axv, bx::max(ayv, azv));
    if (m < 1e-6f)
        return false;
    const float thr = m / 3.0f;
    dirOut = bx::Vec3(axv > thr ? (hp.x > 0.0f ? 1.0f : -1.0f) : 0.0f,
                      ayv > thr ? (hp.y > 0.0f ? 1.0f : -1.0f) : 0.0f,
                      azv > thr ? (hp.z > 0.0f ? 1.0f : -1.0f) : 0.0f);
    return true;
}

/// Closest-hit cube face draw under canvas pixel (px, py), or -1. Shares
/// the overlay-space ray construction with pickNaviCube; \a cubeOut is the
/// cube overlay the hit belongs to (null when the cursor is off the cube).
static int pickCubeDraw(float px, float py,
                        const Render::SceneSnapshot::Overlay *&cubeOut)
{
    cubeOut = nullptr;
    const Render::SceneSnapshot::Overlay *cube = nullptr;
    for (const auto &ov : s_snap.overlays) {
        if (ov.id == kNaviCubeOverlayId) {
            cube = &ov;
            break;
        }
    }
    if (!cube || cube->draws.empty())
        return -1;

    const Render::OverlayAnchor &a = cube->anchor;
    int rx, ry, rw, rh;
    overlayRect(a, rx, ry, rw, rh);
    const float lx = px - float(rx), ly = py - float(ry);
    if (rw <= 0 || rh <= 0 || lx < 0.0f || ly < 0.0f
            || lx > float(rw) || ly > float(rh))
        return -1;

    float viewMtx[16], projMtx[16];
    buildCamera(viewMtx, projMtx);
    float ovView[16];
    bx::mtxIdentity(ovView);
    if (a.orientFromScene) {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                ovView[c * 4 + r] = viewMtx[c * 4 + r];
    }
    ovView[14] = -a.cameraDistance;
    const float aspect = float(rw) / float(rh);
    const float th = std::tan(0.5f * a.fovDeg * bx::kPi / 180.0f);
    const float nx = 2.0f * lx / float(rw) - 1.0f;
    const float ny = 1.0f - 2.0f * ly / float(rh);
    const bx::Vec3 dv =
        bx::normalize(bx::Vec3(nx * th * aspect, ny * th, -1.0f));
    float invV[16];
    bx::mtxInverse(invV, ovView);
    const bx::Vec3 orig = bx::mul(bx::Vec3(0.0f, 0.0f, 0.0f), invV);
    const bx::Vec3 rdir = bx::normalize(bx::mulXyz0(dv, invV));

    int best = -1;
    float bestT = 1e30f;
    for (size_t di = 0; di < cube->draws.size(); ++di) {
        const auto &dc = cube->draws[di];
        if (dc.material.type != Render::Material::Triangle || !dc.mesh
                || !dc.mesh->triangleIndices || !dc.mesh->positions)
            continue;
        // Hover picks only the untextured exact-polygon fill draws. The label
        // squares are textured and coplanar with the octagon faces, poking a
        // little past them at the chamfer corners; skipping them keeps the
        // hover region exactly the face/edge/corner shape.
        if (dc.material.texture)
            continue;
        bx::Vec3 mo = orig, md = rdir;
        float model[16], inv[16];
        if (!dc.identity) {
            std::memcpy(model, dc.model, sizeof(model));
            bx::mtxInverse(inv, model);
            mo = bx::mul(orig, inv);
            md = bx::mulXyz0(rdir, inv);
        }
        const int total = dc.mesh->numTriangleIndices;
        int start = dc.indexStart;
        int count = dc.indexCount ? dc.indexCount : total - start;
        if (start < 0 || start + count > total)
            continue;
        const int32_t *idx = dc.mesh->triangleIndices;
        const float *pos = dc.mesh->positions;
        for (int i = start; i + 2 < start + count; i += 3) {
            float t;
            if (!rayHitsTriangle(mo, md, pos + 3 * idx[i],
                                 pos + 3 * idx[i + 1],
                                 pos + 3 * idx[i + 2], t))
                continue;
            float tw = t;
            if (!dc.identity) {
                bx::Vec3 mp = bx::add(mo, bx::mul(md, t));
                bx::Vec3 wp = bx::mul(mp, model);
                tw = bx::dot(bx::sub(wp, orig), rdir);
            }
            if (tw > 0.0f && tw < bestT) {
                bestT = tw;
                best = int(di);
            }
        }
    }
    static const bool dbg = EM_ASM_INT({
        return new URLSearchParams(location.search).has('debugpick') ? 1 : 0;
    }) != 0;
    if (dbg && best >= 0) {
        bx::Vec3 hp = bx::add(orig, bx::mul(rdir, bestT));
        std::printf("fcviewer: cubepick cur(%.0f,%.0f) rect(%d,%d,%d,%d) "
                    "-> draw %d hit(%.2f,%.2f,%.2f)\n",
                    px, py, rx, ry, rw, rh, best, hp.x, hp.y, hp.z);
    }
    cubeOut = cube;
    return best;
}

/// Hover highlight for the NaviCube: tints the hovered face by feeding a
/// translucent copy of its draw as an overlay in the cube's own viewport.
/// Returns true while the cursor is over the cube (so scene preselection
/// yields to it). No round trip — matches the desktop's local cube hilite.
static bool updateCubeHover(float px, float py)
{
    const Render::SceneSnapshot::Overlay *cube = nullptr;
    int di = pickCubeDraw(px, py, cube);
    if (di < 0 || !cube) {
        if (s_cubeHiliteDraw != -2) {
            s_renderer->removeOverlay(kCubeHiliteOverlayId);
            s_cubeHiliteDraw = -2;
        }
        return false;
    }
    if (di != s_cubeHiliteDraw) {
        s_cubeHiliteDraw = di;
        Render::DrawCall hl = cube->draws[size_t(di)];
        // NaviCube HiliteColor (170,226,255) tint over the face. The face
        // texture is kept (not reset): its rounded-corner texels are
        // transparent and the mesh shader discards near-zero alpha, so the
        // tint is clipped to the actual face shape instead of the full
        // quad. Pushed toward the viewer so it wins the depth test.
        hl.material.diffuse = 0xAAE2FFD0;
        hl.material.pervertexcolor = false;
        hl.material.transparent = true;
        hl.material.polygonoffset = true;
        hl.material.polygonoffsetfactor = -3.0f;
        hl.material.polygonoffsetunits = -3.0f;
        Render::DrawCallList draws;
        draws.push_back(std::move(hl));
        s_renderer->setOverlay(kCubeHiliteOverlayId, std::move(draws),
                               cube->anchor);
    }
    return true;
}

/// Drop any active cube hover tint (on drag start / click, before the cube
/// geometry moves under it).
static void clearCubeHover()
{
    if (s_cubeHiliteDraw != -2) {
        s_renderer->removeOverlay(kCubeHiliteOverlayId);
        s_cubeHiliteDraw = -2;
    }
}

/// Snap the orbit camera to look from world direction \a d (eye offset
/// from the scene center). Keeps the current azimuth for the top/bottom
/// poles where it is otherwise undefined, and recenters the pan.
static void orientToDir(const bx::Vec3 &d)
{
    const bx::Vec3 n = bx::normalize(d);
    const float newPitch = std::asin(bx::clamp(n.z, -1.0f, 1.0f));
    float newYaw = s_yaw;
    if (std::fabs(n.x) > 1e-4f || std::fabs(n.y) > 1e-4f)
        newYaw = std::atan2(n.y, n.x);
    // Match the orbit clamp (avoids the up-vector singularity at ±90°).
    s_pitch = bx::clamp(newPitch, -1.55f, 1.55f);
    s_yaw = newYaw;
    s_panX = s_panY = 0.0f;
}

static const int kNaviButtonsOverlayId = 6;  // OverlayNaviButtons

enum NaviButtonAction {
    NaviBtnNone, NaviBtnTiltUp, NaviBtnTiltDown,
    NaviBtnOrbitLeft, NaviBtnOrbitRight
};

/// Map a click on the NaviCube button overlay (the tilt/orbit arrows
/// ringing the cube) to an orbit-camera nudge. The button quads all
/// cover the whole corner rect and differ only in where their texture
/// is opaque, so — unlike the cube — they are hit-tested by the arrow
/// hot-zones at the rect mid-edges rather than raycast. The corner roll
/// arrows and the menu icon have no place in the fixed-world-up orbit
/// camera and are left unhandled.
static NaviButtonAction pickNaviButton(float px, float py)
{
    const Render::SceneSnapshot::Overlay *btn = nullptr;
    for (const auto &ov : s_snap.overlays) {
        if (ov.id == kNaviButtonsOverlayId) {
            btn = &ov;
            break;
        }
    }
    if (!btn || btn->draws.empty())
        return NaviBtnNone;

    int rx, ry, rw, rh;
    overlayRect(btn->anchor, rx, ry, rw, rh);
    const float lx = px - float(rx), ly = py - float(ry);
    if (rw <= 0 || rh <= 0 || lx < 0.0f || ly < 0.0f
            || lx > float(rw) || ly > float(rh))
        return NaviBtnNone;

    // Local NDC of the button ortho (orthoHeight 2, y up), matching the
    // arrow texture layout: north/south at top/bottom mid, east/west at
    // right/left mid. The cube occupies the centre (|n| < ~0.5), so the
    // outer edge bands never overlap it.
    const float nx = 2.0f * lx / float(rw) - 1.0f;
    const float ny = 1.0f - 2.0f * ly / float(rh);
    const float lat = 0.28f;   // arrow half-width across its travel axis
    const float band = 0.6f;   // how far out along the axis the arrow sits
    if (std::fabs(nx) < lat && ny > band)
        return NaviBtnTiltUp;       // north arrow
    if (std::fabs(nx) < lat && ny < -band)
        return NaviBtnTiltDown;     // south arrow
    if (std::fabs(ny) < lat && nx > band)
        return NaviBtnOrbitRight;   // east arrow
    if (std::fabs(ny) < lat && nx < -band)
        return NaviBtnOrbitLeft;    // west arrow
    return NaviBtnNone;
}

/// Nudge the orbit camera by one NaviCube step (default NaviStepByTurn
/// = 8 -> 45°). Tilt changes elevation, orbit changes azimuth.
static void applyNaviButton(NaviButtonAction a)
{
    const float step = bx::kPi / 4.0f;
    switch (a) {
    case NaviBtnTiltUp:
        s_pitch = bx::clamp(s_pitch + step, -1.55f, 1.55f);
        break;
    case NaviBtnTiltDown:
        s_pitch = bx::clamp(s_pitch - step, -1.55f, 1.55f);
        break;
    case NaviBtnOrbitLeft:
        s_yaw += step;
        break;
    case NaviBtnOrbitRight:
        s_yaw -= step;
        break;
    default:
        break;
    }
}

// Hover highlight state: a local setHighlight() built from the hit
// draw (no server round trip). The next streamed snapshot replaces it
// with the desktop's highlight feed until the mouse moves again.
static uint64_t s_hoverKey = 0;
static int s_hoverPart = -2;

static void applyHover(const PickHit &hit)
{
    if (hit.draw < 0) {
        if (s_hoverKey || s_hoverPart != -2) {
            s_hoverKey = 0;
            s_hoverPart = -2;
            s_renderer->clearHighlight();
        }
        return;
    }
    const auto &dc = s_snap.scene[size_t(hit.draw)];

    // Map the hit triangle to its face part where the mesh carries the
    // per-face table; the whole draw highlights otherwise.
    int part = -1, partStart = 0, partCount = 0;
    const auto &parts = dc.mesh->triangleParts;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (hit.triOffset >= parts[i].first
                && hit.triOffset < parts[i].first + parts[i].second) {
            part = int(i);
            partStart = parts[i].first;
            partCount = parts[i].second;
            break;
        }
    }
    if (dc.objectKey == s_hoverKey && part == s_hoverPart)
        return;
    s_hoverKey = dc.objectKey;
    s_hoverPart = part;

    Render::DrawCall hl = dc;
    // ViewParams::HighlightColor default; drawn slightly toward the
    // viewer so the tint wins the depth test against the base face.
    hl.material.diffuse = 0xE1E114FF;
    hl.material.pervertexcolor = false;
    hl.material.transparent = false;
    hl.material.texture.reset();
    hl.material.polygonoffset = true;
    hl.material.polygonoffsetfactor = -2.0f;
    hl.material.polygonoffsetunits = -2.0f;
    if (part >= 0) {
        hl.partIndex = part;
        hl.wholeObject = false;
        hl.indexStart = partStart;
        hl.indexCount = partCount;
    }
    Render::DrawCallList draws;
    draws.push_back(std::move(hl));
    s_renderer->setHighlight(std::move(draws), false);
}

static void fitCamera();

static void mainLoop()
{
    double w = 0, h = 0;
    emscripten_get_element_css_size("#canvas", &w, &h);
    // Render at the device pixel ratio so the buffer matches physical pixels
    // (crisp on hi-DPI / mobile); the CSS size still fills the viewport, so
    // the browser downsamples nothing.
    double dpr = emscripten_get_device_pixel_ratio();
    if (dpr < 1.0)
        dpr = 1.0;
    int iw = int(w * dpr + 0.5), ih = int(h * dpr + 0.5);
    if (iw > 0 && ih > 0 && (iw != s_width || ih != s_height)) {
        s_width = iw;
        s_height = ih;
        s_dpr = float(dpr);
        emscripten_set_canvas_element_size("#canvas", iw, ih);
        Render::BGFXRenderer::setWindowSize(iw, ih);
        // Until the user takes the camera, reframe on resize/rotation so the
        // whole model stays visible in the new aspect.
        if (!s_userCam)
            fitCamera();
    }

    updateQuality();

    float viewMtx[16], projMtx[16];
    buildCamera(viewMtx, projMtx);
    QColor bg((s_snap.clearColor >> 24) & 0xff,
              (s_snap.clearColor >> 16) & 0xff,
              (s_snap.clearColor >> 8) & 0xff);
    s_renderer->render(bg, viewMtx, projMtx);

    if (s_hudOn) {
        char hud[512];
        std::snprintf(hud, sizeof(hud),
            "mouse: %.0f, %.0f  (canvas %dx%d)\n"
            "cam:   yaw %.3f  pitch %.3f  dist %.2f\n"
            "pan:   %.2f, %.2f   center %.1f, %.1f, %.1f\n"
            "hover: %s\n"
            "%s"
            "[d] toggle HUD   [v] copy cam",
            s_mouseX, s_mouseY, s_width, s_height,
            s_yaw, s_pitch, s_dist, s_panX, s_panY,
            s_center[0], s_center[1], s_center[2], s_hoverDesc,
            s_camMsg[0] ? s_camMsg : "");
        fcviewer_hud(hud);
    }
}

static float panScale()
{
    // World units per CSS pixel of drag: the visible world height spans the
    // buffer's device-px height, and drag deltas come in CSS px, so fold in
    // the dpr (buffer = css * dpr).
    return 2.0f * s_dist * std::tan(0.5f * kFovY * bx::kPi / 180.0f)
        * s_dpr / float(s_height > 0 ? s_height : 1);
}

// Click detection (mouseup without meaningful drag) for the roundtrip
// pick, and hover raycast throttling.
static int s_downX = 0, s_downY = 0;
static bool s_clickOk = false;
static double s_lastHoverMs = 0.0;

static void clientToCanvas(float cx, float cy, float &x, float &y)
{
    // Event coords + the canvas rect are CSS px; scale to device px so they
    // match the (dpr-scaled) drawing buffer used for picking / overlay rects.
    double origin[2] = {0.0, 0.0};
    fcviewer_canvas_origin(origin);
    x = (cx - float(origin[0])) * s_dpr;
    y = (cy - float(origin[1])) * s_dpr;
}

static void canvasPos(const EmscriptenMouseEvent *e, float &x, float &y)
{
    clientToCanvas(float(e->clientX), float(e->clientY), x, y);
}

/// Send a world-ray pick request for canvas pixel (px,py) to the desktop
/// (the selection echo returns through the scene feed).
static void sendPickXY(float px, float py, bool ctrl)
{
    if (!s_wsOpen)
        return;
    bx::Vec3 orig(bx::InitZero), rdir(bx::InitZero);
    screenRay(px, py, orig, rdir);
    uint8_t msg[2 + 6 * sizeof(float)];
    msg[0] = 'P';
    msg[1] = ctrl ? 1 : 0;
    float v[6] = {orig.x, orig.y, orig.z, rdir.x, rdir.y, rdir.z};
    std::memcpy(msg + 2, v, sizeof(v));
    emscripten_websocket_send_binary(s_ws, msg, sizeof(msg));
}

/// Resolve a click/tap at canvas pixel (px,py): the NaviCube claims it
/// first (local orient, then rotate button), otherwise it is a scene pick
/// sent to the desktop. Shared by the mouse-up and touch-tap paths.
static void doTapPick(float px, float py, bool ctrl)
{
    static const bool debugPick = EM_ASM_INT({
        return new URLSearchParams(window.location.search).has('debugpick')
            ? 1 : 0;
    }) != 0;
    bx::Vec3 dir(bx::InitZero);
    NaviButtonAction btn = NaviBtnNone;
    if (s_haveScene && pickNaviCube(px, py, dir)) {
        if (debugPick)
            std::printf("fcviewer: navicube orient (%g,%g) -> dir %g,%g,%g\n",
                        px, py, dir.x, dir.y, dir.z);
        orientToDir(dir);
        interact();
    }
    else if (s_haveScene && (btn = pickNaviButton(px, py)) != NaviBtnNone) {
        if (debugPick)
            std::printf("fcviewer: navicube button (%g,%g) -> %d\n",
                        px, py, int(btn));
        applyNaviButton(btn);
        interact();
    }
    else {
        sendPickXY(px, py, ctrl);
    }
}

static void updateHover(const EmscriptenMouseEvent *e)
{
    if (!s_haveScene)
        return;
    const double now = emscripten_get_now();
    if (now - s_lastHoverMs < 30.0)
        return;
    s_lastHoverMs = now;
    float px, py;
    canvasPos(e, px, py);
    s_mouseX = px;
    s_mouseY = py;
    // NaviCube face hover highlight wins over scene preselection: when the
    // cursor is over the cube, tint the face and clear any scene hover.
    if (updateCubeHover(px, py)) {
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc),
                      "NaviCube draw %d", s_cubeHiliteDraw);
        applyHover(PickHit{});
        return;
    }
    PickHit hit = pickScene(px, py);
    static const bool debugPick = EM_ASM_INT({
        return new URLSearchParams(window.location.search).has('debugpick')
            ? 1 : 0;
    }) != 0;
    if (debugPick)
        std::printf("fcviewer: pick (%g,%g) -> draw %d tri %d t %g\n",
                    px, py, hit.draw, hit.triOffset, hit.t);
    if (hit.draw >= 0)
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc),
                      "scene draw %d tri %d", hit.draw, hit.triOffset);
    else
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc), "none");
    applyHover(hit);
}

static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent *e, void *)
{
    s_dragging = true;
    s_panning = e->button == 2 || e->shiftKey;
    s_lastX = int(e->clientX);
    s_lastY = int(e->clientY);
    s_downX = s_lastX;
    s_downY = s_lastY;
    s_clickOk = e->button == 0 && !e->shiftKey;
    // The cube geometry is about to move under any active hover tint.
    clearCubeHover();
    return EM_TRUE;
}

static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent *e, void *)
{
    s_dragging = false;
    if (s_clickOk && std::abs(int(e->clientX) - s_downX) <= 6
            && std::abs(int(e->clientY) - s_downY) <= 6) {
        float px, py;
        canvasPos(e, px, py);
        doTapPick(px, py, e->ctrlKey);
    }
    s_clickOk = false;
    return EM_TRUE;
}

static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent *e, void *)
{
    if (!s_dragging) {
        updateHover(e);
        return EM_FALSE;
    }
    if (std::abs(int(e->clientX) - s_downX) > 6
            || std::abs(int(e->clientY) - s_downY) > 6)
        s_clickOk = false;
    interact();
    int dx = int(e->clientX) - s_lastX;
    int dy = int(e->clientY) - s_lastY;
    s_lastX = int(e->clientX);
    s_lastY = int(e->clientY);
    if (s_panning) {
        const float scale = panScale();
        // Grab-pan: the scene follows the cursor. CamFrame::right is the
        // negation of screen-right (the historical orbit convention), so
        // +dx must increase panX to move the target opposite the drag.
        s_panX += float(dx) * scale;
        s_panY += float(dy) * scale;
    } else {
        s_yaw -= float(dx) * 0.01f;
        s_pitch = bx::clamp(s_pitch + float(dy) * 0.01f,
                            -1.55f, 1.55f);
    }
    return EM_TRUE;
}

static EM_BOOL onWheel(int, const EmscriptenWheelEvent *e, void *)
{
    interact();
    s_dist *= e->deltaY > 0 ? 1.1f : (1.0f / 1.1f);
    s_dist = bx::clamp(s_dist, 0.01f * s_diag, 50.0f * s_diag);
    return EM_TRUE;
}

// 'v' prints (and copies) the current camera as a ?cam= string so an exact
// viewport can be reproduced by reloading with it appended to the URL.
static EM_BOOL onKeyDown(int, const EmscriptenKeyboardEvent *e, void *)
{
    const char k = e->key[0];
    if (k == 'v' || k == 'V') {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f",
                      s_yaw, s_pitch, s_dist, s_center[0], s_center[1],
                      s_center[2], s_panX, s_panY);
        fcviewer_report_cam(buf);
        std::snprintf(s_camMsg, sizeof(s_camMsg), "cam=%s (copied)\n", buf);
        return EM_TRUE;
    }
    if (k == 'd' || k == 'D') {
        s_hudOn = !s_hudOn;
        if (!s_hudOn)
            fcviewer_hud(nullptr);
        return EM_TRUE;
    }
    return EM_FALSE;
}

// Touch: one finger orbits, two fingers pan (centroid) and pinch-zoom
// (distance ratio). State resets whenever the touch count changes.
static int s_numTouch = 0;
static float s_touchX[2], s_touchY[2];
// Tap candidate: a single finger down + up with no meaningful drag, mapped
// to the same pick as a mouse click (NaviCube orient/button, else scene pick).
static bool s_tapOk = false;
static float s_tapX = 0.0f, s_tapY = 0.0f;

static EM_BOOL onTouch(int type, const EmscriptenTouchEvent *e, void *)
{
    // Active touch positions (up to two).
    int n = 0;
    float x[2] = {0.0f, 0.0f}, y[2] = {0.0f, 0.0f};
    for (int i = 0; i < e->numTouches && n < 2; ++i) {
        if (type == EMSCRIPTEN_EVENT_TOUCHEND
                || type == EMSCRIPTEN_EVENT_TOUCHCANCEL) {
            // Lifted fingers are still listed with isChanged set.
            if (e->touches[i].isChanged)
                continue;
        }
        x[n] = float(e->touches[i].clientX);
        y[n] = float(e->touches[i].clientY);
        ++n;
    }

    if (type == EMSCRIPTEN_EVENT_TOUCHSTART) {
        // One finger down opens a tap candidate; a second finger (a gesture)
        // cancels it. The cube hover tint isn't used on touch, but clear any
        // stale one before the geometry can move.
        clearCubeHover();
        if (e->numTouches == 1 && n == 1) {
            s_tapOk = true;
            s_tapX = x[0];
            s_tapY = y[0];
        }
        else {
            s_tapOk = false;
        }
    }
    else if (type == EMSCRIPTEN_EVENT_TOUCHMOVE && n == s_numTouch) {
        interact();
        if (n == 1) {
            // Drag past the slop cancels the tap so orbit doesn't also pick.
            if (std::abs(x[0] - s_tapX) > 8.0f
                    || std::abs(y[0] - s_tapY) > 8.0f)
                s_tapOk = false;
            s_yaw -= (x[0] - s_touchX[0]) * 0.01f;
            s_pitch = bx::clamp(s_pitch + (y[0] - s_touchY[0]) * 0.01f,
                                -1.55f, 1.55f);
        }
        else if (n == 2) {
            const float scale = panScale();
            s_panX -= 0.5f * (x[0] - s_touchX[0] + x[1] - s_touchX[1])
                * scale;
            s_panY += 0.5f * (y[0] - s_touchY[0] + y[1] - s_touchY[1])
                * scale;
            const float oldDist = std::hypot(s_touchX[1] - s_touchX[0],
                                             s_touchY[1] - s_touchY[0]);
            const float newDist = std::hypot(x[1] - x[0], y[1] - y[0]);
            if (oldDist > 1.0f && newDist > 1.0f) {
                s_dist = bx::clamp(s_dist * oldDist / newDist,
                                   0.01f * s_diag, 50.0f * s_diag);
            }
        }
    }
    else if (type == EMSCRIPTEN_EVENT_TOUCHEND
             || type == EMSCRIPTEN_EVENT_TOUCHCANCEL) {
        // A single finger lifted with no drag = tap: run the same
        // NaviCube-first pick as a mouse click, at the touch-down point.
        // (On a clean tap the lone touch is the one just lifted, so
        // e->numTouches == 1.)
        if (type == EMSCRIPTEN_EVENT_TOUCHEND && s_tapOk
                && e->numTouches == 1) {
            float px, py;
            clientToCanvas(s_tapX, s_tapY, px, py);
            doTapPick(px, py, /*ctrl*/ false);
        }
        s_tapOk = false;
    }

    s_numTouch = n;
    for (int i = 0; i < n; ++i) {
        s_touchX[i] = x[i];
        s_touchY[i] = y[i];
    }
    return EM_TRUE;  // preventDefault: no synthesized mouse events
}

static void fitCamera()
{
    float bmin[3], bmax[3];
    if (s_renderer->boundBox(bmin[0], bmin[1], bmin[2],
                             bmax[0], bmax[1], bmax[2])) {
        for (int i = 0; i < 3; ++i)
            s_center[i] = 0.5f * (bmin[i] + bmax[i]);
        float dx = bmax[0] - bmin[0];
        float dy = bmax[1] - bmin[1];
        float dz = bmax[2] - bmin[2];
        s_diag = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (s_diag <= 0.0f)
            s_diag = 10.0f;
        // Tight perspective fit: back off just enough that the projected
        // bounding box fills the frame in the current view. Project the 8
        // corners onto the current orbit's screen axes and depth, so portrait
        // and landscape each frame snugly and aspect is respected. (Replaces a
        // bounding-sphere fit that over-margined non-round models.)
        const float cp = std::cos(s_pitch), sp = std::sin(s_pitch);
        const float cyw = std::cos(s_yaw), syw = std::sin(s_yaw);
        const bx::Vec3 dir(cp * cyw, cp * syw, sp);
        const bx::Vec3 right =
            bx::normalize(bx::cross(dir, bx::Vec3(0.0f, 0.0f, 1.0f)));
        const bx::Vec3 up = bx::normalize(bx::cross(right, dir));
        const bx::Vec3 c(s_center[0], s_center[1], s_center[2]);
        const float aspect = s_height > 0
            ? float(s_width) / float(s_height) : 1.0f;
        const float tanV = std::tan(0.5f * kFovY * bx::kPi / 180.0f);
        const float invH = 1.0f / (tanV * aspect);   // per-lateral-unit distance
        const float invV = 1.0f / tanV;
        // Exact tight fit: each corner must sit inside the frustum at its own
        // depth (dir points toward the eye, so +f is nearer). The distance is
        // the max over corners of what each needs -- not max(lateral)+max(depth),
        // which over-backs-off when the widest corner isn't also the nearest.
        float dNeed = 0.0f;
        for (int ci = 0; ci < 8; ++ci) {
            const bx::Vec3 corner((ci & 1) ? bmax[0] : bmin[0],
                                  (ci & 2) ? bmax[1] : bmin[1],
                                  (ci & 4) ? bmax[2] : bmin[2]);
            const bx::Vec3 d = bx::sub(corner, c);
            const float f = bx::dot(d, dir);
            dNeed = bx::max(dNeed, std::fabs(bx::dot(d, right)) * invH + f);
            dNeed = bx::max(dNeed, std::fabs(bx::dot(d, up)) * invV + f);
        }
        s_dist = bx::max(dNeed * 1.05f, 0.02f * s_diag);   // small margin + floor
        s_panX = s_panY = 0.0f;
    }
}

/// Feed the loaded snapshot to the renderer; a first load also fits
/// the camera (streamed updates keep the user's).
static void applySnapshot(bool fit)
{
    s_renderer->setBackground(s_snap.background);
    s_renderer->setHiddenLineConfig(s_snap.hlconfig);
    s_renderer->setSectionConfig(s_snap.secconf);
    Render::AOConfig ao = s_snap.aoconf;
    if (s_degraded)
        ao.enabled = false;
    s_renderer->setAOConfig(ao);
    s_renderer->setPBRConfig(s_snap.pbrconf);
    s_renderer->setBumpConfig(s_snap.bumpconf);
    s_renderer->setLightConfig(s_snap.lightconf);
    s_renderer->setVolumetricConfig(s_snap.volconf);
    s_renderer->setWaterConfig(s_snap.waterconf);
    s_renderer->setAutoZoomScale(s_snap.autozoomScale);
    if (!s_snap.hatchRGBA.empty())
        s_renderer->setHatchImage(s_snap.hatchRGBA.data(), 4,
                                  s_snap.hatchWidth, s_snap.hatchHeight);
    Render::DrawCallList draws = s_snap.scene;
    s_renderer->setScene(std::move(draws));
    std::set<int> ids;
    for (const auto &sel : s_snap.selections) {
        ids.insert(sel.first);
        Render::DrawCallList sdraws = sel.second;
        s_renderer->addSelection(sel.first, std::move(sdraws));
    }
    for (int id : s_selIds) {
        if (!ids.count(id))
            s_renderer->removeSelection(id);
    }
    s_selIds.swap(ids);
    // Overlay feeds (foreground superimposition, corner axis cross):
    // replayed with their declarative anchors — the local renderer
    // re-derives viewport and camera each frame, so overlays re-anchor
    // on resize and follow the local orbit camera.
    std::set<int> ovIds;
    for (const auto &ov : s_snap.overlays) {
        ovIds.insert(ov.id);
        Render::DrawCallList odraws = ov.draws;
        s_renderer->setOverlay(ov.id, std::move(odraws), ov.anchor);
    }
    for (int id : s_overlayIds) {
        if (!ovIds.count(id))
            s_renderer->removeOverlay(id);
    }
    s_overlayIds.swap(ovIds);
    if (!s_snap.highlight.empty()) {
        Render::DrawCallList hdraws = s_snap.highlight;
        s_renderer->setHighlight(std::move(hdraws),
                                 s_snap.highlightWholeOnTop);
    }
    else {
        s_renderer->clearHighlight();
    }
    // The snapshot's highlight replaced any local hover tint — forget
    // the hover state so the next mouse move re-applies it.
    s_hoverKey = 0;
    s_hoverPart = -2;
    s_haveScene = true;
    if (fit) {
        fitCamera();   // derives scene center / dist / s_diag (near-far)
        if (s_haveCamParam) {
            // Reproduce an exact viewport: keep fitCamera's s_diag but
            // override the orbit/pan the ?cam= parameter carries.
            s_yaw = s_camParam[0];
            s_pitch = s_camParam[1];
            s_dist = s_camParam[2];
            s_center[0] = s_camParam[3];
            s_center[1] = s_camParam[4];
            s_center[2] = s_camParam[5];
            s_panX = s_camParam[6];
            s_panY = s_camParam[7];
            s_haveCamParam = false;   // only the initial view
            s_userCam = true;         // a reproduced view; don't auto-refit
        }
    }
}

//////////////////////////////////////////////////////////////////////
// Scene streaming: WebSocket push, HTTP polling fallback

/// A versioned scene payload arrived (either transport): parse and
/// re-apply; the first scene also fits the camera.
static void applyScenePayload(const char *data, size_t size)
{
    if (size <= 8)
        return;
    uint64_t version = 0;
    std::memcpy(&version, data, sizeof(version));
    Render::SceneSnapshot snap;
    if (Render::loadSceneSnapshot(data + 8, size - 8, snap)) {
        s_sceneVersion = version;
        bool first = !s_haveScene;
        s_snap = std::move(snap);
        applySnapshot(first);
        std::printf("fcviewer: scene update v%llu, %zu draws, %zu overlays\n",
                    (unsigned long long)version, s_snap.scene.size(),
                    s_snap.overlays.size());
    }
    else {
        std::printf("fcviewer: scene update parse FAILED\n");
    }
}

static void schedulePoll();

static void onPollResult(emscripten_fetch_t *fetch)
{
    // onsuccess covers 204 (unchanged, no body) and 200 (new payload).
    if (fetch->status == 200)
        applyScenePayload(fetch->data, size_t(fetch->numBytes));
    emscripten_fetch_close(fetch);
    schedulePoll();
}

static void onPollError(emscripten_fetch_t *fetch)
{
    emscripten_fetch_close(fetch);
    schedulePoll();
}

static void doPoll(void * = nullptr)
{
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onPollResult;
    attr.onerror = onPollError;
    char url[512];
    std::snprintf(url, sizeof(url), "%s/scene?v=%llu", s_sceneUrl.c_str(),
                  (unsigned long long)s_sceneVersion);
    emscripten_fetch(&attr, url);
}

static void schedulePoll()
{
    emscripten_set_timeout([](void *) { doPoll(); }, 500, nullptr);
}

static void startPolling()
{
    if (s_polling)
        return;
    s_polling = true;
    std::printf("fcviewer: falling back to HTTP polling\n");
    doPoll();
}

static EM_BOOL onWsOpen(int, const EmscriptenWebSocketOpenEvent *, void *)
{
    s_wsOpen = true;
    std::printf("fcviewer: websocket connected\n");
    return EM_TRUE;
}

static EM_BOOL onWsMessage(int, const EmscriptenWebSocketMessageEvent *e,
                           void *)
{
    if (!e->isText)
        applyScenePayload(reinterpret_cast<const char *>(e->data),
                          size_t(e->numBytes));
    return EM_TRUE;
}

static EM_BOOL onWsError(int, const EmscriptenWebSocketErrorEvent *, void *)
{
    s_wsOpen = false;
    startPolling();
    return EM_TRUE;
}

static EM_BOOL onWsClose(int, const EmscriptenWebSocketCloseEvent *, void *)
{
    s_wsOpen = false;
    startPolling();
    return EM_TRUE;
}

/// Connect the live scene stream (WebSocket first, polling fallback).
static void startStream()
{
    std::string url = s_sceneUrl;
    if (url.rfind("http", 0) == 0)
        url = "ws" + url.substr(4);   // http(s):// -> ws(s)://
    url += "/scene";
    if (emscripten_websocket_is_supported()) {
        EmscriptenWebSocketCreateAttributes attr = {
            url.c_str(), nullptr, EM_TRUE};
        s_ws = emscripten_websocket_new(&attr);
        if (s_ws > 0) {
            emscripten_websocket_set_onopen_callback(s_ws, nullptr,
                                                     onWsOpen);
            emscripten_websocket_set_onmessage_callback(s_ws, nullptr,
                                                        onWsMessage);
            emscripten_websocket_set_onerror_callback(s_ws, nullptr,
                                                      onWsError);
            emscripten_websocket_set_onclose_callback(s_ws, nullptr,
                                                      onWsClose);
            return;
        }
    }
    startPolling();
}

int main()
{
    emscripten_set_canvas_element_size("#canvas", s_width, s_height);

    Render::RendererFactory::setResourcePath("/assets");
    Render::BGFXRenderer::setWindowHandle(
        const_cast<char *>("#canvas"));
    Render::BGFXRenderer::setWindowSize(s_width, s_height);
    Render::BGFXRenderer::setMSAASamples(4);

    s_renderer = Render::RendererFactory::create("bgfx - OpenGL", nullptr);
    if (!s_renderer) {
        std::printf("fcviewer: renderer creation failed\n");
        return 1;
    }

    // Parse ?cam= before the first snapshot fit so the override is in place
    // when applySnapshot(fit=true) runs (whether from the bundled snapshot
    // below or the first streamed scene).
    if (char *camParam = fcviewer_cam_param()) {
        if (std::sscanf(camParam, "%f,%f,%f,%f,%f,%f,%f,%f",
                        &s_camParam[0], &s_camParam[1], &s_camParam[2],
                        &s_camParam[3], &s_camParam[4], &s_camParam[5],
                        &s_camParam[6], &s_camParam[7]) == 8) {
            s_haveCamParam = true;
            std::printf("fcviewer: reproducing camera %s\n", camParam);
        }
        std::free(camParam);
    }

    if (Render::loadSceneSnapshot("/scene.fcsd", s_snap)) {
        std::printf("fcviewer: snapshot loaded, %zu draws\n",
                    s_snap.scene.size());
        applySnapshot(true);
    } else {
        std::printf("fcviewer: no /scene.fcsd snapshot, empty scene\n");
    }

    if (char *sceneParam = fcviewer_scene_param()) {
        s_sceneUrl = sceneParam;
        std::free(sceneParam);
        std::printf("fcviewer: streaming from %s\n", s_sceneUrl.c_str());
        startStream();
    }

    s_hudOn = EM_ASM_INT({
        var q = new URLSearchParams(window.location.search);
        return (q.has('hud') || q.has('debugpick')) ? 1 : 0;
    }) != 0;

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                    nullptr, EM_TRUE, onKeyDown);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE,
                                      onMouseDown);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                    nullptr, EM_TRUE, onMouseUp);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                      nullptr, EM_TRUE, onMouseMove);
    emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, onWheel);
    emscripten_set_touchstart_callback("#canvas", nullptr, EM_TRUE,
                                       onTouch);
    emscripten_set_touchmove_callback("#canvas", nullptr, EM_TRUE,
                                      onTouch);
    emscripten_set_touchend_callback("#canvas", nullptr, EM_TRUE,
                                     onTouch);
    emscripten_set_touchcancel_callback("#canvas", nullptr, EM_TRUE,
                                        onTouch);

    emscripten_set_main_loop(mainLoop, 0, 0);
    return 0;
}
