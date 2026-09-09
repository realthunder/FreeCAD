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
#include <cstdarg>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>
#include <emscripten/websocket.h>

#include <GLES3/gl3.h>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "BGFXRenderer.h"
#include "FrameImageConsumer.h"
#include "FrameStreamWire.h"
#include "Page2D.h"
#include "Page2DWire.h"
#include "SceneDump.h"
#include "SceneLadder.h"
#include "StandalonePlatform.h"

// The streamed frame's decoder (docs/CyclesIntegration.md sec 7.1):
// bimg's vendored stb_image, compiled once for every tier in
// ImageDecode.cpp (the texture transport reads through it too).
#define STBI_NO_STDIO
#include <stb/stb_image.h>

static std::unique_ptr<Render::Renderer> s_renderer;
static Render::SceneSnapshot s_snap;
/// The objects this viewer holds, carried across publishes: a delta
/// names only what changed, and the rest of the scene has to come from
/// somewhere (SceneDump.h). Reset whenever the stream restarts, so a
/// reconnect cannot apply a delta onto a model from a previous run.
static Render::SceneObjectModel s_objects;
/// Textures by content key across publishes (SceneSnapshot::textureMemo).
static std::shared_ptr<Render::SceneSnapshot::TextureMemo> s_textureMemo =
    std::make_shared<Render::SceneSnapshot::TextureMemo>();
static bool s_haveScene = false;

/// The plan is what there is instead of the released/refused memos,
/// the armed flag and the rest of the damping the reactive selection
/// accumulated (docs/SceneStreaming.md §7, "Selection is a plan, not a
/// reaction"): Render::planLevels decides once, on events, what every
/// object should hold, and the executor in resolvePending only diffs
/// resident against target. Stale is set by the events — a publish
/// staged, the ladder set growing, the camera settling after a move,
/// the adaptive budget moving materially — and the next round plans
/// before it executes.
static bool s_planStale = true;
/// What the last plan reported: objects the budget held below their
/// desired rung (the honest meaning of "at the geometry budget"), the
/// bytes it targets, and the budget it was drawn against — the last
/// one so a materially different budget can re-plan.
static size_t s_planCapped = 0;
static size_t s_plannedBytes = 0;
static size_t s_plannedBudget = 0;

/// Deltas held back rather than answered with a full root. A delta
/// blocked by manifests still in flight (the gate in
/// applyScenePayload) is not bad news — it is EARLY news on a slow
/// link, where announcements can arrive faster than the manifests of
/// the publish before them resolve. Asking for a full root there set
/// off a livelock measured at 178 "Preparing scene…" rounds on a
/// phone: while the root was in flight every further announcement
/// mis-based against the stale model and asked for another. So a
/// blocked delta waits its turn instead: held in arrival order, tiny
/// by construction, drained the moment the blocking manifests land.
/// The full root remains the fallback, once, if the hold outlives its
/// grace — and only one full-root request may ever be in flight.
struct HeldPayload {
    uint64_t version = 0;
    double heldAt = 0.0;
    std::vector<char> bytes;
};
static std::deque<HeldPayload> s_heldPayloads;
/// Generous: an announcement chain is one delta per publish batch and
/// a delta is a few kilobytes, so a long chain over a slow link is
/// exactly the case the hold exists for. Overflow is the last resort.
static const size_t kHeldPayloadMax = 128;
/// Long, deliberately: it only starts counting once NOTHING is
/// filling, and on a link slow enough to starve a 30 s batch timeout
/// the gaps between batch arrivals alone can span many seconds. A held
/// announcement costs nothing while it waits; a premature full root
/// costs the whole manifest layer again.
static const double kHeldGraceMs = 15000.0;
/// When ANY payload last filled. The grace measures a STALL, not
/// slowness: a cold load over a slow link keeps making progress for
/// minutes — first manifests, then geometry — and lapsing to a full
/// root while anything at all is landing restarts the very download
/// that was moving. A held announcement is cosmetic; waiting out a
/// whole slow load costs nothing but the upgrade it carries.
static double s_lastFillAt = 0.0;
static bool s_fullSceneInFlight = false;
static bool s_heldDrainScheduled = false;
/// Set while a held payload is being re-applied, so a re-block puts it
/// back at the FRONT — deltas chain, and order is the chain.
static bool s_drainingHeld = false;
static void drainHeldPayloads(void * = nullptr);
static void requestFullScene();
static void decLog(const char *fmt, ...);

/// The hold's watchdog: as long as the queue is moving, keep watching;
/// a front that sat through the whole grace is a stall, and the full
/// root is the honest way out of one. Re-arms itself while anything is
/// held, so a stall after early progress is still caught.
static void heldGraceCheck(void *)
{
    if (s_heldPayloads.empty())
        return;
    const double since = std::max(s_heldPayloads.front().heldAt,
                                  s_lastFillAt);
    const double age = emscripten_get_now() - since;
    if (age < kHeldGraceMs) {
        emscripten_async_call(heldGraceCheck, nullptr,
                              int(kHeldGraceMs - age) + 100);
        return;
    }
    decLog("held deltas outlived their grace -> full scene");
    s_heldPayloads.clear();
    requestFullScene();
}

/// The decision journal: every plan, fetch, release, generate, publish
/// merge and full-scene request, with its reason, timestamped. Always
/// recorded — a few thousand short lines is nothing next to one mesh —
/// because the whole point is reading it *after* something looked
/// wrong on a device with no console. `?decisions` mirrors it to the
/// console live; the backend pulls it any time over the control
/// channel ({"cmd":"dumpDecisions"} → an 'L' frame, GET /decisions on
/// the scene server).
static std::deque<std::string> s_decisionLog;
static const size_t kDecisionLogMax = 8192;
static bool s_decisions = false;

static void decLog(const char *fmt, ...)
{
    char buf[240];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    char line[280];
    std::snprintf(line, sizeof(line), "%10.1f %s",
                  emscripten_get_now(), buf);
    if (s_decisionLog.size() >= kDecisionLogMax)
        s_decisionLog.pop_front();
    s_decisionLog.emplace_back(line);
    if (s_decisions)
        std::printf("fcviewer:dec %s\n", buf);
}
static std::set<int> s_selIds;
static std::set<int> s_overlayIds;

// Live streaming (?scene=<http://host:port> page parameter): connect a
// WebSocket to the desktop scene server (FC_BGFX_SERVE_SCENE) and
// re-apply the feeds on every pushed version; fall back to HTTP polling
// where WebSocket fails.
static std::string s_sceneUrl;
// The served document this viewer wants (docs/MultiDocServe.md §4/§6):
// ?doc= at load, updated by a menu switch. Empty = the backend's
// default document. Carried in the hello, on the upgrade request (so a
// reconnect rejoins before the first push) and on every /scene and
// /level fetch.
static std::string s_docName;
// ?client= — this connection's display label; ?token= — the shared
// secret a gated backend checks (§8 stage 3e). Both just pass through
// to the hello.
static std::string s_clientLabel;
static std::string s_tokenParam;

/// Percent-encode \a s for a query-string value.
static std::string urlEncode(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-'
                || c == '_' || c == '.' || c == '~') {
            out += c;
        }
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%%%02X",
                          static_cast<unsigned char>(c));
            out += buf;
        }
    }
    return out;
}

/// The document key every scene-server URL carries, appendable to a
/// query string that already started; empty for the default document.
static std::string docQuery()
{
    return s_docName.empty() ? std::string()
                             : "&doc=" + urlEncode(s_docName);
}

/// The door token every scene-server request carries when the backend
/// is gated (docs/MultiDocServe.md §8): the hello alone is not enough
/// once every endpoint checks it — the polling /scene, the blob and
/// level fetches and the upgrade request all need it. Appendable to a
/// query string that already started.
static std::string tokenQuery()
{
    return s_tokenParam.empty() ? std::string()
                                : "&token=" + urlEncode(s_tokenParam);
}

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

// The page's own origin, for the same-origin scene default — null off
// http(s) (file://, about:) where there is nothing to stream from.
EM_JS(char *, fcviewer_page_origin, (), {
    var o = window.location.origin || '';
    if (o.indexOf('http') != 0)
        return 0;
    var len = lengthBytesUTF8(o) + 1;
    var s = _malloc(len);
    stringToUTF8(o, s, len);
    return s;
});

// One page query parameter by name (decoded), null when absent.
EM_JS(char *, fcviewer_query_param, (const char *name), {
    var p = new URLSearchParams(window.location.search)
        .get(UTF8ToString(name));
    if (!p)
        return 0;
    var len = lengthBytesUTF8(p) + 1;
    var s = _malloc(len);
    stringToUTF8(p, s, len);
    return s;
});

