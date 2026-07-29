// Browser entry point of the standalone bgfx renderer build.
//
// Initializes the FreeCAD bgfx renderer on the #canvas element (bgfx
// creates the WebGL2 context), loads a scene snapshot captured on the
// desktop (FC_BGFX_DUMP_SCENE -> preloaded at /scene.fcsd) and renders
// it with an orbit camera (drag = orbit, shift/right-drag = pan,
// wheel = zoom).

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#include <emscripten/websocket.h>

#include <GLES3/gl3.h>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "BGFXRenderer.h"
#include "SceneDump.h"
#include "StandalonePlatform.h"

static std::unique_ptr<Render::Renderer> s_renderer;
static Render::SceneSnapshot s_snap;
/// The objects this viewer holds, carried across publishes: a delta
/// names only what changed, and the rest of the scene has to come from
/// somewhere (SceneDump.h). Reset whenever the stream restarts, so a
/// reconnect cannot apply a delta onto a model from a previous run.
static Render::SceneObjectModel s_objects;
static bool s_haveScene = false;
static std::set<int> s_selIds;
static std::set<int> s_overlayIds;

// Live streaming (?scene=<http://host:port> page parameter): connect a
// WebSocket to the desktop scene server (FC_BGFX_SERVE_SCENE) and
// re-apply the feeds on every pushed version; fall back to HTTP polling
// where WebSocket fails.
static std::string s_sceneUrl;
static uint64_t s_sceneVersion = 0;
/// The backend run those versions belong to (SceneDump.h, v35). Told to
/// the server on every request, so that a version this viewer carried
/// across a restart is not taken at face value by the run that did not
/// issue it.
static uint64_t s_sessionId = 0;
static EMSCRIPTEN_WEBSOCKET_T s_ws = 0;
static bool s_wsOpen = false;
static bool s_polling = false;
// Dropped-stream reconnect state: once the WebSocket has been open, a
// close/error schedules reconnect attempts (status text along the top)
// instead of the HTTP-polling fallback reserved for no-WebSocket
// environments. The budget defaults to 10 attempts; the backend can
// raise it (or set -1 = infinite, a debugging aid) with a pushed
// {"cmd":"config","reconnect":N} control message.
long s_reconnectLimit = 10;
static bool s_wsEverOpen = false;
static long s_reconnectAttempts = 0;
static bool s_reconnectPending = false;

// One-shot in-page frame capture (docs/RenderDebug.md §4.4): armed by
// a {cmd:"dumpFrame"} control message on the scene WebSocket. The next
// rendered frame is read back with glReadPixels — this device's real
// GPU, unlike any server-side screenshot — and uploaded as a binary
// 'D' frame tagged with the request id.
struct FrameDumpReq {
    bool armed = false;
    uint32_t id = 0;
    int mode = -1;   ///< RenderDebug view-mode override, -1 = as staged
};
static FrameDumpReq s_dumpReq;

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

// Discreet loading indicator (thin bar + label along the top edge, see
// shell.html). text == null hides it; total > 0 shows byte progress,
// total == 0 the indeterminate slide, total < 0 the label only.
EM_JS(void, fcviewer_status, (const char *text, double loaded, double total), {
    if (window.fcviewerStatus)
        window.fcviewerStatus(text ? UTF8ToString(text) : null, loaded, total);
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

// GL renderer identity for capture metadata: the unmasked WebGL
// renderer string (the device's real GPU) plus the user agent.
EM_JS(char *, fcviewer_gl_info, (), {
    try {
        var canvas = document.getElementById('canvas');
        var gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
        var info = "";
        if (gl) {
            var dbg = gl.getExtension('WEBGL_debug_renderer_info');
            info = dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL)
                       : gl.getParameter(gl.RENDERER);
        }
        var s = info + ' | ' + navigator.userAgent;
        var len = lengthBytesUTF8(s) + 1;
        var buf = _malloc(len);
        stringToUTF8(s, buf, len);
        return buf;
    } catch (e) {
        return 0;
    }
});

// Self-reload with a cache-busting query parameter (a bare reload may
// reuse a stale cached .wasm/.js bundle). Refuses a repeat reload for
// the same bust token, so a bad build cannot cause a reload loop.
EM_JS(void, fcviewer_reload, (const char *bust), {
    var b = UTF8ToString(bust);
    try {
        var u = new URL(window.location.href);
        if (u.searchParams.get('bust') === b) {
            console.warn('fcviewer: reload for ' + b
                         + ' already applied, refusing a loop');
            return;
        }
        u.searchParams.set('bust', b);
        window.location.replace(u.toString());
    } catch (e) {
        window.location.reload();
    }
});

static int s_width = 1024;    // drawing-buffer size, device px (css * dpr)
static int s_height = 768;
static float s_dpr = 1.0f;    // devicePixelRatio applied to the buffer

// Orbit camera state around the scene bounds.
static float s_center[3] = {0.0f, 0.0f, 0.0f};
static float s_panX = 0.0f, s_panY = 0.0f;  // pan in camera plane
static float s_yaw = 0.785f;
static float s_pitch = 0.5f;
static float s_roll = 0.0f;   // twist about the view direction (roll arrows)
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

// Progressive refinement (the Fusion 360 pattern): camera interaction drops
// the SSAO pass for cheap frames, idle restores it. MSAA is intentionally NOT
// toggled: changing the sample count re-creates the render target on the next
// render (BGFXRenderer view->init), and reallocating a multisampled target --
// especially the hi-DPI-sized one -- stalls the first drag frame in WebGL,
// which reads as a delay before pan/zoom starts. AO is a cheap flag, so only
// that flexes.
static const double kRefineDelayMs = 300.0;
static double s_lastInteract = -1e9;
static bool s_degraded = false;
// Set once the user drives the camera (orbit/pan/zoom/NaviCube/?cam). Until
// then the camera is the auto fit, so a viewport/orientation change re-fits.
static bool s_userCam = false;

// Idle frame skip: frames still owed after the last change. Camera
// input, resize, snapshot/config application and HUD toggles all mark
// frames dirty; a small run (not 1) lets updateQuality restore full AO
// once the interaction settles and the smoothed HUD numbers update.
// Renderer-side changes (hover/selection highlights, config sets) are
// caught via Renderer::isSceneDirty() in mainLoop instead.
static int s_dirtyFrames = 8;

static void markDirty()
{
    s_dirtyFrames = 8;
}

static void interact()
{
    s_lastInteract = emscripten_get_now();
    s_userCam = true;
    markDirty();
}

static void updateQuality()
{
    const bool moving =
        emscripten_get_now() - s_lastInteract < kRefineDelayMs;
    if (moving == s_degraded)
        return;
    s_degraded = moving;
    Render::AOConfig ao = s_snap.aoconf;
    if (moving) {
        // GTAO has an in-shader fast path (fewer slices/steps via a
        // uniform — no target re-init): AO stays visible while the
        // camera moves and refines once idle. The classic SSAO has no
        // such path, so it keeps the old drop-AO degradation.
        if (ao.method == 1)
            ao.fast = true;
        else
            ao.enabled = false;
    }
    s_renderer->setAOConfig(ao);
}

static double s_dbgMs = 0.0;
static int s_dbgN = 0;
static double s_dbgApplyMs = 0.0;

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
    // Roll twists the right/up frame about the view direction (the curved
    // NaviCube arrows). At s_roll == 0 the frame is unchanged.
    if (s_roll != 0.0f) {
        const float cr = std::cos(s_roll), sr = std::sin(s_roll);
        const bx::Vec3 r = bx::add(bx::mul(f.right, cr), bx::mul(f.up, sr));
        const bx::Vec3 u = bx::sub(bx::mul(f.up, cr), bx::mul(f.right, sr));
        f.right = r;
        f.up = u;
    }
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
    // convention), so the horizontal ray term flips sign. Derived from the
    // frame (not world-up) so it stays consistent under camera roll.
    bx::Vec3 rightCam = bx::neg(f.right);
    orig = f.eye;
    rdir = bx::normalize(bx::add(fwd,
        bx::add(bx::mul(rightCam, nx * th * aspect),
                bx::mul(f.up, ny * th))));
}

