/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include "BGFXRendererP.h"

bool BGFXView::stepParticles(const Render::DrawCallList &scene,
                   float animTime, bool freeze)
{
    for (auto &v : particles)
        v.second.slot = -1;
    particlesLive = false;
    if (!particleStateOk || !bgfx::isValid(m_progPSimInit)) {
        // Nothing can hold state on this backend; the emitters
        // still draw, from their stateless vertex stage alone.
        for (auto &v : particles)
            v.second.destroy();
        particles.clear();
        return false;
    }

    bool pending = false;
    impactReset = false;
    int slot = 0;
    std::set<uint64_t> seen;
    for (const auto &d : scene) {
        const auto &sh = d.material.usershader;
        if (!sh || sh->stage != "particle"
                || sh->simulateSource.empty() || !d.objectKey)
            continue;
        if (!seen.insert(d.objectKey).second)
            continue;   // one state per emitter, not per draw
        if (slot >= kParticleSlots)
            continue;   // over budget: falls back to stateless

        int count = 0;
        float rate = 60.0f, warmup = 0.0f, timeScale = 1.0f;
        emitterParams(*sh, count, rate, warmup, nullptr, &timeScale);
        if (count <= 0)
            continue;
        auto &st = particles[d.objectKey];
        // Claimed before the program check: an emitter whose step
        // program is still compiling keeps the state it has (it
        // draws stateless meanwhile) instead of being collected
        // and reallocated every frame.
        st.lastFrame = uint32_t(frame);
        bgfx::ProgramHandle step =
            _BGFXLib.getUserProgram(*sh, "vs_fc_comp", true);
        if (!bgfx::isValid(step))
            continue;   // still compiling, or failed: stay stateless

        // What the state is of: a different step program, particle
        // count or clock mode is a different simulation and starts
        // over rather than reinterpreting the old numbers.
        uint64_t ident = 1469598103934665603ULL;
        hashBytes(ident, sh->simulateSource.data(),
                  sh->simulateSource.size());
        hashBytes(ident, &count, sizeof(count));
        hashBytes(ident, &rate, sizeof(rate));
        hashBytes(ident, &freeze, sizeof(freeze));
        if (st.identity != ident) {
            st.identity = ident;
            st.needInit = true;
        }

        const uint16_t gridW = uint16_t(std::min(count, 256));
        const uint16_t gridH =
            uint16_t((count + gridW - 1) / std::max<int>(1, gridW));
        // The count is part of the allocation, not just of the grid:
        // two counts can round to the same grid, and the splat's
        // index buffer is sized by the count itself.
        if (st.gridW != gridW || st.gridH != gridH || st.count != count
                || !bgfx::isValid(st.fbo[0])) {
            st.destroy();
            const uint64_t flags = BGFX_TEXTURE_RT
                | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                | BGFX_SAMPLER_MIP_POINT
                | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            for (int i = 0; i < 2; ++i) {
                st.pos[i] = bgfx::createTexture2D(
                    gridW, gridH, false, 1,
                    bgfx::TextureFormat::RGBA32F, flags);
                st.vel[i] = bgfx::createTexture2D(
                    gridW, gridH, false, 1,
                    bgfx::TextureFormat::RGBA32F, flags);
                st.imp[i] = bgfx::createTexture2D(
                    gridW, gridH, false, 1,
                    bgfx::TextureFormat::RGBA32F, flags);
                bgfx::TextureHandle att[3] = {st.pos[i], st.vel[i],
                                              st.imp[i]};
                st.fbo[i] = bgfx::createFrameBuffer(3, att, false);
            }
            // The counter the impact splat draws from: one quad per
            // particle carrying its index and the corner. Static —
            // it only ever says 0, 1, 2, ... and what changes
            // underneath it is the grid those index.
            {
                static const float corner[6][2] = {
                    {0, 0}, {1, 0}, {1, 1},
                    {0, 0}, {1, 1}, {0, 1}};
                const bgfx::Memory *mem = bgfx::alloc(
                    uint32_t(count * 6 * sizeof(float) * 3));
                auto *v = reinterpret_cast<float *>(mem->data);
                for (int i = 0; i < count; ++i) {
                    for (int c = 0; c < 6; ++c) {
                        float *p = v + (i * 6 + c) * 3;
                        p[0] = float(i);
                        p[1] = corner[c][0];
                        p[2] = corner[c][1];
                    }
                }
                PointVertex::init();
                st.idxVb = bgfx::createVertexBuffer(
                    mem, PointVertex::ms_layout);
            }
            st.gridW = gridW;
            st.gridH = gridH;
            st.needInit = true;
        }
        st.count = count;
        st.slot = slot++;
        st.modelIdentity = d.identity;
        if (!d.identity)
            std::memcpy(st.model, d.model, sizeof(st.model));

        // Spawn box: the emitter geometry's own bounds, brought
        // back into the model space the particle positions and the
        // draw's vertex stage both work in. A rotated model matrix
        // makes the box the conservative axis-aligned hull of the
        // rotated bounds — bigger than the seed box, never smaller.
        float bmin[3] = {d.bboxMin[0], d.bboxMin[1], d.bboxMin[2]};
        float bmax[3] = {d.bboxMax[0], d.bboxMax[1], d.bboxMax[2]};
        if (!d.identity) {
            float inv[16];
            bx::mtxInverse(inv, d.model);
            float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
            float hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (int c = 0; c < 8; ++c) {
                const bx::Vec3 corner(
                    (c & 1) ? d.bboxMax[0] : d.bboxMin[0],
                    (c & 2) ? d.bboxMax[1] : d.bboxMin[1],
                    (c & 4) ? d.bboxMax[2] : d.bboxMin[2]);
                const bx::Vec3 out = bx::mul(corner, inv);
                lo[0] = std::min(lo[0], out.x);
                lo[1] = std::min(lo[1], out.y);
                lo[2] = std::min(lo[2], out.z);
                hi[0] = std::max(hi[0], out.x);
                hi[1] = std::max(hi[1], out.y);
                hi[2] = std::max(hi[2], out.z);
            }
            for (int j = 0; j < 3; ++j) {
                bmin[j] = lo[j];
                bmax[j] = hi[j];
            }
        }

        // How far the simulation must reach this frame. Live: the
        // shared effect clock, so an emitter that appears mid-
        // session starts now instead of catching up from zero.
        // Frozen: the warm-up, reached over as many frames as the
        // per-frame step budget needs.
        //
        // Both are wall-clock durations, and the time scale is
        // what turns them into the emitter's own time — the whole
        // of what the scale does. The step keeps its length, so
        // each step advances the same slice of the trajectory it
        // did before and the arc is unchanged; a scale of k simply
        // demands k times as many of them per second, which is the
        // motion playing k times faster. Doing this by scaling the
        // step instead would be a different simulation
        // (kParticleSteps).
        const float dt = 1.0f / std::max(1.0f, rate);
        const float target = (freeze ? warmup : animTime) * timeScale;
        // A frozen frame's state is a pure function of the warm-up,
        // never of what the emitter happened to have simulated
        // before it: a target the state has already run past
        // rewinds to the reset and replays. Without this a
        // shortened warm-up would simply hold the longer one's
        // state and the same inputs would not give the same pixels.
        if (freeze && st.simTime > target + dt * 0.5f)
            st.needInit = true;
        if (st.needInit)
            st.simTime = freeze ? 0.0f : target;
        // Write off a long absence rather than fast-forwarding
        // through it. A frozen frame is exempt: its whole point is
        // to reach a fixed warm-up from zero, however many frames
        // that takes.
        // The allowance is wall-clock time, so it is worth the
        // same scaling as the target: a scaled clock owes
        // proportionally more simulated time for the same absence,
        // and this is also what absorbs the jump when the scale
        // itself is edited live — the emitter resumes at the new
        // rate instead of fast-forwarding through the difference.
        const float maxLag = kParticleMaxLag * timeScale;
        if (!freeze && st.simTime < target - maxLag)
            st.simTime = target - maxLag;
        // This emitter is bound and its sprites move from here on;
        // a frozen frame deliberately does not count, since it is
        // meant to render identically twice.
        particlesLive = particlesLive || !freeze;

        static const bool dbgP = getenv("FC_BGFX_DEBUG_PARTICLES");
        if (dbgP)
            std::printf("bgfx particles: key=%llx slot=%d count=%d "
                        "grid=%dx%d init=%d simTime=%.3f target=%.3f "
                        "dt=%.4f\n",
                        (unsigned long long)d.objectKey, st.slot, count,
                        int(gridW), int(gridH), int(st.needInit),
                        double(st.simTime), double(target), double(dt));

        int steps = 0;
        st.stepCount = 0;
        while (steps < kParticleSteps
                && (st.needInit || st.simTime + dt <= target)) {
            const bool init = st.needInit;
            const int dst = st.needInit ? 0 : 1 - st.cur;
            const uint16_t pass = uint16_t(
                ViewParticleSim0 + st.slot * kParticleSteps + steps);
            const uint16_t vid = this->vid(pass);
            bgfx::setViewFrameBuffer(vid, st.fbo[dst]);
            bgfx::setViewClear(vid, uint16_t(BGFX_CLEAR_NONE),
                               0, 1.0f, 0);
            bgfx::setViewRect(vid, 0, 0, gridW, gridH);
            bgfx::setViewTransform(vid, nullptr, nullptr);
            bgfx::setViewMode(vid, bgfx::ViewMode::Default);

            if (!init) {
                st.simTime += dt;
                bgfx::setTexture(10, s_pstate0, st.pos[st.cur]);
                bgfx::setTexture(11, s_pstate1, st.vel[st.cur]);
            }
            _BGFXLib.pushUserParams(*sh);
            const float grid[4] = {float(gridW), float(gridH),
                                   float(count), dt};
            const float pmin[4] = {bmin[0], bmin[1], bmin[2],
                                   float(d.objectKey & 0xffffu)};
            const float pmax[4] = {bmax[0], bmax[1], bmax[2],
                                   st.simTime};
            bgfx::setUniform(u_pgrid, grid);
            bgfx::setUniform(u_pboxMin, pmin);
            bgfx::setUniform(u_pboxMax, pmax);
            fullscreen(pass, init ? m_progPSimInit : step,
                       BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            st.cur = dst;
            st.needInit = false;
            // The reset reports no impacts (it is a birth, not a
            // collision), so only real steps owe the splat pass
            // anything.
            if (init)
                impactReset = true;
            else if (st.stepCount < kParticleSteps)
                st.stepBuf[st.stepCount++] = dst;
            ++steps;
        }
        st.stamp = freeze ? st.simTime : animTime;
        if (st.simTime + dt <= target)
            pending = true;   // still owes steps: keep redrawing
    }

    // An emitter that left the scene (unbound, hidden, deleted)
    // gives its targets back; coming back is a fresh simulation.
    for (auto it = particles.begin(); it != particles.end();) {
        if (it->second.lastFrame != uint32_t(frame)) {
            it->second.destroy();
            it = particles.erase(it);
        }
        else
            ++it;
    }
    return pending;
}

void BGFXView::splatImpacts(float animTime, bool freeze, const float *foot)
{
    impactActive = false;
    impactNow = animTime;
    if (!foot || !particleStateOk || !bgfx::isValid(m_progPImpact))
        return;
    float ext = std::max(foot[2] - foot[0], foot[3] - foot[1]);
    if (!(ext > 0.0f))
        return;
    // Square, and two cells of margin on every side. The margin is
    // what keeps the water off the map's border: a fragment there
    // would have its neighbourhood clamped, tapping one border cell
    // several times and raising that one hit's ring two or three
    // times over — a bright arc along the rim.
    ext *= float(kImpactRes + 4) / float(kImpactRes);
    const float cx = (foot[0] + foot[2]) * 0.5f;
    const float cy = (foot[1] + foot[3]) * 0.5f;
    const float frame4[4] = {cx - ext * 0.5f, cy - ext * 0.5f,
                             1.0f / ext, float(kImpactRes)};

    // The map goes when it stops being about anywhere: an emitter
    // replaying its history from a reset, or a footprint that moved
    // so that every cell now covers a different piece of the world.
    // Only a real move counts — a bbox that jitters in its last
    // digits would otherwise wipe the rings every frame.
    bool refit = impactReset
        || std::fabs(frame4[2] - impactFrame[2])
            > impactFrame[2] * 1.0e-3f
        || std::fabs(frame4[0] - impactFrame[0]) * frame4[2] > 1.0e-3f
        || std::fabs(frame4[1] - impactFrame[1]) * frame4[2] > 1.0e-3f;
    if (!bgfx::isValid(impactFbo)) {
        impactTex = bgfx::createTexture2D(
            kImpactRes, kImpactRes, false, 1,
            bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(impactTex))
            return;
        impactFbo = bgfx::createFrameBuffer(1, &impactTex, false);
        if (!bgfx::isValid(impactFbo))
            return;
        refit = true;
    }
    std::memcpy(impactFrame, frame4, sizeof(impactFrame));

    const uint16_t vid = this->vid(ViewParticleImpact);
    bgfx::setViewFrameBuffer(vid, impactFbo);
    bgfx::setViewClear(vid,
                       uint16_t(refit ? BGFX_CLEAR_COLOR
                                      : BGFX_CLEAR_NONE),
                       0, 1.0f, 0);
    bgfx::setViewRect(vid, 0, 0, kImpactRes, kImpactRes);
    bgfx::setViewTransform(vid, nullptr, nullptr);
    // Sequential: two steps of one emitter are two reports about
    // the same instant of the same particles, and the later one is
    // the newer news wherever they land in the same cell.
    bgfx::setViewMode(vid, bgfx::ViewMode::Sequential);
    bgfx::touch(vid);   // a refit must clear even with no impacts

    float frozenNow = 0.0f;
    for (auto &kv : particles) {
        auto &st = kv.second;
        const int steps = st.stepCount;
        st.stepCount = 0;   // reported once, whatever happens next
        if (st.slot < 0)
            continue;
        // Every live emitter's reading counts, not only that of one
        // that struck something this frame: a warmed-up frozen
        // emitter takes no more steps and reports no more impacts,
        // and a clock that fell back to zero there would age every
        // ring in the map into the future and show none of them.
        frozenNow = std::max(frozenNow, st.stamp);
        if (steps <= 0 || !bgfx::isValid(st.idxVb))
            continue;
        for (int s = 0; s < steps; ++s) {
            if (!bgfx::isValid(st.imp[st.stepBuf[s]]))
                continue;
            bgfx::setTexture(12, s_pimpsrc, st.imp[st.stepBuf[s]]);
            const float grid[4] = {float(st.gridW), float(st.gridH),
                                   float(st.count), 0.0f};
            bgfx::setUniform(u_pgrid, grid);
            bgfx::setUniform(u_impactFrame, frame4);
            // Every step of this frame is stamped with the frame's
            // reading: they are at most a step apart, which is far
            // below what a ring's shape can show.
            const float now[4] = {st.stamp, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(u_impactNow, now);
            if (!st.modelIdentity)
                bgfx::setTransform(st.model);
            bgfx::setVertexBuffer(0, st.idxVb);
            bgfx::setState(BGFX_STATE_WRITE_RGB
                           | BGFX_STATE_WRITE_A);
            bgfx::submit(vid, m_progPImpact);
            ++drawcount;
        }
    }
    // Frozen, the surface ages the rings against how far the
    // emitters have simulated. Two emitters on different time
    // scales disagree about that by the difference in their
    // scales; nothing about a warm-up capture depends on them
    // agreeing to better than a ring's lifetime.
    if (freeze)
        impactNow = frozenNow;
    impactActive = true;
}