// The client name the menu stored for this browser, or null. Written by
// window.fcviewerSetClient (docs/MultiDocServe.md §6) — the share link
// carries no name, so this is what makes one survive a reload.
EM_JS(char *, fcviewer_stored_client, (), {
    var p = null;
    try { p = window.localStorage.getItem('fcviewer.client'); } catch (e) {}
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
    // The DOM layer, when one is loaded, shows this as a closable card
    // reached from the menu; it claims the feed by setting
    // fcviewerHudCard. Pages with no UI layer (fcviewer.html, a bundled
    // capture) keep the built-in box below — the HUD is the only thing
    // saying what the renderer is doing on a device with no console.
    window.dispatchEvent(new CustomEvent('fc:hud',
                                         { detail: s ? UTF8ToString(s) : null }));
    var el = document.getElementById('__hud');
    if (window.fcviewerHudCard) { if (el) el.style.display = 'none'; return; }
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
// Semantic-channel answers for the DOM layer (docs/ThinClient.md §3):
// each is re-dispatched as a window 'fc:control' CustomEvent so the
// control client correlates on its request id without touching WASM.
EM_JS(void, fcviewer_control_event, (const char *json), {
    try {
        var detail = JSON.parse(UTF8ToString(json));
        window.dispatchEvent(new CustomEvent('fc:control',
                                             { detail: detail }));
    } catch (e) {}
});

// The served-document listing (docs/MultiDocServe.md §4) for the DOM
// layer's menu: the docs push/reply, plus which document this viewer
// is on (what it asked for, else the backend's default).
EM_JS(void, fcviewer_docs_event, (const char *json, const char *current), {
    try {
        var detail = JSON.parse(UTF8ToString(json));
        var cur = UTF8ToString(current);
        detail.current = cur || detail['default'] || '';
        window.dispatchEvent(new CustomEvent('fc:docs',
                                             { detail: detail }));
    } catch (e) {}
});

// This connection's name (docs/MultiDocServe.md §6), pushed rather
// than read: the DOM layer mounts on its own schedule — often before
// this module runs at all — so a one-shot read at mount misses a name
// that came from ?client= or from the store. Mirrored on window too,
// for a panel that mounts after the push.
EM_JS(void, fcviewer_client_event, (const char *name), {
    window.fcviewerClient = UTF8ToString(name);
    window.dispatchEvent(new CustomEvent('fc:client',
                                         { detail: window.fcviewerClient }));
});

// This connection's access mode (docs/MultiDocServe.md §8): the host
// can make a viewer view-only at any time, and the DOM layer has to
// know — an inspector that still offers editable fields would collect
// values the backend then refuses. Also mirrored on window so a panel
// mounting after the push can read it.
EM_JS(void, fcviewer_viewonly_event, (int viewOnly), {
    window.fcviewerViewOnly = !!viewOnly;
    window.dispatchEvent(new CustomEvent('fc:viewonly',
                                         { detail: !!viewOnly }));
});

// The DOM layer's uplink, installed once at startup:
// window.fcviewerControlSend(jsonString) -> bool (false = socket down,
// caller shows its offline state rather than queueing).
EM_JS(void, fcviewer_install_control, (), {
    // The menu's HUD switch. Installed beside the control uplink because
    // it is the same kind of thing: a viewer state the DOM layer drives.
    window.fcviewerSetHud = function(on) { _fcviewer_set_hud(on ? 1 : 0); };
    // The served viewport (docs/CyclesIntegration.md sec 7.1): a device
    // name starts it, '' stops it, for one sub-view (cell 0 = the full
    // canvas, else a split cell's id); state arrives as 'fc:cycles'
    // events carrying the cell.
    window.fcviewerSetCycles = function(device, cell) {
        var d = device || '';
        var len = lengthBytesUTF8(d) + 1;
        var buf = _malloc(len);
        stringToUTF8(d, buf, len);
        _fcviewer_set_cycles(d ? 1 : 0, buf, (cell | 0));
        _free(buf);
    };
    // The selection menu (docs/ThinClientUI.md): mode 0 single / 1 multi;
    // filter 0 elements / 1 object / 2 face / 3 edge / 4 vertex.
    window.fcviewerSetSelMode = function(m) {
        _fcviewer_set_sel_mode(m | 0);
    };
    window.fcviewerSetPickFilter = function(f) {
        _fcviewer_set_pick_filter(f | 0);
    };
    // The split-view chrome's layout push (docs/SplitViews.md sec 9.4):
    // "id,x,y,w,h,p;..." in CSS px, "" = single full-canvas view.
    window.fcviewerSetLayout = function(spec) {
        var len = lengthBytesUTF8(spec) + 1;
        var buf = _malloc(len);
        stringToUTF8(spec, buf, len);
        _fcviewer_set_layout(buf);
        _free(buf);
    };
    // The menu's document switch (docs/MultiDocServe.md §6).
    window.fcviewerSwitchDoc = function(name) {
        var len = lengthBytesUTF8(name) + 1;
        var buf = _malloc(len);
        stringToUTF8(name, buf, len);
        _fcviewer_switch_doc(buf);
        _free(buf);
    };
    // The menu's "who am I" (docs/MultiDocServe.md §6): the label the
    // host's sharing roster names this connection by. Kept in
    // localStorage so the same browser keeps its name across reloads
    // and reconnects — a name typed once should not have to be typed
    // again, and the sharing link carries no name of its own.
    window.fcviewerSetClient = function(name) {
        try { window.localStorage.setItem('fcviewer.client', name); }
        catch (e) {}
        var len = lengthBytesUTF8(name) + 1;
        var buf = _malloc(len);
        stringToUTF8(name, buf, len);
        _fcviewer_set_client(buf);
        _free(buf);
    };
    window.fcviewerClientName = function() {
        return UTF8ToString(_fcviewer_client_name());
    };
    window.fcviewerControlSend = function(s) {
        var len = lengthBytesUTF8(s) + 1;
        var buf = _malloc(len);
        stringToUTF8(s, buf, len);
        var ok = _fcviewer_control_send(buf);
        _free(buf);
        return ok === 1;
    };
});

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

// Whether the last pointer to act was coarse (a fingertip) rather than fine
// (mouse or stylus). A fingertip covers several millimetres of glass and
// carries no cursor, so it cannot be aimed anywhere near as precisely; the
// pick radius follows from this (pickRadiusPx).
static bool s_coarsePointer = false;
// When a stylus was last seen hovering. A pen reaches this viewer as touch
// events (it is a touchscreen), so its taps would otherwise be treated as a
// fingertip's; a recent hover says the contact is a pen tip and can keep the
// fine radius.
static double s_penHoverMs = -1e9;

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

/// ?stream — report what each assembly pass could draw and how much of
/// it is still coarse, and what the budget is holding. Off by default:
/// this fires on every arrival, and a scene arrives in hundreds of
/// chunks. Declared here because the frame reports against it too.
static bool s_streamDebug = false;

/// Reconsider what to fetch when the camera has moved (defined with
/// the fetch order, called from the frame).
static void pumpFetchOnMove();
/// The resident geometry and the budget bounding it, in bytes, and a
/// heartbeat line reporting both against the heap they live in — all
/// defined with the resident set (docs/SceneStreaming.md §6, 4b) and
/// wanted here, where the frame can show and say them.
static size_t residentBytes();
static size_t geometryBudget();
static size_t blobCacheBytes();
static void reportHeap();

static void logStandIns(const Render::SceneSnapshot &snap);

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
/// The apply broken down, because "apply is the cost" is not yet an
/// answer to what to make incremental (docs/SceneStreaming.md §6): the
/// manifest pass, the feed rebuild, handing the feed to the backend,
/// the overlays and the selection replay are five different fixes.
static double s_dbgFinalizeMs = 0.0;
static double s_dbgObjectsMs = 0.0;
static double s_dbgSceneMs = 0.0;
static double s_dbgOverlayMs = 0.0;
static double s_dbgSelMs = 0.0;
/// Frames, and how much of them followed a feed change: a streamed
/// load's cost is as likely to be in the frame that shows the new feed
/// as in the pass that built it.
static double s_dbgFrameMs = 0.0;
static int s_dbgFrames = 0;
static double s_dbgFeedFrameMs = 0.0;
static int s_dbgFeedFrames = 0;
static bool s_feedJustSet = false;

/// Where a streamed load spent itself, as far as the viewer can see:
/// the passes that resolved and assembled it, and the frames that
/// showed the result.
static void dbgReport()
{
    std::printf("fcviewer: DBG %d resolve+commit passes, %.0f ms "
                "total, %.0f ms of it in apply "
                "(finalize %.0f, objects %.0f, scene %.0f, "
                "overlays %.0f, selection %.0f); "
                "%d frames %.0f ms, %d of them after a feed change "
                "%.0f ms\n",
                s_dbgN, s_dbgMs, s_dbgApplyMs, s_dbgFinalizeMs,
                s_dbgObjectsMs, s_dbgSceneMs, s_dbgOverlayMs, s_dbgSelMs,
                s_dbgFrames, s_dbgFrameMs, s_dbgFeedFrames,
                s_dbgFeedFrameMs);
}

/// Charge the time this scope takes to \a acc.
struct DbgScope {
    double *acc;
    double t0;
    explicit DbgScope(double &a)
        : acc(&a), t0(emscripten_get_now())
    {}
    ~DbgScope() { *acc += emscripten_get_now() - t0; }
};

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

// ---- Split-view layout (docs/SplitViews.md sec 9.1/9.3) ------------
//
// The DOM chrome tiles the canvas into cells and pushes their rects
// through fcviewer_set_layout; each cell shows the RESIDENT scene (a
// 3D camera of its own) or the RESIDENT page (a pan/zoom of its own).
// The ACTIVE cell's state lives in the ordinary globals (s_yaw,
// s_pageView, ...) exactly as without a layout -- switching the active
// cell stashes them into its entry and loads the next one's -- so the
// whole input/fit/pick machinery keeps reading the globals it always
// read. An empty list is "no layout": today's single full-canvas view.
struct WasmSubView {
    int id = 0;               ///< stable chrome-issued token (>= 1)
    bool page = false;        ///< content: the page store vs the scene
    float x = 0, y = 0, w = 0, h = 0;   ///< rect, CSS px on the canvas
    // Stashed orbit camera (inactive cells only; the active cell's is
    // live in the globals).
    float center[3] = {0.0f, 0.0f, 0.0f};
    float panX = 0.0f, panY = 0.0f;
    float yaw = 0.785f, pitch = 0.5f, roll = 0.0f;
    float dist = 10.0f;
    bool userCam = false;
    // Stashed page view.
    Render::Page2D::View pageView;
    bool pageUserView = false;
};
static std::vector<WasmSubView> s_subViews;
static int s_activeSub = 0;

/// The active viewport rect in device px: the active cell's, or the
/// whole canvas without a layout. Every screen-space computation --
/// rays, aspect, fits, pan scale -- reads these so it is right in both
/// worlds.
static float vpX()
{ return s_subViews.empty() ? 0.0f : s_subViews[s_activeSub].x * s_dpr; }
static float vpY()
{ return s_subViews.empty() ? 0.0f : s_subViews[s_activeSub].y * s_dpr; }
static float vpW()
{
    return s_subViews.empty() ? float(s_width)
                              : s_subViews[s_activeSub].w * s_dpr;
}
static float vpH()
{
    return s_subViews.empty() ? float(s_height)
                              : s_subViews[s_activeSub].h * s_dpr;
}
/// Which cell a device-px canvas point is in; the current active cell
/// when it is in none (a border, a rounding seam).
static int cellIndexAt(float px, float py)
{
    for (size_t i = 0; i < s_subViews.size(); ++i) {
        const WasmSubView &c = s_subViews[i];
        if (px >= c.x * s_dpr && px < (c.x + c.w) * s_dpr
                && py >= c.y * s_dpr && py < (c.y + c.h) * s_dpr)
            return int(i);
    }
    return s_activeSub;
}

/// World-space ray through a canvas pixel of the current camera.
static void screenRay(float px, float py, bx::Vec3 &orig, bx::Vec3 &rdir)
{
    CamFrame f = camFrame();
    const float vw = vpW(), vh = vpH();
    const float aspect = vh > 0.0f ? vw / vh : 1.0f;
    const float th = std::tan(0.5f * kFovY * bx::kPi / 180.0f);
    const float nx = vw > 0.0f ? 2.0f * (px - vpX()) / vw - 1.0f : 0.0f;
    const float ny = vh > 0.0f ? 1.0f - 2.0f * (py - vpY()) / vh : 0.0f;
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

static void buildCamera(float *viewMtx, float *projMtx,
                        int vw = 0, int vh = 0)
{
    CamFrame f = camFrame();
    // Right-handed like the Coin camera the renderer's shading assumes
    // (camera forward = -z in view space; bx defaults to left-handed). Use the
    // frame's own up so camera roll is reflected in the view matrix.
    bx::mtxLookAt(viewMtx, f.eye, f.at, f.up, bx::Handedness::Right);

    // Aspect of the viewport this camera draws: an explicit sub-view
    // rect when given (renderLayoutFrame), the canvas otherwise.
    if (vw <= 0 || vh <= 0) {
        vw = s_width;
        vh = s_height;
    }
    const float aspect = vh > 0 ? float(vw) / float(vh) : 1.0f;
    // Fit the depth range to the scene instead of standing well clear of
    // it. 4 * diag put the far plane eight bounding-radii out and let the
    // near plane clamp to 0.001 * diag, which on the rack model is a
    // 5163:1 range -- and the near plane is what depth resolution is
    // proportional to. That bought a depth LSB of 0.147 world units at
    // the geometry, so any two surfaces flush against each other (a plate
    // mounted on a panel: everywhere, in an assembly) were within one LSB
    // and the buffer could not say which was in front. Parts INSIDE the
    // model bled through the panels covering them, and did it more the
    // further away the camera stood, because the LSB grows with distance
    // -- so backing off appeared to add detail and approaching appeared
    // to lose it, when what was actually happening is that the far view
    // was drawing things it should have hidden.
    //
    // 0.75 * diag is a bounding sphere of one and a half radii: still
    // slack, and it takes the LSB to ~0.0003 units. A camera closer to
    // the scene than that clamps to a fraction of its own distance, which
    // is the case where the geometry is near the eye and precision is
    // plentiful anyway.
    const float neard = bx::max(0.002f * s_dist, s_dist - 0.75f * s_diag);
    const float fard = s_dist + 0.75f * s_diag;
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
    sx = vpX() + (nx + 1.0f) * 0.5f * vpW();
    sy = vpY() + (1.0f - ny) * 0.5f * vpH();
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

/// The screen-space slack an edge or vertex pick is allowed, in canvas
/// (drawing-buffer) pixels.
///
/// Two corrections to the streamed value, which is the desktop's
/// ViewParams::PickRadius — CSS pixels, sized for a mouse cursor on a
/// ratio-1 screen:
///  * scale to the buffer by the device pixel ratio, or the radius shrinks
///    with every extra pixel of density (5px is under 2 CSS px on a phone);
///  * widen it for a fingertip, which is ~9mm of contact with no cursor to
///    aim by. 15 CSS px (a 30px target) is about 4mm — well inside the
///    contact patch, so it does not pull in elements the finger never
///    covered, and still several times what a mouse gets.
/// A stylus stays on the fine radius: it has a tip and a hover state.
static float pickRadiusPx()
{
    const float base = s_snap.preselconf.pickRadius > 0.5f
        ? s_snap.preselconf.pickRadius : 5.0f;
    const float css = s_coarsePointer ? std::max(base, 15.0f) : base;
    return css * std::max(1.0f, s_dpr);
}

// Selection mode and pick filter, driven by the DOM layer's selection
// menu (docs/ThinClientUI.md). Multi mode makes every plain click a
// Ctrl-click; the filter restricts what a pick may land on (and what
// hover preselects). Object mode picks any element but selects — and
// preselects — the whole object.
enum PickFilter {
    FilterElements = 0,  ///< any sub-element (the default)
    FilterObject   = 1,  ///< whole objects only
    FilterFace     = 2,
    FilterEdge     = 3,
    FilterVertex   = 4,
};
static int s_selMode = 0;               // 0 = single, 1 = multi
static int s_pickFilter = FilterElements;

static bool pickFilterAllows(PickKind k)
{
    switch (s_pickFilter) {
    case FilterFace:   return k == PickFace;
    case FilterEdge:   return k == PickEdge;
    case FilterVertex: return k == PickVertex;
    default:           return true;
    }
}

/// Nearest face (exact ray/triangle), edge and vertex (screen-space proximity
/// within the pick radius) of the draw scene at canvas pixel (px, py), then
/// resolve by the desktop's vertex > edge > face priority — a higher-priority
/// element wins only when its hit is essentially as near the eye as the
/// frontmost (SoFCUnifiedSelection::postProcessPickedList). Draw kinds the
/// pick filter forbids are not considered at all.
static PickHit pickScene(float px, float py)
{
    bx::Vec3 orig(bx::InitZero), rdir(bx::InitZero);
    screenRay(px, py, orig, rdir);
    const CamFrame f = camFrame();
    const bx::Vec3 fwd = bx::normalize(bx::sub(f.at, f.eye));
    const float aspect = vpH() > 0.0f ? vpW() / vpH() : 1.0f;
    const float th = std::tan(0.5f * kFovY * bx::kPi / 180.0f);
    const float radius = pickRadiusPx();
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
            if (!pickFilterAllows(PickFace))
                continue;
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
            if (!pickFilterAllows(PickEdge))
                continue;
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
            if (!pickFilterAllows(PickVertex))
                continue;
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
    const int vx = int(vpX()), vy = int(vpY());
    const int vw = int(vpW()), vh = int(vpH());
    if (a.corner == Render::OverlayAnchor::FullViewport) {
        rx = vx;
        ry = vy;
        rw = vw;
        rh = vh;
        return;
    }
    int edge = int(std::max(1.0f,
        a.sizeFraction * float(std::min(vw, vh))));
    rw = rh = edge;
    const bool right = a.corner == Render::OverlayAnchor::BottomRight
        || a.corner == Render::OverlayAnchor::TopRight;
    const bool top = a.corner == Render::OverlayAnchor::TopLeft
        || a.corner == Render::OverlayAnchor::TopRight;
    const int mx = int(a.marginX), my = int(a.marginY);
    rx = vx + std::max(0, right ? vw - edge - mx : mx);
    ry = vy + std::max(0, top ? my : vh - edge - my);
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
    if (s_pickFilter == FilterObject) {
        // The filter says objects: preselect highlights the WHOLE object
        // the hit belongs to — every pickable draw of it, whole range.
        if (dc.objectKey == s_hoverKey && s_hoverPart == -1
                && s_hoverKind == PickNone)
            return;
        s_hoverKey = dc.objectKey;
        s_hoverPart = -1;
        s_hoverKind = PickNone;
        Render::DrawCallList draws;
        for (const auto &odc : s_snap.scene) {
            if (!odc.mesh || odc.objectKey != dc.objectKey
                    || kindForDraw(odc) == PickNone)
                continue;
            draws.push_back(buildHiliteDraw(odc, -1, 0, 0,
                                            s_snap.preselconf));
        }
        s_renderer->setHighlight(std::move(draws), false);
        return;
    }
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

// kind == PickNone (with part == -1) means the WHOLE object is selected:
// every pickable draw of that object highlights, and the DOM event carries
// an empty `sub`. Sub-element items carry the element kind and part index.
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
            if (it.key != dc.objectKey)
                continue;
            // A whole-object item matches every pickable draw of the object.
            if (it.kind != PickNone && it.kind != dcKind)
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

EM_JS(void, fcviewer_selection_event, (const char *json), {
    try {
        var detail = JSON.parse(UTF8ToString(json));
        window.dispatchEvent(new CustomEvent('fc:selection',
                                             { detail: detail }));
    } catch (e) {}
});

static void jsonEscapeTo(std::string &out, const std::string &s)
{
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (uint8_t(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else out += c;
    }
}

/// Tell the DOM layer what is selected (docs/ThinClient.md §3): a
/// 'fc:selection' CustomEvent on window whose detail is the list of
/// selected items with their resolved identity from the object entries
/// (v38 — empty strings on older backends or unnamed draws). `sub` is
/// the FreeCAD sub-element name reconstructed from the pick
/// (Face3/Edge1/...), empty for a whole-object selection.
static void emitSelectionEvent()
{
    // The scene's home document: where most named objects live. An
    // item from any other document is flagged so the UI shows its
    // doc-qualified path (external links); home objects stay short.
    std::string homeDoc;
    {
        std::map<std::string, int> counts;
        int best = 0;
        for (const auto &v : s_objects.objects) {
            const auto &d = v.second.entry.info.doc;
            if (d.empty())
                continue;
            int n = ++counts[d];
            if (n > best) {
                best = n;
                homeDoc = d;
            }
        }
    }
    std::string json = "[";
    for (const auto &it : s_sel) {
        if (json.size() > 1)
            json += ',';
        char head[64];
        std::snprintf(head, sizeof(head), "{\"objectKey\":\"%llx\"",
                      (unsigned long long)it.key);
        json += head;
        static const char *kindName[] = {"", "Face", "Edge", "Vertex"};
        if (it.part >= 0 && it.kind != PickNone) {
            char sub[32];
            std::snprintf(sub, sizeof(sub), ",\"sub\":\"%s%d\"",
                          kindName[it.kind], it.part + 1);
            json += sub;
        }
        else
            json += ",\"sub\":\"\"";
        auto oit = s_objects.objects.find(it.key);
        if (oit != s_objects.objects.end()) {
            const auto &info = oit->second.entry.info;
            json += ",\"doc\":\"";   jsonEscapeTo(json, info.doc);
            json += "\",\"obj\":\""; jsonEscapeTo(json, info.obj);
            json += "\",\"label\":\""; jsonEscapeTo(json, info.label);
            json += "\",\"type\":\""; jsonEscapeTo(json, info.type);
            json += '"';
            json += info.doc == homeDoc ? ",\"home\":true"
                                        : ",\"home\":false";
        }
        json += '}';
    }
    json += ']';
    fcviewer_selection_event(json.c_str());
}

/// Client-side select at canvas pixel: pick locally, update s_sel, and show
/// the highlight immediately.
///
/// Ctrl = multi-select: toggles the picked sub-element in/out of the set
/// (adding a sub-element drops a whole-object item of the same object — the
/// two are mutually exclusive). Shift (with or without Ctrl) selects the
/// WHOLE object outright: the picked object's element items are dropped and
/// its whole-object item put in their place, other objects' selections kept
/// — the explicit promotion, needed because under Ctrl a re-click means
/// deselect. Plain click replaces the selection and cycles: picking an
/// already-selected sub-element promotes to the whole object; picking any
/// sub-element of a whole-selected object narrows back to that sub-element.
///
/// The pick DOES go up to the backend (sendPick below), and everything drawn
/// here is a prediction of what it will answer (docs/ThinClient.md sec 8.6).
/// This used to be client-side only: an eager sync made the backend re-pick
/// and republish the whole scene, and that echo stalled right after the
/// instant local highlight. What changed is the server side -- deltas, a
/// woken writer, and a source that publishes on a selection instead of on a
/// clock -- so the round trip is now single-digit milliseconds and the echo
/// is merged rather than applied: applySnapshot drops the streamed selection
/// and re-applies this one. The ruling is sec 8.2a's: a click round-trips
/// because selection feeds the tree, the property panel and everything
/// behind Gui::Selection(); a hover never does.
static void sendPick(float px, float py, bool ctrl);

static void selectAt(float px, float py, bool ctrl, bool shift = false)
{
    // Up it goes, whether or not anything was hit: a plain click on
    // nothing clears the selection, and the server has to hear that too.
    // The bit the wire carries is "extend rather than replace", which is
    // what sticky-multi and Shift both are; the finer grammar this client
    // applies below -- promoting to the whole object, the pick filter --
    // has no wire form yet (docs/ThinClient.md sec 8.5).
    sendPick(px, py, ctrl || s_selMode == 1 || shift);

    PickHit hit = pickScene(px, py);
    if (hit.draw < 0) {
        if (!ctrl && !shift && !s_sel.empty()) {
            s_sel.clear();
            rebuildSelection();
            emitSelectionEvent();
        }
        return;
    }
    const auto &dc = s_snap.scene[size_t(hit.draw)];
    int partStart, partCount;
    int part = partForHit(dc, hit, partStart, partCount);
    SelItem item{dc.objectKey, hit.kind, part};
    if (s_pickFilter == FilterObject)
        item = SelItem{dc.objectKey, PickNone, -1};
    // Multi mode is a sticky Ctrl: every plain click extends/toggles.
    const bool multi = ctrl || s_selMode == 1;
    auto same = [&](const SelItem &s) {
        return s.key == item.key && s.kind == item.kind
            && s.part == item.part;
    };
    auto wholeOfObject = [&](const SelItem &s) {
        return s.key == item.key && s.kind == PickNone;
    };
    auto ofObject = [&](const SelItem &s) { return s.key == item.key; };
    if (shift) {
        s_sel.erase(std::remove_if(s_sel.begin(), s_sel.end(), ofObject),
                    s_sel.end());
        s_sel.push_back(SelItem{dc.objectKey, PickNone, -1});
    }
    else if (multi) {
        auto it = std::find_if(s_sel.begin(), s_sel.end(), same);
        if (it != s_sel.end())
            s_sel.erase(it);
        else {
            // A whole-object item and this object's element items are
            // mutually exclusive, whichever way round the add goes.
            if (item.kind == PickNone)
                s_sel.erase(std::remove_if(s_sel.begin(), s_sel.end(),
                                           ofObject),
                            s_sel.end());
            else
                s_sel.erase(std::remove_if(s_sel.begin(), s_sel.end(),
                                           wholeOfObject),
                            s_sel.end());
            s_sel.push_back(item);
        }
    }
    else {
        const bool hadSub = std::any_of(s_sel.begin(), s_sel.end(), same);
        const bool hadWhole = std::any_of(s_sel.begin(), s_sel.end(),
                                          wholeOfObject);
        s_sel.clear();
        if (hadSub && !hadWhole)
            s_sel.push_back(SelItem{dc.objectKey, PickNone, -1});
        else
            s_sel.push_back(item);
    }
    rebuildSelection();
    emitSelectionEvent();
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
//////////////////////////////////////////////////////////////////////
// The served viewport (docs/CyclesIntegration.md sec 7.1): the backend
// path traces this viewer's view and streams the frame; it is blitted
// here through the same FrameConsumer route a desktop Cycles view uses,
// with the engine drawing only depth, lines and selection over it. One
// per SUB-VIEW: the full canvas is cell 0, a split layout's cells are
// their chrome ids -- each traced from its own camera by a session of
// its own, its consumer registered under its sub-view id so the blit
// lands in that cell's bank alone.

static void stashSubCam(WasmSubView &c);
static void loadSubCam(const WasmSubView &c);

struct CyclesCell {
    int cell = 0;                 ///< sub-view id; 0 = the full canvas
    std::unique_ptr<Render::FrameImageConsumer> blit;
    bool on = false;              ///< asked for (until told otherwise)
    bool attached = false;        ///< consumer registered on the renderer
    std::string device;
    std::string status;
    std::string error;
    float progress = 0.0f;
    uint32_t frames = 0;
    int w = 0;
    int h = 0;
    float sentView[16];
    float sentProj[16];
    int sentW = -1;
    int sentH = -1;
    uint32_t pendingStart = 0;    ///< id of the start op awaiting its answer
};
static std::vector<CyclesCell> s_cyclesCells;
static std::string s_cyclesUrlDevice;  ///< ?cycles=<device>: start once the scene is up
/// The ids of the viewer's own ops on the control lane, above anything
/// the DOM layer mints (control.ts counts from 1).
static uint32_t s_cyclesOpSeq = 0;

EM_JS(void, fcviewer_cycles_event, (const char *json), {
    try {
        var detail = JSON.parse(UTF8ToString(json));
        window.fcviewerCycles = window.fcviewerCycles || {};
        window.fcviewerCycles[detail.cell] = detail;
        window.dispatchEvent(new CustomEvent('fc:cycles', { detail: detail }));
    } catch (e) {}
});

static CyclesCell *cyclesCellFor(int cell, bool create)
{
    for (auto &c : s_cyclesCells)
        if (c.cell == cell)
            return &c;
    if (!create)
        return nullptr;
    s_cyclesCells.emplace_back();
    s_cyclesCells.back().cell = cell;
    return &s_cyclesCells.back();
}

static void cyclesEvent(const CyclesCell &c)
{
    std::string json = "{\"cell\":" + std::to_string(c.cell) + ",\"on\":";
    json += c.on ? "true" : "false";
    json += ",\"device\":\"";
    jsonEscapeTo(json, c.device);
    json += "\",\"progress\":" + std::to_string(c.progress);
    json += ",\"status\":\"";
    jsonEscapeTo(json, c.status);
    json += "\",\"error\":\"";
    jsonEscapeTo(json, c.error);
    json += "\",\"width\":" + std::to_string(c.w)
        + ",\"height\":" + std::to_string(c.h)
        + ",\"frames\":" + std::to_string(c.frames) + "}";
    fcviewer_cycles_event(json.c_str());
}

static void cyclesDetach(CyclesCell &c)
{
    if (c.attached && s_renderer) {
        s_renderer->setExternalBaseLayer(false, c.cell);
        s_renderer->setFrameConsumer(nullptr, c.cell);
    }
    c.attached = false;
    c.blit.reset();
    markDirty();
}

/// Register the cell's blit as its frame's base layer. Needs the
/// device up (the first render() brings it up) and, for a layout cell,
/// its bank -- so the frame path retries.
static void cyclesAttach(CyclesCell &c)
{
    if (c.attached || !s_renderer)
        return;
    if (!c.blit)
        c.blit = std::make_unique<Render::FrameImageConsumer>();
    s_renderer->setFrameConsumer(c.blit.get(), c.cell);
    if (!s_renderer->frameConsumerSurface(c.cell)) {
        s_renderer->setFrameConsumer(nullptr, c.cell);
        return;
    }
    s_renderer->setExternalBaseLayer(true, c.cell);
    c.attached = true;
    markDirty();
}

static void appendMatrix(std::string &out, const float *m)
{
    char buf[32];
    out += '[';
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof(buf), "%s%.8g", i ? "," : "", double(m[i]));
        out += buf;
    }
    out += ']';
}

static void appendCameraFields(std::string &out, int cell, const float *view,
                               const float *proj, int w, int h)
{
    out += "\"cell\":" + std::to_string(cell) + ",\"view\":";
    appendMatrix(out, view);
    out += ",\"proj\":";
    appendMatrix(out, proj);
    out += ",\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h);
}

static void rememberCyclesCamera(CyclesCell &c, const float *view, const float *proj,
                                 int w, int h)
{
    std::memcpy(c.sentView, view, sizeof(c.sentView));
    std::memcpy(c.sentProj, proj, sizeof(c.sentProj));
    c.sentW = w;
    c.sentH = h;
}

/// The camera op, sent when it differs from the last one sent: at most
/// once per frame this viewer draws, which is the throttle.
static void sendCyclesCamera(CyclesCell &c, const float *view, const float *proj,
                             int w, int h)
{
    if (!s_wsOpen || s_ws <= 0)
        return;
    if (w == c.sentW && h == c.sentH
            && std::memcmp(view, c.sentView, sizeof(c.sentView)) == 0
            && std::memcmp(proj, c.sentProj, sizeof(c.sentProj)) == 0)
        return;
    std::string msg = "{\"op\":\"cycles.camera\",";
    appendCameraFields(msg, c.cell, view, proj, w, h);
    msg += '}';
    emscripten_websocket_send_utf8_text(s_ws, const_cast<char *>(msg.c_str()));
    rememberCyclesCamera(c, view, proj, w, h);
}

/// Per drawn frame of one sub-view (the single view is cell 0): the
/// camera to the backend and the consumer onto the renderer.
static void cyclesFrame(int cell, const float *view, const float *proj, int w, int h)
{
    CyclesCell *c = cyclesCellFor(cell, false);
    if (!c || !c->on)
        return;
    sendCyclesCamera(*c, view, proj, w, h);
    if (!c->attached)
        cyclesAttach(*c);
}

/// The camera a start op states for \a cell: the live one for the
/// single view or the active cell, a stashed cell's loaded for the
/// build and put back. False when the cell is not a 3D cell.
static bool cyclesStartCamera(int cell, float *view, float *proj, int &w, int &h)
{
    if (cell == 0) {
        if (!s_subViews.empty())
            return false;
        buildCamera(view, proj);
        w = s_width;
        h = s_height;
        return true;
    }
    for (size_t i = 0; i < s_subViews.size(); ++i) {
        WasmSubView &c = s_subViews[i];
        if (c.id != cell || c.page)
            continue;
        w = int(c.w * s_dpr + 0.5f);
        h = int(c.h * s_dpr + 0.5f);
        if (int(i) == s_activeSub) {
            buildCamera(view, proj, w, h);
        }
        else {
            stashSubCam(s_subViews[s_activeSub]);
            loadSubCam(c);
            buildCamera(view, proj, w, h);
            loadSubCam(s_subViews[s_activeSub]);
        }
        return w > 0 && h > 0;
    }
    return false;
}

static void cyclesStop(CyclesCell &c, bool tell)
{
    if (tell && c.on && s_wsOpen && s_ws > 0) {
        std::string msg = "{\"op\":\"cycles\",\"id\":"
            + std::to_string(0x40000000u + (++s_cyclesOpSeq))
            + ",\"action\":\"stop\",\"cell\":" + std::to_string(c.cell) + "}";
        emscripten_websocket_send_utf8_text(s_ws, const_cast<char *>(msg.c_str()));
    }
    c.on = false;
    c.pendingStart = 0;
    c.error.clear();
    c.status.clear();
    c.progress = 0.0f;
    cyclesDetach(c);
}

/// Start (device non-empty) or stop the served viewport of one
/// sub-view. The DOM layer's fcviewerSetCycles and ?cycles= land here.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_set_cycles(int on, const char *device,
                                                         int cell)
{
    if (cell < 0 || cell > 255)
        return;
    if (!on) {
        CyclesCell *c = cyclesCellFor(cell, false);
        if (!c)
            return;
        cyclesStop(*c, true);
        std::printf("fcviewer: path tracing off (cell %d)\n", cell);
        cyclesEvent(*c);
        return;
    }
    CyclesCell &c = *cyclesCellFor(cell, true);
    if (!s_wsOpen || s_ws <= 0) {
        c.error = "not connected";
        cyclesEvent(c);
        return;
    }
    float view[16], proj[16];
    int w = 0, h = 0;
    if (!cyclesStartCamera(cell, view, proj, w, h)) {
        c.error = "no such 3D view";
        cyclesEvent(c);
        return;
    }
    c.device = device && *device ? device : "CPU";
    c.on = true;
    c.error.clear();
    c.status = "starting";
    c.progress = 0.0f;
    c.frames = 0;
    c.pendingStart = 0x40000000u + (++s_cyclesOpSeq);
    std::string msg = "{\"op\":\"cycles\",\"id\":" + std::to_string(c.pendingStart)
        + ",\"action\":\"start\",\"device\":\"";
    jsonEscapeTo(msg, c.device);
    msg += "\",";
    appendCameraFields(msg, cell, view, proj, w, h);
    msg += '}';
    emscripten_websocket_send_utf8_text(s_ws, const_cast<char *>(msg.c_str()));
    rememberCyclesCamera(c, view, proj, w, h);
    std::printf("fcviewer: path tracing on (%s, cell %d)\n", c.device.c_str(), cell);
    cyclesEvent(c);
}

/// A layout is about to replace the cells: a traced cell that is not
/// in \a next stops (its bank goes with it), and the full canvas stops
/// when cells appear -- it is not drawn under a layout -- as every cell
/// does when the layout clears.
static void cyclesLayoutChanged(const std::vector<WasmSubView> &next)
{
    for (auto &c : s_cyclesCells) {
        if (!c.on)
            continue;
        bool kept;
        if (c.cell == 0) {
            kept = next.empty();
        }
        else {
            kept = false;
            for (const auto &n : next)
                kept = kept || (!n.page && n.id == c.cell);
        }
        if (!kept) {
            cyclesStop(c, true);
            cyclesEvent(c);
        }
    }
}

static int cyclesJsonInt(const char *json, const char *key, int fallback)
{
    const char *p = std::strstr(json, key);
    const char *colon = p ? std::strchr(p, ':') : nullptr;
    return colon ? int(std::strtol(colon + 1, nullptr, 10)) : fallback;
}

/// A control-lane text that is the served viewport's: the answer to
/// one of this viewer's own starts (by id) or an unsolicited event.
/// True when consumed here.
static bool handleCyclesControl(const char *json)
{
    if (std::strstr(json, "\"id\":")) {
        const uint32_t id = uint32_t(cyclesJsonInt(json, "\"id\"", 0));
        if (id < 0x40000000u)
            return false;
        for (auto &c : s_cyclesCells) {
            if (!c.pendingStart || c.pendingStart != id)
                continue;
            c.pendingStart = 0;
            if (std::strstr(json, "\"ok\":false")) {
                c.on = false;
                c.error = "refused";
                if (const char *m = std::strstr(json, "\"message\":\"")) {
                    const char *e = std::strchr(m + 11, '"');
                    if (e)
                        c.error.assign(m + 11, size_t(e - m - 11));
                }
                cyclesDetach(c);
                std::printf("fcviewer: path tracing refused (cell %d): %s\n", c.cell,
                            c.error.c_str());
                fcviewer_status(("Path tracing: " + c.error).c_str(), 0.0, -1.0);
            }
            cyclesEvent(c);
            return true;
        }
        return true;   // one of our own (a stop's ack)
    }
    if (std::strstr(json, "\"op\":\"cycles\"") && std::strstr(json, "\"event\":")) {
        if (std::strstr(json, "\"event\":\"error\"")
                || std::strstr(json, "\"event\":\"stopped\"")) {
            CyclesCell *c = cyclesCellFor(cyclesJsonInt(json, "\"cell\"", 0), false);
            if (c) {
                c->error = "stopped";
                if (const char *m = std::strstr(json, "\"message\":\"")) {
                    const char *e = std::strchr(m + 11, '"');
                    if (e)
                        c->error.assign(m + 11, size_t(e - m - 11));
                }
                std::string keep = c->error;
                cyclesStop(*c, false);
                c->error = keep;
                std::printf("fcviewer: path tracing ended (cell %d): %s\n", c->cell,
                            c->error.c_str());
                cyclesEvent(*c);
            }
        }
        return true;
    }
    return false;
}

/// One streamed frame off the socket: decode, stage into its cell's
/// blit, repaint.
static void handleStreamedFrame(const uint8_t *data, size_t size)
{
    Render::StreamedFrameHeader header;
    size_t imageAt = 0;
    if (!Render::readStreamedFrameHeader(data, size, header, imageAt))
        return;
    CyclesCell *c = cyclesCellFor(int(header.cell), false);
    if (!c || !c->on)
        return;   // a frame in flight after a stop
    if (header.format != Render::StreamedFrameHeader::JPEG)
        return;
    int w = 0, h = 0, n = 0;
    // The blit's texture is bottom-up like every image the engine
    // uploads; stb hands the file's top row first.
    stbi_set_flip_vertically_on_load(1);
    stbi_uc *px = stbi_load_from_memory(data + imageAt, int(size - imageAt), &w, &h, &n, 4);
    if (!px) {
        std::printf("fcviewer: streamed frame %u undecodable\n", header.sequence);
        return;
    }
    if (!c->blit)
        c->blit = std::make_unique<Render::FrameImageConsumer>();
    c->blit->setImage(px, w, h, Render::FrameImageConsumer::Format::RGBA8,
                      !(header.flags & Render::StreamedFrameHeader::Encoded));
    stbi_image_free(px);
    c->w = w;
    c->h = h;
    c->progress = header.progress;
    c->status = header.status;
    ++c->frames;
    markDirty();
    cyclesEvent(*c);
}

/// The HUD's line on the served viewport: the first traced view.
static const char *cyclesHudLine()
{
    static char line[96];
    for (const auto &c : s_cyclesCells) {
        if (!c.on)
            continue;
        std::snprintf(line, sizeof(line), "c%d %s %3d%% %dx%d f%u %.20s", c.cell,
                      c.device.c_str(), int(c.progress * 100.0f + 0.5f), c.w, c.h,
                      c.frames, c.status.c_str());
        return line;
    }
    std::snprintf(line, sizeof(line), "off");
    return line;
}

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
        "mem:  heap %5zu MB   geometry %4zu / %zu MB\n"
        "cam:  yaw %8.2f  pitch %7.2f  dist %9.2f\n"
        "pan:  %8.2f,%8.2f  ctr %7.1f,%7.1f,%7.1f\n"
        "hover: %-30.30s\n"
        "cyc:  %-40.40s\n"
        "%-46.46s\n"
        "[d] toggle HUD   [v] copy cam",
        fpsv, s_frameMs, s_renderMs,
        s_width, s_height, double(s_dpr), double(s_snap.effectResolution),
        __DATE__, __TIME__,
        rgba32f ? "fp32" : "fp16", r32f ? "fp32" : "fp16",
        // The wasm heap the browser has handed this page, which is
        // what an allocation failure runs out of — and beside it what
        // the geometry budget thinks it is holding, so the two can be
        // told apart at a glance on a device with no console.
        size_t(emscripten_get_heap_size()) >> 20,
        residentBytes() >> 20, geometryBudget() >> 20,
        s_yaw, s_pitch, s_dist, s_panX, s_panY,
        s_center[0], s_center[1], s_center[2], s_hoverDesc,
        cyclesHudLine(), s_camMsg);
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

/// Answer a {"cmd":"dumpDecisions"} control request: 'L', u32 request
/// id, u32 text length, then the journal as newline-joined text —
/// little-endian, mirroring the dumpFrame answer the server already
/// parses by leading tag.
static void sendDecisionLog(uint32_t id)
{
    if (!s_wsOpen)
        return;
    std::string text;
    for (const auto &line : s_decisionLog) {
        text += line;
        text += '\n';
    }
    std::vector<uint8_t> buf(1 + 4 + 4 + text.size());
    buf[0] = 'L';
    uint32_t v = id;
    std::memcpy(buf.data() + 1, &v, 4);
    v = uint32_t(text.size());
    std::memcpy(buf.data() + 5, &v, 4);
    std::memcpy(buf.data() + 9, text.data(), text.size());
    emscripten_websocket_send_binary(s_ws, buf.data(), buf.size());
    std::printf("fcviewer: decision log %u uploaded, %zu lines\n", id,
                s_decisionLog.size());
}

/// Set whenever a snapshot has been applied, because applying one
/// pushes the producer's OWN world-space light config (which is what
/// relightForCamera exists to correct). A scene is applied more often
/// than its version moves -- 383 applies against 143 versions on the
/// rack model -- so a correction that dedupes on the version alone
/// loses the race with every re-apply, and the scene goes dark again
/// for as long as the camera then sits still.
static bool s_relightDue = false;

/// Re-derive the camera-relative lights against THIS viewer's camera.
///
/// A headlight is fixed in EYE space. The world-space direction the
/// producer sent was unwound through the producer's viewing matrix
/// (ViewLight::eyeSpace), and it is only true for that camera -- this
/// viewer's is its own. Applied verbatim it lights the scene from
/// wherever the producer happened to be pointing, which for a headless
/// serving process is a default looking down -Z: the model's top faces
/// catch everything and every surface facing the viewer falls to
/// ambient. Exactly the reason setAutoZoomScale is recomputed here
/// instead of taken from the snapshot.
static void relightForCamera(const float *viewMtx)
{
    const Render::ViewLightConfig &src = s_snap.viewlightconf;
    bool any = false;
    for (int i = 0; i < src.count; ++i)
        any = any || src.lights[i].eyeSpace;
    if (!any)
        return;
    float inv[16];
    bx::mtxInverse(inv, viewMtx);
    Render::ViewLightConfig out = src;
    for (int i = 0; i < out.count; ++i) {
        Render::ViewLight &l = out.lights[i];
        if (!l.eyeSpace)
            continue;
        const bx::Vec3 d = bx::mulXyz0(
            bx::Vec3(l.eyeDirection[0], l.eyeDirection[1],
                     l.eyeDirection[2]), inv);
        const float len = bx::length(d);
        if (len > 0.0f) {
            l.direction[0] = d.x / len;
            l.direction[1] = d.y / len;
            l.direction[2] = d.z / len;
        }
        if (l.positional) {
            const bx::Vec3 p = bx::mul(
                bx::Vec3(l.eyePosition[0], l.eyePosition[1],
                         l.eyePosition[2]), inv);
            l.position[0] = p.x;
            l.position[1] = p.y;
            l.position[2] = p.z;
        }
    }
    // Re-send when the derived config moves, and whenever a snapshot has
    // been applied since the last send -- an apply pushes the producer's
    // own world-space config, so a still camera must not let this
    // conclude there is nothing left to correct.
    static Render::ViewLightConfig sent;
    static bool sentValid = false;
    if (sentValid && !s_relightDue && sent == out)
        return;
    s_relightDue = false;
    sent = out;
    sentValid = true;
    s_renderer->setViewLightConfig(out);
}

//////////////////////////////////////////////////////////////////////
// The 2D page tier (docs/TechDrawPortAndSection.md sec 24.4): a joined
// document group serving FCPD payloads flips the viewer into page
// mode -- the same vg engine the desktop runs (Page2D), driven
// directly on a backbuffer view with its own pan/zoom. The payload and
// chunk plumbing lives with the blob store further down; here is the
// state, the frame, and what the input handlers branch on.

static std::unique_ptr<Render::Page2D> s_page;
static bool s_pageMode = false;
static uint64_t s_pageVersion = 0;   // last applied page publish
static uint64_t s_pageSession = 0;
static float s_pageW = 0.0f;         // the sheet, Rez units
static float s_pageH = 0.0f;
static bool s_pageUserView = false;  // pan/zoom taken by the user
static Render::Page2D::View s_pageView;

/// What the stream last named per item / image, and who waits on which
/// chunk key. Font names are process-wide (Vg2D registry).
static std::map<uint64_t, std::string> s_pageItemKey;
static std::map<uint64_t, std::string> s_pageImageApplied;
struct PageImageMeta {
    uint16_t w = 0;
    uint16_t h = 0;
    bool repeat = false;
};
static std::map<uint64_t, PageImageMeta> s_pageImageMeta;
static std::map<std::string, std::set<uint64_t>> s_pageItemWanted;
static std::map<std::string, std::set<uint64_t>> s_pageImageWanted;
static std::map<std::string, std::string> s_pageFontWanted; // key -> name
static std::set<std::string> s_pageFontsApplied;

/// The paper sheet, drawn by this viewer (on desktop the Qt scene
/// paints it; the stream carries only what the page contains). Item
/// ids from the stream are FNV hashes; ~0 is not one of them.
static const uint64_t kPaperItemId = ~uint64_t(0);

// Defined with the blob store below.
static bool applyPagePayload(uint64_t version, const char *data,
                             size_t size);
static void pageBlobResolved(const std::string &key, const uint8_t *data,
                             size_t size);
static void pageBlobFailed(const std::string &key);

static void pageFit()
{
    const float vw = vpW(), vh = vpH();
    if (!(s_pageW > 0.0f) || !(s_pageH > 0.0f) || vw <= 0.0f
            || vh <= 0.0f)
        return;
    const float zoom = 0.95f * std::min(vw / s_pageW, vh / s_pageH);
    s_pageView.zoom = zoom;
    // Page content lives at y in [-H, 0] (page y-up): panY is the
    // screen y of page y = 0, so this centers the sheet. Pan is
    // viewport-local, so a page cell fits inside its own rect.
    s_pageView.panX = 0.5f * (vw - zoom * s_pageW);
    s_pageView.panY = zoom * s_pageH + 0.5f * (vh - zoom * s_pageH);
    s_pageView.rotation = 0.0f;
    markDirty();
}

static void pagePaper()
{
    if (!s_page || !(s_pageW > 0.0f))
        return;
    Render::Page2D::Recorder rec;
    rec.rect(0.0f, -s_pageH, s_pageW, s_pageH);
    rec.fillConvex(0xffffffff);
    s_page->setItem(kPaperItemId, Render::Page2D::Kind::Face, 0,
                    std::move(rec));
}

/// The page's chunk keys, for the store sweep's live set.
static void notePageLiveKeys(std::set<std::string> &live)
{
    for (const auto &kv : s_pageItemKey)
        live.insert(kv.second);
    for (const auto &kv : s_pageImageApplied)
        live.insert(kv.second);
    for (const auto &kv : s_pageItemWanted)
        live.insert(kv.first);
    for (const auto &kv : s_pageImageWanted)
        live.insert(kv.first);
}

/// The page-mode frame. False = not handled (not in page mode, or the
/// bgfx device is not up yet -- then one 3D frame runs and brings it
/// up; vg cannot initialize before it).
static bool renderPageFrame()
{
    if (!s_pageMode || !s_page)
        return false;
    if (bgfx::getRendererType() == bgfx::RendererType::Noop)
        return false;
    if (s_dirtyFrames > 0) {
        --s_dirtyFrames;
    }
    else {
        updateHud(true);
        s_lastFrameNow = 0.0;
        return true;
    }
    // A fixed id in the top granule the 3D renderer's block allocator
    // reserves for Page2D (BGFXRenderer.cpp, reserveBlock); its own
    // offscreen pair uses maxViews-2/-1, which never run in this tier.
    const uint16_t viewId =
        uint16_t(bgfx::getCaps()->limits.maxViews - 3);
    bgfx::FrameBufferHandle noFb = BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, noFb);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(s_width),
                      uint16_t(s_height));
    // The backdrop behind the sheet; the paper item paints the sheet.
    bgfx::setViewClear(viewId,
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                           | BGFX_CLEAR_STENCIL,
                       0x55585cff, 1.0f, 0);
    s_pageView.devicePixelRatio = s_dpr;
    s_page->setView(s_pageView);
    s_page->render(viewId, uint16_t(s_width), uint16_t(s_height));
    bgfx::touch(viewId);
    bgfx::frame();
    updateHud(false);
    return true;
}

// ---- Split-view layout: state switching and the layout frame ----------
// (docs/SplitViews.md sec 9.3; the early block above holds the cell
// list and the viewport accessors.)

/// Stash the live globals into a cell / load a cell into them. The
/// globals ARE the active cell's state; these run only on activation
/// changes and around the per-cell work in renderLayoutFrame.
static void stashSubCam(WasmSubView &c)
{
    std::memcpy(c.center, s_center, sizeof(c.center));
    c.panX = s_panX;
    c.panY = s_panY;
    c.yaw = s_yaw;
    c.pitch = s_pitch;
    c.roll = s_roll;
    c.dist = s_dist;
    c.userCam = s_userCam;
    c.pageView = s_pageView;
    c.pageUserView = s_pageUserView;
}

static void loadSubCam(const WasmSubView &c)
{
    std::memcpy(s_center, c.center, sizeof(s_center));
    s_panX = c.panX;
    s_panY = c.panY;
    s_yaw = c.yaw;
    s_pitch = c.pitch;
    s_roll = c.roll;
    s_dist = c.dist;
    s_userCam = c.userCam;
    s_pageView = c.pageView;
    s_pageUserView = c.pageUserView;
}

/// Whether pointer input is over page content: the active cell's
/// content with a layout, the wire-driven page mode without one.
static bool activeIsPage()
{
    if (!s_subViews.empty())
        return s_subViews[s_activeSub].page;
    return s_pageMode;
}

static void setActiveSub(int idx)
{
    if (s_subViews.empty() || idx == s_activeSub || idx < 0
            || idx >= int(s_subViews.size()))
        return;
    stashSubCam(s_subViews[s_activeSub]);
    s_activeSub = idx;
    loadSubCam(s_subViews[s_activeSub]);
}

/// Pointer-position activation (Blender's active-follows-cursor): the
/// cell under a press, a wheel, or a hover becomes the active one.
static void setActiveSubAt(float px, float py)
{
    if (!s_subViews.empty())
        setActiveSub(cellIndexAt(px, py));
}

/// One wall-clock frame of a split layout: page cells on the fixed
/// top-granule ids, 3D cells through renderSubViews -- every cell into
/// its own rect of the same backbuffer, one bgfx frame for all
/// (docs/SplitViews.md sec 9.2/9.3).
static void renderLayoutFrame()
{
    if (!s_renderer)
        return;
    stashSubCam(s_subViews[s_activeSub]);

    // Page2D's fixed id sits just under its offscreen pair
    // (renderPageFrame); further page cells walk down from it, still
    // inside the top granule the 3D block allocator never touches
    // (8 ids: the offscreen pair + up to 5 page cells + this base).
    const uint16_t pageBase =
        uint16_t(bgfx::getCaps()->limits.maxViews - 3);
    static const int kMaxPageCells = 5;
    int pageK = 0;
    bool anyPage = false;

    static std::vector<Render::Renderer::SubViewFrame> frames;
    static std::vector<std::array<float, 32>> mats;
    frames.clear();
    mats.clear();
    mats.reserve(s_subViews.size());
    int activeFrame = -1;

    // Two passes: the 3D cells' frames are built (and their banks
    // warmed) BEFORE any page cell queues a draw -- prepareSubViews
    // crosses frame boundaries, and a boundary after a page draw is
    // queued would commit it early and drop the page rect from this
    // frame's composite.
    for (size_t i = 0; i < s_subViews.size(); ++i) {
        WasmSubView &c = s_subViews[i];
        if (c.page)
            continue;
        const int dx = int(c.x * s_dpr + 0.5f);
        const int dy = int(c.y * s_dpr + 0.5f);
        const int dw = int(c.w * s_dpr + 0.5f);
        const int dh = int(c.h * s_dpr + 0.5f);
        if (dw <= 0 || dh <= 0)
            continue;
        mats.emplace_back();
        auto &m = mats.back();
        loadSubCam(c);
        buildCamera(m.data(), m.data() + 16, dw, dh);
        // A traced cell follows its own camera (sec 7.1).
        cyclesFrame(c.id, m.data(), m.data() + 16, dw, dh);
        Render::Renderer::SubViewFrame f;
        f.id = c.id;
        f.x = dx;
        f.y = dy;
        f.width = dw;
        f.height = dh;
        f.viewMatrix = m.data();
        f.projMatrix = m.data() + 16;
        if (int(i) == s_activeSub)
            activeFrame = int(frames.size());
        frames.push_back(f);
    }
    loadSubCam(s_subViews[s_activeSub]);

    QColor bg((s_snap.clearColor >> 24) & 0xff,
              (s_snap.clearColor >> 16) & 0xff,
              (s_snap.clearColor >> 8) & 0xff);
    if (!frames.empty()) {
        // Idempotent per-frame warm-up (docs/SplitViews.md sec 10, the
        // bail quirk): build fresh banks' targets each in its own
        // committed frame, and release the full-canvas bank a layout
        // obsoletes. Once every bank is warm this is a handful of map
        // lookups. Sitting here rather than in fcviewer_set_layout, it
        // also covers a layout restored before the renderer existed.
        s_renderer->prepareSubViews(bg, frames.data(),
                                    int(frames.size()));
    }

    for (size_t i = 0; i < s_subViews.size(); ++i) {
        WasmSubView &c = s_subViews[i];
        if (!c.page)
            continue;
        const int dx = int(c.x * s_dpr + 0.5f);
        const int dy = int(c.y * s_dpr + 0.5f);
        const int dw = int(c.w * s_dpr + 0.5f);
        const int dh = int(c.h * s_dpr + 0.5f);
        if (dw <= 0 || dh <= 0)
            continue;
        if (pageK >= kMaxPageCells)
            continue;
        const uint16_t vid = uint16_t(pageBase - pageK);
        ++pageK;
        anyPage = true;
        bgfx::FrameBufferHandle noFb = BGFX_INVALID_HANDLE;
        bgfx::setViewFrameBuffer(vid, noFb);
        bgfx::setViewRect(vid, uint16_t(dx), uint16_t(dy),
                          uint16_t(dw), uint16_t(dh));
        bgfx::setViewClear(vid,
                           BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                               | BGFX_CLEAR_STENCIL,
                           0x55585cff, 1.0f, 0);
        if (s_page && s_pageW > 0.0f) {
            // The view rect above places the cell; render() only
            // needs the rect's size for its projection.
            c.pageView.devicePixelRatio = s_dpr;
            s_page->setView(c.pageView);
            s_page->render(vid, uint16_t(dw), uint16_t(dh));
        }
        bgfx::touch(vid);
    }

    bool framePumped = false;
    if (!frames.empty()) {
        // Feed-level camera-relative state follows the ACTIVE sub-view
        // (docs/SplitViews.md sec 9.2, known approximations).
        const float *am = static_cast<const float *>(
            frames[activeFrame >= 0 ? activeFrame : 0].viewMatrix);
        relightForCamera(am);
        s_renderer->setAutoZoomScale(
            s_dist * std::tan(0.5f * kFovY * bx::kPi / 180.0f) * 0.0857f);
        const double renderT0 = emscripten_get_now();
        s_renderer->renderSubViews(bg, frames.data(),
                                   int(frames.size()));
        const double rdt = emscripten_get_now() - renderT0;
        s_renderMs = s_renderMs > 0.0 ? s_renderMs * 0.9 + rdt * 0.1 : rdt;
        // Success or bail, the frame boundary was crossed (the last
        // submit, or renderSubViews' own drain).
        framePumped = true;
    }
    if (!framePumped && anyPage
            && bgfx::getRendererType() != bgfx::RendererType::Noop) {
        // All-page layout: pump the frame the way renderPageFrame does.
        bgfx::frame();
    }
    updateHud(false);
}

/// The DOM chrome's layout push: "id,x,y,w,h,p;..." in CSS px on the
/// canvas (p = 1 for a page cell), empty = back to the single
/// full-canvas view. Cell state is keyed on id: cells that stay keep
/// their camera / page view, new 3D cells clone the active camera
/// (desktop split parity), new page cells fit their rect, and a
/// resized cell whose user has not taken the view re-fits.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_set_layout(const char *spec)
{
    if (!s_subViews.empty())
        stashSubCam(s_subViews[s_activeSub]);
    const int oldActiveId =
        s_subViews.empty() ? 0 : s_subViews[s_activeSub].id;

    std::vector<WasmSubView> next;
    std::vector<bool> fits;
    const char *q = spec ? spec : "";
    while (*q) {
        WasmSubView c;
        int pg = 0;
        int n = std::sscanf(q, "%d,%f,%f,%f,%f,%d", &c.id, &c.x, &c.y,
                            &c.w, &c.h, &pg);
        const char *semi = std::strchr(q, ';');
        q = semi ? semi + 1 : q + std::strlen(q);
        if (n != 6 || c.id <= 0 || c.w <= 0.0f || c.h <= 0.0f)
            continue;
        c.page = pg != 0;
        bool fit = false;
        const WasmSubView *prev = nullptr;
        for (const auto &o : s_subViews) {
            if (o.id == c.id) {
                prev = &o;
                break;
            }
        }
        if (prev) {
            // Matched on id alone: a content flip (the 3D/Page chip)
            // keeps BOTH state sets -- flipping to Page and back must
            // not lose the cell's camera.
            std::memcpy(c.center, prev->center, sizeof(c.center));
            c.panX = prev->panX;
            c.panY = prev->panY;
            c.yaw = prev->yaw;
            c.pitch = prev->pitch;
            c.roll = prev->roll;
            c.dist = prev->dist;
            c.userCam = prev->userCam;
            c.pageView = prev->pageView;
            c.pageUserView = prev->pageUserView;
            const bool resized = prev->w != c.w || prev->h != c.h;
            const bool flipped = prev->page != c.page;
            fit = (resized || flipped)
                && (c.page ? !c.pageUserView : !c.userCam);
        }
        else if (c.page) {
            fit = true;   // a fresh page cell frames the sheet
        }
        else {
            // Clone the live camera, like the desktop split does.
            stashSubCam(c);
        }
        next.push_back(c);
        fits.push_back(fit);
    }

    // Clearing the layout adopts a 3D cell's camera for the single
    // view -- the last-active one when it is 3D, else any 3D cell.
    // Without this a page-cell-active clear left the page cell's
    // untouched default orbit on the full canvas.
    if (next.empty() && !s_subViews.empty()) {
        const WasmSubView *adopt = nullptr;
        for (const auto &o : s_subViews) {
            if (!o.page && (!adopt || o.id == oldActiveId))
                adopt = &o;
        }
        if (adopt)
            loadSubCam(*adopt);
    }

    // Traced views the new layout does not carry stop first: their
    // consumers go before the banks they were registered under.
    cyclesLayoutChanged(next);
    // 3D cells that vanished give their renderer bank back.
    for (const auto &o : s_subViews) {
        if (o.page)
            continue;
        bool kept = false;
        for (const auto &c : next)
            kept = kept || (!c.page && c.id == o.id);
        if (!kept && s_renderer)
            s_renderer->dropSubView(o.id);
    }

    s_subViews = std::move(next);
    s_activeSub = 0;
    for (size_t i = 0; i < s_subViews.size(); ++i) {
        if (s_subViews[i].id == oldActiveId)
            s_activeSub = int(i);
    }

    // Fits run through the globals + viewport accessors, so each cell
    // is made active for its own fit.
    const int keepActive = s_activeSub;
    for (size_t i = 0; i < s_subViews.size(); ++i) {
        if (!fits[i])
            continue;
        s_activeSub = int(i);
        loadSubCam(s_subViews[i]);
        if (s_subViews[i].page) {
            s_pageUserView = false;
            pageFit();
        }
        else {
            fitCamera();
        }
        stashSubCam(s_subViews[i]);
    }
    s_activeSub = keepActive;
    if (!s_subViews.empty())
        loadSubCam(s_subViews[s_activeSub]);
    markDirty();
}

// ---- Uplink: the camera this viewer is looking through ----------------
//
// docs/ThinClient.md sec 8.5. The server keeps a mirror viewer per
// connection (docs/ThinClient.md sec 8.3) and resolves this client's picks
// through it, so what it needs is not a matrix but the camera fields: the
// pick radius it applies is in pixels of THIS canvas, and the tolerances an
// edit mode computes there are this client's. Sent as the 'C' frame -- 'C',
// type byte, viewport width and height as little-endian u16, then thirteen
// little-endian floats.

/// The camera's world orientation as a quaternion in Coin's convention: the
/// camera looks down its own -Z with +Y up, so the rotation's columns are
/// the world right, up and backward axes.
static void cameraQuaternion(const CamFrame &f, float q[4])
{
    const bx::Vec3 fwd = bx::normalize(bx::sub(f.at, f.eye));
    // CamFrame::right is the negation of the camera's right axis (the orbit
    // frame kept the historical pan convention) -- the same correction
    // screenRay makes, and it must be the same one or the mirror would
    // resolve every pick mirrored about the vertical.
    const bx::Vec3 x = bx::neg(f.right);
    const bx::Vec3 y = f.up;
    const bx::Vec3 z = bx::neg(fwd);

    // Shepperd: take the branch whose divisor is largest, so the square root
    // is never near zero.
    const float m[3][3] = {{x.x, y.x, z.x}, {x.y, y.y, z.y}, {x.z, y.z, z.z}};
    const float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0.0f) {
        const float s2 = std::sqrt(trace + 1.0f) * 2.0f;
        q[0] = (m[2][1] - m[1][2]) / s2;
        q[1] = (m[0][2] - m[2][0]) / s2;
        q[2] = (m[1][0] - m[0][1]) / s2;
        q[3] = 0.25f * s2;
    }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s2 = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q[0] = 0.25f * s2;
        q[1] = (m[0][1] + m[1][0]) / s2;
        q[2] = (m[0][2] + m[2][0]) / s2;
        q[3] = (m[2][1] - m[1][2]) / s2;
    }
    else if (m[1][1] > m[2][2]) {
        const float s2 = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q[0] = (m[0][1] + m[1][0]) / s2;
        q[1] = 0.25f * s2;
        q[2] = (m[1][2] + m[2][1]) / s2;
        q[3] = (m[0][2] - m[2][0]) / s2;
    }
    else {
        const float s2 = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q[0] = (m[0][2] + m[2][0]) / s2;
        q[1] = (m[1][2] + m[2][1]) / s2;
        q[2] = 0.25f * s2;
        q[3] = (m[1][0] - m[0][1]) / s2;
    }
}