static void buildCamera(float *viewMtx, float *projMtx)
{
    CamFrame f = camFrame();
    // Right-handed like the Coin camera the renderer's shading assumes
    // (camera forward = -z in view space; bx defaults to left-handed). Use the
    // frame's own up so camera roll is reflected in the view matrix.
    bx::mtxLookAt(viewMtx, f.eye, f.at, f.up, bx::Handedness::Right);

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

/// Which kind of sub-element a pick resolved to. Mirrors the desktop's
/// primitive priority (SoFCUnifiedSelection::getPriority): a vertex beats an
/// edge beats a face when their hit points are essentially coincident.
enum PickKind { PickNone = 0, PickFace = 1, PickEdge = 2, PickVertex = 3 };

struct PickHit {
    int draw = -1;          ///< index into s_snap.scene
    PickKind kind = PickNone;
    int offset = -1;        ///< first index of the hit element in the mesh
                            ///< index buffer for `kind`: triangle (stride 3),
                            ///< line segment (stride 2) or point (stride 1)
    float t = 1e30f;        ///< forward depth of the hit (world units from eye)
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

/// Project a world point to canvas pixels with the current camera, matching
/// screenRay's frame. Returns false (and leaves outputs unset) when the point
/// is behind the camera. `depth` is the forward distance from the eye.
static bool projectToScreen(const bx::Vec3 &p, const CamFrame &f,
                            const bx::Vec3 &fwd, float th, float aspect,
                            float &sx, float &sy, float &depth)
{
    bx::Vec3 w = bx::sub(p, f.eye);
    depth = bx::dot(w, fwd);
    if (depth <= 1e-6f)
        return false;
    // screenRay builds the ray from -f.right (rightCam), f.up and fwd, so the
    // inverse projection uses the same orthonormal basis.
    const bx::Vec3 rightCam = bx::neg(f.right);
    const float nx = bx::dot(w, rightCam) / (depth * th * aspect);
    const float ny = bx::dot(w, f.up) / (depth * th);
    sx = (nx + 1.0f) * 0.5f * float(s_width);
    sy = (1.0f - ny) * 0.5f * float(s_height);
    return true;
}

/// Squared distance from point (px,py) to the segment (ax,ay)-(bx,by); `u`
/// gets the closest-point parameter along the segment (clamped to [0,1]).
static float distToSegment2(float px, float py, float ax, float ay,
                            float bx_, float by, float &u)
{
    const float dx = bx_ - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    u = len2 > 1e-12f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    u = std::max(0.0f, std::min(1.0f, u));
    const float cx = ax + u * dx, cy = ay + u * dy;
    const float ex = px - cx, ey = py - cy;
    return ex * ex + ey * ey;
}

/// Transform a model-space position by dc's model matrix (identity fast path).
static bx::Vec3 toWorld(const Render::DrawCall &dc, const float *pos)
{
    bx::Vec3 p(pos[0], pos[1], pos[2]);
    return dc.identity ? p : bx::mul(p, dc.model);
}

/// Nearest face (exact ray/triangle), edge and vertex (screen-space proximity
/// within the pick radius) of the draw scene at canvas pixel (px, py), then
/// resolve by the desktop's vertex > edge > face priority — a higher-priority
/// element wins only when its hit is essentially as near the eye as the
/// frontmost (SoFCUnifiedSelection::postProcessPickedList).
static PickHit pickScene(float px, float py)
{
    bx::Vec3 orig(bx::InitZero), rdir(bx::InitZero);
    screenRay(px, py, orig, rdir);
    const CamFrame f = camFrame();
    const bx::Vec3 fwd = bx::normalize(bx::sub(f.at, f.eye));
    const float aspect = s_height > 0
        ? float(s_width) / float(s_height) : 1.0f;
    const float th = std::tan(0.5f * kFovY * bx::kPi / 180.0f);
    const float radius = s_snap.preselconf.pickRadius > 0.5f
        ? s_snap.preselconf.pickRadius : 5.0f;
    const float rdotf = bx::dot(rdir, fwd);  // ray-param -> forward-depth

    PickHit face, edge, vert;
    float faceRayT = 1e30f;                  // face nearest, in ray units
    float edgeDist2 = radius * radius;       // edge/vertex nearest, in px^2
    float vertDist2 = radius * radius;

    for (size_t di = 0; di < s_snap.scene.size(); ++di) {
        const auto &dc = s_snap.scene[di];
        if (!dc.mesh || !dc.mesh->positions)
            continue;
        const float *pos = dc.mesh->positions;

        if (dc.material.type == Render::Material::Triangle
                && dc.mesh->triangleIndices) {
            if (dc.bboxMin[0] <= dc.bboxMax[0]
                    && !rayHitsBBox(dc.bboxMin, dc.bboxMax, orig, rdir, faceRayT))
                continue;
            // Cast in model space (transform the ray by the inverse model
            // matrix); the hit converts back to a world-space ray t so draws
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
                if (tw > 0.0f && tw < faceRayT) {
                    faceRayT = tw;
                    face.draw = int(di);
                    face.kind = PickFace;
                    face.offset = i;
                    face.t = tw * rdotf;
                }
            }
        }
        else if (dc.material.type == Render::Material::Line
                && dc.mesh->lineIndices) {
            const int total = dc.mesh->numLineIndices;
            int start = dc.indexStart;
            int count = dc.indexCount ? dc.indexCount : total - start;
            if (start < 0 || start + count > total)
                continue;
            const int32_t *idx = dc.mesh->lineIndices;
            for (int i = start; i + 1 < start + count; i += 2) {
                float ax, ay, ad, bx2, by, bd;
                if (!projectToScreen(toWorld(dc, pos + 3 * idx[i]),
                                     f, fwd, th, aspect, ax, ay, ad))
                    continue;
                if (!projectToScreen(toWorld(dc, pos + 3 * idx[i + 1]),
                                     f, fwd, th, aspect, bx2, by, bd))
                    continue;
                float u;
                float d2 = distToSegment2(px, py, ax, ay, bx2, by, u);
                if (d2 < edgeDist2) {
                    edgeDist2 = d2;
                    edge.draw = int(di);
                    edge.kind = PickEdge;
                    edge.offset = i;
                    // Depth at the screen-space foot u must be interpolated
                    // perspective-correctly: 1/depth is what varies linearly
                    // along the projected segment. A plain linear blend of ad,
                    // bd badly overestimates depth on a foreshortened edge (a
                    // near-view-aligned ridge), which then reads as "behind"
                    // the adjacent face and loses the priority test — the edge
                    // becomes pickable only from outside its faces.
                    const float invd = (1.0f - u) / ad + u / bd;
                    edge.t = invd > 1e-9f ? 1.0f / invd : ad;
                }
            }
        }
        else if (dc.material.type == Render::Material::Point
                && dc.mesh->pointIndices) {
            const int total = dc.mesh->numPointIndices;
            int start = dc.indexStart;
            int count = dc.indexCount ? dc.indexCount : total - start;
            if (start < 0 || start + count > total)
                continue;
            const int32_t *idx = dc.mesh->pointIndices;
            for (int i = start; i < start + count; ++i) {
                float sx, sy, sd;
                if (!projectToScreen(toWorld(dc, pos + 3 * idx[i]),
                                     f, fwd, th, aspect, sx, sy, sd))
                    continue;
                const float ex = px - sx, ey = py - sy;
                const float d2 = ex * ex + ey * ey;
                if (d2 < vertDist2) {
                    vertDist2 = d2;
                    vert.draw = int(di);
                    vert.kind = PickVertex;
                    vert.offset = i;
                    vert.t = sd;
                }
            }
        }
    }

    // Priority resolution: the frontmost element sets the reference depth; a
    // vertex, then an edge, is promoted over it when within a small depth
    // tolerance (coincident), matching the desktop's 0.2-world-unit test
    // scaled to the scene.
    float depthMin = 1e30f;
    if (face.kind) depthMin = std::min(depthMin, face.t);
    if (edge.kind) depthMin = std::min(depthMin, edge.t);
    if (vert.kind) depthMin = std::min(depthMin, vert.t);
    const float tol = std::max(0.2f, 0.01f * s_diag);
    if (vert.kind && vert.t <= depthMin + tol)
        return vert;
    if (edge.kind && edge.t <= depthMin + tol)
        return edge;
    return face;
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
// Same idea for the rotate arrows (button overlay): a tinted copy of the
// hovered arrow's draw, one id higher so it draws after both the cube and
// the button overlay.
static const int kNaviButtonHiliteOverlayId = 21;
static int s_btnHiliteDraw = -2;          // button draw index currently tinted

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
    s_roll = 0.0f;   // a face/edge/corner click gives a level, untwisted view
    s_panX = s_panY = 0.0f;
}

static const int kNaviButtonsOverlayId = 6;  // OverlayNaviButtons

enum NaviButtonAction {
    NaviBtnNone, NaviBtnTiltUp, NaviBtnTiltDown,
    NaviBtnOrbitLeft, NaviBtnOrbitRight,
    NaviBtnRollLeft, NaviBtnRollRight,
    NaviBtnBackside,  // the corner dot: flip 180° to the opposite side
    NaviBtnMenu   // the view-menu icon (hover highlight only for now)
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
    // The two curved roll arrows sit in the upper-left / upper-right, as an
    // annular sector between the cube body and the rim (see
    // NaviCube.cpp createButtonTex TEX_ARROW_LEFT/RIGHT: radius ~1, ~32-72°
    // and its mirror). Angularly disjoint from N/E/W above.
    // Flip-side dot: the top-right corner (NaviCube.cpp createButtonTex
    // TEX_DOT_BACKSIDE at design ~(0.9,0.9) -> NDC top-right corner, past the
    // curved-arrow rim).
    if (nx > 0.72f && ny > 0.72f)
        return NaviBtnBackside;
    const float rad = std::sqrt(nx * nx + ny * ny);
    if (rad > 0.72f && rad < 1.10f) {
        const float ang = std::atan2(ny, nx) * 180.0f / bx::kPi;
        if (ang > 26.0f && ang < 78.0f)
            return NaviBtnRollRight;    // upper-right curved arrow
        if (ang > 102.0f && ang < 154.0f)
            return NaviBtnRollLeft;     // upper-left curved arrow
    }
    // View-menu icon: lower-right of the button rect (NaviCube.cpp
    // createMenuTex translate 12/16,13/16 -> NDC ~(0.5,-0.6)). Approximate
    // box; disjoint from the |nx|<0.28 south arrow.
    if (nx > 0.3f && ny < -0.5f)
        return NaviBtnMenu;
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
    case NaviBtnRollLeft:
        s_roll -= step;
        break;
    case NaviBtnRollRight:
        s_roll += step;
        break;
    case NaviBtnBackside:
        s_yaw += bx::kPi;   // flip 180° to view the opposite side
        break;
    default:
        break;
    }
}

/// Drop any active arrow hover tint.
static void clearButtonHover()
{
    if (s_btnHiliteDraw != -2) {
        s_renderer->removeOverlay(kNaviButtonHiliteOverlayId);
        s_btnHiliteDraw = -2;
    }
}

/// Draw index of the arrow quad for a rotate-button action, matching the
/// order the button overlay is built in (NaviCube.cpp buildCoinButtons over
/// m_Buttons: NORTH, SOUTH, EAST, WEST, roll-left, roll-right, dot, menu).
/// The four handled arrows are the first four draws.
static int naviButtonDrawIndex(NaviButtonAction a)
{
    switch (a) {
    case NaviBtnTiltUp:     return 0;   // TEX_ARROW_NORTH
    case NaviBtnTiltDown:   return 1;   // TEX_ARROW_SOUTH
    case NaviBtnOrbitRight: return 2;   // TEX_ARROW_EAST
    case NaviBtnOrbitLeft:  return 3;   // TEX_ARROW_WEST
    case NaviBtnRollLeft:   return 4;   // TEX_ARROW_LEFT  (curved)
    case NaviBtnRollRight:  return 5;   // TEX_ARROW_RIGHT (curved)
    case NaviBtnBackside:   return 6;   // TEX_DOT_BACKSIDE (corner dot)
    default:                return -1;
    }
}

/// Hover highlight for the rotate arrows: when the cursor is over a handled
/// arrow hot-zone, tint that arrow's quad the NaviCube HiliteColor and feed
/// it as its own overlay. Like updateCubeHover, the arrow texture is kept, so
/// the tint is clipped to the arrow shape (the shader discards near-zero
/// alpha). Returns true while the cursor is over a handled arrow so scene
/// preselection yields to it.
static bool updateButtonHover(float px, float py)
{
    const Render::SceneSnapshot::Overlay *btn = nullptr;
    for (const auto &ov : s_snap.overlays) {
        if (ov.id == kNaviButtonsOverlayId) {
            btn = &ov;
            break;
        }
    }
    int di = -1;
    if (btn && !btn->draws.empty()) {
        const NaviButtonAction act = pickNaviButton(px, py);
        // The view-menu icon is the last button draw (buildCoinButtons adds it
        // after the arrows and the — here inactive — hilite switch).
        di = act == NaviBtnMenu ? int(btn->draws.size()) - 1
                                : naviButtonDrawIndex(act);
    }
    if (di < 0 || size_t(di) >= btn->draws.size()) {
        clearButtonHover();
        return false;
    }
    if (di != s_btnHiliteDraw) {
        s_btnHiliteDraw = di;
        Render::DrawCall hl = btn->draws[size_t(di)];
        // NaviCube HiliteColor (170,226,255), opaque so the arrow pops over
        // its normal translucent ButtonColor.
        hl.material.diffuse = 0xAAE2FFFF;
        hl.material.pervertexcolor = false;
        hl.material.transparent = true;
        Render::DrawCallList draws;
        draws.push_back(std::move(hl));
        s_renderer->setOverlay(kNaviButtonHiliteOverlayId, std::move(draws),
                               btn->anchor);
    }
    return true;
}

// Hover highlight state: a local setHighlight() built from the hit
// draw (no server round trip). The next streamed snapshot replaces it
// with the desktop's highlight feed until the mouse moves again.
static uint64_t s_hoverKey = 0;
static int s_hoverPart = -2;
static PickKind s_hoverKind = PickNone;

/// The sub-element kind a scene draw carries, from its material type.
static PickKind kindForDraw(const Render::DrawCall &dc)
{
    switch (dc.material.type) {
    case Render::Material::Line:  return PickEdge;
    case Render::Material::Point: return PickVertex;
    case Render::Material::Triangle: return PickFace;
    default: return PickNone;
    }
}

/// The element-part table in dc's mesh for the given pick kind.
static const std::vector<std::pair<int, int>> *
partsForKind(const Render::DrawCall &dc, PickKind kind)
{
    if (!dc.mesh)
        return nullptr;
    switch (kind) {
    case PickEdge:   return &dc.mesh->lineParts;
    case PickVertex: return &dc.mesh->pointParts;
    default:         return &dc.mesh->triangleParts;
    }
}

/// The part covering index offset `offset` in `parts`, or -1 (whole object)
/// when the table is empty or nothing matches.
static int partForOffset(const std::vector<std::pair<int, int>> &parts,
                         int offset, int &partStart, int &partCount)
{
    partStart = 0;
    partCount = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (offset >= parts[i].first
                && offset < parts[i].first + parts[i].second) {
            partStart = parts[i].first;
            partCount = parts[i].second;
            return int(i);
        }
    }
    return -1;
}

/// The element part covering a pick hit in dc's mesh, or -1 (whole object).
static int partForHit(const Render::DrawCall &dc, const PickHit &hit,
                      int &partStart, int &partCount)
{
    partStart = 0;
    partCount = 0;
    const auto *parts = partsForKind(dc, hit.kind);
    return parts ? partForOffset(*parts, hit.offset, partStart, partCount) : -1;
}

/// Build a highlight draw for face `part` of `dc` (whole object if part<0),
/// styled by cfg (color + outline like the desktop backend). Shared by the
/// hover (preselect) and the client-side selection. cfg is streamed from the
/// backend (preselconf / selconf), so styling tracks the ViewParams instead
/// of being hardcoded.
static Render::DrawCall buildHiliteDraw(
        const Render::DrawCall &dc, int part, int partStart, int partCount,
        const Render::PreselHighlightConfig &cfg)
{
    Render::DrawCall hl = dc;
    hl.material.diffuse = cfg.color;
    hl.material.pervertexcolor = false;
    hl.material.transparent = false;
    hl.material.texture.reset();

    // Edge / vertex highlight: thicken the hit line segment / point and tint
    // it, restricted to the element's index range. Pushed toward the viewer so
    // it wins the depth test against the geometry it sits on.
    if (dc.material.type == Render::Material::Line
            || dc.material.type == Render::Material::Point) {
        hl.material.linecolor = cfg.color;
        hl.material.emissive = cfg.color;
        if (dc.material.type == Render::Material::Line)
            hl.material.linewidth = std::max(dc.material.linewidth, 1.0f) + 2.0f;
        else
            hl.material.pointsize = std::max(dc.material.pointsize, 1.0f) + 4.0f;
        if (part >= 0) {
            hl.partIndex = part;
            hl.wholeObject = false;
            hl.indexStart = partStart;
            hl.indexCount = partCount;
        }
        hl.material.polygonoffset = true;
        hl.material.polygonoffsetfactor = -2.0f;
        hl.material.polygonoffsetunits = -2.0f;
        return hl;
    }

    // Outline-only needs a resolved face (partIndex >= 0): the renderer draws
    // the face outline for a draw with faceoutline && partIndex>=0 and skips
    // the fill when outlineonly is set.
    const bool outline = part >= 0 && cfg.faceOutline;
    if (part >= 0) {
        hl.partIndex = part;
        hl.wholeObject = false;
        hl.indexStart = partStart;
        hl.indexCount = partCount;
    }
    if (outline) {
        hl.material.faceoutline = true;
        hl.material.outlineonly = cfg.outlineOnly;
        hl.material.emissive = cfg.color;         // outline colour (spec.color)
        hl.material.outlinewidth = cfg.outlineWidth;
    }
    if (!outline || !cfg.outlineOnly) {
        // A filled tint is drawn (whole-object, no outline, or outline-with-
        // fill): push it toward the viewer so it wins the depth test.
        hl.material.polygonoffset = true;
        hl.material.polygonoffsetfactor = -2.0f;
        hl.material.polygonoffsetunits = -2.0f;
    }
    return hl;
}

