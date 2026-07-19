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

#include <bx/math.h>

#include "BGFXRenderer.h"
#include "SceneDump.h"
#include "StandalonePlatform.h"

static std::unique_ptr<Render::Renderer> s_renderer;
static Render::SceneSnapshot s_snap;
static bool s_haveScene = false;
static std::set<int> s_selIds;

// Live streaming (?scene=<http://host:port> page parameter): poll the
// desktop scene server (FC_BGFX_SERVE_SCENE) and re-apply the feeds on
// every version change.
static std::string s_sceneUrl;
static uint64_t s_sceneVersion = 0;

EM_JS(char *, fcviewer_scene_param, (), {
    var p = new URLSearchParams(window.location.search).get('scene');
    if (!p)
        return 0;
    var len = lengthBytesUTF8(p) + 1;
    var s = _malloc(len);
    stringToUTF8(p, s, len);
    return s;
});

static int s_width = 1024;
static int s_height = 768;

// Orbit camera state around the scene bounds.
static float s_center[3] = {0.0f, 0.0f, 0.0f};
static float s_panX = 0.0f, s_panY = 0.0f;  // pan in camera plane
static float s_yaw = 0.785f;
static float s_pitch = 0.5f;
static float s_dist = 10.0f;
static float s_diag = 10.0f;
static bool s_dragging = false;
static bool s_panning = false;
static int s_lastX = 0, s_lastY = 0;

static void buildCamera(float *viewMtx, float *projMtx)
{
    const float cp = std::cos(s_pitch), sp = std::sin(s_pitch);
    const float cy = std::cos(s_yaw), sy = std::sin(s_yaw);
    // Z-up spherical orbit (FreeCAD convention).
    bx::Vec3 dir(cp * cy, cp * sy, sp);
    bx::Vec3 up(0.0f, 0.0f, 1.0f);
    bx::Vec3 right = bx::normalize(bx::cross(dir, up));
    bx::Vec3 camUp = bx::normalize(bx::cross(right, dir));
    bx::Vec3 at(s_center[0], s_center[1], s_center[2]);
    at = bx::add(at, bx::add(bx::mul(right, s_panX),
                             bx::mul(camUp, s_panY)));
    bx::Vec3 eye = bx::add(at, bx::mul(dir, s_dist));
    // Right-handed like the Coin camera the renderer's shading assumes
    // (camera forward = -z in view space; bx defaults to left-handed).
    bx::mtxLookAt(viewMtx, eye, at, up, bx::Handedness::Right);

    const float aspect = s_height > 0
        ? float(s_width) / float(s_height) : 1.0f;
    const float neard = bx::max(0.001f * s_diag, s_dist - 4.0f * s_diag);
    const float fard = s_dist + 4.0f * s_diag;
    // WebGL keeps the GL clip conventions (homogeneous depth); bgfx is
    // not initialized yet when the first frame builds its camera, so
    // don't ask getCaps().
    bx::mtxProj(projMtx, 45.0f, aspect, neard, fard, true,
                bx::Handedness::Right);
}

static void mainLoop()
{
    double w = 0, h = 0;
    emscripten_get_element_css_size("#canvas", &w, &h);
    int iw = int(w), ih = int(h);
    if (iw > 0 && ih > 0 && (iw != s_width || ih != s_height)) {
        s_width = iw;
        s_height = ih;
        emscripten_set_canvas_element_size("#canvas", iw, ih);
        Render::BGFXRenderer::setWindowSize(iw, ih);
    }

    float viewMtx[16], projMtx[16];
    buildCamera(viewMtx, projMtx);
    QColor bg((s_snap.clearColor >> 24) & 0xff,
              (s_snap.clearColor >> 16) & 0xff,
              (s_snap.clearColor >> 8) & 0xff);
    s_renderer->render(bg, viewMtx, projMtx);
}

static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent *e, void *)
{
    s_dragging = true;
    s_panning = e->button == 2 || e->shiftKey;
    s_lastX = int(e->clientX);
    s_lastY = int(e->clientY);
    return EM_TRUE;
}

static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent *, void *)
{
    s_dragging = false;
    return EM_TRUE;
}

static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent *e, void *)
{
    if (!s_dragging)
        return EM_FALSE;
    int dx = int(e->clientX) - s_lastX;
    int dy = int(e->clientY) - s_lastY;
    s_lastX = int(e->clientX);
    s_lastY = int(e->clientY);
    if (s_panning) {
        const float scale = 2.0f * s_dist
            * std::tan(0.5f * 45.0f * bx::kPi / 180.0f)
            / float(s_height > 0 ? s_height : 1);
        s_panX -= float(dx) * scale;
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
    s_dist *= e->deltaY > 0 ? 1.1f : (1.0f / 1.1f);
    s_dist = bx::clamp(s_dist, 0.01f * s_diag, 50.0f * s_diag);
    return EM_TRUE;
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
        s_dist = 2.0f * s_diag;
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
    s_renderer->setAOConfig(s_snap.aoconf);
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
    if (!s_snap.highlight.empty()) {
        Render::DrawCallList hdraws = s_snap.highlight;
        s_renderer->setHighlight(std::move(hdraws),
                                 s_snap.highlightWholeOnTop);
    }
    else {
        s_renderer->clearHighlight();
    }
    s_haveScene = true;
    if (fit)
        fitCamera();
}

//////////////////////////////////////////////////////////////////////
// Scene-server polling

static void schedulePoll();

static void onPollResult(emscripten_fetch_t *fetch)
{
    // onsuccess covers 204 (unchanged, no body) and 200 (new payload).
    if (fetch->status == 200 && fetch->numBytes > 8) {
        uint64_t version = 0;
        std::memcpy(&version, fetch->data, sizeof(version));
        Render::SceneSnapshot snap;
        if (Render::loadSceneSnapshot(fetch->data + 8,
                                      size_t(fetch->numBytes) - 8, snap)) {
            s_sceneVersion = version;
            bool first = !s_haveScene;
            s_snap = std::move(snap);
            applySnapshot(first);
            std::printf("fcviewer: scene update v%llu, %zu draws\n",
                        (unsigned long long)version, s_snap.scene.size());
        }
        else {
            std::printf("fcviewer: scene update parse FAILED\n");
        }
    }
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
        doPoll();
    }

    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE,
                                      onMouseDown);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                    nullptr, EM_TRUE, onMouseUp);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
                                      nullptr, EM_TRUE, onMouseMove);
    emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, onWheel);

    emscripten_set_main_loop(mainLoop, 0, 0);
    return 0;
}