/// How this viewer states its camera to the server
/// (docs/ThinClient.md sec 8.10a), chosen with `?camup=`. The uplink is
/// the scarce direction on the links this tier exists for, and in view
/// mode the only reader of a mirror's camera is a click -- so how often
/// the camera is worth sending is a question with an answer, not a
/// given.
enum class CamUplink {
    Frame,     ///< `frame` (A): once per client frame when it changed
    Rate,      ///< `rate` (B): the same, throttled to s_camRateMs
    Lazy,      ///< `lazy` (C): only with a click, and only when it moved
    LazyOne,   ///< `lazy1` (C'): the same, carried inside the click
};
/// The default is the lazy, camera-inside-the-pick one, on the
/// measurement of docs/ThinClient.md sec 8.10b: through an orbit it is
/// 71x fewer bytes and 100x fewer messages than stating the camera every
/// frame, with no click-to-push cost and the same pick. It is right
/// because in view mode the server reads a mirror's camera at exactly
/// one moment -- the click -- so the camera has no reader between
/// clicks. An edit mode does read it continuously, and turning the
/// per-frame send back on for one is this one value.
static CamUplink s_camUplink = CamUplink::LazyOne;
static double s_camRateMs = 100.0;

static uint8_t s_camFrame[6 + 13 * sizeof(float)];
static bool s_camFrameSent = false;
static double s_camSentAt = 0.0;

/// Forget what the server was told. The coalescing above assumes the
/// server still holds the last camera as this connection's mirror, and
/// there are two moments it does not: a reconnect, and a switch to
/// another document (whose serving source keeps its own mirrors, keyed
/// by connection). Under `frame` that self-corrects within a frame;
/// under `lazy` nothing would ever resend it, and the next click would
/// resolve in the synthetic framing -- so the cache is dropped at both.
static void invalidateCameraFrame()
{
    s_camFrameSent = false;
}

/// Pack this viewer's camera and canvas into the 'C' frame. False when
/// there is nothing sensible to say yet (no canvas).
static bool packCameraFrame(uint8_t *packed)
{
    const float vw = vpW(), vh = vpH();
    if (vw < 1.0f || vh < 1.0f)
        return false;

    const CamFrame f = camFrame();
    float q[4];
    cameraQuaternion(f, q);
    // The depth range buildCamera draws with, so the mirror's frustum is
    // the one this client's ray was computed in.
    const float neard = bx::max(0.002f * s_dist, s_dist - 0.75f * s_diag);
    const float fard = s_dist + 0.75f * s_diag;
    const float v[13] = {
        f.eye.x, f.eye.y, f.eye.z,
        q[0], q[1], q[2], q[3],
        kFovY * float(bx::kPi) / 180.0f,   // Coin's heightAngle: the full
                                           // vertical angle, in radians
        neard, fard, vw / vh,
        s_dpr, pickRadiusPx(),
    };

    packed[0] = 'C';
    packed[1] = 1;    // always perspective: the browser viewer has no
                      // orthographic mode
    const uint16_t pw = uint16_t(bx::min(vw, 65535.0f));
    const uint16_t ph = uint16_t(bx::min(vh, 65535.0f));
    std::memcpy(packed + 2, &pw, 2);
    std::memcpy(packed + 4, &ph, 2);
    std::memcpy(packed + 6, v, sizeof(v));
    return true;
}

/// Would \a packed tell the server something it does not already hold?
/// The packed bytes ARE the version number of sec 8.10a, and a better
/// one than a counter: a counter says the camera was touched, these say
/// the framing the server would resolve a ray in is not the framing the
/// ray was computed in.
static bool cameraFrameIsNews(const uint8_t *packed)
{
    return !s_camFrameSent
        || std::memcmp(packed, s_camFrame, sizeof(s_camFrame)) != 0;
}

static void markCameraFrameSent(const uint8_t *packed)
{
    std::memcpy(s_camFrame, packed, sizeof(s_camFrame));
    s_camFrameSent = true;
    s_camSentAt = emscripten_get_now();
}

/// State this viewer's camera, if it moved since the last time.
///
/// Coalesced by comparing the packed bytes: a camera that did not change
/// costs the pack and a memcmp, and an idle viewer sends nothing at all.
/// Under `frame` this is called once per client frame, so at most one
/// frame per client frame goes up -- which is what keeps a drag from
/// flooding the uplink; under `rate` a moving camera is throttled
/// further; under `lazy` it is called only from a pick.
///
/// \a force sends it whether or not it moved. Nothing forces it under
/// `lazy` -- that is the whole of what `lazy` is -- so the safety net
/// that made forcing right there (a server that has forgotten this
/// client's mirror) is invalidateCameraFrame() instead.
static bool sendCameraFrame(bool force = false)
{
    if (!s_wsOpen || s_ws <= 0)
        return false;
    uint8_t packed[sizeof(s_camFrame)];
    if (!packCameraFrame(packed))
        return false;   // no canvas yet
    if (!force && !cameraFrameIsNews(packed))
        return false;
    emscripten_websocket_send_binary(s_ws, packed, sizeof(packed));
    markCameraFrameSent(packed);
    return true;
}

/// The per-frame camera send, under whichever policy is in force.
static void tickCameraUplink()
{
    switch (s_camUplink) {
    case CamUplink::Frame:
        sendCameraFrame();
        break;
    case CamUplink::Rate:
        // Throttled, not dropped: the cache is left alone when a send is
        // skipped, so the camera still goes up at the next slot and a
        // camera that stops moving is always stated in the end.
        if (emscripten_get_now() - s_camSentAt >= s_camRateMs)
            sendCameraFrame();
        break;
    case CamUplink::Lazy:
    case CamUplink::LazyOne:
        break;   // only a click states the camera
    }
}

/// Send a click up as the 'P' pick the wire already carries: 'P', a flags
/// byte, then the world ray as six little-endian floats.
///
/// The local selection has already been drawn by the time this runs -- that
/// is the prediction of docs/ThinClient.md sec 8.6 -- and this is what makes
/// the server's selection authoritative: the tree, the property panel and
/// the ~1500 call sites behind Gui::Selection() are all on that side, and
/// the ruling of sec 8.2a is that a click round-trips while a hover does
/// not. The echo is merged, never applied over the local state.
static void sendPick(float px, float py, bool ctrl)
{
    if (!s_wsOpen || s_ws <= 0)
        return;

    bx::Vec3 orig(bx::InitZero), rdir(bx::InitZero);
    screenRay(px, py, orig, rdir);
    uint8_t pick[2 + 6 * sizeof(float)];
    pick[0] = 'P';
    pick[1] = ctrl ? 1 : 0;
    const float v[6] = {orig.x, orig.y, orig.z, rdir.x, rdir.y, rdir.z};
    std::memcpy(pick + 2, v, sizeof(v));

    // The camera goes with the click, and the policy decides how. The
    // server resolves the ray against the camera it last heard about, so
    // a pick made in a framing it has not been told about would be
    // resolved in the previous one -- which is a wrong answer, not a
    // missing one.
    //
    //  frame/rate  force it: cheap next to what those policies already
    //              send, and it needs no assumption about what the
    //              server still holds.
    //  lazy        send it only when it is news (sec 8.10a), the
    //              coalescing that pays for the policy.
    //  lazy1       the same test, but the camera rides inside the pick
    //              as one 'Q' message: one frame instead of two, and the
    //              pairing is atomic rather than merely ordered.
    if (s_camUplink == CamUplink::LazyOne) {
        uint8_t cam[sizeof(s_camFrame)];
        if (packCameraFrame(cam) && cameraFrameIsNews(cam)) {
            uint8_t both[1 + sizeof(cam) + sizeof(pick)];
            both[0] = 'Q';
            std::memcpy(both + 1, cam, sizeof(cam));
            std::memcpy(both + 1 + sizeof(cam), pick, sizeof(pick));
            emscripten_websocket_send_binary(s_ws, both, sizeof(both));
            markCameraFrameSent(cam);
            return;
        }
    }
    else if (s_camUplink == CamUplink::Lazy) {
        sendCameraFrame(/*force*/ false);
    }
    else {
        sendCameraFrame(/*force*/ true);
    }
    emscripten_websocket_send_binary(s_ws, pick, sizeof(pick));
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
        // With a layout the chrome re-pushes the cell rects on a
        // resize and the push re-fits; the whole-canvas fits below
        // would act on the active cell against a stale rect.
        if (s_subViews.empty()) {
            if (!s_userCam)
                fitCamera();
            if (s_pageMode && !s_pageUserView)
                pageFit();
        }
        markDirty();
    }

    // Page mode: the vg frame instead of the 3D renderer's. A false
    // return means the device is not up yet -- the 3D frame below runs
    // once and brings it up.
    if (s_subViews.empty() && s_pageMode && renderPageFrame())
        return;

    // A ?cycles= start (sec 7.1) waits for the stream and the scene, so
    // the camera it sends is the fitted one at the real canvas size.
    // Here rather than in the frame, which an idle scene skips.
    if (!s_cyclesUrlDevice.empty() && s_wsOpen && !s_snap.scene.empty()
            && s_subViews.empty()) {
        std::string device;
        device.swap(s_cyclesUrlDevice);
        fcviewer_set_cycles(1, device.c_str(), 0);
    }

    updateQuality();

    // Before the idle skip below, because a camera that just stopped
    // moving is exactly the one the server has not been told about yet.
    // Free when nothing moved -- sendCameraFrame compares the packed
    // bytes and an unchanged camera sends nothing -- and free outright
    // under the lazy policies, which say nothing until a click
    // (docs/ThinClient.md sec 8.10a).
    tickCameraUplink();

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
    // BEFORE the idle skip, not after: the settle detector lives in
    // this pump, and a camera stops moving ~130 ms (the eight dirty
    // frames) before the 300 ms settle threshold can pass — so with
    // the pump behind the skip, the frames that would notice the
    // settle were exactly the frames the skip suppressed. The replan
    // never fired, the scene sat below its desire at a stale plan with
    // budget to spare, and the first hover's highlight — by dirtying
    // the renderer — was what let the planner breathe again. Measured
    // as "meshes only upgrade when I move the mouse". The pump is a
    // handful of float compares on a settled scene; the skip still
    // saves the render.
    pumpFetchOnMove();
    if (s_dirtyFrames > 0) {
        --s_dirtyFrames;
    } else if (s_renderer && !s_renderer->isSceneDirty()
               && !s_renderer->isSceneAnimated()
               // A refinement in flight asks for the next sample through
               // animating(); sceneAnimated covers only the time-animated
               // media (fire/cloud/water/caustics) and would hold the very
               // frames the accumulation is waiting for. It goes false by
               // itself at the sample budget, so this does not keep a
               // converged view awake.
               && !s_renderer->animating()) {
        updateHud(true);
        s_lastFrameNow = 0.0;   // keep the idle gap out of the fps EMA
        return;
    }
    // The heap, on a slow heartbeat. An allocation that fails takes
    // the page with it and leaves nothing to inspect, so what the heap
    // was doing in the seconds before has to have been said already —
    // and on a phone the only way it gets said is ?log beaconing it
    // somewhere else (shell.html).
    reportHeap();

    if (!s_subViews.empty()) {
        renderLayoutFrame();
        return;
    }

    float viewMtx[16], projMtx[16];
    buildCamera(viewMtx, projMtx);
    // The served viewport follows this camera (sec 7.1).
    cyclesFrame(0, viewMtx, projMtx, s_width, s_height);
    // The camera-relative lights, for the same reason as the autozoom
    // scale below: both were baked against the producer's camera.
    relightForCamera(viewMtx);
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
    s_dbgFrameMs += rdt;
    ++s_dbgFrames;
    if (s_feedJustSet) {
        s_feedJustSet = false;
        s_dbgFeedFrameMs += rdt;
        ++s_dbgFeedFrames;
    }

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
        * s_dpr / (vpH() > 0.0f ? vpH() : 1.0f);
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
static void doTapPick(float px, float py, bool ctrl, bool shift = false)
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
        // Draws instantly here and states the same click to the backend,
        // which owns the authoritative selection (docs/ThinClient.md
        // sec 8.2a).
        selectAt(px, py, ctrl, shift);
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
static void tapOrDouble(float clientX, float clientY, bool ctrl,
                        bool shift = false)
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
        doTapPick(px, py, ctrl, shift);
        s_lastTapMs = now;
        s_lastTapX = clientX;
        s_lastTapY = clientY;
    }
}

/// Preselect the scene element at canvas pixel (px,py) — the raycast and the
/// highlight it drives, with no NaviCube handling. Shared by pointer hover
/// and by the touch loupe, which is over the scene by construction.
static void applySceneHover(float px, float py)
{
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

/// Preselect whatever sits under a hovering pointer at CSS-pixel
/// (clientX,clientY). Shared by the mouse move handler and the stylus hover
/// uplink.
static void updateHoverAt(float clientX, float clientY)
{
    if (!s_haveScene)
        return;
    const double now = emscripten_get_now();
    if (now - s_lastHoverMs < 30.0)
        return;
    s_lastHoverMs = now;
    float px, py;
    clientToCanvas(clientX, clientY, px, py);
    s_mouseX = px;
    s_mouseY = py;
    // The cell under the cursor is the active one (Blender's
    // active-follows-cursor); page cells have no scene hover.
    setActiveSubAt(px, py);
    if (activeIsPage()) {
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc), "page");
        applyHover(PickHit{});
        return;
    }
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
    applySceneHover(px, py);
}

static void updateHover(const EmscriptenMouseEvent *e)
{
    updateHoverAt(float(e->clientX), float(e->clientY));
}