static void applyHover(const PickHit &hit)
{
    if (hit.draw < 0) {
        if (s_hoverKey || s_hoverPart != -2 || s_hoverKind != PickNone) {
            s_hoverKey = 0;
            s_hoverPart = -2;
            s_hoverKind = PickNone;
            s_renderer->clearHighlight();
        }
        return;
    }
    const auto &dc = s_snap.scene[size_t(hit.draw)];
    int partStart, partCount;
    int part = partForHit(dc, hit, partStart, partCount);
    if (dc.objectKey == s_hoverKey && part == s_hoverPart
            && hit.kind == s_hoverKind)
        return;
    s_hoverKey = dc.objectKey;
    s_hoverPart = part;
    s_hoverKind = hit.kind;
    Render::DrawCallList draws;
    draws.push_back(buildHiliteDraw(dc, part, partStart, partCount,
                                    s_snap.preselconf));
    s_renderer->setHighlight(std::move(draws), false);
}

// ---- Client-side selection (instant; no round trip) --------------------
// A tap selects locally and shows the selection highlight immediately, then
// syncs the pick to the backend in the background (batched) only to keep its
// Gui::Selection in step. The backend's own streamed selection feed is
// ignored while the client owns selection (see the snapshot replay).
static const int kClientSelId = Render::SelIdSelected | 0x1;

struct SelItem { uint64_t key; PickKind kind; int part; };
static std::vector<SelItem> s_sel;

/// Rebuild the local selection highlight from s_sel against the CURRENT scene
/// and feed it as one selection (kClientSelId). Called on select and after
/// each snapshot (the scene draws may have been replaced). Each item is drawn
/// against the scene draw matching both its object and its element kind (the
/// object's face / edge / vertex draw).
static void rebuildSelection()
{
    Render::DrawCallList draws;
    for (const auto &dc : s_snap.scene) {
        if (!dc.mesh)
            continue;
        const PickKind dcKind = kindForDraw(dc);
        if (dcKind == PickNone)
            continue;
        for (const auto &it : s_sel) {
            if (it.key != dc.objectKey || it.kind != dcKind)
                continue;
            int partStart = 0, partCount = 0, part = it.part;
            const auto *parts = partsForKind(dc, dcKind);
            if (part >= 0 && parts && size_t(part) < parts->size()) {
                partStart = (*parts)[size_t(part)].first;
                partCount = (*parts)[size_t(part)].second;
            }
            else {
                part = -1;
            }
            draws.push_back(buildHiliteDraw(dc, part, partStart, partCount,
                                            s_snap.selconf));
        }
    }
    if (draws.empty())
        s_renderer->removeSelection(kClientSelId);
    else
        s_renderer->addSelection(kClientSelId, std::move(draws));
}

/// Client-side select at canvas pixel: pick locally, update s_sel (Ctrl =
/// toggle/extend, plain = replace), and show the highlight immediately.
///
/// The selection is NOT synced to the backend here: an eager sync makes the
/// backend re-pick and republish the whole scene, whose echo stalls right
/// after the (instant) local highlight. Selection stays entirely client-side;
/// when a modeling operation is added it will submit the accumulated selection
/// batched together with the operation, so the backend only re-picks once, per
/// client, at commit time (no per-tap sync delay, no cross-client interference).
static void selectAt(float px, float py, bool ctrl)
{
    PickHit hit = pickScene(px, py);
    if (hit.draw < 0) {
        if (!ctrl && !s_sel.empty()) {
            s_sel.clear();
            rebuildSelection();
        }
        return;
    }
    const auto &dc = s_snap.scene[size_t(hit.draw)];
    int partStart, partCount;
    int part = partForHit(dc, hit, partStart, partCount);
    SelItem item{dc.objectKey, hit.kind, part};
    if (ctrl) {
        auto same = [&](const SelItem &s) {
            return s.key == item.key && s.kind == item.kind
                && s.part == item.part;
        };
        auto it = std::find_if(s_sel.begin(), s_sel.end(), same);
        if (it != s_sel.end())
            s_sel.erase(it);
        else
            s_sel.push_back(item);
    }
    else {
        s_sel.clear();
        s_sel.push_back(item);
    }
    rebuildSelection();
}

static bool fitCamera();

// Browser-measured frame timing for the HUD: the wall-clock period between
// mainLoop calls (the real displayed frame rate, unlike the backend's own
// render time) and the CPU time spent inside the render call, both smoothed
// with an exponential moving average.
static double s_lastFrameNow = 0.0;
static double s_frameMs = 0.0;   // smoothed frame period
static double s_renderMs = 0.0;  // smoothed render()-call CPU time

// Fixed-width fields + fixed line count so the HUD box never resizes
// as the numbers change: the widest line (fps) has constant width, the
// variable strings (hover / cam) are truncated, and the cam line is
// always present (blank until [v]). The bld: line = build stamp (mobile
// caches serve stale builds; verify at a glance) + whether the AO depth
// prepass / pyramid run fp32 on this GPU or fell back to fp16 (fp16
// quantization bands the AO). `idle` replaces the fps value while the
// idle frame skip holds the last presented frame.
static void updateHud(bool idle)
{
    if (!s_hudOn)
        return;
    char fpsv[16];
    if (idle)
        std::snprintf(fpsv, sizeof(fpsv), "  idle");
    else
        std::snprintf(fpsv, sizeof(fpsv), "%6.1f",
                      s_frameMs > 0.0 ? 1000.0 / s_frameMs : 0.0);
    const bgfx::Caps *caps = bgfx::getCaps();
    const bool rgba32f = 0 != (caps->formats[bgfx::TextureFormat::RGBA32F]
                               & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
    const bool r32f = 0 != (caps->formats[bgfx::TextureFormat::R32F]
                            & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
    char hud[640];
    std::snprintf(hud, sizeof(hud),
        "fps:  %s  (frame %6.1f ms  render %6.1f ms)\n"
        "res:  %5d x%5d   dpr %4.2f   effRes %4.2f\n"
        "bld:  %.11s %.8s   prepass %s  aomip %s\n"
        "cam:  yaw %8.2f  pitch %7.2f  dist %9.2f\n"
        "pan:  %8.2f,%8.2f  ctr %7.1f,%7.1f,%7.1f\n"
        "hover: %-30.30s\n"
        "%-46.46s\n"
        "[d] toggle HUD   [v] copy cam",
        fpsv, s_frameMs, s_renderMs,
        s_width, s_height, double(s_dpr), double(s_snap.effectResolution),
        __DATE__, __TIME__,
        rgba32f ? "fp32" : "fp16", r32f ? "fp32" : "fp16",
        s_yaw, s_pitch, s_dist, s_panX, s_panY,
        s_center[0], s_center[1], s_center[2], s_hoverDesc,
        s_camMsg);
    fcviewer_hud(hud);
}

static std::string jsonEscape(const char *s)
{
    std::string out;
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        }
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else {
            out += char(c);
        }
    }
    return out;
}

/// Read back the presented frame (called right after render(), same
/// RAF tick — the drawing buffer is not preserved across ticks) and
/// upload it: 'D', u32 request id, u32 metadata length, metadata
/// JSON, u32 width, u32 height, RGBA8 pixels bottom-up (little-endian
/// throughout, matching the scene server's parser).
static void captureAndSendFrame(uint32_t id, int mode)
{
    if (!s_wsOpen || s_width <= 0 || s_height <= 0)
        return;
    char *info = fcviewer_gl_info();
    std::string meta = "{\"canvasSize\":[" + std::to_string(s_width) + ","
        + std::to_string(s_height) + "]"
        + ",\"devicePixelRatio\":" + std::to_string(s_dpr)
        + ",\"snapshotFormat\":"
        + std::to_string(Render::sceneDumpVersion())
        + ",\"sceneVersion\":" + std::to_string(s_sceneVersion)
        + ",\"viewMode\":"
        + std::to_string(mode >= 0 ? mode : s_snap.debugconf.viewMode)
        + ",\"freezeFrame\":"
        + (s_snap.debugconf.freezeFrame ? "true" : "false")
        + ",\"renderer\":\"" + (info ? jsonEscape(info) : std::string())
        + "\"}";
    std::free(info);

    const size_t npix = size_t(s_width) * size_t(s_height) * 4;
    std::vector<uint8_t> buf(1 + 4 + 4 + meta.size() + 8 + npix);
    uint8_t *p = buf.data();
    p[0] = 'D';
    uint32_t v = id;
    std::memcpy(p + 1, &v, 4);
    v = uint32_t(meta.size());
    std::memcpy(p + 5, &v, 4);
    std::memcpy(p + 9, meta.data(), meta.size());
    size_t off = 9 + meta.size();
    v = uint32_t(s_width);
    std::memcpy(p + off, &v, 4);
    v = uint32_t(s_height);
    std::memcpy(p + off + 4, &v, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, s_width, s_height, GL_RGBA, GL_UNSIGNED_BYTE,
                 p + off + 8);
    emscripten_websocket_send_binary(s_ws, buf.data(), buf.size());
    std::printf("fcviewer: dumpFrame %u uploaded %dx%d (mode %d)\n",
                id, s_width, s_height, mode);
}

static void mainLoop()
{
    const double frameNow = emscripten_get_now();
    if (s_lastFrameNow > 0.0) {
        const double dt = frameNow - s_lastFrameNow;
        s_frameMs = s_frameMs > 0.0 ? s_frameMs * 0.9 + dt * 0.1 : dt;
    }
    s_lastFrameNow = frameNow;

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
        markDirty();
    }

    updateQuality();

    // One-shot dumpFrame capture: the mode override applies to just
    // this frame's debug pass, then the staged config is restored
    // after the readback below.
    const bool dumping = s_dumpReq.armed;
    if (dumping && s_dumpReq.mode >= 0) {
        Render::RenderDebugConfig conf = s_snap.debugconf;
        conf.viewMode = s_dumpReq.mode;
        s_renderer->setRenderDebugConfig(conf);
    }

    // Idle frame skip: static camera, no pending renderer change, no
    // time-animated effects — a new frame would be pixel-identical to
    // the presented one, so skip the render (mobile battery/thermal;
    // the canvas keeps showing the last frame). Interaction restores
    // rendering instantly via markDirty; renderer-side changes (hover
    // and selection highlights, configs, a fresh scene feed, the idle
    // AO refine from updateQuality) surface through isSceneDirty; water
    // and fire keep rendering through isSceneAnimated. The HUD shows
    // `idle` in the fps field while frames are held.
    if (s_dirtyFrames > 0) {
        --s_dirtyFrames;
    } else if (s_renderer && !s_renderer->isSceneDirty()
               && !s_renderer->isSceneAnimated()) {
        updateHud(true);
        s_lastFrameNow = 0.0;   // keep the idle gap out of the fps EMA
        return;
    }

    float viewMtx[16], projMtx[16];
    buildCamera(viewMtx, projMtx);
    // Recompute the autozoom (screen-constant) scale from THIS viewer's camera
    // each frame, replacing the value baked into the snapshot from the desktop
    // camera; otherwise screen-constant content (datum labels) keeps the desktop
    // size and grows/shrinks as the browser user zooms. Mirrors Coin's
    // translateAutoZoomScale (worldToScreenScale/(5*aspect)), which for this
    // perspective camera is proportional to the focal distance; the constant is
    // calibrated so the glyph keeps the size the captured desktop scale gave.
    s_renderer->setAutoZoomScale(
        s_dist * std::tan(0.5f * kFovY * bx::kPi / 180.0f) * 0.0857f);
    QColor bg((s_snap.clearColor >> 24) & 0xff,
              (s_snap.clearColor >> 16) & 0xff,
              (s_snap.clearColor >> 8) & 0xff);
    const double renderT0 = emscripten_get_now();
    s_renderer->render(bg, viewMtx, projMtx);
    const double rdt = emscripten_get_now() - renderT0;
    s_renderMs = s_renderMs > 0.0 ? s_renderMs * 0.9 + rdt * 0.1 : rdt;

    if (dumping) {
        s_dumpReq.armed = false;
        captureAndSendFrame(s_dumpReq.id, s_dumpReq.mode);
        if (s_dumpReq.mode >= 0)
            s_renderer->setRenderDebugConfig(s_snap.debugconf);
    }

    updateHud(false);
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
        // Client-side select (instant); the backend is synced in the
        // background from the queued pick.
        selectAt(px, py, ctrl);
    }
}

// Last committed tap/click (CSS px + time) for double-tap/double-click
// detection, shared by the mouse and touch paths.
static double s_lastTapMs = -1e9;
static float s_lastTapX = 0.0f, s_lastTapY = 0.0f;

/// A committed tap/click at CSS-pixel (clientX,clientY): a second one soon
/// after and near the first is a double = zoom-to-fit; otherwise a normal
/// pick. Detected manually (not the dblclick event) so mouse and touch behave
/// identically and the window is tunable.
static void tapOrDouble(float clientX, float clientY, bool ctrl)
{
    const double now = emscripten_get_now();
    if (now - s_lastTapMs < 450.0
            && std::fabs(clientX - s_lastTapX) < 30.0f
            && std::fabs(clientY - s_lastTapY) < 30.0f) {
        fitCamera();
        interact();
        s_lastTapMs = -1e9;   // consume so a 3rd tap starts fresh
    }
    else {
        float px, py;
        clientToCanvas(clientX, clientY, px, py);
        doTapPick(px, py, ctrl);
        s_lastTapMs = now;
        s_lastTapX = clientX;
        s_lastTapY = clientY;
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
        clearButtonHover();
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc),
                      "NaviCube draw %d", s_cubeHiliteDraw);
        applyHover(PickHit{});
        return;
    }
    // Rotate arrows ringing the cube: tint the hovered arrow.
    if (updateButtonHover(px, py)) {
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc),
                      "NaviCube arrow %d", s_btnHiliteDraw);
        applyHover(PickHit{});
        return;
    }
    PickHit hit = pickScene(px, py);
    static const bool debugPick = EM_ASM_INT({
        return new URLSearchParams(window.location.search).has('debugpick')
            ? 1 : 0;
    }) != 0;
    static const char *kKindName[] = {"none", "face", "edge", "vertex"};
    const char *kn = kKindName[hit.kind <= PickVertex ? hit.kind : 0];
    if (debugPick)
        std::printf("fcviewer: pick (%g,%g) -> draw %d %s off %d t %g\n",
                    px, py, hit.draw, kn, hit.offset, hit.t);
    if (hit.draw >= 0)
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc),
                      "scene draw %d %s %d", hit.draw, kn, hit.offset);
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
    clearButtonHover();
    return EM_TRUE;
}

static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent *e, void *)
{
    s_dragging = false;
    if (s_clickOk && std::abs(int(e->clientX) - s_downX) <= 6
            && std::abs(int(e->clientY) - s_downY) <= 6) {
        tapOrDouble(float(e->clientX), float(e->clientY), e->ctrlKey);
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
        std::snprintf(s_camMsg, sizeof(s_camMsg), "cam=%s (copied)", buf);
        markDirty();
        return EM_TRUE;
    }
    if (k == 'd' || k == 'D') {
        s_hudOn = !s_hudOn;
        if (!s_hudOn)
            fcviewer_hud(nullptr);
        markDirty();
        return EM_TRUE;
    }
    return EM_FALSE;
}

// Touch: one finger orbits, two fingers pan (centroid) and pinch-zoom
// (distance ratio). State resets whenever the touch count changes.
static int s_numTouch = 0;
static float s_touchX[2], s_touchY[2];
// Tap candidate: a single finger down + up with no meaningful drag, routed
// through tapOrDouble (single = pick, double = zoom-to-fit).
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
        clearButtonHover();
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
            // Match the mouse grab-pan sign convention (onMouseMove: +dx
            // increases panX); the previous -= reversed two-finger pan on the
            // X axis while Y was already correct.
            s_panX += 0.5f * (x[0] - s_touchX[0] + x[1] - s_touchX[1])
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
        // A single finger lifted with no drag = tap. A second such tap soon
        // after and near the first is a double-tap -> zoom-to-fit; otherwise
        // run the same NaviCube-first pick as a mouse click, at the touch-down
        // point. (On a clean tap the lone touch is the one just lifted, so
        // e->numTouches == 1.)
        if (type == EMSCRIPTEN_EVENT_TOUCHEND && s_tapOk
                && e->numTouches == 1)
            tapOrDouble(s_tapX, s_tapY, /*ctrl*/ false);
        s_tapOk = false;
    }

    s_numTouch = n;
    for (int i = 0; i < n; ++i) {
        s_touchX[i] = x[i];
        s_touchY[i] = y[i];
    }
    return EM_TRUE;  // preventDefault: no synthesized mouse events
}

/// The boxes of a publish that is staged but not yet merged into the
/// model; defined with the fetch order those boxes exist for.
static const std::map<uint64_t, std::array<float, 6>> &pendingBoxes();