static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent *e, void *)
{
    {
        float px, py;
        canvasPos(e, px, py);
        setActiveSubAt(px, py);
    }
    s_coarsePointer = false;
    s_dragging = true;
    s_panning = e->button == 2 || e->shiftKey;
    s_lastX = int(e->clientX);
    s_lastY = int(e->clientY);
    s_downX = s_lastX;
    s_downY = s_lastY;
    // Shift+left is grab-pan once it moves, but a motionless shift+click
    // is the whole-object select — the slop check on move/up arbitrates.
    s_clickOk = e->button == 0;
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
        if (activeIsPage()) {
            // No picking on a page yet (doc sec 24.6); a double click
            // refits the sheet.
            static double lastUp = 0.0;
            const double now = emscripten_get_now();
            if (now - lastUp < 350.0) {
                s_pageUserView = false;
                pageFit();
            }
            lastUp = now;
        }
        else {
            tapOrDouble(float(e->clientX), float(e->clientY), e->ctrlKey,
                        e->shiftKey);
        }
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
    if (activeIsPage()) {
        // A page has no orbit: every drag pans, in device pixels.
        s_pageView.panX += float(dx) * s_dpr;
        s_pageView.panY += float(dy) * s_dpr;
        s_pageUserView = true;
        return EM_TRUE;
    }
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
    setActiveSubAt(float(e->mouse.targetX) * s_dpr,
                   float(e->mouse.targetY) * s_dpr);
    interact();
    if (activeIsPage()) {
        // Zoom about the cursor: the page point under it stays put --
        // in viewport-local device px, since a page cell's pan is
        // local to its own rect.
        const float f = e->deltaY > 0 ? (1.0f / 1.15f) : 1.15f;
        const float zoom =
            bx::clamp(s_pageView.zoom * f, 0.002f, 1000.0f);
        const float applied = zoom / s_pageView.zoom;
        const float mx = float(e->mouse.targetX) * s_dpr - vpX();
        const float my = float(e->mouse.targetY) * s_dpr - vpY();
        s_pageView.panX = mx - applied * (mx - s_pageView.panX);
        s_pageView.panY = my - applied * (my - s_pageView.panY);
        s_pageView.zoom = zoom;
        s_pageUserView = true;
        return EM_TRUE;
    }
    s_dist *= e->deltaY > 0 ? 1.1f : (1.0f / 1.1f);
    s_dist = bx::clamp(s_dist, 0.01f * s_diag, 50.0f * s_diag);
    return EM_TRUE;
}

// 'v' prints (and copies) the current camera as a ?cam= string so an exact
// viewport can be reproduced by reloading with it appended to the URL.
// True while a DOM form control (the inspector's filter box, a future
// numeric field) owns the keyboard: the viewer's shortcuts must never
// eat keystrokes typed into the UI layer.
// Where the touch loupe is picking, and which finger it belongs to, for
// the DOM chrome to mark. Reported rather than drawn here: the chrome
// owns 2D overlay, and the mark has to be visible when the pick lands on
// nothing at all — which is exactly when the highlight cannot say it.
EM_JS(void, fcviewer_loupe_mark, (double x, double y, double fx, double fy,
                                  int active), {
    window.dispatchEvent(new CustomEvent('fc:loupe', {
        detail: active ? { x: x, y: y, fromX: fx, fromY: fy } : null }));
});

EM_JS(int, fcviewer_dom_has_keyboard, (), {
    var el = document.activeElement;
    if (!el)
        return 0;
    var tag = el.tagName;
    return (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT'
            || el.isContentEditable) ? 1 : 0;
});

static EM_BOOL onKeyDown(int, const EmscriptenKeyboardEvent *e, void *)
{
    if (fcviewer_dom_has_keyboard())
        return EM_FALSE;
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

// ---- The touch loupe: press and hold to preselect ----------------------
// A fingertip has no hover, so a tap is the only way to say "that one" — and
// it commits before it shows what it hit. A single finger held still past
// kLoupeHoldMs preselects instead of orbiting: the highlight comes up under
// the finger and then follows it, so the wrong target can be corrected
// before the finger lifts (the iOS text-cursor idiom), and the lift commits
// what is highlighted. Anything saying the gesture was really about the
// camera — a drag past the tap slop before the threshold, a second finger —
// cancels it. The longer press that docs/ThinClient.md §7 reserves for the
// context menu then composes on top: by the time that menu opens, its target
// is already showing.
static const double kLoupeHoldMs = 350.0;
// The pick sits this far above the contact point, from the moment the
// loupe engages: the ring and its centre dot appear just past the
// fingertip's outline, so the user watches the exact point being picked
// instead of the finger covering it. (It used to engage at the press
// point and lift only on the first movement; the always-visible cursor
// won — aim is corrected by watching the ring, not by trusting the
// press.) The distance is the streamed ViewParams::TouchLoupeLift, in
// CSS px — zero is a deliberate "pick under the finger"; an older
// backend's snapshot never writes the field and leaves the config's
// default.
static float loupeLiftPx()
{
    return std::max(0.0f, s_snap.preselconf.loupeLift);
}
static bool s_loupe = false;          // holding, with a live preselection
static float s_loupeX = 0.0f, s_loupeY = 0.0f;  // CSS px being picked
// Which press a pending hold timer was armed for. A held finger emits no
// events, so the threshold has to be a timer — and it is a timer rather than
// a frame check because a scene still streaming in can be several hundred
// milliseconds per frame, which would make the hold engage whenever the
// renderer got round to it instead of when the user's finger said so.
// Bumping this abandons any timer still in flight.
static uint32_t s_loupeGen = 0;

/// Give up the loupe, dropping any preselection it was showing, and abandon
/// a hold that has not fired yet.
static void cancelLoupe()
{
    if (s_loupe) {
        std::snprintf(s_hoverDesc, sizeof(s_hoverDesc), "none");
        applyHover(PickHit{});
        markDirty();
    }
    s_loupe = false;
    ++s_loupeGen;
    fcviewer_loupe_mark(0, 0, 0, 0, 0);
}

/// Preselect at the loupe's current pick point, and tell the chrome where
/// that point is so it can mark it.
static void loupePick()
{
    float px, py;
    clientToCanvas(s_loupeX, s_loupeY, px, py);
    applySceneHover(px, py);
    fcviewer_loupe_mark(s_loupeX, s_loupeY, s_touchX[0], s_touchY[0], 1);
    markDirty();
}

/// The hold threshold has passed: if the finger is still down, still still,
/// and still alone, the press becomes a preselection.
static void loupeHoldFired(void *arg)
{
    if (uint32_t(uintptr_t(arg)) != s_loupeGen)
        return;             // armed for a gesture that is already over
    if (s_loupe || !s_tapOk || s_numTouch != 1 || !s_haveScene)
        return;
    float px, py;
    clientToCanvas(s_loupeX, s_loupeY, px, py);
    bx::Vec3 dir(bx::InitZero);
    // A hold on the NaviCube belongs to the cube; leave the tap to it.
    if (pickNaviCube(px, py, dir) || pickNaviButton(px, py) != NaviBtnNone)
        return;
    s_loupe = true;
    // The pick point is above the fingertip from the start.
    s_loupeY -= loupeLiftPx();
    loupePick();
}

/// A stylus hovering over the canvas, from the pointermove listener installed
/// below. A pen is the one touchscreen input that reports a position before it
/// touches down, so preselection — which a mouse gets free with its cursor —
/// costs it no gesture at all. Ignored while a drag or a touch gesture owns
/// the pointer.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_pen_hover(float clientX,
                                                        float clientY)
{
    s_penHoverMs = emscripten_get_now();
    s_coarsePointer = false;
    if (s_dragging || s_numTouch > 0)
        return;
    updateHoverAt(clientX, clientY);
}

/// The stylus left the glass: drop the preselection with it. A highlight left
/// behind by a pointer that is no longer there reads as a selection.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_pen_leave()
{
    if (s_dragging || s_numTouch > 0)
        return;
    std::snprintf(s_hoverDesc, sizeof(s_hoverDesc), "none");
    applyHover(PickHit{});
}