/// Frame everything the viewer knows about. False when it knows of
/// nothing yet and the camera was left alone.
static bool fitCamera()
{
    float bmin[3], bmax[3];
    // Prefer the boxes the root manifest named over the geometry that
    // has arrived. A scene is drawn while it is still coming in
    // (docs/SceneStreaming.md §6), and fitting to what happens to be
    // resident would frame the first few objects and then lurch on
    // every arrival; the root's boxes are the whole model from the
    // first payload. Falls back to the renderer's bound box for a
    // scene that carries no object manifest at all — a bundled
    // capture, or a publish from before v33.
    bool have = s_objects.boundBox(bmin, bmax)
        || (s_renderer && s_renderer->boundBox(bmin[0], bmin[1], bmin[2],
                                               bmax[0], bmax[1], bmax[2]));
    // A staged publish has not reached the model yet, and on a cold
    // load it *is* the model: fitting without it would frame nothing
    // on the first pass and leave the camera — which the fetch order
    // is sorted by (§6) — pointing at an arbitrary default while every
    // object in the scene is being asked for.
    for (const auto &item : pendingBoxes()) {
        const float *b = item.second.data();
        if (b[0] > b[3] || b[1] > b[4] || b[2] > b[5])
            continue;   // an empty box, written inside out
        for (int i = 0; i < 3; ++i) {
            bmin[i] = have ? std::min(bmin[i], b[i]) : b[i];
            bmax[i] = have ? std::max(bmax[i], b[3 + i]) : b[3 + i];
        }
        have = true;
    }
    if (have) {
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
    return have;
}

/// The automatic camera: frame the model, then let a ?cam= parameter
/// reproduce an exact viewport over it.
///
/// One place, because it is wanted at two moments — when a publish is
/// staged, so the fetch order is sorted by the camera the page asked
/// for, and when one is applied. The parameter is consumed by the
/// first of those that has a model to fit against, and only then: it
/// keeps fitCamera's s_diag (the near/far derivation), which a fit
/// that framed nothing has not computed.
static void autoFitCamera()
{
    if (!fitCamera() || !s_haveCamParam)
        return;
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

/// Feed the loaded snapshot to the renderer; a first load also fits
/// the camera (streamed updates keep the user's).
static void applySnapshot(bool fit)
{
    const double dbgA0 = emscripten_get_now();
    struct AccA { double t0; ~AccA() { s_dbgApplyMs += emscripten_get_now() - t0; } }
        accA{dbgA0};
    markDirty();
    // One line per apply — the streamed updates were previously
    // silent, which made "did the page get the republish?" guesswork.
    std::printf("fcviewer: apply snapshot: %zu draws, %zu post, "
                "%zu splices%s\n",
                s_snap.scene.size(),
                s_snap.usershaderconf.shaders.size(),
                s_snap.usershaderconf.splices.size(),
                fit ? " (fit)" : "");
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
    s_renderer->setBloomConfig(s_snap.bloomconf);
    s_renderer->setRenderDebugConfig(s_snap.debugconf);
    // User shaders (docs/RenderDebug.md §6.3): the post-stage list;
    // material-stage programs ride the scene draw materials. Programs
    // load from the server-compiled binaries the snapshot carries.
    s_renderer->setUserShaderConfig(s_snap.usershaderconf);
    s_renderer->setAutoZoomScale(s_snap.autozoomScale);
    s_renderer->setEffectResolution(s_snap.effectResolution);
    s_renderer->setSSAOResolution(s_snap.ssaoResolution);
    if (s_snap.hatch && !s_snap.hatch->pixels.empty())
        s_renderer->setHatchImage(s_snap.hatch->pixels.data(), 4,
                                  s_snap.hatch->width, s_snap.hatch->height);
    Render::DrawCallList draws = s_snap.scene;
    s_renderer->setScene(std::move(draws));
    // The client owns selection (rebuildSelection below): the backend's own
    // streamed selection feed is ignored so its slow full re-stream never
    // drives the visible selection. Any previously applied streamed selection
    // is dropped.
    for (int id : s_selIds)
        s_renderer->removeSelection(id);
    s_selIds.clear();
    // Re-apply the local selection against the (possibly replaced) scene draws.
    rebuildSelection();
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
    s_hoverKind = PickNone;
    s_haveScene = true;
    if (fit)
        autoFitCamera();   // derives scene center / dist / s_diag (near-far)
}

/// Install a fully resolved snapshot as the scene being rendered.
static void commitSnapshot(Render::SceneSnapshot &&snap, uint64_t version)
{
    s_sceneVersion = version;
    // Refit not only on the very first scene: while the camera is
    // still the auto fit (the user hasn't driven it), a streamed
    // scene replacing a bundled snapshot reframes too — the old fit
    // may point at entirely different geometry.
    // A camera the user drove — or one a ?cam= parameter reproduced,
    // possibly already at staging time (§6) — is never refitted, even
    // by the first scene.
    bool first = !s_userCam;
    s_snap = std::move(snap);
    applySnapshot(first);
    fcviewer_status(nullptr, 0.0, 0.0);
    std::printf("fcviewer: scene update v%llu, %zu draws, %zu overlays\n",
                (unsigned long long)version, s_snap.scene.size(),
                s_snap.overlays.size());
}

//////////////////////////////////////////////////////////////////////
// Deferred texture payloads (SceneDump.h, v26)
//
// A streamed snapshot names its textures by content key instead of
// carrying the pixels, because a republish fires on every feed change
// — down to a selection pick — while the embedded images do not
// change with it. The pixels are fetched once per key from
// GET /blob?key=, then kept here and in IndexedDB, so a republish
// costs nothing and a page reload costs nothing either.
//
// Resolution is all-or-nothing per snapshot: the staged snapshot is
// only applied once every key it names is in hand. Progressive
// application would mean handing the backend a texture it has already
// keyed a GPU upload on under the same textureId, which it would not
// re-upload.

/// db_name for the emscripten IndexedDB helpers. Content addressed, so
/// one store serves every document and every backend.
static const char *kBlobDb = "fcviewer-blobs";

/// Whether payloads are kept in IndexedDB across page loads. ?noidb
/// turns it off, which is the quickest way to tell a store problem
/// apart from a stream problem.
static bool blobPersistEnabled()
{
    static int on = -1;
    if (on < 0)
        on = EM_ASM_INT({
            return new URLSearchParams(location.search).has('noidb') ? 0 : 1;
        });
    return on != 0;
}

/// Take a payload the store handed back, but only if it is the payload
/// that key names. The store is content addressed, so the key IS the
/// hash and checking costs one pass over bytes that would otherwise
/// have been downloaded — cheap next to the fetch it saves.
///
/// This is not paranoia about bit rot. A store entry whose content
/// does not match its key is indistinguishable from a correct one at
/// every later step: it parses, it renders, and it produces garbage
/// geometry and out-of-bounds picks instead of an error. Nothing short
/// of comparing the bytes to the key catches it, and until something
/// did, a profile that acquired one bad entry stayed broken through
/// every reload — while a private window worked, since it had no
/// store at all.
static void blobResolved(const std::string &key,
                         std::shared_ptr<std::vector<uint8_t>> data,
                         bool fromDb);
static void queueBatch(const std::string &key, uint32_t size);
static void fetchBlob(const std::string &key);
static void fetchFromNetwork(const std::string &key, uint32_t size);

static void blobFromDb(const std::string &key, const uint8_t *bytes,
                       size_t size, uint32_t batchSize)
{
    if (Render::sha1Hex(bytes, size) == key) {
        blobResolved(key, std::make_shared<std::vector<uint8_t>>(
                              bytes, bytes + size), true);
        return;
    }
    std::printf("fcviewer: cached blob %s is not what its key names, "
                "dropping it\n", key.c_str());
    emscripten_idb_async_delete(kBlobDb, key.c_str(), nullptr,
                                [](void *) {}, [](void *) {});
    // Straight to the network: the store just proved untrustworthy for
    // this key, and the delete may not have landed yet.
    fetchFromNetwork(key, batchSize);
}

typedef std::shared_ptr<std::vector<uint8_t>> BlobData;
static std::map<std::string, BlobData> s_blobCache;
/// Keys with a load in flight (IndexedDB or HTTP), so a second
/// deferred texture naming the same key does not start a second one.
static std::set<std::string> s_blobInFlight;
/// Requests, not keys and not bytes: what a viewer's throughput turns
/// out to depend on is how many of them are outstanding at once (see
/// kInFlightRequests).
static size_t s_requestsInFlight = 0;
/// Keys this snapshot could not obtain. Cleared whenever a new
/// snapshot is staged: one retry per publish, never a fetch loop.
static std::set<std::string> s_blobFailed;

static Render::SceneSnapshot s_pendingSnap;
static uint64_t s_pendingVersion = 0;
/// ?stream — report what each assembly pass could draw and how much of
/// it is still coarse. Off by default: this fires on every arrival, and
/// a scene arrives in hundreds of chunks.
static bool s_streamDebug = false;
/// ?nofetchorder — ask for every payload the moment it is named, in the
/// order the publish names them, as the viewer did before there was a
/// fetch order (docs/SceneStreaming.md §6). This is the benchmark
/// baseline: it hands the whole queue to the browser at once, which is
/// the fastest a scene can arrive and the least useful order for it to
/// arrive in.
static bool s_noFetchOrder = false;
/// ?noprogressive — do not draw a publish until all of it is in hand,
/// as the viewer did before 2b-3. The ladder still exists underneath —
/// this suppresses the *showing* of it, so a load has one visible
/// event, which is what makes "when is it usable" and "when is it
/// finished" separable numbers.
///
/// Deliberately independent of the flag above: they turn off two
/// different things, and a benchmark that moves both at once measures
/// neither.
static bool s_noProgressive = false;
static bool s_pendingValid = false;
/// The publish being shown still has payloads outstanding. A scene is
/// put on screen as soon as any of it can be drawn
/// (docs/SceneStreaming.md §6), so the snapshot that arrives keeps
/// resolving after it has been committed: from then on the live
/// snapshot is the one chunks fill, and each arrival re-assembles the
/// feed. There is only ever one snapshot resolving — a publish that
/// stages while this is set supersedes it.
static bool s_liveOutstanding = false;
/// The live snapshot has payloads that arrived since it was last
/// assembled. Kept apart from `s_liveOutstanding` because the two go
/// false at different moments: the batch that completes a publish
/// clears the outstanding flag and is itself the one still needing to
/// be shown.
static bool s_liveUnapplied = false;

static void resolvePending();

/// Bytes one request should carry. Meshes are many and small, so a
/// request each would be mostly round trips: they are packed into
/// batches up to this budget, and a full batch is issued immediately
/// while the next one fills, so batches run in parallel. A chunk
/// larger than the budget is not a special case — it simply ends up
/// alone in its batch, which is the one-resource-one-request shape
/// textures always have.
///
/// 256 KB is about where transfer time stops being dominated by
/// per-request overhead on a slow link (~200 ms at 10 Mbps), while
/// still leaving a large model enough separate requests to use the
/// browser's parallel connections and to report progress.
static const size_t kRequestBytes = 256 * 1024;
/// Matches the server's cap on one POST /blobs (SceneServer.cpp).
static const size_t kBatchKeyMax = 512;

/// Keys waiting to be asked for, with the size each contributes.
static std::vector<std::pair<std::string, uint32_t>> s_batchQueue;
static size_t s_batchBytes = 0;
static bool s_batchScheduled = false;
/// Set while a batch response is being unpacked, so the staged
/// snapshot is re-examined once at the end rather than per blob.
static bool s_batchApplying = false;

/// A cached payload that will not parse means the store is stale or
/// damaged — most often written by a build whose chunk layout this one
/// no longer agrees with. The store is a pure optimization, so throw
/// all of it away and reload rather than repair it entry by entry: the
/// case is rare, the cost is one round of refetching, and a
/// half-trusted cache is worse than none. Once per page, so a payload
/// the server itself cannot serve cannot loop.
static bool s_blobStoreReset = false;
/// A commit already queued for the next tick (see resolvePending).
static bool s_commitScheduled = false;

static void commitResolved();
static void requestFullScene();

static void resetBlobStore()
{
    if (s_blobStoreReset)
        return;
    s_blobStoreReset = true;
    std::printf("fcviewer: blob store unusable, clearing and reloading\n");
    // Everything after this point is abandoned: navigating away tears
    // the module down, and code that keeps running into a page that is
    // going away reads memory it no longer owns. The flag stops the
    // scene pipeline; the reload itself waits for the clear to finish
    // and then for a fresh tick, so no C++ frame is still on the stack
    // under it.
    auto done = [](void *) {
        emscripten_async_call([](void *) {
            emscripten_run_script("location.reload()");
        }, nullptr, 0);
    };
    emscripten_idb_async_clear(kBlobDb, nullptr, done, done);
}

/// Memory the resident payload cache may hold. Past it, everything the
/// applied scene does not name is dropped — IndexedDB still has it, so
/// the cost of being wrong is one local read, not a download.
static const size_t kBlobCacheBudget = 192u * 1024 * 1024;

static void pruneBlobCache()
{
    size_t total = 0;
    for (const auto &entry : s_blobCache)
        total += entry.second ? entry.second->size() : 0;
    if (total <= kBlobCacheBudget)
        return;
    std::set<std::string> inUse;
    for (const auto &chunk : s_snap.deferredChunks)
        inUse.insert(chunk.key);
    for (auto it = s_blobCache.begin(); it != s_blobCache.end();) {
        if (inUse.count(it->first))
            ++it;
        else
            it = s_blobCache.erase(it);
    }
}

/// Cache a payload (from either source) and let the staged snapshot
/// make progress.
static void blobResolved(const std::string &key, BlobData data,
                         bool fromDb)
{
    if (s_blobStoreReset)
        return;
    s_blobInFlight.erase(key);
    s_blobCache[key] = data;
    if (!fromDb && data && blobPersistEnabled()) {
        // Persist for the next page load. The store is keyed by content
        // hash, so this never overwrites anything with different bytes.
        emscripten_idb_async_store(
            kBlobDb, key.c_str(), data->data(), int(data->size()),
            nullptr, [](void *) {},
            [](void *) {
                std::printf("fcviewer: blob store to IndexedDB failed\n");
            });
    }
    if (!s_batchApplying)
        resolvePending();
}

static void blobFailed(const std::string &key)
{
    s_blobInFlight.erase(key);
    s_blobFailed.insert(key);
    std::printf("fcviewer: blob %s unavailable, dropped\n", key.c_str());
    if (!s_batchApplying)
        resolvePending();
}

static void fetchBlob(const std::string &key)
{
    if (s_sceneUrl.empty()) {
        blobFailed(key);
        return;
    }
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.userData = new std::string(key);
    attr.onsuccess = [](emscripten_fetch_t *fetch) {
        --s_requestsInFlight;
        std::unique_ptr<std::string> key(
            static_cast<std::string *>(fetch->userData));
        auto data = std::make_shared<std::vector<uint8_t>>(
            fetch->data, fetch->data + fetch->numBytes);
        emscripten_fetch_close(fetch);
        blobResolved(*key, data, false);
    };
    attr.onerror = [](emscripten_fetch_t *fetch) {
        --s_requestsInFlight;
        std::unique_ptr<std::string> key(
            static_cast<std::string *>(fetch->userData));
        emscripten_fetch_close(fetch);
        blobFailed(*key);
    };
    ++s_requestsInFlight;
    std::string url = s_sceneUrl + "/blob?key=" + key;
    emscripten_fetch(&attr, url.c_str());
}

/// Unpack a POST /blobs reply: 'FCBB', u32 count, then per entry the
/// 40-byte key, u32 length (0xffffffff = the server does not have it)
/// and the payload. Anything the reply did not answer is failed, so no
/// key is left in flight forever.
static void applyBatch(const uint8_t *data, size_t size,
                       const std::vector<std::string> &asked)
{
    if (s_blobStoreReset)
        return;
    s_batchApplying = true;
    std::set<std::string> seen;
    if (size >= 8 && std::memcmp(data, "FCBB", 4) == 0) {
        uint32_t count = 0;
        std::memcpy(&count, data + 4, 4);
        size_t pos = 8;
        for (uint32_t i = 0; i < count && pos + 44 <= size; ++i) {
            std::string key(reinterpret_cast<const char *>(data + pos), 40);
            pos += 40;
            uint32_t len = 0;
            std::memcpy(&len, data + pos, 4);
            pos += 4;
            seen.insert(key);
            if (len == 0xffffffffu) {
                blobFailed(key);
                continue;
            }
            if (pos + len > size)
                break;
            blobResolved(key, std::make_shared<std::vector<uint8_t>>(
                                  data + pos, data + pos + len), false);
            pos += len;
        }
    }
    for (const auto &key : asked) {
        if (!seen.count(key))
            blobFailed(key);
    }
    s_batchApplying = false;
    resolvePending();
}

/// One request for a packed batch of keys.
struct BatchRequest {
    std::string body;
    std::vector<std::string> keys;
};

static void flushBatch()
{
    if (s_batchQueue.empty())
        return;
    auto items = std::move(s_batchQueue);
    s_batchQueue.clear();
    s_batchBytes = 0;

    auto *req = new BatchRequest;
    size_t bytes = 0;
    for (const auto &item : items) {
        req->keys.push_back(item.first);
        req->body += item.first;
        req->body += '\n';
        bytes += item.second;
    }
    if (s_sceneUrl.empty()) {
        std::unique_ptr<BatchRequest> owned(req);
        for (const auto &key : owned->keys)
            blobFailed(key);
        return;
    }

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    static const char *headers[] = {"Content-Type", "text/plain", nullptr};
    attr.requestHeaders = headers;
    attr.requestData = req->body.data();
    attr.requestDataSize = req->body.size();
    attr.userData = req;
    attr.onsuccess = [](emscripten_fetch_t *fetch) {
        --s_requestsInFlight;
        std::unique_ptr<BatchRequest> req(
            static_cast<BatchRequest *>(fetch->userData));
        std::vector<uint8_t> data(fetch->data, fetch->data + fetch->numBytes);
        emscripten_fetch_close(fetch);
        applyBatch(data.data(), data.size(), req->keys);
    };
    attr.onerror = [](emscripten_fetch_t *fetch) {
        --s_requestsInFlight;
        std::unique_ptr<BatchRequest> req(
            static_cast<BatchRequest *>(fetch->userData));
        emscripten_fetch_close(fetch);
        applyBatch(nullptr, 0, req->keys);
    };
    // Not just meshes since v33: group manifests, materials and
    // shaders are all many-and-small and share this path.
    std::printf("fcviewer: chunk batch, %zu keys, %zu bytes\n",
                req->keys.size(), bytes);
    ++s_requestsInFlight;
    std::string url = s_sceneUrl + "/blobs";
    emscripten_fetch(&attr, url.c_str());
}

/// Pack a key into the batch being filled. A full batch goes out at
/// once and the next starts, so requests overlap; a partial one is
/// flushed on the next tick, by which time the other misses of this
/// pass have joined it.
static void queueBatch(const std::string &key, uint32_t size)
{
    s_batchQueue.emplace_back(key, size);
    s_batchBytes += size;
    if (s_batchBytes >= kRequestBytes || s_batchQueue.size() >= kBatchKeyMax) {
        flushBatch();
        return;
    }
    if (!s_batchScheduled) {
        s_batchScheduled = true;
        emscripten_async_call([](void *) {
            s_batchScheduled = false;
            flushBatch();
        }, nullptr, 0);
    }
}

/// How a payload is fetched, decided from its size and nothing else.
///
/// The line is one batch's worth: a payload that would fill a whole
/// request by itself gains nothing from sharing one, so it takes a
/// plain `GET /blob?key=` — which the browser can cache and revalidate
/// on its own, where a batched POST cannot be. Anything smaller shares
/// a request, because below that size the round trip *is* the cost:
/// the demo scene's 105 payloads as 105 requests would be slower than
/// sending them all inline.
///
/// The rule is about size and not about what the payload holds. A
/// small texture shares a request with its neighbours and a large mesh
/// takes its own, and neither this function nor the deferred entry it
/// serves knows which kind it is looking at — which is the point:
/// adding a payload kind should not mean adding a fetch path.
static const size_t kOwnRequestBytes = kRequestBytes;

static void fetchFromNetwork(const std::string &key, uint32_t size)
{
    if (size && size < kOwnRequestBytes)
        queueBatch(key, size);
    else
        fetchBlob(key);
}

/// IndexedDB first — a reload or a revisit skips the network entirely.
static void requestBlob(const std::string &key, uint32_t size = 0)
{
    if (!s_blobInFlight.insert(key).second)
        return;
    if (!blobPersistEnabled()) {
        fetchFromNetwork(key, size);
        return;
    }
    emscripten_idb_async_load(
        kBlobDb, key.c_str(), new std::pair<std::string, uint32_t>(key, size),
        [](void *arg, void *ptr, int num) {
            std::unique_ptr<std::pair<std::string, uint32_t>> item(
                static_cast<std::pair<std::string, uint32_t> *>(arg));
            blobFromDb(item->first, static_cast<uint8_t *>(ptr),
                       size_t(num), item->second);
            std::free(ptr);
        },
        [](void *arg) {
            // Not stored yet (the common first-visit case) or the store
            // is unavailable: go to the network.
            std::unique_ptr<std::pair<std::string, uint32_t>> item(
                static_cast<std::pair<std::string, uint32_t> *>(arg));
            fetchFromNetwork(item->first, item->second);
        });
}

/// Assemble a snapshot out of the payloads that have arrived so far.
/// False means it cannot be applied at all and the caller must ask for
/// a full scene.
static bool assembleResolved(Render::SceneSnapshot &snap)
{
    // The manifest layout stages a group's draws in a fixed slot and
    // its materials in a table; this is the pass that puts both where
    // the backend expects them (SceneDump.h).
    if (!snap.finalize)
        return true;
    snap.finalize(snap);
    // Then the objects, which the snapshot alone cannot assemble:
    // a delta names only what changed, so the feed is built from
    // the model this viewer carries between publishes. Each draw
    // comes out at the best rung it holds — the geometry if it has
    // landed, a box on its bounds if it has not — so this runs per
    // arrival rather than once, and each pass refines the last.
    if (Render::applySceneObjects(snap, s_objects)) {
        if (s_streamDebug) {
            size_t coarse = 0;
            for (const auto &d : snap.scene)
                coarse += d.standIn ? 1 : 0;
            std::printf("fcviewer: scene at %zu draws, %zu of them coarse,"
                        " %zu objects still arriving\n",
                        snap.scene.size(), coarse, s_objects.unresolved());
        }
        return true;
    }
    std::printf("fcviewer: publish is a delta against v%llu, "
                "holding v%llu — asking for a full scene\n",
                (unsigned long long)snap.baseVersion,
                (unsigned long long)s_objects.version);
    return false;
}

static bool stillArriving(const Render::SceneSnapshot &snap)
{
    for (const auto &entry : snap.deferredChunks) {
        if (entry.fill)
            return true;
    }
    return false;
}

/// Show what the staged snapshot can draw. Runs on every arrival that
/// made progress, not only on the last one.
static void commitResolved()
{
    if (s_blobStoreReset)
        return;
    if (s_pendingValid) {
        Render::SceneSnapshot snap = std::move(s_pendingSnap);
        uint64_t version = s_pendingVersion;
        s_pendingValid = false;
        s_pendingSnap = Render::SceneSnapshot();
        if (!assembleResolved(snap)) {
            requestFullScene();
            return;
        }
        // From here the committed snapshot is the one still being
        // filled: the fills write into whichever snapshot they are
        // handed, so nothing is copied and nothing is staged twice.
        commitSnapshot(std::move(snap), version);
        s_liveOutstanding = stillArriving(s_snap);
        s_liveUnapplied = false;
    }
    else if (s_liveUnapplied) {
        s_liveUnapplied = false;
        if (!assembleResolved(s_snap)) {
            requestFullScene();
            return;
        }
        // Refit while the camera is still the automatic one: the fit
        // runs off the bounding boxes the root named, so it does not
        // move as the geometry inside them arrives.
        applySnapshot(!s_userCam);
        if (!s_liveOutstanding)
            fcviewer_status(nullptr, 0.0, 0.0);
    }
    else {
        return;
    }
    pruneBlobCache();
}

//////////////////////////////////////////////////////////////////////
// Fetch order (docs/SceneStreaming.md §6, phase 4)

/// The boxes of the objects this publish introduced. The model behind
/// the feed carries the boxes of everything published earlier, but the
/// objects of the publish now arriving are not in it yet — they are
/// merged when the snapshot is first assembled, which is *after* their
/// chunks have to be asked for. On a cold load that is every object in
/// the scene, so without this the first and most important ordering
/// decision would be the one made blind.
static std::map<uint64_t, std::array<float, 6>> s_pendingBox;

static const std::map<uint64_t, std::array<float, 6>> &pendingBoxes()
{
    return s_pendingBox;
}

static void indexPendingBoxes(const Render::SceneSnapshot &snap)
{
    s_pendingBox.clear();
    for (const auto &up : snap.objectUpdates) {
        std::array<float, 6> box{};
        std::memcpy(box.data(), up.entry.bbox, sizeof(box));
        s_pendingBox[up.entry.objectKey] = box;
    }
}

/// How many requests this viewer keeps outstanding at once. A fetch
/// order is only an order if there is a queue to order: ask for
/// everything the moment it is named and the sort decides nothing,
/// because the requests are all issued in the same tick and come back
/// in whatever order the network answers them.
///
/// **Requests, not bytes**, because bytes is not what a window of
/// outstanding work is bounded by anywhere: a request is a socket and
/// a round trip whatever it carries. Counting bytes would also
/// throttle a scene of large chunks while leaving a scene of small
/// ones unbounded, which is exactly backwards.
///
/// The width is not about throughput, though it took `?noprogressive`
/// to see that: with the display held back, windows of 8, 16 and 64
/// fetched the same 60 MB scene in 1.61 s, 1.53 s and 1.41 s — the
/// fetch barely notices. What the width really buys is *coalescing*.
/// Every arrival re-assembles the feed and hands it to the backend,
/// and a batch that lands while another is still in flight is folded
/// into the same pass; the same three windows with the display on
/// cost 65 s, 17 s and 7.7 s. So the number to tune here is really the
/// per-arrival cost of showing a scene, and until that is incremental
/// (§6) a wide window is how it is paid for. 64 is where the curve
/// flattens on this harness, and still leaves a large model most of
/// its queue to re-sort — which on a large model is the point.
static const size_t kInFlightRequests = 64;

/// The camera as one round of ordering sees it, with the priority of
/// each object it has already had to work out.
///
/// Both halves are memoization and both are needed. The camera frame
/// costs four trigonometric functions, and a chunk shared by every
/// object in the scene carries every one of those objects — so scoring
/// a queue of a few hundred chunks against a few hundred owners apiece
/// re-derived the same camera millions of times per load. Measured, it
/// was the whole cost of the fetch: a load that should have been
/// network-bound spent its time in `sin`.
struct FetchOrder {
    CamFrame cam;
    bx::Vec3 fwd = bx::InitZero;
    float th = 0.0f;
    float aspect = 1.0f;
    std::map<uint64_t, float> memo;

    FetchOrder()
        : cam(camFrame())
    {
        fwd = bx::normalize(bx::sub(cam.at, cam.eye));
        th = std::tan(0.5f * kFovY * bx::kPi / 180.0f);
        aspect = s_height > 0 ? float(s_width) / float(s_height) : 1.0f;
    }

    /// Fetch priority of one object: larger is wanted sooner.
    ///
    /// Projected size, which is distance and extent in one number, and
    /// is what "fetch what matters" means on a screen: a near wall and
    /// a far building can be equally worth having. Off-screen is a
    /// penalty rather than an exclusion — the object is still fetched,
    /// just behind everything visible, so a viewer that never moves
    /// still ends up holding the whole scene, and one that turns
    /// around finds the work already started.
    float owner(uint64_t key)
    {
        auto memoIt = memo.find(key);
        if (memoIt != memo.end())
            return memoIt->second;
        float score = 0.0f;
        const float *bbox = nullptr;
        auto it = s_objects.objects.find(key);
        if (it != s_objects.objects.end())
            bbox = it->second.entry.bbox;
        else {
            auto pit = s_pendingBox.find(key);
            if (pit != s_pendingBox.end())
                bbox = pit->second.data();
        }
        if (bbox)
            score = project(bbox);
        memo.emplace(key, score);
        return score;
    }

    /// The priority of a chunk: **the best of the objects that need
    /// it**, never the first of them. One material can back a whole
    /// scene and one mesh every instance of a part, and such a chunk
    /// is named by whichever manifest happened to be read first —
    /// which may be the smallest object in the far distance. Fetched
    /// at that object's priority it holds up every object that shares
    /// it, and since an object is only drawn once its whole appearance
    /// is resident (SceneDump.cpp), one starved material is a scene
    /// that never leaves its boxes.
    float chunk(const Render::SceneSnapshot::DeferredChunk &c)
    {
        float best;
        if (c.owners.empty()) {
            // The root's own sections and the overlay feeds, which no
            // object claims: they are what names the rest, and the
            // navigation cube is wanted before any of the model.
            best = std::numeric_limits<float>::max();
        }
        else {
            best = 0.0f;
            for (uint64_t key : c.owners)
                best = std::max(best, owner(key));
        }
        // Per byte, because the question a fetch order answers is not
        // "what is most worth having" but "what is most worth
        // *spending the next byte on*" — and the two differ by a
        // factor of a thousand here. A material is a few hundred bytes
        // and lifts every object that names it off the grey box it is
        // drawn as; the mesh that finishes one of those objects is a
        // hundred kilobytes. Rank by size alone and the appearance of
        // the whole model queues behind the geometry of its first few
        // parts, which is what a ladder exists not to do (§6).
        //
        // It also keeps the rule the deferred list is built on: what a
        // consumer does with a chunk follows from its size and never
        // from what is inside it (SceneDump.h). Nothing here knows a
        // material from a mesh — it does not need to, because the
        // appearance layer *is* the small one.
        return best / float(std::max<uint32_t>(c.size, 1));
    }

private:
    float project(const float *bbox) const
    {
        const bx::Vec3 center(0.5f * (bbox[0] + bbox[3]),
                              0.5f * (bbox[1] + bbox[4]),
                              0.5f * (bbox[2] + bbox[5]));
        const float radius = 0.5f * bx::length(bx::Vec3(bbox[3] - bbox[0],
                                                        bbox[4] - bbox[1],
                                                        bbox[5] - bbox[2]));
        const bx::Vec3 rel = bx::sub(center, cam.eye);
        const float along = bx::dot(rel, fwd);
        // Clamped at the object's own radius: something at or behind
        // the eye is not a hundred times more urgent than something in
        // front of it, it is off-screen, which the penalty below says.
        const float dist = std::max(along, radius + 1e-4f);
        float score = radius / dist;
        const float halfV = th * std::max(along, 0.0f);
        // CamFrame::right is the negation of the camera's right axis
        // (screenRay), which does not matter to a symmetric test.
        const bool onScreen = along + radius > 0.0f
            && std::fabs(bx::dot(rel, cam.right)) <= halfV * aspect + radius
            && std::fabs(bx::dot(rel, cam.up)) <= halfV + radius;
        return onScreen ? score : score * 1e-3f;
    }
};

/// Fill in what the snapshot being resolved still needs, ask for what
/// it is missing in the order the camera wants it, and show what that
/// made drawable. The target is the staged publish until it is
/// committed and the live scene afterwards — a scene keeps arriving
/// after it is first drawn (docs/SceneStreaming.md §6).
static void resolvePending()
{
    if (s_blobStoreReset)
        return;
    const double dbgT0 = emscripten_get_now();
    struct Acc { double t0; ~Acc() {
        s_dbgMs += emscripten_get_now() - t0;
        if (s_streamDebug && (++s_dbgN % 25) == 0)
            std::printf("fcviewer: DBG %d resolve+commit passes, %.0f ms "
                        "total, %.0f ms of it in apply\n",
                        s_dbgN, s_dbgMs, s_dbgApplyMs);
    } } acc{dbgT0};
    Render::SceneSnapshot *target = s_pendingValid ? &s_pendingSnap
        : (s_liveOutstanding ? &s_snap : nullptr);
    if (!target)
        return;
    size_t missing = 0, total = 0;
    bool incomplete = false;
    /// Whether anything new became drawable this round. Without it a
    /// commit would be scheduled for every payload that merely failed
    /// or was already in hand.
    bool filled = false;

    // Every out-of-band payload the snapshot named — group manifests,
    // meshes, materials, shaders, textures — in one pass. A fill can
    // name further payloads (a group names its meshes and materials, a
    // material names its textures), so this runs in rounds until one
    // resolves nothing new rather than in a single pass.
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < target->deferredChunks.size(); ++i) {
            auto &entry = target->deferredChunks[i];
            if (!entry.fill)
                continue;
            auto it = s_blobCache.find(entry.key);
            bool failed = s_blobFailed.count(entry.key) != 0;
            if ((it == s_blobCache.end() || !it->second) && !failed)
                continue;
            // Take the callable and clear the slot before running it:
            // it may append entries, and a reference into the vector
            // does not survive that.
            auto fill = entry.fill;
            entry.fill = nullptr;
            // A null payload asks the entry to give up. Whether that
            // is survivable is its business, not ours — a texture says
            // yes and the draw renders untextured.
            bool ok = failed
                ? fill(*target, nullptr, 0)
                : fill(*target, it->second->data(), it->second->size());
            if (ok) {
                progress = true;
                filled = true;
            }
            else {
                if (!failed)
                    resetBlobStore();
                incomplete = true;
            }
        }
    }
    if (incomplete) {
        // A payload could not be obtained or would not parse. This
        // publish will not complete, so stop resolving it rather than
        // re-requesting what already failed — but show what did
        // arrive: a draw whose mesh is missing is left out of the feed
        // rather than submitted with nothing behind it
        // (applySceneObjects), so a partial scene is a valid one. The
        // objects it could not describe wait for the next publish.
        std::printf("fcviewer: snapshot incomplete, showing what arrived\n");
        for (auto &entry : target->deferredChunks)
            entry.fill = nullptr;
    }
    else {
        total += target->deferredChunks.size();
        // What is outstanding, in the order the camera wants it. The
        // sort is over the chunks not yet asked for, so it costs
        // nothing once the scene is mostly in hand — the common case
        // being a delta with a handful of chunks.
        std::vector<std::pair<float, size_t>> want;
        FetchOrder order;
        for (size_t i = 0; i < target->deferredChunks.size(); ++i) {
            const auto &entry = target->deferredChunks[i];
            if (!entry.fill)
                continue;
            ++missing;
            if (s_blobInFlight.count(entry.key))
                continue;
            // ?nofetchorder scores nothing: every chunk ties, the sort
            // below leaves them in the order the publish named them,
            // and the window check is skipped, which between them is
            // the fetch exactly as it was before there was an order.
            want.emplace_back(s_noFetchOrder ? 0.0f : order.chunk(entry), i);
        }
        // Ties keep publish order, which is the order the objects were
        // named in: a scene the camera has no opinion about (nothing
        // fitted yet, or everything equally distant) streams exactly as
        // it did before this.
        std::stable_sort(want.begin(), want.end(),
                         [](const std::pair<float, size_t> &a,
                            const std::pair<float, size_t> &b) {
                             return a.first > b.first;
                         });
        if (s_streamDebug) {
            std::printf("fcviewer: fetch: %zu outstanding of %zu, %zu "
                        "askable, %zu requests in flight\n",
                        missing, total, want.size(), s_requestsInFlight);
        }
        for (const auto &item : want) {
            // Past the window, stop asking. Every resolution pumps this
            // function again, so the rest of the queue is issued as the
            // window drains, re-sorted against wherever the camera is
            // by then.
            //
            // Only ever between batches, though — never mid-batch.
            // Stopping as soon as the byte count is reached cuts the
            // batch being filled short, and a stream of quarter-full
            // requests is slower in two ways at once: more round trips
            // for the same bytes, and more arrivals, each of which
            // re-runs the whole assembly pass (§6). That cost is what
            // made a first, narrow window three and a half times
            // slower than no ordering at all; batches kept whole, it
            // is the same number of requests as before, just asked for
            // in a different order.
            if (!s_noFetchOrder && s_batchQueue.empty()
                    && s_requestsInFlight >= kInFlightRequests)
                break;
            const auto &entry = target->deferredChunks[item.second];
            requestBlob(entry.key, entry.size);
        }
        // Whatever is left half-packed goes now. queueBatch would send
        // it on the next tick, which is right when more keys may still
        // join it — but this round has decided what it wants, and a
        // tick is not free: a browser that considers the page
        // backgrounded clamps the timer to a second, and with a window
        // in front of the queue that second is paid once per batch
        // rather than once per load. Measured on headless Chromium,
        // that alone was the difference between a load finishing in
        // four seconds and in sixty.
        flushBatch();
    }
    if (target == &s_snap) {
        s_liveOutstanding = missing != 0;
        s_liveUnapplied = s_liveUnapplied || filled;
    }
    if (missing) {
        fcviewer_status("loading scene", double(total - missing),
                        double(total));
        if (!filled) {
            // Nothing new became drawable — the requests above are
            // what this round accomplished.
            return;
        }
    }

    // ?noprogressive holds everything back until the publish is whole,
    // which is the load with one visible event in it.
    if (s_noProgressive && missing)
        return;

    // Something is drawable, but this runs inside a fetch or IndexedDB
    // callback, and applying a scene from there puts the whole apply —
    // draw lists, GPU uploads, the refit — on top of an already deep
    // callback stack. Hand it to a fresh tick instead, so the stack
    // depth of an apply does not depend on how the last blob happened
    // to arrive.
    if (!s_commitScheduled) {
        s_commitScheduled = true;
        emscripten_async_call([](void *) {
            s_commitScheduled = false;
            commitResolved();
        }, nullptr, 0);
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
        // A version means something only within the run that issued it
        // (SceneDump.h, v35). A restarted backend counts from one
        // again, so the version we hold can name a publish that never
        // happened — and would otherwise be read as "already applied",
        // or worse, as a base a delta could be applied onto. The blob
        // cache survives: its keys are content hashes, so the new run
        // republishes the same bytes under the same keys.
        if (snap.sessionId != s_sessionId) {
            if (s_sessionId)
                std::printf("fcviewer: backend session changed, "
                            "dropping the object model\n");
            s_sessionId = snap.sessionId;
            s_objects = Render::SceneObjectModel();
            s_sceneVersion = 0;
        }
        // The WebSocket push loop re-sends the current scene on connect;
        // skip the echo of a version already applied (the initial HTTP
        // fetch).
        else if (s_haveScene && version == s_sceneVersion)
            return;
        // A publish that stages while the last one is still arriving
        // supersedes it, and its outstanding chunks are abandoned with
        // it. That is only safe if this publish describes the objects
        // those chunks were going to: a full root always does, a delta
        // names only what changed. So a delta arriving over an
        // unfinished scene is answered with a request for a full root
        // rather than leaving those objects with no geometry until
        // something happens to touch them again.
        if (snap.baseVersion && s_liveOutstanding && s_objects.unresolved()) {
            std::printf("fcviewer: delta v%llu arrived with %zu objects "
                        "still incomplete — asking for a full scene\n",
                        (unsigned long long)snap.manifestVersion,
                        s_objects.unresolved());
            requestFullScene();
            return;
        }
        // Payloads the publish only named are fetched before it is
        // applied; with none outstanding (the usual case, every key
        // already cached) this commits inline.
        s_liveOutstanding = false;
        s_liveUnapplied = false;
        s_pendingSnap = std::move(snap);
        s_pendingVersion = version;
        s_pendingValid = true;
        // Before anything of this publish is asked for: what it names
        // is sorted by where its objects are, and it is the only thing
        // that knows where the new ones are (§6).
        indexPendingBoxes(s_pendingSnap);
        // And fit to them now, not at the first commit: the order the
        // publish is fetched in is decided before any of it has been
        // drawn, so the camera has to be pointing at the model by
        // then. Only while it is still the automatic one — a user who
        // has moved it has said where they are looking.
        if (!s_userCam)
            autoFitCamera();
        s_blobFailed.clear();
        resolvePending();
    }
    else {
        // A payload in a newer serializer format than this build can
        // read means the backend was rebuilt: refresh ourselves with a
        // cache-busting reload (works over WebSocket and polling both).
        uint32_t v = Render::sceneSnapshotVersion(data + 8, size - 8);
        if (v > Render::sceneDumpVersion()) {
            std::printf("fcviewer: scene format v%u newer than built "
                        "v%u, reloading\n", v, Render::sceneDumpVersion());
            char bust[32];
            std::snprintf(bust, sizeof(bust), "v%u", v);
            fcviewer_reload(bust);
            return;
        }
        std::printf("fcviewer: scene update parse FAILED\n");
    }
}

/// JSON control messages pushed by the scene server as WebSocket text
/// frames (docs/RenderDebug.md §4.4). The messages are tiny and
/// self-generated, so field extraction is by simple search.
static void handleControlMessage(const char *json)
{
    if (std::strstr(json, "\"cmd\":\"dumpFrame\"")) {
        long id = 0, mode = -1;
        const char *p = std::strstr(json, "\"id\"");
        const char *colon = p ? std::strchr(p, ':') : nullptr;
        if (colon)
            id = std::strtol(colon + 1, nullptr, 10);
        p = std::strstr(json, "\"mode\"");
        colon = p ? std::strchr(p, ':') : nullptr;
        if (colon)
            mode = std::strtol(colon + 1, nullptr, 10);
        s_dumpReq.armed = true;
        s_dumpReq.id = uint32_t(id);
        s_dumpReq.mode = int(mode);
        markDirty();
        std::printf("fcviewer: dumpFrame request %ld (mode %ld)\n",
                    id, mode);
    }
    else if (std::strstr(json, "\"cmd\":\"config\"")) {
        // Server-pushed viewer policy (currently just the reconnect
        // budget: attempts after a dropped stream; -1 = infinite, a
        // backend debugging aid via FC_BGFX_VIEWER_RECONNECT).
        const char *p = std::strstr(json, "\"reconnect\"");
        const char *colon = p ? std::strchr(p, ':') : nullptr;
        if (colon) {
            s_reconnectLimit = std::strtol(colon + 1, nullptr, 10);
            std::printf("fcviewer: reconnect budget %ld\n",
                        s_reconnectLimit);
        }
    }
    else if (std::strstr(json, "\"cmd\":\"reload\"")) {
        char bust[64] = "";
        const char *p = std::strstr(json, "\"cacheBust\"");
        const char *colon = p ? std::strchr(p + 11, ':') : nullptr;
        const char *open = colon ? std::strchr(colon, '"') : nullptr;
        if (open) {
            const char *close = std::strchr(open + 1, '"');
            if (close && close - open - 1 < long(sizeof(bust))) {
                std::memcpy(bust, open + 1, size_t(close - open - 1));
                bust[close - open - 1] = '\0';
            }
        }
        std::printf("fcviewer: reload requested (bust '%s')\n", bust);
        fcviewer_reload(bust);
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

static bool connectWs();
static int s_pollTick = 0;

static void doPoll(void * = nullptr)
{
    if (s_wsOpen) {
        // A WebSocket retry succeeded: the socket carries the stream
        // (and the control channel) from here on.
        std::printf("fcviewer: websocket recovered, polling stops\n");
        s_polling = false;
        return;
    }
    // Periodic WebSocket retry (~10s at the 500ms poll): a socket that
    // failed transiently at page load (proxy/port-forward hiccup) must
    // not demote the page to polling forever — the control channel
    // (dumpFrame, config, reload pushes) only rides the socket.
    if (++s_pollTick >= 20) {
        s_pollTick = 0;
        if (s_ws > 0) {
            emscripten_websocket_delete(s_ws);
            s_ws = 0;
        }
        connectWs();
    }
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onPollResult;
    attr.onerror = onPollError;
    char url[512];
    std::snprintf(url, sizeof(url), "%s/scene?v=%llu&s=%llu",
                  s_sceneUrl.c_str(),
                  (unsigned long long)s_sceneVersion,
                  (unsigned long long)s_sessionId);
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

// This bundle's build stamp (fcviewer.stamp next to the page, written
// post-link — see stamp.cmake), reported in the hello so the backend
// can reload pages running a superseded build. Empty until fetched.
static std::string s_buildStamp;

/// Version handshake: tell the server which snapshot format this build
/// reads (it answers with a reload when the served format is newer),
/// which bundle build it is (reload on stamp mismatch), and register
/// as a control-channel viewer (dumpFrame). Re-sent when the build
/// stamp arrives after the socket opened.
static void sendHello()
{
    char hello[160];
    if (!s_buildStamp.empty())
        std::snprintf(hello, sizeof(hello),
                      "{\"cmd\":\"hello\",\"snapshot\":%u,"
                      "\"build\":\"%s\"}",
                      Render::sceneDumpVersion(), s_buildStamp.c_str());
    else
        std::snprintf(hello, sizeof(hello),
                      "{\"cmd\":\"hello\",\"snapshot\":%u}",
                      Render::sceneDumpVersion());
    emscripten_websocket_send_utf8_text(s_ws, hello);
}

static EM_BOOL onWsOpen(int, const EmscriptenWebSocketOpenEvent *, void *)
{
    s_wsOpen = true;
    if (s_reconnectAttempts > 0) {
        std::printf("fcviewer: reconnected after %ld attempt(s)\n",
                    s_reconnectAttempts);
        fcviewer_status(nullptr, 0.0, 0.0);
    }
    else {
        std::printf("fcviewer: websocket connected\n");
    }
    s_wsEverOpen = true;
    s_reconnectAttempts = 0;
    sendHello();
    return EM_TRUE;
}

static EM_BOOL onWsMessage(int, const EmscriptenWebSocketMessageEvent *e,
                           void *)
{
    if (!e->isText)
        applyScenePayload(reinterpret_cast<const char *>(e->data),
                          size_t(e->numBytes));
    else
        handleControlMessage(reinterpret_cast<const char *>(e->data));
    return EM_TRUE;
}

static bool connectWs();

/// The WebSocket dropped (or never came up). Before the first
/// successful open this falls back to HTTP polling (no-WebSocket
/// environments); afterwards it schedules reconnect attempts with the
/// status bar narrating, up to s_reconnectLimit (-1 = infinite).
static void onWsDown()
{
    if (!s_wsOpen && s_reconnectPending)
        return;
    bool wasOpen = s_wsOpen;
    s_wsOpen = false;
    if (!s_wsEverOpen) {
        startPolling();
        return;
    }
    if (wasOpen)
        std::printf("fcviewer: scene stream lost\n");
    if (s_reconnectLimit >= 0 && s_reconnectAttempts >= s_reconnectLimit) {
        std::printf("fcviewer: giving up after %ld reconnect attempts\n",
                    s_reconnectAttempts);
        fcviewer_status("Disconnected \xe2\x80\x94 reload to retry",
                        0.0, -1.0);
        return;
    }
    ++s_reconnectAttempts;
    char label[64];
    if (s_reconnectLimit >= 0)
        std::snprintf(label, sizeof(label),
                      "Reconnecting\xe2\x80\xa6 (%ld/%ld)",
                      s_reconnectAttempts, s_reconnectLimit);
    else
        std::snprintf(label, sizeof(label),
                      "Reconnecting\xe2\x80\xa6 (%ld)",
                      s_reconnectAttempts);
    fcviewer_status(label, 0.0, 0.0);
    s_reconnectPending = true;
    // Exponential backoff, 2s -> 30s cap: a backend restart takes
    // 60-90s (xvfb + scene build), so the 10-attempt default must span
    // minutes, not seconds (10 attempts ~ 2 minutes; the infinite
    // debug budget settles at one try per 30s).
    double delay = 2000.0;
    for (long i = 1; i < s_reconnectAttempts && delay < 30000.0; ++i)
        delay *= 1.4;
    if (delay > 30000.0)
        delay = 30000.0;
    emscripten_set_timeout([](void *) {
        s_reconnectPending = false;
        if (s_ws > 0) {
            emscripten_websocket_close(s_ws, 1000, "reconnect");
            emscripten_websocket_delete(s_ws);
            s_ws = 0;
        }
        if (!connectWs())
            onWsDown();   // socket creation failed: burn an attempt
    }, int(delay), nullptr);
}

static EM_BOOL onWsError(int, const EmscriptenWebSocketErrorEvent *, void *)
{
    onWsDown();
    return EM_TRUE;
}

static EM_BOOL onWsClose(int, const EmscriptenWebSocketCloseEvent *, void *)
{
    onWsDown();
    return EM_TRUE;
}

/// Open the scene-stream WebSocket and hook the callbacks. Returns
/// false when WebSockets are unavailable or creation failed.
static bool connectWs()
{
    if (!emscripten_websocket_is_supported())
        return false;
    std::string url = s_sceneUrl;
    if (url.rfind("http", 0) == 0)
        url = "ws" + url.substr(4);   // http(s):// -> ws(s)://
    // What we hold, stated in the upgrade request rather than in a
    // hello: it is available before the socket opens, so the server's
    // push loop can act on it without a round trip, and it is the same
    // query the polling transport uses.
    char held[64];
    std::snprintf(held, sizeof(held), "/scene?v=%llu&s=%llu",
                  (unsigned long long)s_sceneVersion,
                  (unsigned long long)s_sessionId);
    url += held;
    EmscriptenWebSocketCreateAttributes attr = {
        url.c_str(), nullptr, EM_TRUE};
    s_ws = emscripten_websocket_new(&attr);
    if (s_ws <= 0)
        return false;
    emscripten_websocket_set_onopen_callback(s_ws, nullptr, onWsOpen);
    emscripten_websocket_set_onmessage_callback(s_ws, nullptr,
                                                onWsMessage);
    emscripten_websocket_set_onerror_callback(s_ws, nullptr, onWsError);
    emscripten_websocket_set_onclose_callback(s_ws, nullptr, onWsClose);
    return true;
}

/// Connect the live scene stream (WebSocket first, polling fallback).
static void startStream()
{
    if (!connectWs())
        startPolling();
}

//////////////////////////////////////////////////////////////////////
// Initial scene load with progress: the WebSocket delivers the scene as
// one opaque message (browsers expose no intra-message progress), but the
// server's HTTP path sends a Content-Length — so the FIRST scene is
// fetched over HTTP to drive the top progress bar, then the WebSocket
// takes over for live updates (its re-push of the same version is
// skipped by the guard in applyScenePayload).

static std::string s_initialPayload;

static void applyInitialPayload(void *)
{
    applyScenePayload(s_initialPayload.data(), s_initialPayload.size());
    s_initialPayload.clear();
    if (!s_haveScene)
        fcviewer_status("Scene load failed", 0.0, -1.0);
    else
        fcviewer_status(nullptr, 0.0, 0.0);
    startStream();
}

static void onInitFetchProgress(emscripten_fetch_t *fetch)
{
    const double got = double(fetch->dataOffset) + double(fetch->numBytes);
    char label[96];
    if (fetch->totalBytes > 0)
        std::snprintf(label, sizeof(label),
                      "Loading scene\xe2\x80\xa6 %.1f / %.1f MB",
                      got / 1e6, double(fetch->totalBytes) / 1e6);
    else
        std::snprintf(label, sizeof(label),
                      "Loading scene\xe2\x80\xa6 %.1f MB", got / 1e6);
    fcviewer_status(label, got, double(fetch->totalBytes));
}

static void onInitFetchDone(emscripten_fetch_t *fetch)
{
    if (fetch->status == 200 && fetch->numBytes > 8) {
        s_initialPayload.assign(fetch->data, fetch->data + fetch->numBytes);
        emscripten_fetch_close(fetch);
        // Let the label paint before the synchronous parse + GPU upload.
        fcviewer_status("Preparing scene\xe2\x80\xa6", 0.0, 0.0);
        emscripten_set_timeout(applyInitialPayload, 30, nullptr);
        return;
    }
    // 204 = the desktop has not published a scene yet; anything else =
    // transfer problem. Either way the live stream delivers the first
    // scene (the indicator hides when it applies). With a bundled
    // snapshot already on screen there is nothing to wait for.
    emscripten_fetch_close(fetch);
    if (s_haveScene)
        fcviewer_status(nullptr, 0.0, 0.0);
    else
        fcviewer_status("Waiting for scene\xe2\x80\xa6", 0.0, 0.0);
    startStream();
}

static void onInitFetchError(emscripten_fetch_t *fetch)
{
    emscripten_fetch_close(fetch);
    if (s_haveScene)
        fcviewer_status(nullptr, 0.0, 0.0);
    else
        fcviewer_status("Waiting for scene\xe2\x80\xa6", 0.0, 0.0);
    startStream();
}

/// Fetch this bundle's build stamp (sits next to the page, served
/// no-store like the bundle itself). Arriving after the socket opened
/// re-sends the hello so the backend still learns the build.
static void fetchBuildStamp()
{
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = [](emscripten_fetch_t *fetch) {
        if (fetch->status == 200 && fetch->numBytes > 0) {
            std::string stamp;
            for (uint64_t i = 0; i < fetch->numBytes && i < 40; ++i) {
                char c = fetch->data[i];
                if (std::isalnum(static_cast<unsigned char>(c)))
                    stamp += c;
            }
            if (!stamp.empty()) {
                s_buildStamp = stamp;
                std::printf("fcviewer: build %s\n", stamp.c_str());
                if (s_wsOpen)
                    sendHello();
            }
        }
        emscripten_fetch_close(fetch);
    };
    attr.onerror = [](emscripten_fetch_t *fetch) {
        emscripten_fetch_close(fetch);
    };
    emscripten_fetch(&attr, "fcviewer.stamp");
}

/// Ask the backend for a scene this viewer can apply from nothing.
/// Dropping the model first is what makes the answer usable — a full
/// root replaces it wholesale — and asking from version 0 is what says
/// "send it whether or not you think I am current".
///
/// The backend publishes only full roots today, so this is the path a
/// viewer takes after a reconnect rather than a routine one. Serving a
/// full root on demand once deltas go on the wire is the server-side
/// half of the phase (docs/SceneStreaming.md §5).
static void requestFullScene()
{
    s_objects = Render::SceneObjectModel();
    s_pendingValid = false;
    s_pendingSnap = Render::SceneSnapshot();
    s_liveOutstanding = false;
    s_liveUnapplied = false;
    s_sceneVersion = 0;
    s_haveScene = false;
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onInitFetchDone;
    attr.onerror = onInitFetchError;
    char url[512];
    std::snprintf(url, sizeof(url), "%s/scene?v=0", s_sceneUrl.c_str());
    emscripten_fetch(&attr, url);
}

static void startInitialFetch()
{
    fcviewer_status("Loading scene\xe2\x80\xa6", 0.0, 0.0);
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onInitFetchDone;
    attr.onerror = onInitFetchError;
    attr.onprogress = onInitFetchProgress;
    char url[512];
    std::snprintf(url, sizeof(url), "%s/scene?v=%llu&s=%llu",
                  s_sceneUrl.c_str(),
                  (unsigned long long)s_sceneVersion,
                  (unsigned long long)s_sessionId);
    emscripten_fetch(&attr, url);
}

int main()
{
    emscripten_set_canvas_element_size("#canvas", s_width, s_height);

    Render::RendererFactory::setResourcePath("/assets");
    Render::BGFXRenderer::setWindowHandle(
        const_cast<char *>("#canvas"));
    Render::BGFXRenderer::setWindowSize(s_width, s_height);
    // ?msaa=N overrides the scene multisample count (default 4). Lets us test
    // whether the WebGL2-only overlay artifact ("box in the corner") is driven
    // by MSAA on the real driver: ?msaa=0 turns it off.
    int msaaSamples = EM_ASM_INT({
        var m = new URLSearchParams(window.location.search).get('msaa');
        return m === null ? 4 : (parseInt(m) | 0);
    });
    std::printf("fcviewer: MSAA=%d\n", msaaSamples);

    s_renderer = Render::RendererFactory::create("bgfx - OpenGL", nullptr);
    if (!s_renderer) {
        std::printf("fcviewer: renderer creation failed\n");
        return 1;
    }
    // Applied before the first render(), so the initial target is built with
    // the requested sample count.
    s_renderer->setMSAASamples(msaaSamples);

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

    // Before the first payload is asked for, not after: these decide
    // what the very first round of fetching does, and on a cold load
    // that round covers every object in the scene.
    s_streamDebug = EM_ASM_INT({
        return new URLSearchParams(window.location.search).has('stream')
            ? 1 : 0;
    }) != 0;
    s_noFetchOrder = EM_ASM_INT({
        return new URLSearchParams(window.location.search)
            .has('nofetchorder') ? 1 : 0;
    }) != 0;
    s_noProgressive = EM_ASM_INT({
        return new URLSearchParams(window.location.search)
            .has('noprogressive') ? 1 : 0;
    }) != 0;
    if (s_noFetchOrder)
        std::printf("fcviewer: fetch order off, asking for everything\n");
    if (s_noProgressive)
        std::printf("fcviewer: progressive display off, waiting for the "
                    "whole publish\n");

    if (char *sceneParam = fcviewer_scene_param()) {
        s_sceneUrl = sceneParam;
        std::free(sceneParam);
        std::printf("fcviewer: streaming from %s\n", s_sceneUrl.c_str());
        // First streamed scene over HTTP for the progress bar (any bundled
        // snapshot stays on screen beneath it); the WebSocket takes over
        // after.
        fetchBuildStamp();
        startInitialFetch();
    }
    else {
        // No scene stream — nothing left to wait for; drop the indicator
        // left from the emscripten download phase.
        fcviewer_status(nullptr, 0.0, 0.0);
    }


    s_hudOn = EM_ASM_INT({
        var q = new URLSearchParams(window.location.search);
        if (q.has('hud') || q.has('debugpick')) return 1;
        // Always show the HUD on touch/mobile: there is no keyboard to
        // toggle it with [d], and it is the only fps readout there.
        var touch = ('ontouchstart' in window)
            || (navigator.maxTouchPoints || 0) > 0;
        return touch ? 1 : 0;
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