/// Route pen hover to the viewer. Emscripten's mouse callbacks do not see a
/// stylus on a touchscreen: the browser delivers it as touch events, which
/// the touch handler preventDefaults (so no compatibility mouse events are
/// synthesized either). pointermove is the only place its hover exists.
/// Mouse and finger are both left to their existing handlers.
EM_JS(void, fcviewer_install_pen_hover, (), {
    var c = document.getElementById('canvas');
    if (!c)
        return;
    c.addEventListener('pointermove', function(ev) {
        if (ev.pointerType === 'pen')
            _fcviewer_pen_hover(ev.clientX, ev.clientY);
    });
    c.addEventListener('pointerout', function(ev) {
        if (ev.pointerType === 'pen')
            _fcviewer_pen_leave();
    });
});

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
        if (n >= 1) {
            float px, py;
            clientToCanvas(x[0], y[0], px, py);
            setActiveSubAt(px, py);
        }
        // A fingertip aims far more coarsely than a cursor (pickRadiusPx),
        // unless a stylus was hovering just now — then this contact is its
        // tip and stays fine.
        s_coarsePointer = emscripten_get_now() - s_penHoverMs > 2000.0;
        // One finger down opens a tap candidate; a second finger (a gesture)
        // cancels it. The cube hover tint isn't used on touch, but clear any
        // stale one before the geometry can move.
        clearCubeHover();
        clearButtonHover();
        if (activeIsPage()) {
            // No tap-pick and no loupe on a page: fingers only pan and
            // pinch it.
            s_tapOk = false;
        }
        else if (e->numTouches == 1 && n == 1) {
            s_tapOk = true;
            s_tapX = x[0];
            s_tapY = y[0];
            // Arm the hold: if this finger is still here, and still still,
            // when the timer fires, the press becomes a preselection.
            s_loupeX = x[0];
            s_loupeY = y[0];
            ++s_loupeGen;
            emscripten_async_call(loupeHoldFired,
                                  (void *)uintptr_t(s_loupeGen),
                                  int(kLoupeHoldMs));
        }
        else {
            s_tapOk = false;
            // A second finger is a camera gesture, whatever the first was
            // doing.
            cancelLoupe();
        }
    }
    else if (type == EMSCRIPTEN_EVENT_TOUCHMOVE && n == s_numTouch) {
        if (s_loupe && n == 1) {
            // Refining, not orbiting: the drag moves the pick, which keeps
            // its lift above the fingertip.
            s_loupeX += x[0] - s_touchX[0];
            s_loupeY += y[0] - s_touchY[0];
            // Adopt the new contact point BEFORE picking: the mark's
            // leader is drawn from it, and reporting the previous one
            // anchors the leader a move behind the finger.
            s_numTouch = n;
            s_touchX[0] = x[0];
            s_touchY[0] = y[0];
            loupePick();
            return EM_TRUE;
        }
        interact();
        if (activeIsPage()) {
            if (n == 1) {
                s_pageView.panX += (x[0] - s_touchX[0]) * s_dpr;
                s_pageView.panY += (y[0] - s_touchY[0]) * s_dpr;
            }
            else if (n == 2) {
                s_pageView.panX +=
                    0.5f * (x[0] - s_touchX[0] + x[1] - s_touchX[1])
                    * s_dpr;
                s_pageView.panY +=
                    0.5f * (y[0] - s_touchY[0] + y[1] - s_touchY[1])
                    * s_dpr;
                const float oldDist = std::hypot(s_touchX[1] - s_touchX[0],
                                                 s_touchY[1] - s_touchY[0]);
                const float newDist = std::hypot(x[1] - x[0], y[1] - y[0]);
                if (oldDist > 1.0f && newDist > 1.0f) {
                    const float zoom = bx::clamp(
                        s_pageView.zoom * newDist / oldDist, 0.002f,
                        1000.0f);
                    // Pinch zooms about the midpoint, in
                    // viewport-local device px.
                    const float f = zoom / s_pageView.zoom;
                    const float mx = 0.5f * (x[0] + x[1]) * s_dpr - vpX();
                    const float my = 0.5f * (y[0] + y[1]) * s_dpr - vpY();
                    s_pageView.panX = mx - f * (mx - s_pageView.panX);
                    s_pageView.panY = my - f * (my - s_pageView.panY);
                    s_pageView.zoom = zoom;
                }
            }
            s_pageUserView = true;
        }
        else if (n == 1) {
            // Drag past the slop cancels the tap so orbit doesn't also pick,
            // and says this press was a camera gesture, not a hold.
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
        if (s_loupe) {
            // The lift commits what the loupe is showing — the user has
            // already seen it, so this never goes through the double-tap
            // window (a hold is deliberate, and zoom-to-fit would be a
            // surprising answer to one).
            if (type == EMSCRIPTEN_EVENT_TOUCHEND) {
                float px, py;
                clientToCanvas(s_loupeX, s_loupeY, px, py);
                selectAt(px, py, /*ctrl*/ false);
            }
            cancelLoupe();
        }
        else if (type == EMSCRIPTEN_EVENT_TOUCHEND && s_tapOk
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
        const float aspect = vpH() > 0.0f ? vpW() / vpH() : 1.0f;
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

/// ?shapevertices=1 -- draw the vertex points that sit on the ends of a
/// shape's edges (docs/SceneStreaming.md #13b). Off, like the desktop
/// parameter of the same name, and it matters more here: such a point
/// lands exactly on an edge already drawn, and it costs a 32-byte
/// sprite instance record against the 4 bytes it occupies in the heap,
/// so on a phone it is the most expensive thing on screen per unit of
/// what it shows. All or nothing per point set, and only where the
/// producer classified every one of its vertices as an edge endpoint --
/// a point cloud always draws.
static bool s_shapeVertices = false;

/// ?cavity=<0|1> -- screen-space cavity shading, ON here, which is the
/// viewer's own choice and not a relay of the producer's. Same reason
/// the desktop parameter flipped on: the element contract may withhold
/// an object's edge and vertex sets, and cavity is what still draws a
/// crease when no line does -- one fullscreen multiply over targets the
/// prepass has already paid for, which is why it survives even the
/// degraded tier that drops AO. The producer's tuning (valley, ridge,
/// radius) is kept when the snapshot carried it; only the switch is the
/// viewer's, so a scene from a build older than v42 -- which says
/// nothing about cavity at all -- gets the same look as a current one.
static bool s_cavity = true;
// ?accum=<N> -- idle temporal refinement (docs/RenderEngine.md sec 3.5),
// 0 = off, which is the default here for the same reason it is on the
// desktop: what it spends is the READER's GPU and, on a phone, their
// battery. That makes it a fact about this machine rather than about the
// model somebody authored, so it is deliberately NOT taken from the
// scene snapshot -- the producer does not get to spend the viewer's
// power. Same rule the desktop follows by keeping both properties local
// (Prop_NoPersist).
static int s_accumSamples = 0;

/// ?leveldebug -- narrate the level plan and the element gates, which is
/// the desktop's FC_LEVEL_DEBUG / Render_LevelDebug by another door
/// (there is no environment to read here). Pushed with the per-snapshot
/// settings rather than once at startup: set once at startup it was
/// SILENTLY LOST -- every "render levels:" line this tier is written to
/// print stayed dark, and the gate counters could not be read at all
/// until the flag was forced. An instrument nobody has seen fire is not
/// an instrument.
static bool s_levelDebug = false;

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
    // ...including how many objects the publish says it holds only part
    // of (SceneDump v55). Zero is the settled answer; a non-zero count
    // is the producer's capture budget still draining, and the only
    // evidence on this side that the mark travelled at all -- the gates
    // that read it report separately, and cannot distinguish "the rule
    // did not fire" from "the bit never arrived".
    std::set<uint64_t> incomplete;
    for (const auto &d : s_snap.scene) {
        if (d.objectIncomplete)
            incomplete.insert(d.objectKey);
    }
    std::printf("fcviewer: apply snapshot: %zu draws, %zu post, "
                "%zu splices, %zu objects incomplete%s\n",
                s_snap.scene.size(),
                s_snap.usershaderconf.shaders.size(),
                s_snap.usershaderconf.splices.size(),
                incomplete.size(),
                fit ? " (fit)" : "");
    s_renderer->setBackground(s_snap.background);
    s_renderer->setHiddenLineConfig(s_snap.hlconfig);
    s_renderer->setSectionConfig(s_snap.secconf);
    Render::AOConfig ao = s_snap.aoconf;
    if (s_degraded)
        ao.enabled = false;
    s_renderer->setAOConfig(ao);
    // Cavity is one fullscreen multiply over targets the prepass
    // already paid for, so it survives the degraded tier that drops AO.
    Render::CavityConfig cavity = s_snap.cavityconf;
    cavity.enabled = s_cavity;
    s_renderer->setCavityConfig(cavity);
    // Idle refinement: viewer-local, so unlike every other config here
    // it does not come from s_snap.
    Render::TemporalConfig temporal;
    temporal.enabled = s_accumSamples > 0;
    if (s_accumSamples > 0)
        temporal.samples = s_accumSamples;
    s_renderer->setTemporalConfig(temporal);
    s_renderer->setMatcapConfig(s_snap.matcapconf);
    s_renderer->setPBRConfig(s_snap.pbrconf);
    s_renderer->setBumpConfig(s_snap.bumpconf);
    s_renderer->setLightConfig(s_snap.lightconf);
    s_renderer->setViewLightConfig(s_snap.viewlightconf);
    // That pushed the PRODUCER's world-space directions; any
    // camera-relative light among them has to be re-derived against this
    // viewer's camera before the next frame draws.
    s_relightDue = true;
    s_renderer->setVolumetricConfig(s_snap.volconf);
    s_renderer->setWaterConfig(s_snap.waterconf);
    s_renderer->setBloomConfig(s_snap.bloomconf);
    s_renderer->setOutputConfig(s_snap.outconf);
    s_renderer->setRenderDebugConfig(s_snap.debugconf);
    // User shaders (docs/RenderDebug.md §6.3): the post-stage list;
    // material-stage programs ride the scene draw materials. Programs
    // load from the server-compiled binaries the snapshot carries.
    s_renderer->setUserShaderConfig(s_snap.usershaderconf);
    s_renderer->setAutoZoomScale(s_snap.autozoomScale);
    // The element gates (#13b). The edge one is pushed on and stays
    // dormant here for want of an uploaded-bytes meter; the load gate
    // is the desktop's, where geometry arrives into a live view -- this
    // tier's scene arrives as a snapshot that is applied whole.
    s_renderer->setElementGates(s_shapeVertices, /*pressureEdges*/ true,
                                /*loadingDrop*/ false,
                                /*staggerFrames*/ 15);
    s_renderer->setLevelDebug(s_levelDebug);
    s_renderer->setEffectResolution(s_snap.effectResolution);
    s_renderer->setSSAOResolution(s_snap.ssaoResolution);
    if (s_snap.hatch && !s_snap.hatch->pixels.empty())
        s_renderer->setHatchImage(s_snap.hatch->pixels.data(), 4,
                                  s_snap.hatch->width, s_snap.hatch->height);
    {
        DbgScope dbg(s_dbgSceneMs);
        Render::DrawCallList draws = s_snap.scene;
        s_renderer->setScene(std::move(draws));
        s_feedJustSet = true;
    }
    // The client owns selection (rebuildSelection below): the backend's own
    // streamed selection feed is ignored so its slow full re-stream never
    // drives the visible selection. Any previously applied streamed selection
    // is dropped.
    {
        DbgScope dbg(s_dbgSelMs);
        for (int id : s_selIds)
            s_renderer->removeSelection(id);
        s_selIds.clear();
        // Re-apply the local selection against the (possibly replaced)
        // scene draws.
        rebuildSelection();
    }
    {
        DbgScope dbg(s_dbgOverlayMs);
        // Overlay feeds (foreground superimposition, corner axis
        // cross): replayed with their declarative anchors — the local
        // renderer re-derives viewport and camera each frame, so
        // overlays re-anchor on resize and follow the local orbit
        // camera.
        std::set<int> ovIds;
        for (const auto &ov : s_snap.overlays) {
            ovIds.insert(ov.id);
            // A republish re-parses the overlay feed into fresh payload
            // objects, and for the length of their re-read (a local
            // cache hit, usually, but a visible frame regardless) the
            // feed is a cube with no faces or no glyphs. While a
            // previous feed is on screen, hold it: the renderer keeps
            // drawing what it was last given, and the new feed goes up
            // only once every mesh and texture it names is in hand.
            // Without this the navigation cube blinked once per
            // announcement of a 62-delta chain.
            bool whole = true;
            for (const auto &d : ov.draws) {
                if ((d.mesh
                     && !(d.mesh->numVertices > 0 && d.mesh->positions))
                        || (d.material.texture
                            && d.material.texture->deferred)) {
                    whole = false;
                    break;
                }
            }
            if (!whole && s_overlayIds.count(ov.id)) {
                decLog("overlay %d held (feed not whole yet)", ov.id);
                continue;
            }
            Render::DrawCallList odraws = ov.draws;
            s_renderer->setOverlay(ov.id, std::move(odraws), ov.anchor);
        }
        for (int id : s_overlayIds) {
            if (!ovIds.count(id))
                s_renderer->removeOverlay(id);
        }
        s_overlayIds.swap(ovIds);
    }
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
static bool stillArriving(const Render::SceneSnapshot &snap);

/// Ride the geometry ladders of unchanged objects across a delta
/// commit (docs/SceneStreaming.md §7): a delta names only what
/// changed, and a plan over only what changed would silently exempt
/// the rest of the model from both upgrades and eviction. Only
/// geometry — its fill and release closures capture the mesh they
/// write, never the parse they were born in, so they are safe to run
/// against any snapshot; the manifest layer's closures are not, which
/// is why a delta over missing manifests still asks for a full root
/// (applyScenePayload).
static void carryLadders(Render::SceneSnapshot &fresh)
{
    // Everything the fresh publish already names, at any rung: an
    // entry it covers must not ride in beside its replacement.
    std::set<std::string> named;
    for (const auto &entry : fresh.deferredChunks) {
        if (!entry.key.empty())
            named.insert(entry.key);
        for (const auto &lvl : entry.levels) {
            if (!lvl.key.empty())
                named.insert(lvl.key);
        }
    }
    // Objects this delta re-describes: whatever of theirs it did NOT
    // name again is dead geometry — an edit that re-keyed the mesh —
    // not an unchanged ladder to preserve.
    std::set<uint64_t> redescribed;
    for (const auto &up : fresh.objectUpdates)
        redescribed.insert(up.entry.objectKey);
    size_t carried = 0;
    size_t textures = 0;
    for (const auto &entry : s_snap.deferredChunks) {
        // A texture still on its way rides too: its fill writes only
        // its own object, which the model's draws hold (and which the
        // fresh publish's re-parse handed back through the memo), and
        // a publish that does not name its key again would otherwise
        // drop the one fetch that was going to fill it -- the map then
        // stayed white on every object the delta left alone.
        if (entry.texture && entry.fill) {
            if (!entry.key.empty() && !named.count(entry.key)) {
                fresh.deferredChunks.push_back(entry);
                ++textures;
            }
            continue;
        }
        if (!entry.release)
            continue;
        bool covered = !entry.key.empty() && named.count(entry.key);
        for (const auto &lvl : entry.levels)
            covered = covered
                || (!lvl.key.empty() && named.count(lvl.key) != 0);
        if (covered)
            continue;
        // Owners that left the model, or were re-described without
        // this entry, take their claim with them; the ownerless (the
        // view's own) always ride.
        std::vector<uint64_t> owners;
        for (uint64_t owner : entry.owners) {
            if (s_objects.objects.count(owner) && !redescribed.count(owner))
                owners.push_back(owner);
        }
        if (owners.empty() && !entry.owners.empty())
            continue;
        fresh.deferredChunks.push_back(entry);
        fresh.deferredChunks.back().owners = std::move(owners);
        ++carried;
    }
    if (textures)
        decLog("carried %zu texture fetches across the delta", textures);
    if (carried) {
        decLog("carried %zu geometry ladders across the delta", carried);
        if (s_streamDebug)
            std::printf("fcviewer: carried %zu geometry ladders across the "
                        "delta\n", carried);
    }
}

/// Re-sweep the store ledger from the entries of the scene being
/// installed: a commit is where the superseded snapshot's payload
/// objects die — its overlay meshes, the re-keyed objects' old rungs —
/// and a whole snapshot's binds drop at once. Residency itself needs
/// no reconciling any more: it rides on the entries (carried, or
/// freshly parsed with resident = -1), which is the point of the
/// ladder owning it.
static void rebindHeld(const Render::SceneSnapshot &fresh);
static void installRungBinder();

static void commitSnapshot(Render::SceneSnapshot &&snap, uint64_t version)
{
    s_sceneVersion = version;
    installRungBinder();
    // The plan's universe is the whole scene, and a delta is not (§7):
    // carry the unchanged ladders over, then plan against the merged
    // set. This is also what makes abandoning the superseded
    // snapshot's outstanding geometry safe.
    if (snap.baseVersion)
        carryLadders(snap);
    // Ladders the fresh publish re-declared were NOT carried — they
    // are covered, and their replacements parse empty. Their geometry
    // survives by content key instead: without this, an announcement
    // chain (a cold backend building levels) re-described the scene
    // every ~200 ms and each commit dumped the whole resident set back
    // onto the network — the journal's endless re-asks of the same
    // keys, the crawl, and the boxes that outstayed the load.
    if (s_haveScene) {
        const size_t adopted = Render::carryResidentRungs(snap, s_snap);
        if (adopted)
            decLog("adopted %zu resident rungs across the re-parse",
                   adopted);
    }
    rebindHeld(snap);
    s_planStale = true;
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
    // Only when there is nothing left to wait for. A publish is
    // committed on the first arrival that can be drawn, not on the
    // last (§6), so hiding the indicator here used to blank it for the
    // tick until the next round put it back — a flicker at the moment
    // the model first appears, which is the worst moment for one.
    if (!stillArriving(s_snap))
        fcviewer_status(nullptr, 0.0, 0.0);
    decLog("commit v%llu: %zu draws, %zu overlays",
           (unsigned long long)version, s_snap.scene.size(),
           s_snap.overlays.size());
    logStandIns(s_snap);
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
/// Set when the local store has stopped answering (see
/// s_blobInFlight): a read that neither succeeds nor fails takes its
/// payload out of the load for good, and retrying the same store only
/// hangs again. The store is a pure optimization, so the recovery is
/// simply to stop using it for the rest of the session and fetch from
/// the network instead.
static bool s_idbUnresponsive = false;

static bool blobPersistEnabled()
{
    static int on = -1;
    if (on < 0)
        on = EM_ASM_INT({
            return new URLSearchParams(location.search).has('noidb') ? 0 : 1;
        });
    return on != 0 && !s_idbUnresponsive;
}

//////////////////////////////////////////////////////////////////////
// The persistent store's ledger, and its lazy collection
// (docs/SceneStreaming.md §7, "the store is a cache with a budget").
// IndexedDB otherwise only ever grows: every edit re-keys content, so
// a working session strictly adds blobs, and the emscripten helpers
// cannot even enumerate what is there. The ledger is a meta record in
// the same database under a fixed non-hash key: every blob written is
// noted with its size and an epoch-ms last-touched stamp, every store
// hit refreshes the stamp, and a sweep runs only when the tracked
// total exceeds the budget — losing your last reference makes you a
// candidate, only storage pressure makes you garbage. Keys the live
// scene names at any rung are never swept. Two tabs race on the meta
// record last-writer-wins; the worst either can suffer is a key
// deleted under the other's feet, which costs one refetch.

struct StoreMetaEntry {
    uint32_t bytes = 0;
    double touched = 0.0;  ///< epoch ms — comparable across sessions
};
static std::map<std::string, StoreMetaEntry> s_storeMeta;
static size_t s_storeMetaBytes = 0;
static bool s_storeMetaLoaded = false;
static bool s_storeMetaDirty = false;
static bool s_storeMetaFlushArmed = false;
static const char *kStoreMetaKey = "!meta";

/// Bytes the persistent store may hold before a sweep;
/// ?storebudget=<MB> overrides.
static size_t storeBudget()
{
    static long mb = -1;
    if (mb < 0) {
        mb = EM_ASM_INT({
            var v = new URLSearchParams(location.search).get('storebudget');
            return v === null ? 512 : (parseInt(v) | 0);
        });
        if (mb <= 0)
            mb = 512;
    }
    return size_t(mb) << 20;
}

static void armStoreMetaFlush();

/// Note a blob written to or read from the store. An untracked key a
/// warm session hits joins the ledger here, so a store predating the
/// ledger converges to tracked as it is actually used.
static void touchStoreMeta(const std::string &key, uint32_t bytes)
{
    if (!blobPersistEnabled())
        return;
    auto &meta = s_storeMeta[key];
    if (!meta.bytes)
        s_storeMetaBytes += bytes;
    meta.bytes = bytes;
    meta.touched = emscripten_date_now();
    s_storeMetaDirty = true;
    armStoreMetaFlush();
}

static void dropStoreMeta(const std::string &key)
{
    auto it = s_storeMeta.find(key);
    if (it == s_storeMeta.end())
        return;
    s_storeMetaBytes -=
        std::min<size_t>(s_storeMetaBytes, it->second.bytes);
    s_storeMeta.erase(it);
    s_storeMetaDirty = true;
}

/// Evict least-recently-touched blobs the live scene does not name
/// until the tracked total is comfortably under the budget. Runs from
/// the debounced flush only — collection is lazy by design. Defined
/// after the staging state it reads (the live set spans the live AND
/// the staged snapshot).
static void sweepStore();

static void flushStoreMeta()
{
    if (!s_storeMetaDirty || !blobPersistEnabled())
        return;
    s_storeMetaDirty = false;
    std::string out;
    out.reserve(s_storeMeta.size() * 64);
    char line[96];
    for (const auto &kv : s_storeMeta) {
        std::snprintf(line, sizeof(line), "%s %u %.0f\n",
                      kv.first.c_str(), kv.second.bytes,
                      kv.second.touched);
        out += line;
    }
    emscripten_idb_async_store(kBlobDb, kStoreMetaKey,
                               const_cast<char *>(out.data()),
                               int(out.size()), nullptr, [](void *) {},
                               [](void *) {});
}

/// One debounced pass: sweep if over budget (it edits the ledger),
/// then persist the ledger once. Ten seconds coalesces a whole load's
/// worth of writes into one meta store.
static void armStoreMetaFlush()
{
    if (s_storeMetaFlushArmed)
        return;
    s_storeMetaFlushArmed = true;
    emscripten_async_call([](void *) {
        s_storeMetaFlushArmed = false;
        sweepStore();
        flushStoreMeta();
    }, nullptr, 10000);
}

static void loadStoreMeta()
{
    if (!blobPersistEnabled()) {
        s_storeMetaLoaded = true;
        return;
    }
    emscripten_idb_async_load(
        kBlobDb, kStoreMetaKey, nullptr,
        [](void *, void *ptr, int num) {
            // The buffer belongs to the glue (it frees it after this
            // returns) — parse, never free, never keep.
            const char *p = static_cast<const char *>(ptr);
            const char *end = p + num;
            while (p < end) {
                const char *nl = static_cast<const char *>(
                    std::memchr(p, '\n', size_t(end - p)));
                if (!nl)
                    break;
                char key[64];
                unsigned bytes = 0;
                double touched = 0.0;
                std::string ln(p, size_t(nl - p));
                if (std::sscanf(ln.c_str(), "%63s %u %lf", key, &bytes,
                                &touched) == 3
                        && bytes > 0) {
                    auto &meta = s_storeMeta[key];
                    if (!meta.bytes)
                        s_storeMetaBytes += bytes;
                    meta.bytes = bytes;
                    meta.touched = std::max(meta.touched, touched);
                }
                p = nl + 1;
            }
            s_storeMetaLoaded = true;
            // Over budget from previous sessions: collect soon.
            if (s_storeMetaBytes > storeBudget())
                armStoreMetaFlush();
        },
        [](void *) { s_storeMetaLoaded = true; });
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
        touchStoreMeta(key, uint32_t(size));
        blobResolved(key, std::make_shared<std::vector<uint8_t>>(
                              bytes, bytes + size), true);
        return;
    }
    std::printf("fcviewer: cached blob %s is not what its key names, "
                "dropping it\n", key.c_str());
    dropStoreMeta(key);
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
/// Keys with a load in flight, and *when it was asked for*.
///
/// The time is what makes this recoverable. A request that neither
/// resolves nor fails — a socket that dies with the page backgrounded,
/// a batch abandoned on a flaky mobile link — leaves its key here
/// forever, and a key that is permanently "in flight" is one the fetch
/// will never ask for again. That was survivable when it only cost
/// those chunks; with a budget it is fatal, because their bytes are
/// counted as already spent (see `pledged`), so a handful of ghosts
/// can consume the whole allowance and leave every object at its box.
/// Observed on a phone: 349 of 382 outstanding payloads unaskable with
/// *zero* requests actually in flight, and a scene of grey boxes.
static std::map<std::string, double> s_blobInFlight;

/// The subset of in-flight keys that actually went to the NETWORK —
/// the IndexedDB probe missed, or the store is off. What the loading
/// indicator answers to: a warm store repaints the scene from local
/// reads the user cannot perceive as "loading", and flashing a
/// progress bar over that work reads as a problem where there is none.
static std::set<std::string> s_netInFlight;

/// How long a payload may be outstanding before it is presumed lost
/// and may be asked for again. Long enough that a slow link is never
/// mistaken for a dead one — the point is to recover from silence, not
/// to race it.
static const double kInFlightTimeoutMs = 30000.0;
/// A wake-up already queued to re-examine stalled requests.
static bool s_retryScheduled = false;

static void resolvePending();

/// The reconciler heartbeat (docs/SceneStreaming.md §7): while the
/// scene is not where the plan says it should be — a ladder below its
/// target, a manifest outstanding, a request in flight, a generate
/// awaiting its announcement — wake up, reconcile, re-arm. Defined
/// after the resident bookkeeping it reads; this is its arming point.
///
/// It re-arms from the WAKE-UP itself — never from inside
/// resolvePending, whose early returns (a blob-store reset, a snapshot
/// with nothing outstanding) previously ended the chain with requests
/// still hung. It subsumes what used to be separate mechanisms, each
/// grown around one measured silence: the stall watchdog (an IndexedDB
/// read that never answers has no XHR and so no timeout — one read
/// pending, five minutes of a converged-looking viewer one mesh short
/// of its plan), the in-flight retry chain, and the one silence
/// nothing covered at all: a generate whose producer-side job was
/// dropped left the ladder coarse forever, because the retry loop was
/// arrival-driven and the answer to a generate is an announcement that
/// was never going to arrive.
static void armHeartbeat();
/// Requests, not keys and not bytes: what a viewer's throughput turns
/// out to depend on is how many of them are outstanding at once (see
/// kInFlightRequests).
static size_t s_requestsInFlight = 0;
/// Keys this snapshot could not obtain. Cleared whenever a new
/// snapshot is staged: one retry per publish, never a fetch loop.
static std::set<std::string> s_blobFailed;

//////////////////////////////////////////////////////////////////////
// The resident set (docs/SceneStreaming.md §6, phase 4b)

/// The store's ledger of geometry the scene is holding: content key →
/// {bytes, how many ladders currently hold that rung filled}. This is
/// ACCOUNTING, not authority — residency truth is each ladder's own
/// `resident` field (SceneDump.h, docs/SceneStreaming.md §7 "the
/// ladder owns its fetch state") — but the accounting has to be keyed
/// by content, because one mesh backs every instance of a part and a
/// staged publish and the live scene transiently hold the same rungs:
/// the budget must charge those bytes once, however many ladders bind
/// them.
struct HeldRung {
    uint32_t bytes = 0;
    int binders = 0;
};
static std::map<std::string, HeldRung> s_held;
static size_t s_residentBytes = 0;

/// The error tier the last plan granted each object (planLevels'
/// objectErr out-map): what turns an owner into a rung, both for the
/// executor's needed set and for the per-instance draw binding (§7,
/// "one rung per instance"). Negative = boxed; absent = the plan has
/// not seen the object yet.
static std::map<uint64_t, float> s_objectErr;
static float objectErrOf(uint64_t owner)
{
    auto it = s_objectErr.find(owner);
    return it == s_objectErr.end()
        ? std::numeric_limits<float>::quiet_NaN()
        : it->second;
}

/// draw.mesh (a ladder's identity object) → its entry in the LIVE
/// scene's deferred list, for the per-instance binder below. Rebuilt
/// when the scene version or the ladder count moves; a miss is not an
/// error — a staged snapshot's fresh entries and bridged draws simply
/// fall through to the identity mesh, the pre-Stage-B behavior.
static std::map<const void *, size_t> s_meshEntryAt;
static uint64_t s_meshMapVersion = ~0ull;
static size_t s_meshMapCount = 0;

static void refreshMeshEntryMap()
{
    if (s_meshMapVersion == s_sceneVersion
            && s_meshMapCount == s_snap.deferredChunks.size())
        return;
    s_meshEntryAt.clear();
    for (size_t i = 0; i < s_snap.deferredChunks.size(); ++i) {
        const auto &entry = s_snap.deferredChunks[i];
        if (entry.release && entry.levelMeshes) {
            if (auto id = entry.levelMeshes->identity())
                s_meshEntryAt.emplace(id.get(), i);
        }
    }
    s_meshMapVersion = s_sceneVersion;
    s_meshMapCount = s_snap.deferredChunks.size();
}

/// The per-instance rung binding (§7, "one rung per instance"),
/// consulted by assembly for every geometry draw: which rung should
/// THIS draw's owner stand on, and is it resident? Answering null
/// falls through to the ladder's identity mesh, the bridges and the
/// box — so every miss here degrades to exactly the old behavior.
static void installRungBinder()
{
    if (s_objects.rungBinder)
        return;
    s_objects.rungBinder = [](const Render::DrawCall &d)
        -> std::shared_ptr<const Render::MeshData> {
        if (!d.mesh || !d.objectKey)
            return nullptr;
        refreshMeshEntryMap();
        auto at = s_meshEntryAt.find(d.mesh.get());
        if (at == s_meshEntryAt.end()
                || at->second >= s_snap.deferredChunks.size())
            return nullptr;
        const auto &entry = s_snap.deferredChunks[at->second];
        if (!entry.levelMeshes || !entry.residentMask)
            return nullptr;
        const float err = objectErrOf(d.objectKey);
        // Unplanned → identity (the pre-plan default); boxed → the
        // box path answers, not a mesh.
        if (std::isnan(err) || err < 0.0f)
            return nullptr;
        const int count = std::min<int>(int(Render::planRungs(entry)), 16);
        const int want =
            std::min(Render::rungWithin(entry, err), count - 1);
        if (want < 0)
            return nullptr;
        // The wanted rung if held; else the nearest resident FINER
        // rung (paid for and better-looking); else the nearest
        // coarser — the stand-in while the fetch climbs.
        int pick = -1;
        for (int r = want; r < count; ++r) {
            if (entry.residentMask & uint16_t(1u << r)) {
                pick = r;
                break;
            }
        }
        if (pick < 0) {
            for (int r = want; r-- > 0;) {
                if (entry.residentMask & uint16_t(1u << r)) {
                    pick = r;
                    break;
                }
            }
        }
        if (pick < 0)
            return nullptr;
        auto mesh =
            entry.levelMeshes->at(Render::planRungKey(entry, size_t(pick)));
        return (mesh && mesh->numVertices > 0 && mesh->positions)
            ? mesh
            : nullptr;
    };
}

/// A ladder holds a rung: charge the bytes on the first binder only.
static void bindRung(const std::string &key, uint32_t size)
{
    auto &held = s_held[key];
    if (held.binders++ == 0) {
        held.bytes = size;
        s_residentBytes += size;
    }
}

/// A ladder lets a rung go (release, or its arrays were overwritten by
/// another rung's fill). The bytes leave the books with the last
/// binder; the key itself may live on in the payload cache and the
/// local store — losing your last reference makes you a GC candidate,
/// not garbage (§7, "collection is lazy").
static void unbindRung(const std::string &key)
{
    auto it = s_held.find(key);
    if (it == s_held.end())
        return;
    if (--it->second.binders <= 0) {
        s_residentBytes -= std::min(s_residentBytes,
                                    size_t(it->second.bytes));
        s_held.erase(it);
    }
}

/// See the declaration above commitSnapshot: a commit is where the
/// superseded snapshot's payload objects die, so the ledger is re-swept
/// from the entries that survive — the one moment incremental
/// bind/unbind cannot cover, because a whole snapshot's binds drop at
/// once.
static void rebindHeld(const Render::SceneSnapshot &fresh)
{
    std::map<std::string, HeldRung> held;
    size_t bytes = 0;
    for (const auto &entry : fresh.deferredChunks) {
        if (!entry.release || !entry.residentMask)
            continue;
        const int count = std::min<int>(int(Render::planRungs(entry)), 16);
        for (int r = 0; r < count; ++r) {
            if (!(entry.residentMask & uint16_t(1u << r)))
                continue;
            const std::string &key =
                Render::planRungKey(entry, size_t(r));
            if (key.empty())
                continue;
            auto &h = held[key];
            if (h.binders++ == 0) {
                h.bytes = Render::planRungSize(entry, size_t(r));
                bytes += h.bytes;
            }
        }
    }
    if (held.size() < s_held.size()) {
        decLog("%zu resident payloads died with the old scene",
               s_held.size() - held.size());
        if (s_streamDebug)
            std::printf("fcviewer: %zu resident payloads died with the old "
                        "scene\n", s_held.size() - held.size());
    }
    s_held.swap(held);
    s_residentBytes = bytes;
}

/// Whether the viewer has already said it is at its budget, so the
/// line is printed on the transition and not on every round.
static bool s_atBudgetReported = false;

/// Geometry the viewer may hold resident, past which a mesh is given
/// back rather than a new one refused. It bounds what the *scene*
/// costs, which is not what pruneBlobCache bounds: that one drops
/// payloads the scene does not name, and a model too large for memory
/// is one whose named geometry is itself the problem.
///
/// Payload bytes rather than the expanded arrays, because it is what
/// every part of the stream already states (SceneDump.h) and it tracks
/// the geometry within a constant factor. What matters for a budget is
/// that it is proportional and known before the fetch, not that it is
/// the exact heap cost — and the factor between the two is measured
/// rather than assumed, which is what fits the budget to a device
/// nobody tested on (Render::MemoryBudget).
static Render::MemoryBudget s_budget;

/// How strongly a payload's size counts against it, for the two
/// decisions that rank chunks (Render::RungRanker::value). 0 ignores size and
/// ranks purely by what the camera sees; 1 ranks strictly per byte.
///
/// **Fetching leans on size, keeping does not.** What to download next
/// is a question about a rate — the appearance layer costs a
/// thousandth of the geometry and lifts every object a whole rung, so
/// discounting by size is what puts a model in its real colours in a
/// fraction of a second. What to keep is a question about a stock, and
/// there size is beside the point: the near, detailed object is the
/// one worth its memory even though it is the expensive one.
///
/// The fetch default is a *square root* rather than the full per-byte
/// discount it started as. Per byte, a payload a thousand times
/// smaller was a thousand times preferred, which is far more than
/// "colour first" needs and left a large near mesh queued behind every
/// trivial distant one — visible as boxes in the foreground of a
/// half-loaded model. At 0.5 the same payload is thirty times
/// preferred: the appearance layer still arrives first by a wide
/// margin, and geometry is ordered much more by where it is.
///
/// Both are overridable per load — `?fetchweight=` and `?keepweight=`
/// — because the right values depend on the model and the link, and
/// tuning them should not need a rebuild.
static const float kFetchSizeWeight = 0.5f;
static const float kKeepSizeWeight = 0.0f;
static float s_fetchSizeWeight = kFetchSizeWeight;
static float s_keepSizeWeight = kKeepSizeWeight;

static size_t residentBytes()
{
    return s_residentBytes;
}

static size_t geometryBudget()
{
    return s_budget.value();
}

/// The heap, on a slow heartbeat. An allocation that fails takes the
/// page with it and leaves nothing to inspect, so whatever the heap
/// was doing in the seconds before has to have been said already — and
/// on a device with no console the only way it gets said is ?log
/// beaconing it somewhere else (shell.html). Beside it the budget, so
/// that "the viewer is holding too much" and "something else is" are
/// distinguishable without a debugger.
/// The same heartbeat is what fits the budget to this device: the two
/// numbers the adaptation needs are the two being reported, so it costs
/// a call. It runs whether or not anyone is reading — a budget that
/// only adapted under ?stream would be a different viewer from the one
/// people load.
static void reportHeap()
{
    static double last = 0.0;
    const double now = emscripten_get_now();
    if (now - last < 3000.0)
        return;
    last = now;
    const size_t heap = size_t(emscripten_get_heap_size());
    s_budget.observe(s_residentBytes, blobCacheBytes(), heap);
    // A budget that adapted past what the last plan was drawn against
    // is one of the events a plan answers to — an eighth either way,
    // so the heartbeat's ordinary jitter does not replan a quiet scene.
    if (!s_planStale && s_plannedBudget
            && (s_budget.value() > s_plannedBudget + s_plannedBudget / 8
                || s_budget.value() + s_plannedBudget / 8 < s_plannedBudget)) {
        s_planStale = true;
        decLog("budget moved %zu -> %zu MB -> replan",
               s_plannedBudget >> 20, s_budget.value() >> 20);
    }
    if (!s_streamDebug)
        return;
    std::printf("fcviewer: heap %zu MB, geometry %zu of %zu MB "
                "(x%.2f, ceiling %zu MB), %zu payloads resident, "
                "%zu cached\n",
                heap >> 20, s_residentBytes >> 20, geometryBudget() >> 20,
                s_budget.expansion(), s_budget.ceiling() >> 20,
                s_held.size(), s_blobCache.size());
}

static Render::SceneSnapshot s_pendingSnap;
static uint64_t s_pendingVersion = 0;
/// How long the view's own chunks may hold the model back without
/// arriving. The barrier below is what puts the navigation cube on
/// screen before the geometry, and a barrier with no release is a
/// deadlock waiting for a request that never completes: nothing else
/// is in flight to notice, and the model would sit at its box rung for
/// the rest of the session.
///
/// Measured from the last time the fetch made progress, not from when
/// the barrier engaged — that is the difference between a slow link
/// and a stalled one. A slow link keeps landing chunks and keeps the
/// barrier; only a fetch that has produced nothing at all for this
/// long gives it up. Six seconds is long enough for one batch over a
/// bad mobile link and short enough that a hang is a hesitation rather
/// than a broken page.
static const double kViewGraceMs = 6000.0;
/// When the fetch last resolved anything, or 0 while the view's chunks
/// are all in hand. Only view chunks are in flight while the barrier
/// holds, so any progress at all is progress on them.
static double s_viewProgressAt = 0.0;
/// A re-check already queued for the moment the grace runs out. The
/// release cannot be decided by the arrival that would have renewed it
/// — a stall is exactly the absence of one, so nothing would ever run
/// the check again and the model would wait forever. The hold
/// therefore schedules its own deadline.
static bool s_viewGraceScheduled = false;
/// Whether the release has already been reported for this stall, so
/// the note is one line rather than one per round.
static bool s_viewGraceReported = false;

/// ?nofetchorder — ask for every payload the moment it is named, in the
/// order the publish names them, as the viewer did before there was a
/// fetch order (docs/SceneStreaming.md §6). This is the benchmark
/// baseline: it hands the whole queue to the browser at once, which is
/// the fastest a scene can arrive and the least useful order for it to
/// arrive in.
static bool s_noFetchOrder = false;
/// ?lodpx=<pixels> — the screen-space error a coarser level may commit
/// before the exact mesh is required (§7, phase 5d). The selection
/// tolerance: a level's stated error times the owner's projected size
/// must land under this many pixels for the level to stand in. 0
/// disables selection entirely — every mesh entry fetches its finest
/// built level, the pre-5d behavior.
static float s_lodPx = 2.0f;
/// ?genlod — ask the server to build every declared-but-unbuilt level
/// the current publish names (§7, phase 5c). A debug stand-in for
/// level *selection* (phase 5d), which will ask for the one level a
/// draw actually wants; until then this is how the request channel and
/// the producer's work queue are exercised end to end.
static bool s_genLod = false;
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
/// Whether the staged snapshot has been offered the live scene's
/// resident rungs yet (carryResidentRungs). Cleared at staging, set by
/// the first resolve pass — which is the one that would otherwise ask
/// the network for geometry the scene already holds (see the adoption
/// site in resolvePending).
static bool s_pendingAdopted = false;

/// The store sweep, declared with the ledger above; the live set it
/// must not touch spans the live and the staged snapshot.
static void sweepStore()
{
    if (!blobPersistEnabled() || !s_storeMetaLoaded)
        return;
    if (s_storeMetaBytes <= storeBudget())
        return;
    std::set<std::string> live;
    const auto note = [&live](const Render::SceneSnapshot &snap) {
        for (const auto &entry : snap.deferredChunks) {
            if (!entry.key.empty())
                live.insert(entry.key);
            for (const auto &lvl : entry.levels) {
                if (!lvl.key.empty())
                    live.insert(lvl.key);
            }
        }
    };
    note(s_snap);
    if (s_pendingValid)
        note(s_pendingSnap);
    notePageLiveKeys(live);
    std::vector<std::pair<double, std::string>> order;
    for (const auto &kv : s_storeMeta) {
        if (!live.count(kv.first))
            order.emplace_back(kv.second.touched, kv.first);
    }
    std::sort(order.begin(), order.end());
    // To seven eighths, not to the line: a sweep per store-write once
    // at the boundary would be collection per allocation.
    const size_t target = storeBudget() - storeBudget() / 8;
    size_t droppedBytes = 0, dropped = 0;
    for (const auto &item : order) {
        if (s_storeMetaBytes <= target)
            break;
        auto it = s_storeMeta.find(item.second);
        if (it == s_storeMeta.end())
            continue;
        droppedBytes += it->second.bytes;
        ++dropped;
        dropStoreMeta(item.second);
        emscripten_idb_async_delete(kBlobDb, item.second.c_str(), nullptr,
                                    [](void *) {}, [](void *) {});
    }
    if (dropped) {
        decLog("store sweep: %zu blobs, %zu KB dropped — %zu of %zu MB "
               "tracked", dropped, droppedBytes >> 10,
               s_storeMetaBytes >> 20, storeBudget() >> 20);
        std::printf("fcviewer: store sweep: %zu blobs, %zu KB dropped — "
                    "%zu of %zu MB tracked\n",
                    dropped, droppedBytes >> 10, s_storeMetaBytes >> 20,
                    storeBudget() >> 20);
    }
}

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

/// What the local payload cache is holding, in the bytes it arrived as.
/// Reported to the budget, which must not charge them to geometry as
/// expansion: they are raw payloads held about one for one.
static size_t blobCacheBytes()
{
    size_t total = 0;
    for (const auto &entry : s_blobCache)
        total += entry.second ? entry.second->size() : 0;
    return total;
}

static void pruneBlobCache()
{
    // A payload that has been parsed is held twice: as the bytes it
    // arrived as, and as the geometry they were read into. The second
    // is what the scene draws and what the budget bounds (§6 phase
    // 4b); the first is finished with the moment the fill returns, and
    // keeping it means a viewer that has been told to hold 8 MB of
    // geometry is really holding that plus every byte it ever
    // downloaded. Dropping it costs a local read if the payload is
    // ever wanted again — which is exactly what eviction already
    // assumes, since it drops the payload too.
    for (const auto &res : s_held)
        s_blobCache.erase(res.first);
    const size_t total = blobCacheBytes();
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
    s_netInFlight.erase(key);
    s_blobCache[key] = data;
    if (!fromDb && data && blobPersistEnabled()) {
        // Persist for the next page load. The store is keyed by content
        // hash, so this never overwrites anything with different bytes.
        // Noted in the ledger optimistically — a failed store leaves a
        // phantom entry whose sweep-delete is harmless.
        touchStoreMeta(key, uint32_t(data->size()));
        emscripten_idb_async_store(
            kBlobDb, key.c_str(), data->data(), int(data->size()),
            nullptr, [](void *) {},
            [](void *) {
                std::printf("fcviewer: blob store to IndexedDB failed\n");
            });
    }
    if (data)
        pageBlobResolved(key, data->data(), data->size());
    if (!s_batchApplying)
        resolvePending();
}

static void blobFailed(const std::string &key)
{
    s_blobInFlight.erase(key);
    s_netInFlight.erase(key);
    s_blobFailed.insert(key);
    std::printf("fcviewer: blob %s unavailable, dropped\n", key.c_str());
    pageBlobFailed(key);
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
    // A request that dies without an answer — a phone backgrounded
    // mid-transfer, a tunnel that folds without an RST — otherwise
    // fires NEITHER callback: the window slot never comes back, the
    // key stays "in flight", and enough of them is a viewer that sits
    // below its plan forever (measured: six minutes of coarse spheres
    // with a quarter of the budget free). The timeout turns silence
    // into onerror, which fails the keys and frees the slot; the
    // in-flight sweep then re-asks.
    attr.timeoutMSecs = (unsigned long)kInFlightTimeoutMs;
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
    std::string url = s_sceneUrl + "/blob?key=" + key + tokenQuery();
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
    // Same silence-into-onerror turn as fetchBlob, with headroom: a
    // batch is up to a third of a megabyte and the server may be
    // tessellating ahead of the reply. Timing out work the server is
    // still doing only costs a duplicate ask; never timing out costs
    // the stall.
    attr.timeoutMSecs = (unsigned long)(2.0 * kInFlightTimeoutMs);
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
    if (!s_tokenParam.empty())
        url += "?token=" + urlEncode(s_tokenParam);
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
    // The loading indicator answers to this set, and the IndexedDB
    // probe that routes here is asynchronous — on a cold store the
    // status round runs before any miss has landed, so the transition
    // to "the network is actually involved" has to say so itself or
    // the bar would wait for the first arrival to appear.
    if (s_netInFlight.insert(key).second && s_netInFlight.size() == 1)
        fcviewer_status("loading scene", 0.0, 0.0);
    if (size && size < kOwnRequestBytes)
        queueBatch(key, size);
    else
        fetchBlob(key);
}

/// IndexedDB first — a reload or a revisit skips the network entirely.
static void requestBlob(const std::string &key, uint32_t size = 0)
{
    if (!s_blobInFlight.emplace(key, emscripten_get_now()).second)
        return;
    armHeartbeat();
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
            // `ptr` belongs to the caller: emscripten_idb_async_load's
            // glue frees the buffer as soon as this returns
            // (libidbstore.js). Freeing it here as well put a block on
            // the free list twice, which dlmalloc does not notice —
            // the damage surfaced later as a trap inside malloc on
            // some unrelated allocation, and only ever on a visit with
            // a warm store, because a cold one never takes this path.
        },
        [](void *arg) {
            // Not stored yet (the common first-visit case) or the store
            // is unavailable: go to the network.
            std::unique_ptr<std::pair<std::string, uint32_t>> item(
                static_cast<std::pair<std::string, uint32_t> *>(arg));
            fetchFromNetwork(item->first, item->second);
        });
}

//////////////////////////////////////////////////////////////////////
// Page-tier chunk plumbing (doc sec 24.4): the wanted tables the page
// state above declared, resolved through the same kind-blind blob
// machinery the 3D tier fetches with.

/// A chunk the page waits on landed (any route: cache, IndexedDB,
/// network, inline delta bytes).
static void pageBlobResolved(const std::string &key, const uint8_t *data,
                             size_t size)
{
    auto wi = s_pageItemWanted.find(key);
    if (wi != s_pageItemWanted.end()) {
        for (uint64_t id : wi->second) {
            // Apply only if the stream still names this key for the id
            // (a later publish may have re-keyed it while the fetch
            // was out).
            auto ck = s_pageItemKey.find(id);
            if (ck == s_pageItemKey.end() || ck->second != key)
                continue;
            Render::Page2D::Kind kind;
            uint32_t layer = 0;
            std::vector<uint8_t> ops;
            if (Render::decodePageItemChunk(data, size, kind, layer, ops))
                s_page->setItem(id, kind, layer, std::move(ops));
            else
                std::printf("fcviewer: page item chunk %s undecodable\n",
                            key.c_str());
        }
        s_pageItemWanted.erase(wi);
        markDirty();
    }
    auto ii = s_pageImageWanted.find(key);
    if (ii != s_pageImageWanted.end()) {
        for (uint64_t id : ii->second) {
            const PageImageMeta &meta = s_pageImageMeta[id];
            if (size == size_t(meta.w) * meta.h * 4) {
                s_page->setImage(id, meta.w, meta.h, data, meta.repeat);
                s_pageImageApplied[id] = key;
            }
            else {
                std::printf("fcviewer: page image chunk %s wrong size\n",
                            key.c_str());
            }
        }
        s_pageImageWanted.erase(ii);
        markDirty();
    }
    auto fi = s_pageFontWanted.find(key);
    if (fi != s_pageFontWanted.end()) {
        Render::Page2D::registerFont(fi->second.c_str(), data,
                                     uint32_t(size));
        s_pageFontsApplied.insert(fi->second);
        s_pageFontWanted.erase(fi);
        markDirty();
    }
}

static void pageBlobFailed(const std::string &key)
{
    s_pageItemWanted.erase(key);
    s_pageImageWanted.erase(key);
    s_pageFontWanted.erase(key);
}

/// Want a chunk: the cache answers now, everything else answers
/// through blobResolved.
static void pageWantChunk(const std::string &key, uint32_t size)
{
    auto it = s_blobCache.find(key);
    if (it != s_blobCache.end() && it->second) {
        pageBlobResolved(key, it->second->data(), it->second->size());
        return;
    }
    requestBlob(key, size);
}

static bool applyPagePayload(uint64_t version, const char *data,
                             size_t size)
{
    Render::PageSnapshot snap;
    if (!Render::loadPageSnapshot(data, size, snap)) {
        std::printf("fcviewer: page payload parse FAILED\n");
        return false;
    }
    if (!s_page)
        s_page = std::make_unique<Render::Page2D>();

    // A version means something only within the run that issued it --
    // the same v35 contract as the scene stream; the blob cache
    // survives (content keys).
    if (snap.sessionId != s_pageSession) {
        if (s_pageSession)
            std::printf("fcviewer: page session changed, dropping the "
                        "page\n");
        s_pageSession = snap.sessionId;
        s_page->clear();
        s_pageItemKey.clear();
        s_pageImageApplied.clear();
        s_pageImageMeta.clear();
        s_pageItemWanted.clear();
        s_pageImageWanted.clear();
        s_pageFontWanted.clear();
        s_pageVersion = 0;
        s_pageUserView = false;
    }
    else if (s_pageMode && version == s_pageVersion)
        return true; // the reconnect echo, already applied

    if (snap.baseVersion && snap.baseVersion != s_pageVersion) {
        // A delta chained onto a version this viewer does not hold:
        // ask for the full payload; the push loop answers in order.
        std::printf("fcviewer: page delta base %llu != held %llu, "
                    "resync\n",
                    (unsigned long long)snap.baseVersion,
                    (unsigned long long)s_pageVersion);
        if (s_wsOpen) {
            static const char resync[] = "{\"cmd\":\"resync\"}";
            emscripten_websocket_send_utf8_text(
                s_ws, const_cast<char *>(resync));
        }
        return true;
    }

    const bool full = snap.baseVersion == 0;
    const bool firstSheet = !(s_pageW > 0.0f);
    s_pageMode = true;
    s_pageW = snap.pageWidth;
    s_pageH = snap.pageHeight;
    pagePaper();
    if (firstSheet || !s_pageUserView)
        pageFit();

    for (const auto &f : snap.fonts) {
        if (s_pageFontsApplied.count(f.name))
            continue;
        s_pageFontWanted[f.key] = f.name;
        pageWantChunk(f.key, f.size);
    }

    for (const auto &img : snap.images) {
        PageImageMeta meta;
        meta.w = img.width;
        meta.h = img.height;
        meta.repeat = img.repeat != 0;
        s_pageImageMeta[img.id] = meta;
        auto it = s_pageImageApplied.find(img.id);
        if (it != s_pageImageApplied.end() && it->second == img.key)
            continue;
        s_pageImageWanted[img.key].insert(img.id);
        pageWantChunk(img.key, img.size);
    }
    if (full) {
        std::set<uint64_t> namedImages;
        for (const auto &img : snap.images)
            namedImages.insert(img.id);
        for (auto it = s_pageImageApplied.begin();
             it != s_pageImageApplied.end();) {
            if (!namedImages.count(it->first)) {
                s_page->removeImage(it->first);
                s_pageImageMeta.erase(it->first);
                it = s_pageImageApplied.erase(it);
            }
            else
                ++it;
        }
    }

    for (uint64_t id : snap.removed) {
        s_page->removeItem(id);
        s_pageItemKey.erase(id);
    }
    std::set<uint64_t> named;
    for (auto &up : snap.updates) {
        const uint64_t id = up.entry.objectKey;
        named.insert(id);
        auto ck = s_pageItemKey.find(id);
        if (ck != s_pageItemKey.end() && ck->second == up.entry.key)
            continue; // unchanged: the retained item stands
        s_pageItemKey[id] = up.entry.key;
        s_pageItemWanted[up.entry.key].insert(id);
        if (up.hasInline) {
            // v37: a delta's chunk rides inline -- ingest under its
            // key, store included, then resolve like any arrival.
            auto bytes = std::make_shared<std::vector<uint8_t>>(
                std::move(up.inlineData));
            blobResolved(up.entry.key, bytes, false);
        }
        else
            pageWantChunk(up.entry.key, up.entry.size);
    }
    if (full) {
        // A full root retires what it does not name.
        for (auto it = s_pageItemKey.begin(); it != s_pageItemKey.end();) {
            if (!named.count(it->first)) {
                s_page->removeItem(it->first);
                it = s_pageItemKey.erase(it);
            }
            else
                ++it;
        }
    }

    s_pageVersion = version;
    // Ride the shared stream-position state, so reconnects and the
    // polling fallback report where this viewer actually is.
    s_sessionId = snap.sessionId;
    s_sceneVersion = version;
    std::printf("fcviewer: page v%llu, %zu items named, sheet %.0fx%.0f\n",
                (unsigned long long)version, snap.updates.size(),
                (double)s_pageW, (double)s_pageH);
    markDirty();
    return true;
}

/// Assemble a snapshot out of the payloads that have arrived so far.
/// False means it cannot be applied at all and the caller must ask for
/// a full scene.
/// Ground truth for the box hunt: the objects whose scene draws are
/// stand-ins right now. The plan's own view of who is on the box (the
/// "plan left" lines) can disagree with this — a draw is a box
/// whenever its mesh is not resident, whatever the plan says — and
/// that disagreement is exactly what this line exists to expose.
/// Logged only when the set changes; assembly runs per arrival.
static void logStandIns(const Render::SceneSnapshot &snap)
{
    static std::set<uint64_t> s_lastStanding;
    std::set<uint64_t> standing;
    for (const auto &d : snap.scene) {
        if (d.standIn && d.objectKey)
            standing.insert(d.objectKey);
    }
    if (standing == s_lastStanding)
        return;
    s_lastStanding = standing;
    if (standing.empty()) {
        decLog("drawn as box: none");
        return;
    }
    std::string keys;
    size_t n = 0;
    for (uint64_t k : standing) {
        if (n++ >= 16) {
            keys += " ...";
            break;
        }
        char buf[24];
        std::snprintf(buf, sizeof(buf), " %llx", (unsigned long long)k);
        keys += buf;
    }
    decLog("drawn as box: %zu objects:%s", standing.size(), keys.c_str());
    // When only a few are left, their ladders' whole state: the draw
    // says box, so which chunk is it waiting on, and what does the
    // fetch layer believe about that chunk? A mismatch here — resident
    // payload under one key, draw waiting on another — is the re-key
    // residual; "in flight" that never lands is the stall.
    if (standing.size() <= 3) {
        for (uint64_t k : standing) {
            for (const auto &entry : snap.deferredChunks) {
                bool owned = false;
                for (uint64_t owner : entry.owners)
                    owned = owned || owner == k;
                if (!owned)
                    continue;
                decLog("  box obj %llx chunk %.8s rung(resident %x, "
                       "plan %d, asked %d)%s%s%s",
                       (unsigned long long)k, entry.key.c_str(),
                       unsigned(entry.residentMask), int(entry.plan),
                       int(entry.asked),
                       entry.fill ? " fill" : "",
                       entry.refill ? "" : " NO-REFILL",
                       s_blobInFlight.count(entry.key) ? " in-flight" : "");
            }
        }
    }
}

static bool assembleResolved(Render::SceneSnapshot &snap)
{
    // The manifest layout stages a group's draws in a fixed slot and
    // its materials in a table; this is the pass that puts both where
    // the backend expects them (SceneDump.h).
    if (!snap.finalize)
        return true;
    {
        DbgScope dbg(s_dbgFinalizeMs);
        snap.finalize(snap);
    }
    DbgScope dbg(s_dbgObjectsMs);
    // Then the objects, which the snapshot alone cannot assemble:
    // a delta names only what changed, so the feed is built from
    // the model this viewer carries between publishes. Each draw
    // comes out at the best rung it holds — the geometry if it has
    // landed, a box on its bounds if it has not — so this runs per
    // arrival rather than once, and each pass refines the last.
    if (Render::applySceneObjects(snap, s_objects)) {
        logStandIns(snap);
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

/// Whether anything the executor would still act on is left: geometry
/// below its planned rung or with a request out, or a manifest-layer
/// chunk not yet in hand. Geometry standing at its plan is *done* even
/// though its ladder could go finer — which is what lets a scene under
/// a budget ever count as complete (§7).
static bool stillArriving(const Render::SceneSnapshot &snap)
{
    for (const auto &entry : snap.deferredChunks) {
        if (entry.release) {
            if (entry.asked >= 0)
                return true;
            if (Render::planStep(entry,
                                 Render::planNeeded(entry, objectErrOf))
                    .fetch >= 0)
                return true;
        }
        else if (entry.fill)
            return true;
    }
    return false;
}

/// The heartbeat's condition: anything at all between the scene and
/// its plan. Wider than stillArriving() on purpose — a ladder standing
/// on a sibling rung while a generate is out counts as *arrived* there
/// (a scene under a budget must be able to complete) but not as *at
/// plan* here: the generate's answer is an announcement, and if the
/// producer dropped the job no arrival will ever re-ask. The heartbeat
/// is what does.
static bool sceneBelowPlan()
{
    if (!s_blobInFlight.empty())
        return true;
    const Render::SceneSnapshot &snap =
        s_pendingValid ? s_pendingSnap : s_snap;
    for (const auto &entry : snap.deferredChunks) {
        if (!entry.release) {
            if (entry.fill)
                return true;
            continue;
        }
        // Surplus is deliberately not "below plan": a kept stand-in is
        // the budget's business (evicted under pressure, not on a
        // heartbeat), and counting it would keep the heartbeat awake
        // over a scene that is finished.
        const auto step = Render::planStep(
            entry, Render::planNeeded(entry, objectErrOf));
        if (step.fetch >= 0 || step.generate >= 0)
            return true;
    }
    return false;
}

/// See the declaration above requestBlob: one timer, re-armed from its
/// own wake-up, quiet the moment the scene is at plan.
static void armHeartbeat()
{
    if (s_retryScheduled)
        return;
    s_retryScheduled = true;
    emscripten_async_call([](void *) {
        s_retryScheduled = false;
        if (s_blobStoreReset || !sceneBelowPlan())
            return;
        resolvePending();
        armHeartbeat();
    }, nullptr, int(kInFlightTimeoutMs / 4));
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
            // A mis-based delta while the root that will supersede it
            // is already on its way is noise, not a new emergency.
            decLog("full scene: staged delta base v%llu vs model v%llu",
                   (unsigned long long)snap.baseVersion,
                   (unsigned long long)s_objects.version);
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
            decLog("full scene: live delta base v%llu vs model v%llu",
                   (unsigned long long)s_snap.baseVersion,
                   (unsigned long long)s_objects.version);
            requestFullScene();
            return;
        }
        // Refit while the camera is still the automatic one: the fit
        // runs off the bounding boxes the root named, so it does not
        // move as the geometry inside them arrives.
        applySnapshot(!s_userCam);
        if (!s_liveOutstanding) {
            fcviewer_status(nullptr, 0.0, 0.0);
            // The breakdown that matters is the one for the whole
            // load, and the periodic report is unlikely to land on its
            // last pass.
            if (s_streamDebug)
                dbgReport();
        }
    }
    else {
        return;
    }
    pruneBlobCache();
    // A commit is what the held deltas queue behind (one stages only
    // once the one before it is in the model): let the next one go.
    if (!s_heldPayloads.empty() && !s_heldDrainScheduled) {
        s_heldDrainScheduled = true;
        emscripten_async_call(drainHeldPayloads, nullptr, 0);
    }
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

/// This tier's answer to "how is a rung obtained": the local store, and
/// the network behind it. Everything about it is browser-shaped — an
/// IndexedDB read, a batched GET, a window counted in round trips — and
/// none of that is visible to the policy that drives it
/// (Render::RungProvider).
///
/// `generate` asks the server's work queue (docs/SceneStreaming.md §7,
/// phase 5c): a declared level has no bytes and no key, so it cannot
/// be fetched, only asked to be built — GET /level, fire and forget.
/// The answer never comes back on this request; it arrives as an
/// ordinary publish whose manifest names the new key.
class ViewerRungProvider : public Render::RungProvider {
public:
    void request(const std::string &key, uint32_t size) override
    {
        requestBlob(key, size);
    }

    size_t outstanding() const override { return s_requestsInFlight; }

    size_t window() const override { return kInFlightRequests; }

    void flush() override { flushBatch(); }

    bool generate(const Render::LevelRequest &req) override
    {
        if (s_sceneUrl.empty())
            return false;
        // Dedup with a deadline, not for a page life: the answer to a
        // generate is an announcement, and a producer that dropped the
        // job (a restarted backend, a request lost on a folding link)
        // sends none — a memo with no expiry left the ladder coarse
        // forever, silently. The server dedups too, so a re-ask of a
        // job still running costs a 202 and nothing else.
        const double now = emscripten_get_now();
        auto memo = asked.find({req.source, req.level});
        if (memo != asked.end()) {
            if (now - memo->second < kGenerateTimeoutMs)
                return true;
            decLog("generate level %u of %.8s unanswered for %.0fs "
                   "-> re-ask", req.level, req.source.c_str(),
                   (now - memo->second) / 1000.0);
        }
        asked[{req.source, req.level}] = now;
        decLog("generate level %u of %.8s", req.level, req.source.c_str());
        if (s_streamDebug)
            std::printf("fcviewer: asking for level %u of %s\n",
                        req.level, req.source.c_str());
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strcpy(attr.requestMethod, "GET");
        // Deliberately outside the fetch window: this carries no
        // payload and competes with nothing — the reply is an empty
        // 202 either way.
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.onsuccess = [](emscripten_fetch_t *fetch) {
            emscripten_fetch_close(fetch);
        };
        attr.onerror = [](emscripten_fetch_t *fetch) {
            emscripten_fetch_close(fetch);
        };
        std::string url = s_sceneUrl + "/level?source=" + req.source
            + "&level=" + std::to_string(req.level) + docQuery()
            + tokenQuery();
        emscripten_fetch(&attr, url.c_str());
        return true;
    }

private:
    /// When each job was last asked for. How long to trust the memo:
    /// generous — generation on a loaded backend takes real time and a
    /// re-ask of a live job is harmless, but the announcement usually
    /// lands within seconds.
    static constexpr double kGenerateTimeoutMs = 60000.0;
    std::map<std::pair<std::string, uint32_t>, double> asked;
};

static ViewerRungProvider s_provider;

/// The request for rung \a index of \a entry's ladder. The source is
/// the finest *built* rung's key — on a coarse-first ladder the exact
/// mesh has no key to name, and the server canonicalizes whichever
/// built sibling this happens to be onto one job identity. The exact
/// rung itself (error 0) is asked for by its sentinel, because ladder
/// positions of the coarser rungs double as generator grid levels and
/// the exact mesh is not on that grid.
static Render::LevelRequest levelRequestFor(
        const Render::SceneSnapshot::DeferredChunk &entry, size_t index)
{
    Render::LevelRequest req;
    for (size_t i = entry.levels.size(); i-- > 0;) {
        if (!entry.levels[i].key.empty()) {
            req.source = entry.levels[i].key;
            break;
        }
    }
    req.level = entry.levels[index].error == 0.0f
        ? Render::kExactMeshLevel
        : uint32_t(index);
    return req;
}

/// The ranking half of the ladder now lives in ../SceneLadder.h, where
/// the desktop can reach it too: what a payload is worth is a question
/// about a camera and a bounding box, and neither half of that changes
/// because the payload arrives over a socket rather than out of a
/// tessellator (docs/SceneStreaming.md §7).
///
/// What stays here is what only this tier can answer: where the camera
/// is, and where to look up the bounds of an object key — the viewer
/// has two places to look, because a staged publish announces an
/// object's bounds before the object itself has been applied.
static Render::RungRanker makeRanker()
{
    const CamFrame cam = camFrame();
    Render::LadderView view;
    const auto put = [](float *dst, const bx::Vec3 &v) {
        dst[0] = v.x;
        dst[1] = v.y;
        dst[2] = v.z;
    };
    put(view.eye, cam.eye);
    put(view.at, cam.at);
    put(view.right, cam.right);
    put(view.up, cam.up);
    view.fovY = kFovY;
    view.aspect = vpH() > 0.0f ? vpW() / vpH() : 1.0f;

    Render::LadderWeights weights;
    weights.acquire = s_fetchSizeWeight;
    weights.keep = s_keepSizeWeight;

    return Render::RungRanker(view, [](uint64_t key) -> const float * {
        auto it = s_objects.objects.find(key);
        if (it != s_objects.objects.end())
            return it->second.entry.bbox;
        auto pit = s_pendingBox.find(key);
        if (pit != s_pendingBox.end())
            return pit->second.data();
        return nullptr;
    }, weights);
}

/// Walk one payload back down the ladder: give the geometry back and
/// leave the entry as it was before it arrived, so the ordinary fetch
/// path can climb it again if the camera comes back
/// (docs/SceneStreaming.md §6, phase 4b).
///
/// The draws that named the mesh are not touched. They keep naming it,
/// and the next assembly finds it empty and puts them on the box at
/// their bounds — the same choice it made while the mesh was still on
/// its way. That is the whole point of the ladder running backwards:
/// there is no eviction state, only a rung.
static void releaseRungs(Render::SceneSnapshot::DeferredChunk &entry,
                         uint16_t mask)
{
    if (!entry.levelMeshes)
        return;
    const int count = std::min<int>(int(Render::planRungs(entry)), 16);
    for (int r = 0; r < count; ++r) {
        const uint16_t bit = uint16_t(1u << r);
        if (!(mask & bit) || !(entry.residentMask & bit))
            continue;
        const std::string &key = Render::planRungKey(entry, size_t(r));
        entry.levelMeshes->release(key);
        entry.residentMask &= uint16_t(~bit);
        // The payload itself, which is the other half of what it
        // costs to hold. The local store keeps it, so what was just
        // given up is a read and not a download.
        s_blobCache.erase(key);
        unbindRung(key);
    }
    // The feed may still name a mesh that was just emptied, so the
    // scene on screen is a rung out of date until it is rebuilt.
    s_liveUnapplied = true;
}

/// A geometry arrival: parse \a data as \a rung into that rung's own
/// mesh object (§7, "one rung per instance") and bind it in the books.
/// Nothing is overwritten — a sibling rung another instance stands on
/// keeps its arrays until the plan releases it.
static bool fillRung(Render::SceneSnapshot::DeferredChunk &entry, int rung,
                     const void *data, size_t size)
{
    if (!entry.levelMeshes || rung < 0 || rung >= 16)
        return false;
    const std::string &key = Render::planRungKey(entry, size_t(rung));
    if (key.empty() || !entry.levelMeshes->fill(key, data, size))
        return false;
    // A rung that does not say what it was built at is indistinguishable
    // from the exact tessellation to everything past the binder, and the
    // element gate's coarse-faces rule is one of those things: it holds
    // an object's edges back while its FACES are still rough. Without
    // this the browser's whole coarse half is structurally dead.
    entry.levelMeshes->stampError(key, Render::planRungError(entry,
                                                             size_t(rung)));
    // The identity parse is no longer outstanding either way — the
    // generic closure and this are two doors into the same store.
    entry.fill = nullptr;
    const uint16_t bit = uint16_t(1u << rung);
    if (!(entry.residentMask & bit)) {
        entry.residentMask |= bit;
        bindRung(key, Render::planRungSize(entry, size_t(rung)));
    }
    if (entry.asked == rung)
        entry.asked = -1;
    return true;
}

/// Run the planner if any event marked it stale: what the whole scene
/// should hold, one target rung per owned geometry entry
/// (docs/SceneStreaming.md §7). Cheap enough to run on discovery —
/// a few hundred entries through a greedy heap — and deterministic,
/// so re-running it against unchanged inputs re-produces the plan
/// rather than perturbing it.
static void planIfStale(Render::SceneSnapshot &target)
{
    if (!s_planStale)
        return;
    // A pending DELTA is not a plan universe: it names only what
    // changed, and a plan drawn over three objects' ladders against
    // the whole budget grants them their unbudgeted desire — measured
    // on a phone as 73 mini-plans ("plan: 3 objects, 0 capped")
    // re-granting exact meshes faster than the merged plan's
    // downgrades could take them back: geometry 10 of 8 MB, stable,
    // zero releases. Stay stale instead: the commit merges the delta
    // over the model (carryLadders) and replans over the whole scene;
    // until then the executor treats the delta's entries as unplanned,
    // and what an unplanned entry with nothing resident fetches is the
    // coarsest built rung — a few kilobytes that show the object — not
    // the exact mesh. A pending FULL root still plans here: its
    // universe is complete by definition, and the cold load's fetch
    // order depends on it (§6).
    if (s_pendingValid && &target == &s_pendingSnap && target.baseVersion)
        return;
    s_planStale = false;
    Render::RungRanker ranker = makeRanker();
    Render::PlanParams params;
    params.budgetBytes = geometryBudget();
    params.tolerancePx = s_lodPx;
    params.viewportPx = vpH();
    // Per-object grants for the executor's needed sets and the
    // per-instance draw binding (§7, "one rung per instance").
    params.objectErr = &s_objectErr;
    /// The objects the plan left on the box, kept for the journal: a
    /// box that reads as "nearby" on the screen while far neighbors
    /// hold meshes is either a huge diamPx the greedy still skipped —
    /// a costing bug — or a tiny diamPx on a visibly large object,
    /// which is a scoring bug. The journal line tells the two apart.
    struct Boxed {
        float diamPx;
        uint64_t key;
        size_t tiers;
    };
    std::vector<Boxed> boxed;
    /// Objects granted less than their desired tier, biggest first:
    /// the budget's actual losers. The one reading worth doing when a
    /// large object sits coarse at a full budget — if it tops this
    /// list the knapsack chose wrong; absent from it, the plan wanted
    /// it fine and the fetch layer is where to look.
    struct Capped {
        float diamPx;
        uint64_t key;
        int tier;
        size_t tiers;
    };
    std::vector<Capped> capped;
    params.trace = [&boxed, &capped](uint64_t key, float diamPx, int tier,
                                     size_t tiers) {
        if (tier < 0)
            boxed.push_back({diamPx, key, tiers});
        else if (tier + 1 < int(tiers))
            capped.push_back({diamPx, key, tier, tiers});
    };
    const auto stats = Render::planLevels(target, ranker, params);
    if (!capped.empty()) {
        std::sort(capped.begin(), capped.end(),
                  [](const Capped &a, const Capped &b) {
                      return a.diamPx > b.diamPx;
                  });
        const size_t n = std::min<size_t>(capped.size(), 5);
        for (size_t i = 0; i < n; ++i)
            decLog("plan capped obj %llx diam %.0fpx at tier %d of %zu",
                   (unsigned long long)capped[i].key,
                   double(capped[i].diamPx), capped[i].tier,
                   capped[i].tiers);
    }
    if (!boxed.empty()) {
        std::sort(boxed.begin(), boxed.end(),
                  [](const Boxed &a, const Boxed &b) {
                      return a.diamPx > b.diamPx;
                  });
        const CamFrame cam = camFrame();
        decLog("plan left %zu objects on the box (eye %.1f %.1f %.1f), "
               "largest first:",
               boxed.size(), double(cam.eye.x), double(cam.eye.y),
               double(cam.eye.z));
        const size_t n = std::min<size_t>(boxed.size(), 12);
        for (size_t i = 0; i < n; ++i) {
            const float *bb = nullptr;
            auto it = s_objects.objects.find(boxed[i].key);
            if (it != s_objects.objects.end())
                bb = it->second.entry.bbox;
            else {
                auto pit = s_pendingBox.find(boxed[i].key);
                if (pit != s_pendingBox.end())
                    bb = pit->second.data();
            }
            if (bb)
                decLog("  box obj %llx diam %.1fpx tiers %zu "
                       "bbox (%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
                       (unsigned long long)boxed[i].key,
                       double(boxed[i].diamPx), boxed[i].tiers,
                       double(bb[0]), double(bb[1]), double(bb[2]),
                       double(bb[3]), double(bb[4]), double(bb[5]));
            else
                decLog("  box obj %llx diam %.1fpx tiers %zu (no bbox)",
                       (unsigned long long)boxed[i].key,
                       double(boxed[i].diamPx), boxed[i].tiers);
        }
    }
    s_planCapped = stats.capped;
    s_plannedBytes = stats.plannedBytes;
    s_plannedBudget = params.budgetBytes;
    decLog("plan: %zu objects, %zu KB of %zu MB budget, %zu capped "
           "(tol %.1fpx)",
           stats.objects, stats.plannedBytes >> 10,
           params.budgetBytes >> 20, stats.capped,
           double(params.tolerancePx));
    if (s_streamDebug)
        std::printf("fcviewer: plan: %zu objects, %zu KB targeted of "
                    "%zu MB budget, %zu capped\n",
                    stats.objects, stats.plannedBytes >> 10,
                    params.budgetBytes >> 20, stats.capped);
}

/// What the budget is actually holding, and what it turned away — the
/// two score distributions side by side, because that is the only way
/// to read *why* a particular mesh is a box (§6 phase 4b).
///
/// The scores are what the fetch order and the eviction both rank by:
/// the projected size of the best object that wants this payload,
/// divided by the payload's bytes. Printing the quartiles of each set
/// says whether the boundary between them is sharp — a clean split
/// means the camera decided it — or whether the two overlap, which
/// means the resident set is frozen: with a margin on displacement,
/// payloads of comparable value cannot take each other's place, so
/// whichever arrived first stays.
static void reportBudget(Render::SceneSnapshot &snap)
{
    Render::RungRanker order = makeRanker();
    std::vector<float> in, out;
    size_t inBytes = 0, outBytes = 0;
    for (const auto &entry : snap.deferredChunks) {
        if (!entry.release)
            continue;
        if (entry.residentMask) {
            in.push_back(order.residency(entry));
            const int count =
                std::min<int>(int(Render::planRungs(entry)), 16);
            for (int r = 0; r < count; ++r) {
                if (entry.residentMask & uint16_t(1u << r))
                    inBytes += Render::planRungSize(entry, size_t(r));
            }
        }
        else {
            out.push_back(order.residency(entry));
            outBytes += entry.size;
        }
    }
    auto q = [](std::vector<float> &v, const char *what, size_t bytes) {
        if (v.empty()) {
            std::printf("fcviewer:   %s: none\n", what);
            return;
        }
        std::sort(v.begin(), v.end());
        std::printf("fcviewer:   %s: %zu chunks, %zu KB, score "
                    "min %.3g / q1 %.3g / med %.3g / q3 %.3g / max %.3g\n",
                    what, v.size(), bytes >> 10, v.front(),
                    v[v.size() / 4], v[v.size() / 2], v[(3 * v.size()) / 4],
                    v.back());
    };
    q(in, "resident", inBytes);
    q(out, "wanted  ", outBytes);
}

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
            dbgReport();
    } } acc{dbgT0};
    // A stale plan re-opens a live scene that had nothing outstanding:
    // a camera settling over a complete scene is how upgrades and
    // releases happen at all once a load has converged (§7).
    Render::SceneSnapshot *target = s_pendingValid ? &s_pendingSnap
        : ((s_liveOutstanding || (s_planStale && s_haveScene)) ? &s_snap
                                                               : nullptr);
    if (!target)
        return;
    size_t missing = 0, total = 0;
    /// Releasable bytes outstanding, reported beside the budget: a
    /// viewer that is "full" while holding almost nothing is one whose
    /// allowance has been eaten by requests that never landed, which
    /// is otherwise invisible from outside.
    size_t pledgedBytes = 0;
    /// The same two counts in bytes, which is what the progress
    /// indicator is driven from. Counting chunks measures the wrong
    /// thing by an order of magnitude: a scene's payload is
    /// concentrated in a minority of large meshes, so the many small
    /// manifests and materials resolve early and carry the bar to
    /// four fifths while four fifths of the *bytes* are still coming.
    /// Measured on the 200-object scene, it reached 78% eleven
    /// seconds in and spent the next eleven crawling to 81% — which
    /// reads as a hang followed by a jump. Every deferred payload
    /// states its size (§5), so weighting by it costs nothing.
    size_t missingBytes = 0, totalBytes = 0;
    bool incomplete = false;
    /// Whether anything new became drawable this round. Without it a
    /// commit would be scheduled for every payload that merely failed
    /// or was already in hand.
    bool filled = false;
    /// The same, restricted to the view's own chunks. It is what
    /// renews the barrier's grace, and it has to be *its* progress:
    /// counting any arrival would let the model chunks released by an
    /// expired grace renew it and re-engage the hold they just
    /// escaped, so a stalled overlay would stutter the whole load
    /// instead of stepping out of the way once.
    bool viewFilled = false;

    // Every out-of-band payload the snapshot named — group manifests,
    // meshes, materials, shaders, textures — in one pass. A fill can
    // name further payloads (a group names its meshes and materials, a
    // material names its textures), so this runs in rounds until one
    // resolves nothing new rather than in a single pass.
    const size_t knownChunks = target->deferredChunks.size();
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < target->deferredChunks.size(); ++i) {
            auto &entry = target->deferredChunks[i];
            if (!entry.fill)
                continue;
            // Geometry answers to the plan, and its arrivals are the
            // executor's below — which rung of a ladder these bytes
            // are is a decision, not a key lookup (§7, "the ladder
            // owns its fetch state").
            if (entry.release)
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
            const bool wasView = entry.owners.empty();
            // A null payload asks the entry to give up. Whether that
            // is survivable is its business, not ours — a texture says
            // yes and the draw renders untextured.
            bool ok = failed
                ? fill(*target, nullptr, 0)
                : fill(*target, it->second->data(), it->second->size());
            // `entry` is not valid from here on — a group's fill names
            // its meshes and materials, appending them, and a reference
            // into the vector does not survive the growth.
            // Progress of any kind, which is what the held deltas'
            // grace measures a stall against.
            if (ok && !failed)
                s_lastFillAt = emscripten_get_now();
            if (ok) {
                progress = true;
                filled = true;
                viewFilled = viewFilled || wasView;
            }
            else {
                if (!failed)
                    resetBlobStore();
                incomplete = true;
            }
        }
    }
    // A fill that named further payloads grew the ladder set — group
    // manifests appending their meshes — and entries the plan has
    // never seen must not fetch at a default the next plan would
    // contradict. Discovery is one of the plan's events (§7).
    const bool ladderSetGrew = target->deferredChunks.size() != knownChunks;
    if (ladderSetGrew) {
        s_planStale = true;
        decLog("ladder set grew %zu -> %zu -> replan", knownChunks,
               target->deferredChunks.size());
    }
    // A staged snapshot's fresh ladders start with nothing resident,
    // and commit-time adoption has not run yet — but the executor
    // below runs NOW, in the window between staging and commit.
    // Without this, every ask it issues for a rung the live scene
    // already holds is a real re-download and re-parse: on an
    // announcement chain that was ~40 view-owned chunks fetched anew
    // per delta, every ~200 ms, for the length of the chain.
    if (s_haveScene && target == &s_pendingSnap
            && (ladderSetGrew || !s_pendingAdopted)) {
        s_pendingAdopted = true;
        const size_t adopted = Render::carryResidentRungs(*target, s_snap);
        if (adopted)
            decLog("adopted %zu resident rungs at staging", adopted);
    }
    // Progress is what unblocks a held delta: the manifests it was
    // early for may just have landed. Only once none are outstanding —
    // a drain attempt is a re-parse, and re-trying per arrival while
    // the blockers are plainly still in flight is noise for nothing.
    // A fresh tick, so the drain never re-enters this resolve.
    if (filled && !s_heldPayloads.empty() && !s_heldDrainScheduled) {
        bool blocked = false;
        for (const auto &entry : target->deferredChunks) {
            if (entry.fill && !entry.release && !entry.owners.empty()) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            s_heldDrainScheduled = true;
            emscripten_async_call(drainHeldPayloads, nullptr, 0);
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
        // Anything outstanding long enough to be presumed lost is
        // forgotten, so it can be asked for again. Without this a
        // request that dies quietly is a chunk the fetch never
        // reconsiders *and* a hole in the budget that nothing can
        // fill (see s_blobInFlight).
        {
            const double now = emscripten_get_now();
            size_t lost = 0;
            for (auto it = s_blobInFlight.begin();
                 it != s_blobInFlight.end();) {
                if (now - it->second < kInFlightTimeoutMs) {
                    ++it;
                    continue;
                }
                ++lost;
                s_netInFlight.erase(it->first);
                it = s_blobInFlight.erase(it);
            }
            if (lost) {
                // A read that hangs is nearly always the local store,
                // which a phone under storage pressure can leave
                // pending forever — and asking it again just hangs
                // again. It is an optimization, so give it up for the
                // session and let the network answer.
                if (blobPersistEnabled()) {
                    s_idbUnresponsive = true;
                    std::printf("fcviewer: the local blob store stopped "
                                "answering — using the network instead\n");
                }
                std::printf("fcviewer: %zu payloads never arrived in %.0f s "
                            "— asking again\n",
                            lost, kInFlightTimeoutMs / 1000.0);
            }
        }
        // The plan first, if any event marked it stale: everything
        // below *executes* it, and decides nothing of its own (§7,
        // "Selection is a plan, not a reaction").
        planIfStale(*target);
        // What is outstanding, in the order the camera wants it. The
        // sort is over the chunks not yet asked for, so it costs
        // nothing once the scene is mostly in hand — the common case
        // being a delta with a handful of chunks.
        /// An askable chunk: its priority, its entry, and — for
        /// geometry — WHICH rung the ask is for, because fetch
        /// identity no longer rides on entry.key/size (§7, "the
        /// ladder owns its fetch state"). -1 = the entry's own key
        /// (the manifest layer).
        struct WantItem {
            float score;
            size_t index;
            int rung;
        };
        std::vector<WantItem> want;
        /// Resident rungs no owner needs, collected rather than shed:
        /// eviction is the reverse of arrival, driven by the budget —
        /// a surplus rung stays resident (and drawable, should the
        /// camera drift back) until its bytes are actually wanted.
        /// Scored by residency so the least valuable go first.
        struct SurplusItem {
            float score;
            size_t index;
            uint16_t mask;
        };
        std::vector<SurplusItem> surplus;
        Render::RungRanker order = makeRanker();
        /// Whether the view's own chunks are all in hand yet — counting
        /// the ones already asked for, since the point is to wait for
        /// them rather than merely to ask first.
        bool viewPending = false;
        /// Releasable bytes asked for and not yet arrived. They are
        /// already spent as far as the budget is concerned — a window
        /// of sixty-four requests can be a large share of it in flight
        /// at once — and counted here rather than kept in a running
        /// total, which could only drift.
        size_t pledged = 0;
        /// The executor's moves this round, for the debug line.
        size_t rungUp = 0, rungDown = 0, rungsReleased = 0;
        /// Why a ladder below its plan was NOT asked for this round —
        /// the silent paths (a request presumed still out, a refill
        /// closure gone missing, a rung that failed this publish) that
        /// a stall hides in.
        size_t wantsFetch = 0, inFlightSkips = 0, noRefillSkips = 0;
        for (size_t i = 0; i < target->deferredChunks.size(); ++i) {
            auto &entry = target->deferredChunks[i];
            totalBytes += entry.size;
            if (entry.release) {
                // Geometry answers to the plan: diff the rungs this
                // ladder holds against the rungs its owners need, and
                // act (§7, "one rung per instance"). The step is pure
                // (SceneLadder); every side effect lives here, and
                // every one of them moves the ladder toward its plan —
                // which is what makes the round idempotent and a scene
                // at plan silent.
                const uint16_t needed =
                    Render::planNeeded(entry, objectErrOf);
                const auto step = Render::planStep(entry, needed);
                if (step.generate >= 0)
                    s_provider.generate(
                        levelRequestFor(entry, size_t(step.generate)));
                if (step.surplus) {
                    // Surplus above what any owner needs is NOT shed
                    // here: shedding at replan turned every camera
                    // drift across a rung boundary into a release/
                    // refetch cycle. It stays resident until the
                    // budget pass below actually wants the bytes.
                    surplus.push_back({order.residency(entry), i,
                                       step.surplus});
                    continue;
                }
                if (step.fetch < 0)
                    continue;  // At plan, or standing until a generate lands.
                ++wantsFetch;
                const std::string &wantKey =
                    Render::planRungKey(entry, size_t(step.fetch));
                const uint32_t wantSize =
                    Render::planRungSize(entry, size_t(step.fetch));
                // Bytes already local fill right here: an inline
                // delta, a rung another snapshot's twin fetched, or a
                // rung this ladder released and wants back — the store
                // kept it, so the climb is a parse, not a download.
                auto cached = s_blobCache.find(wantKey);
                if (cached != s_blobCache.end() && cached->second) {
                    const uint16_t before = entry.residentMask;
                    if (fillRung(entry, step.fetch,
                                 cached->second->data(),
                                 cached->second->size())) {
                        // The arrival side of the fetch history: every
                        // ask should eventually pair with one of these,
                        // and an ask that never does is the journal's
                        // evidence of a fill that went missing.
                        decLog("fill %.8s rung %d (resident %x), obj %llx",
                               wantKey.c_str(), step.fetch,
                               unsigned(entry.residentMask),
                               (unsigned long long)(entry.owners.empty()
                                                        ? 0
                                                        : entry.owners[0]));
                        if (!before
                                || uint16_t(1u << step.fetch) > before)
                            ++rungUp;
                        else
                            ++rungDown;
                        filled = true;
                        viewFilled = viewFilled || entry.owners.empty();
                        s_lastFillAt = emscripten_get_now();
                    }
                    else {
                        // Geometry that will not parse is a corrupt
                        // store, same as the manifest layer's verdict.
                        resetBlobStore();
                    }
                    continue;
                }
                if (entry.asked >= 0) {
                    const std::string &askedKey = Render::planRungKey(
                        entry, size_t(entry.asked));
                    if (!askedKey.empty()
                            && s_blobInFlight.count(askedKey)) {
                        ++inFlightSkips;
                        // One request per ladder: whichever rung lands
                        // re-runs this diff against wherever the plan
                        // is by then.
                        ++missing;
                        missingBytes += wantSize;
                        pledged += wantSize;
                        pledgedBytes = pledged;
                        viewPending = viewPending || entry.owners.empty();
                        continue;
                    }
                    // Nothing is actually out for it any more: the
                    // request resolved (a wanted rung was consumed
                    // above; an unwanted one just sits in the cache),
                    // failed, or was presumed lost. Re-askable.
                    entry.asked = -1;
                }
                // A rung this publish could not obtain waits for the
                // next one (s_blobFailed clears at staging) — the
                // ladder stands on whatever it holds meanwhile.
                if (s_blobFailed.count(wantKey))
                    continue;
                if (!entry.refill) {
                    // No closure to parse an arrival with — an
                    // abandoned load. Nothing to ask for.
                    ++noRefillSkips;
                    continue;
                }
                ++missing;
                missingBytes += wantSize;
                viewPending = viewPending || entry.owners.empty();
                // ?nofetchorder scores nothing: every chunk ties, the
                // sort below leaves them in publish order, and the
                // window check is skipped — the fetch exactly as it
                // was before there was an order.
                want.push_back({s_noFetchOrder
                                    ? 0.0f
                                    : order.acquisition(entry, wantSize),
                                i, step.fetch});
                continue;
            }
            if (!entry.fill)
                continue;
            ++missing;
            missingBytes += entry.size;
            // Anything no object claims is the view's own — the overlays
            // and the root's sections — and none of the model is asked
            // for while one is outstanding (see the issue loop).
            viewPending = viewPending || entry.owners.empty();
            if (s_blobInFlight.count(entry.key))
                continue;
            want.push_back({s_noFetchOrder ? 0.0f : order.acquisition(entry),
                            i, -1});
        }
        // Ties keep publish order, which is the order the objects were
        // named in: a scene the camera has no opinion about (nothing
        // fitted yet, or everything equally distant) streams exactly as
        // it did before this.
        std::stable_sort(want.begin(), want.end(),
                         [](const WantItem &a, const WantItem &b) {
                             return a.score > b.score;
                         });
        // The barrier, and its release. Progress is what renews it: a
        // link slow enough to take seconds per batch keeps the cube
        // ahead of the model, while a request that has produced
        // nothing at all for the grace period gives the model its
        // bandwidth back rather than stranding it on boxes.
        const double nowMs = emscripten_get_now();
        if (!viewPending) {
            s_viewProgressAt = 0.0;
            s_viewGraceReported = false;
        }
        else if (s_viewProgressAt == 0.0 || viewFilled)
            s_viewProgressAt = nowMs;
        const bool holdForView = viewPending && !s_noFetchOrder
            && nowMs - s_viewProgressAt < kViewGraceMs;
        if (holdForView && !s_viewGraceScheduled) {
            s_viewGraceScheduled = true;
            const double wait =
                kViewGraceMs - (nowMs - s_viewProgressAt) + 50.0;
            emscripten_async_call([](void *) {
                s_viewGraceScheduled = false;
                resolvePending();
            }, nullptr, int(wait > 0.0 ? wait : 50.0));
        }
        if (viewPending && !holdForView && !s_viewGraceReported) {
            s_viewGraceReported = true;
            std::printf("fcviewer: the view's own chunks have not arrived "
                        "in %.0f ms — letting the model through\n",
                        kViewGraceMs);
        }
        if (s_streamDebug) {
            std::printf("fcviewer: fetch: %zu outstanding of %zu, %zu "
                        "askable, %zu requests in flight%s\n",
                        missing, total, want.size(), s_requestsInFlight,
                        holdForView ? ", holding for the view" : "");
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
                    && s_provider.outstanding() >= s_provider.window())
                break;
            auto &entry = target->deferredChunks[item.index];
            // **The view's own chunks are a barrier, not just a
            // priority.** Sorting put them first, which decides the
            // order requests are *issued* in — and that is not what
            // decides when they arrive, because the window issues
            // sixty-four at once and they all share the link. A
            // navigation cube is half a megabyte against a model's
            // tens, so ranking it first still delivered it after a
            // third of the geometry: measured, no cube and no axis
            // cross twenty seconds into a throttled load.
            //
            // So nothing owned is asked for until nothing unowned is
            // outstanding. The queue is sorted, so the first owned
            // entry is where the model begins and the rest of the
            // round can be abandoned. The model is not held back by
            // it: its bottom rung is a box per object, which the root
            // alone draws (§6), and the view's own chunks are a fixed
            // few hundred kilobytes rather than a share of the scene.
            // The hold degrades rather than deadlocks — see
            // kViewGraceMs.
            if (holdForView && !entry.owners.empty())
                break;
            // No admission control here, deliberately: the plan is
            // budget-feasible by construction, so an entry in this
            // queue is one the scene is entitled to hold. Deciding it
            // again at issue time is exactly the per-chunk reactive
            // layer the plan replaced (§7).
            if (entry.release && item.rung >= 0) {
                const std::string &reqKey =
                    Render::planRungKey(entry, size_t(item.rung));
                const uint32_t reqSize =
                    Render::planRungSize(entry, size_t(item.rung));
                // The ask is now out for this rung; the diff skips the
                // ladder until it lands, fails, or is presumed lost.
                entry.asked = int16_t(item.rung);
                pledged += reqSize;
                pledgedBytes = pledged;
                // The fetch history, one line per request actually
                // issued: which rung was asked for, standing where, on
                // whose account. The plan line above it is the reason.
                const int target = entry.plan
                        == Render::SceneSnapshot::DeferredChunk::kPlanUnset
                    ? Render::finestBuiltRung(entry)
                    : int(entry.plan);
                decLog("ask %.8s %uB rung(resident %x, target %d) obj %llx",
                       reqKey.c_str(), reqSize,
                       unsigned(entry.residentMask), target,
                       (unsigned long long)(entry.owners.empty()
                                                ? 0 : entry.owners[0]));
                s_provider.request(reqKey, reqSize);
                continue;
            }
            decLog("ask %.8s %uB%s obj %llx", entry.key.c_str(),
                   entry.size, entry.owners.empty() ? " (view)" : "",
                   (unsigned long long)(entry.owners.empty()
                                            ? 0 : entry.owners[0]));
            s_provider.request(entry.key, entry.size);
        }
        // The budget's eviction pass — the only place a resident rung
        // is given back. Arrival's reverse: only when what is held
        // plus what is pledged overruns the budget do surplus rungs
        // go, least valuable first, and only as many as it takes. A
        // scene whose budget has room keeps every stand-in it ever
        // fetched, which is what makes a drifting camera free.
        if (!surplus.empty()) {
            const size_t budget = geometryBudget();
            size_t holding = residentBytes() + pledged;
            if (budget && holding > budget) {
                std::sort(surplus.begin(), surplus.end(),
                          [](const SurplusItem &a, const SurplusItem &b) {
                              return a.score < b.score;
                          });
                for (const auto &item : surplus) {
                    if (holding <= budget)
                        break;
                    auto &entry = target->deferredChunks[item.index];
                    const uint16_t mask =
                        uint16_t(item.mask & entry.residentMask);
                    if (!mask)
                        continue;
                    size_t bytes = 0;
                    const int count = std::min<int>(
                        int(Render::planRungs(entry)), 16);
                    for (int r = 0; r < count; ++r) {
                        if (mask & uint16_t(1u << r))
                            bytes += Render::planRungSize(entry, size_t(r));
                    }
                    decLog("evict %.8s rungs %x of %x (%zu KB), "
                           "%zu of %zu MB, obj %llx",
                           entry.key.c_str(), unsigned(mask),
                           unsigned(entry.residentMask), bytes >> 10,
                           holding >> 20, budget >> 20,
                           (unsigned long long)(entry.owners.empty()
                                                    ? 0 : entry.owners[0]));
                    releaseRungs(entry, mask);
                    ++rungsReleased;
                    holding -= std::min(holding, bytes);
                }
            }
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
        if (s_streamDebug && (rungUp || rungDown || rungsReleased))
            std::printf("fcviewer: rungs: %zu up, %zu down, %zu released\n",
                        rungUp, rungDown, rungsReleased);
        // A ladder below its plan that this round did NOT ask for is
        // where a stall hides; say why, once per change. wantsFetch
        // counts every below-plan ladder, the skips split the silent
        // ones by cause.
        {
            static size_t s_lastInFlightSkips = 0, s_lastNoRefill = 0;
            if (inFlightSkips != s_lastInFlightSkips
                || noRefillSkips != s_lastNoRefill) {
                s_lastInFlightSkips = inFlightSkips;
                s_lastNoRefill = noRefillSkips;
                if (inFlightSkips || noRefillSkips)
                    decLog("below plan: %zu ladders (%zu awaiting a "
                           "request already out, %zu with no refill)",
                           wantsFetch, inFlightSkips, noRefillSkips);
                else
                    decLog("below plan: clear");
            }
        }
        s_provider.flush();
        // ⭐ A round is driven by an arrival, so a load whose requests
        // have all stalled has nothing left to drive one: no arrival,
        // no round, no timeout noticed, no retry — and the model sits
        // at its boxes for good. Two hung requests were enough to do
        // it, budget or no budget. So while anything is outstanding,
        // wake up and look. This is the same lesson the overlay
        // barrier's grace taught: the thing that recovers from silence
        // cannot be scheduled by the noise it is waiting for.
        armHeartbeat();
    }
    if (target == &s_snap) {
        s_liveOutstanding = missing != 0;
        s_liveUnapplied = s_liveUnapplied || filled;
    }
    // ?genlod: ask the producer to build every level the ladder
    // declares unbuilt (§7, phase 5c). Level *selection* (5d) will ask
    // for the one level a draw wants; this stands in for it, so the
    // request channel and the work queue run end to end. The answer
    // lands as an ordinary publish whose delta re-names the ladder
    // with a key in place — generate() dedups, so re-running per round
    // costs a set lookup.
    if (s_genLod) {
        size_t builtMiddles = 0;
        for (const auto &entry : target->deferredChunks) {
            // Every unbuilt rung, the exact one of a coarse-first
            // ladder included (it is the last entry then, unkeyed).
            for (uint32_t i = 0; i < uint32_t(entry.levels.size()); ++i) {
                if (entry.levels[i].key.empty())
                    // The source is the finest built rung —
                    // entry.key may have been retargeted to a level.
                    s_provider.generate(levelRequestFor(entry, i));
                else if (i + 1 < uint32_t(entry.levels.size()))
                    ++builtMiddles;
            }
        }
        // A middle rung with a key is the announcement having arrived:
        // the server built it, the republish renamed the ladder, and
        // this viewer parsed the new manifest. The one line that
        // proves the whole 5c loop closed.
        static size_t s_builtMiddlesSeen = 0;
        if (builtMiddles > s_builtMiddlesSeen) {
            s_builtMiddlesSeen = builtMiddles;
            std::printf("fcviewer: %zu generated levels announced\n",
                        builtMiddles);
        }
    }
    // A scene held at its budget is not a scene still loading (§6
    // phase 4b): the executor has reached the plan, and the plan says
    // some objects stay below the detail the camera wanted. The
    // camera — not time — is what changes the answer, so the note is
    // printed on the transition, not per round.
    const bool atBudget = !missing && s_planCapped
        && s_provider.outstanding() == 0 && s_batchQueue.empty();
    if (atBudget && !s_atBudgetReported) {
        s_atBudgetReported = true;
        fcviewer_status(nullptr, 0.0, 0.0);
        std::printf("fcviewer: at the geometry budget (%zu MB) — the plan "
                    "holds %zu objects below their wanted detail. Holding "
                    "%zu KB in %zu payloads, %zu KB planned\n",
                    geometryBudget() >> 20, s_planCapped,
                    s_residentBytes >> 10, s_held.size(),
                    s_plannedBytes >> 10);
        reportBudget(*target);
    }
    else if (!atBudget) {
        s_atBudgetReported = false;
    }
    if (missing) {
        // A load the network never touched is not one the user should
        // watch: a warm store answers everything from IndexedDB, the
        // scene repaints in well under a perceptible "load", and a
        // progress bar over it reads as a problem where there is none.
        // The bar (and its status text) appears only while a request
        // is actually out on the wire — s_netInFlight, joined at the
        // IndexedDB miss — and folds away the moment none is.
        if (s_netInFlight.empty() && s_batchQueue.empty()) {
            fcviewer_status(nullptr, 0.0, 0.0);
        }
        else {
            // Until every object's manifest is in, the byte total is
            // not known — a manifest is what names the meshes under
            // it, so most of the scene's weight is undiscovered and a
            // fraction over what is known would run to nearly full and
            // then fall back as the rest appeared. Indeterminate is
            // what "the size is not known yet" means, and it is the
            // honest answer for the second or two that phase lasts.
            bool discovering = s_objects.objects.empty();
            for (const auto &item : s_objects.objects) {
                // An empty drawsKey is an object no manifest has ever
                // been read for, so its meshes are not in the totals
                // yet. Not `unresolved()`, which also counts an object
                // whose manifest arrived and whose material has not —
                // its bytes are known, and waiting for them would
                // leave the bar indeterminate for most of the load.
                if (item.second.drawsKey.empty()) {
                    discovering = true;
                    break;
                }
            }
            if (discovering)
                fcviewer_status("loading scene", 0.0, 0.0);
            else
                fcviewer_status("loading scene",
                                double(totalBytes - missingBytes),
                                double(totalBytes));
        }
        if (!filled && !s_liveUnapplied) {
            // Nothing new became drawable — the requests above are
            // what this round accomplished. An eviction counts as
            // something to show: a draw that gave its geometry back
            // has to be redrawn at the rung below it, and nothing
            // else is going to arrive and prompt that.
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

/// A camera move is a change of mind about what to fetch, and under a
/// full budget it is the only thing that can be (§6 phase 4b).
///
/// While a scene is arriving, every arrival re-sorts the queue and
/// issues the next of it, so the order follows the camera for free.
/// Once the budget is full that stops: the chunks left are exactly the
/// ones worth less than what is resident, nothing is asked for, and so
/// nothing arrives to reconsider them. Turning to face them is what
/// changes the answer, and the frame is where that is known.
///
/// Rate-limited rather than run per frame, because a round costs a
/// sort of the outstanding queue and an orbit is a hundred frames of
/// continuous change; a fifth of a second of staleness in a fetch that
/// takes seconds is not a difference anyone can see.
/// How long the camera must hold still before it counts as settled —
/// the planner's event (§7). Short enough that a zoom's end feels
/// immediate, long enough that a continuous orbit plans zero times
/// rather than sixty a second.
static const double kPlanSettleMs = 300.0;

static void pumpFetchOnMove()
{
    static float lastCam[8] = {0.0f};
    static double lastPump = 0.0;
    static double movedAt = -1.0;
    const float cam[8] = {s_yaw, s_pitch, s_dist, s_center[0], s_center[1],
                          s_center[2], s_panX, s_panY};
    // Movement worth reacting to, not float inequality: a touch
    // fling's inertia decays exponentially and keeps the camera
    // changing by epsilons long after it LOOKS still, so an exact
    // compare never settles and the planner never runs — "stationary
    // detection is not reliable", measured as one settle event in a
    // whole session of spins. The thresholds are far below anything
    // visible, and comparing against the last ACCEPTED camera means a
    // slow real drift still accumulates into a move.
    const float span = std::max(s_dist, 1e-3f);
    const float eps[8] = {1e-4f, 1e-4f, span * 1e-4f, span * 1e-4f,
                          span * 1e-4f, span * 1e-4f, span * 1e-4f,
                          span * 1e-4f};
    bool moved = false;
    for (int i = 0; i < 8; ++i)
        moved = moved || std::fabs(cam[i] - lastCam[i]) > eps[i];
    const double now = emscripten_get_now();
    if (moved) {
        std::copy(cam, cam + 8, lastCam);
        movedAt = now;
        // While a scene is arriving, a move re-sorts the outstanding
        // queue so the fetch follows the camera. Rate-limited: a fifth
        // of a second of staleness in a fetch that takes seconds is
        // not a difference anyone can see, and a round costs a sort.
        if (s_liveOutstanding && now - lastPump >= 200.0) {
            lastPump = now;
            resolvePending();
        }
        return;
    }
    // The camera settling is a plan event: the plan is drawn against
    // the camera someone is actually looking through, once, not
    // through every frame of the motion — that discreteness is the
    // hysteresis (§7).
    if (movedAt >= 0.0 && now - movedAt >= kPlanSettleMs) {
        movedAt = -1.0;
        s_planStale = true;
        decLog("camera settled -> replan");
    }
    if (s_planStale && s_haveScene)
        resolvePending();
}

//////////////////////////////////////////////////////////////////////
// Scene streaming: WebSocket push, HTTP polling fallback

/// A versioned scene payload arrived (either transport): parse and
/// re-apply; the first scene also fits the camera.
///
/// False means the payload itself was no good. It does **not** mean
/// there is a scene on screen: a publish is staged here and drawn once
/// enough of it has arrived (§6), so a caller that treats "nothing
/// visible yet" as failure is reading the wrong thing.
static bool applyScenePayload(const char *data, size_t size)
{
    if (size <= 8)
        return false;
    uint64_t version = 0;
    std::memcpy(&version, data, sizeof(version));
    // The 2D page tier: an FCPD payload routes to the page store (doc
    // sec 24.4). Its own format-skew probe mirrors the scene one
    // below -- sceneSnapshotVersion answers 0 for a foreign magic, so
    // that probe can never see a newer page format.
    if (uint32_t pv = Render::pageSnapshotVersion(data + 8, size - 8)) {
        if (pv > Render::pageDumpVersion()) {
            std::printf("fcviewer: page format v%u newer than built "
                        "v%u, reloading\n", pv, Render::pageDumpVersion());
            char bust[32];
            std::snprintf(bust, sizeof(bust), "p%u", pv);
            fcviewer_reload(bust);
            return true;
        }
        return applyPagePayload(version, data + 8, size - 8);
    }
    Render::SceneSnapshot snap;
    // Textures are held by content key across publishes, so a
    // re-parse hands back the objects the model's draws already hold
    // (SceneSnapshot::textureMemo).
    snap.textureMemo = s_textureMemo;
    if (Render::loadSceneSnapshot(data + 8, size - 8, snap)) {
        // A scene payload means the joined group serves 3D: leave page
        // mode (the page store keeps its state -- switching back is a
        // session change and resets it there).
        s_pageMode = false;
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
            s_textureMemo->clear();
            s_sceneVersion = 0;
            // Deltas held from the old session chain to nothing now.
            s_heldPayloads.clear();
            // So does the client-side selection: its keys named the
            // old model's objects. A backend restart made this stale
            // only in theory; a document switch (which is a session
            // change by construction — sessions are per stream) makes
            // it a click on the wrong document.
            if (!s_sel.empty()) {
                s_sel.clear();
                rebuildSelection();
                emitSelectionEvent();
            }
        }
        // The WebSocket push loop re-sends the current scene on connect;
        // skip the echo of a version already applied (the initial HTTP
        // fetch).
        else if (s_haveScene && version == s_sceneVersion)
            return true;
        // A publish that stages while the last one is still arriving
        // supersedes it, and its outstanding chunks are abandoned with
        // it. Geometry survives that: the commit carries unchanged
        // objects' ladders across a delta (carryLadders), so a mesh
        // below its plan is re-asked from the merged scene. What does
        // not survive is the manifest layer — a group, material or
        // texture still in flight describes objects a delta does not
        // re-name. But even there, most of what is outstanding when
        // level announcements chain is safely abandonable: the view's
        // own sections are re-sent whole every publish, and a manifest
        // whose owners this delta all re-describes is superseded by
        // the very payload that arrived. Only a chunk some object NOT
        // in the delta is still waiting for forces the full root —
        // without that precision, each announcement in a chain reset
        // the scene while the previous one's manifests were in flight,
        // which is the "Preparing scene…" flashing loop.
        // The answer to a resync is an ordinary full payload on the
        // socket; it arriving — by either route — is what ends the
        // single-flight window.
        if (!snap.baseVersion)
            s_fullSceneInFlight = false;
        // A delta at or behind the model is history: the same version
        // arrives twice on racing delivery paths, and the twin of one
        // already applied would stage, mis-base against the model that
        // moved on, and buy a full root for nothing.
        if (snap.baseVersion && s_haveScene
                && snap.manifestVersion <= s_objects.version) {
            decLog("drop stale delta v%llu (model at v%llu)",
                   (unsigned long long)snap.manifestVersion,
                   (unsigned long long)s_objects.version);
            return true;
        }
        // ANY publish staged and not yet committed must not be
        // superseded by a delta — the deltas that follow BASE on it,
        // and superseding it means the model never advances through
        // it: the next delta mis-bases and buys a full root. They are
        // early, not conflicting: held, and drained once the staged
        // one commits. (A full root may still supersede — it
        // re-describes everything.)
        const bool pendingStaging = s_pendingValid;
        // The one manifest-layer hold that inline deltas (v37) cannot
        // retire: a FULL ROOT still filling its manifests. A delta
        // carries its own manifests, so nothing of ITS layer is ever
        // in flight — but the root's un-redescribed objects' manifests
        // are per-parse state, and staging over them strands those
        // objects at their boxes for the rest of the session. So a
        // delta waits for the cold or resync root to finish its
        // manifest layer, under the same grace as every other hold.
        size_t rootManifests = 0;
        if (snap.baseVersion && s_liveOutstanding && !s_snap.baseVersion) {
            for (const auto &entry : s_snap.deferredChunks) {
                if (entry.fill && !entry.release && !entry.owners.empty())
                    ++rootManifests;
            }
            if (rootManifests)
                decLog("delta v%llu behind the root's %zu manifests",
                       (unsigned long long)snap.manifestVersion,
                       rootManifests);
        }
        // Anything already held queues everything behind it: deltas
        // are a chain, and a younger delta whose manifests happen to
        // be in hand must not overtake an older one still waiting —
        // measured as "delta base v3, holding v2" mis-bases on a fast
        // link, each buying a resync. (Exempt while draining: the
        // drain IS the queue moving.)
        if (snap.baseVersion
                && (rootManifests || pendingStaging
                    || (!s_drainingHeld && !s_heldPayloads.empty()))) {
            // Early, not wrong: on a slow link the announcements a
            // fresh session provokes arrive faster than the manifests
            // of the publish before them. Hold the delta in chain
            // order and drain it when the blocking manifests land —
            // answering with a full root here set off a livelock
            // measured at 178 "Preparing scene…" rounds on a phone.
            if (s_fullSceneInFlight) {
                decLog("drop delta v%llu (full scene in flight)",
                       (unsigned long long)snap.manifestVersion);
                return true;
            }
            if (s_heldPayloads.size() >= kHeldPayloadMax) {
                decLog("held queue full at v%llu -> full scene",
                       (unsigned long long)snap.manifestVersion);
                s_heldPayloads.clear();
                requestFullScene();
                return true;
            }
            // The same version can arrive twice (two delivery paths
            // race after a reconnect); one copy of the chain is the
            // chain.
            for (const auto &held : s_heldPayloads) {
                if (held.version == snap.manifestVersion)
                    return true;
            }
            if (pendingStaging)
                decLog("hold delta v%llu behind the staging publish "
                       "(%zu held)",
                       (unsigned long long)snap.manifestVersion,
                       s_heldPayloads.size() + 1);
            else
                decLog("hold delta v%llu behind the root's %zu manifests "
                       "(%zu held)",
                       (unsigned long long)snap.manifestVersion,
                       rootManifests, s_heldPayloads.size() + 1);
            HeldPayload held;
            held.version = snap.manifestVersion;
            held.heldAt = emscripten_get_now();
            held.bytes.assign(data, data + size);
            const bool first = s_heldPayloads.empty();
            if (s_drainingHeld)
                s_heldPayloads.push_front(std::move(held));
            else
                s_heldPayloads.push_back(std::move(held));
            if (first)
                emscripten_async_call(heldGraceCheck, nullptr,
                                      int(kHeldGraceMs) + 100);
            return true;
        }
        decLog("staged v%llu%s: %zu updates, %zu removed",
               (unsigned long long)snap.manifestVersion,
               snap.baseVersion ? " (delta)" : " (full)",
               snap.objectUpdates.size(), snap.objectsRemoved.size());
        // Payloads the publish only named are fetched before it is
        // applied; with none outstanding (the usual case, every key
        // already cached) this commits inline.
        s_liveOutstanding = false;
        s_liveUnapplied = false;
        s_pendingSnap = std::move(snap);
        s_pendingVersion = version;
        s_pendingValid = true;
        s_pendingAdopted = false;
        // First scene on a blank canvas: the commit below waits for the
        // root's manifests, which on a cold cache over a slow link is
        // seconds to minutes of black. The background is already in
        // hand and costs nothing — paint it now, so the wait happens
        // over the scene's backdrop and the loading bar instead of a
        // dead page (user report: "long pause with no background").
        if (s_snap.scene.empty() && !s_pendingSnap.baseVersion) {
            s_renderer->setBackground(s_pendingSnap.background);
            markDirty();
        }
        // A delta's group manifests ride in the payload itself (v37):
        // ingest them under their keys — cache, store and all, exactly
        // as if the network had just answered — so the resolve below
        // fills them in the same tick and the delta stages without a
        // single manifest round trip. New by definition, so this never
        // duplicates a fetch; the guard only folds the N resolves into
        // the one that follows.
        {
            size_t inlined = 0;
            s_batchApplying = true;
            for (auto &entry : s_pendingSnap.deferredChunks) {
                if (entry.inlineData.empty())
                    continue;
                blobResolved(entry.key,
                             std::make_shared<std::vector<uint8_t>>(
                                 std::move(entry.inlineData)),
                             false);
                entry.inlineData.clear();
                ++inlined;
            }
            s_batchApplying = false;
            if (inlined)
                decLog("v%llu carried %zu manifests inline",
                       (unsigned long long)s_pendingSnap.manifestVersion,
                       inlined);
        }
        // A publish staging is a plan event: the ladder set is about
        // to change, and the fetch order below wants targets drawn
        // against it (§7).
        s_planStale = true;
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
        return true;
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
            return true;
        }
        std::printf("fcviewer: scene update parse FAILED\n");
    }
    return false;
}

/// Re-apply the oldest held delta now that something changed. One per
/// call: if it re-blocks it goes back to the front and the next fill
/// progress tries again; if it applies, the next held payload rides
/// the next tick, after this one's own manifests have had their
/// chance.
static void drainHeldPayloads(void *)
{
    s_heldDrainScheduled = false;
    // One at a time, and only between commits: a drained delta stages,
    // and staging the next one on its heels superseded it before the
    // model advanced — the mis-base this queue exists to prevent,
    // reintroduced by its own eagerness. The commit of the staged one
    // schedules the next drain (commitResolved).
    if (s_heldPayloads.empty() || s_fullSceneInFlight || s_pendingValid)
        return;
    HeldPayload item = std::move(s_heldPayloads.front());
    s_heldPayloads.pop_front();
    decLog("drain held delta v%llu (%zu still held)",
           (unsigned long long)item.version, s_heldPayloads.size());
    s_drainingHeld = true;
    applyScenePayload(item.bytes.data(), item.bytes.size());
    s_drainingHeld = false;
}

/// JSON control messages pushed by the scene server as WebSocket text
/// frames (docs/RenderDebug.md §4.4). The messages are tiny and
/// self-generated, so field extraction is by simple search.
static void handleControlMessage(const char *json)
{
    if (handleCyclesControl(json))
        return;
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
        // The host flipped this connection's mode
        // (docs/MultiDocServe.md §8). The refusal is the backend's, but
        // the UI must not offer what will be refused — tell the DOM
        // layer, which greys its editors and says so.
        if (const char *vo = std::strstr(json, "\"viewOnly\"")) {
            bool on = std::strstr(vo, "true") != nullptr;
            std::printf("fcviewer: %s\n",
                        on ? "view-only mode" : "editing enabled");
            fcviewer_viewonly_event(on ? 1 : 0);
            // Label only (total < 0 = no bar): the page with no DOM UI
            // layer has nowhere else to learn this.
            fcviewer_status(on ? "View only \xe2\x80\x94 the host has "
                                 "disabled editing" : nullptr,
                            0.0, on ? -1.0 : 0.0);
        }
    }
    else if (std::strstr(json, "\"cmd\":\"dumpDecisions\"")) {
        long id = 0;
        const char *p = std::strstr(json, "\"id\"");
        const char *colon = p ? std::strchr(p, ':') : nullptr;
        if (colon)
            id = std::strtol(colon + 1, nullptr, 10);
        sendDecisionLog(uint32_t(id));
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
    else if (std::strstr(json, "\"cmd\":\"docs\"")) {
        // The served-document listing (docs/MultiDocServe.md §4):
        // pushed on serve/unserve, or the answer to a docs request.
        // The DOM layer's menu redraws from it.
        fcviewer_docs_event(json, s_docName.c_str());
    }
    else if (std::strstr(json, "\"cmd\":\"error\"")) {
        // Transport-level refusals. Unknown document: joined to
        // nothing, and the docs push riding alongside fills the menu —
        // the status line says why the canvas is empty.
        if (std::strstr(json, "\"UnknownDocument\""))
            fcviewer_status("Unknown document \xe2\x80\x94 "
                            "pick one from the menu", 0.0, -1.0);
        else if (std::strstr(json, "\"NoDocument\""))
            fcviewer_status("No document is being served", 0.0, -1.0);
        // Told to leave (docs/MultiDocServe.md §8): the host kicked
        // this connection or stopped sharing, or the door token was
        // wrong. Reconnecting would be knocking on a closed door.
        else if (std::strstr(json, "\"Kicked\"")) {
            s_reconnectLimit = 0;
            fcviewer_status("Disconnected by host", 0.0, -1.0);
        }
        else if (std::strstr(json, "\"BadToken\"")) {
            s_reconnectLimit = 0;
            fcviewer_status("Not authorized \xe2\x80\x94 "
                            "check the share link", 0.0, -1.0);
        }
        // The grant door said no (docs/ShareAccess.md §2): the link may
        // be fine and the name or identity not covered — a different
        // message than a bad link, because the fix is different (ask
        // the host, not re-copy the URL).
        else if (std::strstr(json, "\"Refused\"")) {
            s_reconnectLimit = 0;
            fcviewer_status("Access refused \xe2\x80\x94 "
                            "this invite does not cover you", 0.0, -1.0);
        }
        std::printf("fcviewer: server error %s\n", json);
    }
    else if (std::strstr(json, "\"id\":") || std::strstr(json, "\"op\":")) {
        // A semantic-channel answer (docs/ThinClient.md §4.2) — not for
        // the viewer, for the DOM layer riding on it.
        fcviewer_control_event(json);
    }
}

/// Turn the HUD on or off from the DOM layer's menu — the counterpart of
/// [d], which is no use on a device with no keyboard. Turning it off
/// dispatches one last 'fc:hud' with no text, so the card closes with it.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_set_hud(int on)
{
    if (s_hudOn == (on != 0))
        return;
    s_hudOn = on != 0;
    if (!s_hudOn)
        fcviewer_hud(nullptr);
    markDirty();
}

/// The DOM layer's uplink for semantic operations (getProperties,
/// setProperty, ...): send a JSON text frame on the live scene socket.
/// Returns 0 when the socket is down — the DOM side treats that as its
/// "offline" state, it must not queue.
extern "C" EMSCRIPTEN_KEEPALIVE int fcviewer_control_send(const char *json)
{
    if (s_ws <= 0 || !s_wsOpen)
        return 0;
    return emscripten_websocket_send_utf8_text(
                   s_ws, const_cast<char *>(json)) >= 0 ? 1 : 0;
}

/// Selection mode from the DOM layer's selection menu: 0 = single,
/// 1 = multi (every click toggles/extends, the sticky Ctrl).
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_set_sel_mode(int mode)
{
    s_selMode = mode ? 1 : 0;
}

/// Pick filter from the selection menu (PickFilter values). The hover
/// highlight may be showing something the new filter forbids — clear
/// it; the next pointer move rebuilds it under the new rule. The
/// selection itself is kept: a filter narrows what picks may land on
/// from now on, it does not revoke what was already selected.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_set_pick_filter(int filter)
{
    if (filter < FilterElements || filter > FilterVertex)
        filter = FilterElements;
    if (filter == s_pickFilter)
        return;
    s_pickFilter = filter;
    applyHover(PickHit{});
}

/// Switch this viewer to another served document, from the menu's
/// document section (docs/MultiDocServe.md §6). The wire does the
/// heavy lifting: the switch verb resets the connection's version, so
/// the answer is the new document's full root — and that root carries
/// the new stream's session id, which is exactly the reconnect path:
/// applyScenePayload drops the object model wholesale and the scene,
/// selection mirror and level state rebuild from nothing. No machinery
/// is duplicated here.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_switch_doc(const char *name)
{
    s_docName = name ? name : "";
    std::printf("fcviewer: switching to document '%s'\n",
                s_docName.c_str());
    // Each served document keeps its own mirrors, so the camera this
    // connection stated to the old one says nothing about the new one
    // (docs/ThinClient.md sec 8.10a).
    invalidateCameraFrame();
    fcviewer_status("Switching document\xe2\x80\xa6", 0.0, 0.0);
    if (s_wsOpen) {
        std::string msg = "{\"cmd\":\"switch\",\"doc\":\"";
        jsonEscapeTo(msg, s_docName);
        msg += "\"}";
        emscripten_websocket_send_utf8_text(
                s_ws, const_cast<char *>(msg.c_str()));
        return;
    }
    // Polling fallback: no switch verb without a socket — hold
    // nothing, and the next poll fetches the named document whole
    // (doPoll carries the doc key).
    s_sceneVersion = 0;
    s_sessionId = 0;
    s_haveScene = false;
}

/// Name this connection, from the menu (docs/MultiDocServe.md §6): the
/// label the host's sharing roster shows. Kept for later hellos too, so
/// a reconnect keeps the name; sent now when the socket is up.
extern "C" EMSCRIPTEN_KEEPALIVE void fcviewer_set_client(const char *name)
{
    s_clientLabel = name ? name : "";
    std::printf("fcviewer: client name '%s'\n", s_clientLabel.c_str());
    fcviewer_client_event(s_clientLabel.c_str());
    if (s_wsOpen) {
        std::string msg = "{\"cmd\":\"client\",\"name\":\"";
        jsonEscapeTo(msg, s_clientLabel);
        msg += "\"}";
        emscripten_websocket_send_utf8_text(
                s_ws, const_cast<char *>(msg.c_str()));
    }
}

/// What the menu shows as the current name.
extern "C" EMSCRIPTEN_KEEPALIVE const char *fcviewer_client_name()
{
    return s_clientLabel.c_str();
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
    // A gated backend refusing the token will refuse it next time
    // too; polling on would be a request every 500ms forever.
    if (fetch->status == 403) {
        emscripten_fetch_close(fetch);
        s_polling = false;
        fcviewer_status("Not authorized \xe2\x80\x94 check the share link",
                        0.0, -1.0);
        return;
    }
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
    std::snprintf(url, sizeof(url), "%s/scene?v=%llu&s=%llu%s%s",
                  s_sceneUrl.c_str(),
                  (unsigned long long)s_sceneVersion,
                  (unsigned long long)s_sessionId,
                  docQuery().c_str(), tokenQuery().c_str());
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
    std::string hello = "{\"cmd\":\"hello\",\"snapshot\":"
        + std::to_string(Render::sceneDumpVersion())
        + ",\"page\":" + std::to_string(Render::pageDumpVersion());
    if (!s_buildStamp.empty())
        hello += ",\"build\":\"" + s_buildStamp + "\"";
    // The document wire (docs/MultiDocServe.md §4): which document to
    // join, who this connection is, and the door token when the
    // backend has one. All optional; absent means what it always did.
    if (!s_docName.empty()) {
        hello += ",\"doc\":\"";
        jsonEscapeTo(hello, s_docName);
        hello += '"';
    }
    if (!s_clientLabel.empty()) {
        hello += ",\"client\":\"";
        jsonEscapeTo(hello, s_clientLabel);
        hello += '"';
    }
    if (!s_tokenParam.empty()) {
        hello += ",\"token\":\"";
        jsonEscapeTo(hello, s_tokenParam);
        hello += '"';
    }
    hello += '}';
    // Guard against a stale open event: when a reconnect has already
    // replaced s_ws, the superseded socket's onWsOpen still fires and
    // would send on the new, still-connecting handle — a DOM exception.
    // Skipping is right, not a loss: the new socket's own open callback
    // sends the hello.
    unsigned short ready = 0;
    if (emscripten_websocket_get_ready_state(s_ws, &ready)
                != EMSCRIPTEN_RESULT_SUCCESS
            || ready != 1 /* OPEN */)
        return;
    emscripten_websocket_send_utf8_text(s_ws,
                                        const_cast<char *>(hello.c_str()));
}

static EM_BOOL onWsOpen(int, const EmscriptenWebSocketOpenEvent *, void *)
{
    s_wsOpen = true;
    // A new connection has no mirror on the server, whatever this page
    // told the old one (docs/ThinClient.md sec 8.10a).
    invalidateCameraFrame();
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
    // The document listing is pushed on every serve/unserve, but a
    // connection made between pushes would wait forever for its first
    // — ask once, so the menu has its document section from the start.
    static const char docs[] = "{\"cmd\":\"docs\"}";
    emscripten_websocket_send_utf8_text(s_ws, const_cast<char *>(docs));
    return EM_TRUE;
}

static EM_BOOL onWsMessage(int, const EmscriptenWebSocketMessageEvent *e,
                           void *)
{
    if (!e->isText) {
        // A streamed frame (FrameStreamWire.h) or the scene.
        if (Render::isStreamedFrame(e->data, size_t(e->numBytes)))
            handleStreamedFrame(e->data, size_t(e->numBytes));
        else
            applyScenePayload(reinterpret_cast<const char *>(e->data),
                              size_t(e->numBytes));
    }
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
    // The document too: a reconnect must rejoin it before the push
    // loop sends anything, which is also what makes the held version
    // mean something (sessions are per document).
    url += docQuery();
    url += tokenQuery();
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
    const bool ok = applyScenePayload(s_initialPayload.data(),
                                      s_initialPayload.size());
    s_initialPayload.clear();
    if (!ok) {
        fcviewer_status("Scene load failed", 0.0, -1.0);
    }
    else if (!s_pendingValid && !s_liveOutstanding) {
        fcviewer_status(nullptr, 0.0, 0.0);
    }
    // Otherwise the payload is staged and its chunks are on the way:
    // resolvePending owns the indicator from here, and anything
    // written now would be a wrong answer for the moment before the
    // first commit. That moment used to read "Scene load failed",
    // because a scene being drawable was once the same thing as a
    // payload being good — which stopped being true when a publish
    // began to be drawn while it was still arriving (§6).
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
    s_fullSceneInFlight = false;
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
    s_fullSceneInFlight = false;
    // Refused at the door: neither the stream nor a retry can help
    // until the page is reloaded with the right token.
    if (fetch->status == 403) {
        emscripten_fetch_close(fetch);
        fcviewer_status("Not authorized \xe2\x80\x94 check the share link",
                        0.0, -1.0);
        return;
    }
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
    // One in flight, ever: the storm this guards against was every
    // mis-based delta of an announcement chain asking for another
    // root while the first was still downloading — 178 requests, 20
    // seconds of "Preparing scene…", measured on a phone.
    if (s_fullSceneInFlight) {
        decLog("full scene already in flight (holding v%llu)",
               (unsigned long long)s_objects.version);
        return;
    }
    s_fullSceneInFlight = true;
    decLog("full scene requested (holding v%llu)%s",
           (unsigned long long)s_objects.version,
           s_wsOpen ? " via ws resync" : " via http");
    // The model is deliberately NOT wiped: a full root retires what it
    // does not name and re-takes what it renames as the groups fill
    // (applySceneObjects), and until then the old draws are the bridge
    // that keeps the scene from flashing to boxes. Only a backend
    // session change invalidates the model wholesale, and that path
    // clears it itself (applyScenePayload).
    s_pendingValid = false;
    s_pendingSnap = Render::SceneSnapshot();
    s_liveOutstanding = false;
    s_liveUnapplied = false;
    s_sceneVersion = 0;
    s_haveScene = false;
    // Over the socket when there is one: the push loop answers with
    // the full current payload IN ORDER, so every later delta bases on
    // it — the repair is atomic. The HTTP route (the fallback) shares
    // the origin's connection pool with the fetch window and can
    // starve behind its own geometry requests for the length of a
    // load, which is how one full-root request became a hang.
    if (s_wsOpen) {
        fcviewer_status("Preparing scene\xe2\x80\xa6", 0.0, 0.0);
        static const char resync[] = "{\"cmd\":\"resync\"}";
        emscripten_websocket_send_utf8_text(s_ws, const_cast<char *>(resync));
        return;
    }
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onInitFetchDone;
    attr.onerror = onInitFetchError;
    char url[512];
    std::snprintf(url, sizeof(url), "%s/scene?v=0%s%s", s_sceneUrl.c_str(),
                  docQuery().c_str(), tokenQuery().c_str());
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
    std::snprintf(url, sizeof(url), "%s/scene?v=%llu&s=%llu%s%s",
                  s_sceneUrl.c_str(),
                  (unsigned long long)s_sceneVersion,
                  (unsigned long long)s_sessionId,
                  docQuery().c_str(), tokenQuery().c_str());
    emscripten_fetch(&attr, url);
}

int main()
{
    emscripten_set_canvas_element_size("#canvas", s_width, s_height);
    fcviewer_install_control();
    // The persistent store's ledger (lazy blob collection, §7): load
    // early so a warm session's touches land on real entries.
    loadStoreMeta();

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

    // Both of these are consumed INSIDE applySnapshot, so like ?cam=
    // above they have to be parsed before the bundled snapshot applies
    // below -- a bundled scene applies exactly once, so a value read
    // afterwards would never reach the frame at all.
    // ?cavity=<0|1> -- see s_cavity. On unless asked otherwise, so the
    // parameter exists to turn the pass OFF (and to A/B what it is
    // standing in for when the contract withholds the edges).
    {
        const int on = EM_ASM_INT({
            const v = new URLSearchParams(window.location.search)
                .get('cavity');
            return v === null ? -1 : ((v === '0' || v === 'false') ? 0 : 1);
        });
        if (on >= 0) {
            s_cavity = on != 0;
            std::printf("fcviewer: cavity %s\n", s_cavity ? "ON" : "off");
        }
    }
    // ?accum=<N> -- idle temporal refinement over N jittered samples
    // (bare ?accum means the desktop default of 32, ?accum=0 turns it
    // off). Off unless asked for: see s_accumSamples.
    {
        const int n = EM_ASM_INT({
            const p = new URLSearchParams(window.location.search);
            if (!p.has('accum')) return -1;
            const v = p.get('accum');
            if (v === null || v === '') return 32;
            const n = parseInt(v, 10);
            return isNaN(n) ? 32 : n;
        });
        if (n >= 0) {
            s_accumSamples = n;
            if (s_accumSamples > 0)
                std::printf("fcviewer: idle refinement ON, %d samples\n",
                            s_accumSamples);
            else
                std::printf("fcviewer: idle refinement off\n");
        }
    }

    const bool bundledScene = Render::loadSceneSnapshot("/scene.fcsd", s_snap);
    if (bundledScene) {
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
    // ?decisions mirrors the always-on decision journal to the console
    // (docs/SceneStreaming.md §7); the journal itself is retrieved any
    // time via the control channel, GET /decisions on the scene server.
    s_decisions = EM_ASM_INT({
        return new URLSearchParams(window.location.search)
            .has('decisions') ? 1 : 0;
    }) != 0;
    s_genLod = EM_ASM_INT({
        return new URLSearchParams(window.location.search)
            .has('genlod') ? 1 : 0;
    }) != 0;
    {
        const double px = EM_ASM_DOUBLE({
            const v = new URLSearchParams(window.location.search)
                .get('lodpx');
            return v === null ? -1.0 : parseFloat(v);
        });
        if (px >= 0.0)
            s_lodPx = float(px);
    }
    // ?shapevertices=<0|1> -- see s_shapeVertices. Only the vertex gate
    // is offered: the edge gate reads the GPU budget crossing, and this
    // tier's budget is its CPU half only (resident payload and heap),
    // which cannot see the GPU buffers an edge draw would free. It
    // stays pushed and dormant rather than pretending, and arms itself
    // the day this tier grows an uploaded-bytes meter (#13a).
    {
        const int on = EM_ASM_INT({
            const v = new URLSearchParams(window.location.search)
                .get('shapevertices');
            return v === null ? -1 : ((v === '0' || v === 'false') ? 0 : 1);
        });
        if (on >= 0) {
            s_shapeVertices = on != 0;
            std::printf("fcviewer: shape vertices %s\n",
                        s_shapeVertices ? "ON" : "off");
        }
    }
    // ?leveldebug -- narrate the level plan and the element gates, the
    // desktop's FC_LEVEL_DEBUG / Render_LevelDebug by another door
    // (there is no environment to read here). The gate counters and the
    // element audit are computed on this tier whether or not anyone
    // reads them, and until this there was no way to read them: the
    // desktop prints through Base::Console and the browser half of
    // FC_RENDER_MSG goes to the JS console, so the same lines land in
    // devtools. Off by default -- the audit prints on every change of
    // its verdict, which is chatty while a scene streams in.
    if (EM_ASM_INT({
            return new URLSearchParams(window.location.search)
                .has('leveldebug') ? 1 : 0;
        }) != 0) {
        s_levelDebug = true;
        std::printf("fcviewer: level debug ON\n");
    }
    // ?membudget=<MB> — pin the resident geometry budget (§6 phase 4b),
    // which otherwise fits itself to the device. A real budget is larger
    // than any demo scene, so the only way to exercise the ladder
    // running backwards is to say what it is.
    {
        const int mb = EM_ASM_INT({
            const v = new URLSearchParams(window.location.search)
                .get('membudget');
            return v === null ? 0 : (parseInt(v, 10) | 0);
        });
        s_budget.reset(mb > 0 ? size_t(mb) * 1024 * 1024 : 0);
        if (mb > 0)
            std::printf("fcviewer: geometry budget %d MB (pinned)\n", mb);
        else
            std::printf("fcviewer: geometry budget %zu MB to start, "
                        "adapting; %zu MB system, %zu MB device hint, "
                        "%zu MB heap ceiling\n",
                        s_budget.value() >> 20,
                        Render::MemoryBudget::systemMemory() >> 20,
                        Render::MemoryBudget::deviceHint() >> 20,
                        s_budget.ceiling() >> 20);
    }
    // ?camup=frame|rate|lazy|lazy1 -- the camera uplink policy
    // (docs/ThinClient.md sec 8.10a). A URL parameter and not a build
    // switch because the three are meant to be measured against each
    // other on the same bundle, and because which one is right depends
    // on the link and on whether anyone is editing.
    //   frame  once per frame when it changed (what stage 3 shipped)
    //   rate   the same, throttled (?camuphz=<n>, default 10)
    //   lazy   only with a click, and only when it moved
    //   lazy1  the same, carried inside the click as one 'Q' message
    //          -- the default, see sec 8.10b
    {
        const int mode = EM_ASM_INT({
            const v = new URLSearchParams(window.location.search)
                .get('camup');
            if (v === null)
                return -1;
            return v === 'rate' ? 1
                 : v === 'lazy' ? 2
                 : v === 'lazy1' ? 3 : 0;
        });
        const int hz = EM_ASM_INT({
            const v = new URLSearchParams(window.location.search)
                .get('camuphz');
            return v === null ? 0 : (parseInt(v, 10) | 0);
        });
        if (hz > 0)
            s_camRateMs = 1000.0 / double(hz);
        if (mode >= 0) {
            static const char *const kNames[] = {"frame", "rate", "lazy",
                                                 "lazy1"};
            s_camUplink = CamUplink(mode);
            if (s_camUplink == CamUplink::Rate)
                std::printf("fcviewer: camera uplink rate, %.0f ms\n",
                            s_camRateMs);
            else
                std::printf("fcviewer: camera uplink %s\n", kNames[mode]);
        }
    }
    // ?fetchweight= / ?keepweight= — how much payload size discounts a
    // chunk's value when deciding what to ask for next and what to
    // hold (see kFetchSizeWeight). 0 = ignore size, rank by what the
    // camera sees; 1 = strictly per byte. Read as a percentage so the
    // integer-only EM_ASM_INT path can carry a fraction.
    {
        auto weight = [](const char *name, float fallback) {
            const int pct = EM_ASM_INT({
                const v = new URLSearchParams(window.location.search)
                    .get(UTF8ToString($0));
                return v === null ? -1
                    : Math.round(Math.max(0, Math.min(2, parseFloat(v))) * 100);
            }, name);
            if (pct < 0)
                return fallback;
            const float w = float(pct) / 100.0f;
            std::printf("fcviewer: %s %.2f\n", name, w);
            return w;
        };
        s_fetchSizeWeight = weight("fetchweight", kFetchSizeWeight);
        s_keepSizeWeight = weight("keepweight", kKeepSizeWeight);
    }
    s_noProgressive = EM_ASM_INT({
        return new URLSearchParams(window.location.search)
            .has('noprogressive') ? 1 : 0;
    }) != 0;
    if (s_noFetchOrder)
        std::printf("fcviewer: fetch order off, asking for everything\n");
    if (s_noProgressive)
        std::printf("fcviewer: progressive display off, waiting for the "
                    "whole publish\n");

    // The document wire's page parameters (docs/MultiDocServe.md §6),
    // read before the first fetch so the initial scene is already the
    // named document's.
    if (char *cycles = fcviewer_query_param("cycles")) {
        s_cyclesUrlDevice = *cycles ? cycles : "CPU";
        std::free(cycles);
    }
    if (char *doc = fcviewer_query_param("doc")) {
        s_docName = doc;
        std::free(doc);
        std::printf("fcviewer: document '%s'\n", s_docName.c_str());
    }
    if (char *client = fcviewer_query_param("client")) {
        s_clientLabel = client;
        std::free(client);
    }
    else if (char *saved = fcviewer_stored_client()) {
        // A name typed into the menu once (docs/MultiDocServe.md §6):
        // the share link carries none, so without this every reload
        // would face the host with an unnamed connection again. An
        // explicit ?client= still wins — it names this visit.
        s_clientLabel = saved;
        std::free(saved);
    }
    // Whatever it ended up being, tell the menu.
    fcviewer_client_event(s_clientLabel.c_str());
    if (char *token = fcviewer_query_param("token")) {
        s_tokenParam = token;
        std::free(token);
    }

    if (char *sceneParam = fcviewer_scene_param()) {
        // ?scene=off pins the old dump-only behavior explicitly.
        if (std::strcmp(sceneParam, "off") != 0)
            s_sceneUrl = sceneParam;
        std::free(sceneParam);
    }
    else if (!bundledScene) {
        // The scene server serves this page itself now (one tunnel
        // carries page, stream and blobs — ShareAccess.md §5.1/§7.5),
        // so absent an explicit ?scene= the page streams from where it
        // came from, and a share link shrinks to ?token=…&doc=….
        // A build with a bundled snapshot keeps the dump-viewing
        // default: its origin is a static file server, not a backend.
        if (char *origin = fcviewer_page_origin()) {
            s_sceneUrl = origin;
            std::free(origin);
        }
    }
    if (!s_sceneUrl.empty()) {
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
    fcviewer_install_pen_hover();

    emscripten_set_main_loop(mainLoop, 0, 0);
    return 0;
}
