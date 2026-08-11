# Draw submission: getting the frame's draws onto the GPU

Status: **plan**, plus the instruments it needs. Built so far:
`render instancing:` (what batching already collapses), `render passes:`
(per-pass CPU/GPU cost), and `FC_NO_AUDIT=1` in the harness. No
submission route has been changed yet.

**IMPORTANT -- read before spending any number on this page.**

**1. A frame timed with the swap waiting for the display is not a frame
cost.** Qt's default swap interval is 1, which pins the frame to the
vblank grid: a 20 ms frame is *presented* in 33.3 ms on a 60 Hz screen.
The wait is idle and it lands in `outside`, where it reads as though the
application were spending it. **Every row whose clock matters wants
`FC_SWAP_INTERVAL=0`** (and `FC_SPIN_SLEEP=0` in the harness). See "IT
WAS THE DISPLAY".

**2. Numbers taken under `vglrun`/Xvfb are NOT frame costs either.**
Every measurement here predating the native run was, and the harness
moved the frame 50.6 -> 33.3 ms and one span by 19x. The VirtualGL
recipe is still correct for "is it the real GPU" -- it is -- but not for
"what does a frame cost".

**3. The baseline (default culling row, 12850 draws, native, swap 0):**
frame **20.4 ms** against **19.8 ms of GPU**. **This scene is
GPU-bound.** The CPU spends 16.0 ms and then waits ~4 ms for the GPU.
Of the CPU: bgfx backend 9.9, our own C++ 6.0, **all of Coin+Qt 0.8**.

**4. The original headline was wrong twice**: "30577 draws, 32 ms, 76 ms"
was the culling-OFF validation row *with* the cull audit inflating it.
Always state the culling config and the audit setting with any number.

This is the workstream that follows occlusion culling, and it starts
where `docs/FarFieldProxies.md` §12.19 ends: both sides of box-based
occlusion are measured and both are exhausted. Ten times the occluders
bought 3.1% (§12.16); the tightest possible occludee bound — every
triangle and segment asked separately, at a cost no frame could pay —
buys 8.9% (§12.19). What is left is not culling.

## The measured problem

**CORRECTED 2026-08-11. The numbers this document was scoped against
were wrong twice over, and both errors inflated the prize.** The
superseded headline read "30577 draws, 32 ms submit, 76 ms frame". That
sample was taken (a) with the **cull audit on**, which adds the entire
17727-row scene list to the frame as a second rasterization, and (b)
**before the scene converged**, while occlusion culling was still
resolving. Neither is a frame a user ever renders.

First correction: the audit. Converged tails of two audit-off runs
(`clean_run.out` / `clean2.out`, 10 windows each, deterministic draw
counts), same rack model and camera, **both on the software-oracle
culling row** -- the audit is the only thing toggled between the columns:

| | audit ON (converged) | **audit OFF (converged)** | the instrument |
|---|---|---|---|
| draws | 24213 | **6485** | +17728 |
| frame | 80.41 ms | **54.78 ms** | +25.6 ms |
| submit | 28.70 ms | **10.77 ms** | +17.9 ms |
| gpu | 36.16 ms | **19.64 ms** | +16.5 ms |
| prims | 74.1 M | **33.5 M** (5165/draw) | |

(medians of 10 converged windows; audit-off submit spread 10.47-11.04,
gpu 19.50-19.81, frame 53.99-55.95.)

**The audit's cost is exactly the scene draw list**: 6485 + 17727 =
24212, against 24213 measured. That is the id pass re-rendering every
row, and it was on by default -- `cull_audit.py` hard-coded
`RenderDebug_CullAudit = True` until `64cb4090d9`. So **every timing
this workstream has ever quoted included it.**

### What the correction does to the plan

**bgfx's `submit` is a minority of the frame's CPU.** On the row above,
10.8 ms of 54.8 ms; at the default culling config, 16.4 ms of 50.6 ms.

- gpu 19.6 ms;
- and `cpuTimeFrame` minus the backend's submit leaves **~44 ms that is
  neither**. bgfx's own field names are explicit: `cpuTimeFrame` is "CPU
  time between two `bgfx::frame` calls" (the whole application frame),
  while `cpuTimeBegin/End` is "**render thread** CPU submit" -- the
  backend issuing GL calls. Our 466-line `submit()`, the cull, the
  instance grouping and Coin's composite all live in the ~44 ms, and
  **nothing currently measures any of it.**

**So the largest item in the frame is the one with no instrument on
it**, and phases 1 and 2 below both target that unmeasured bucket rather
than the 10.8 ms the `render passes:` line accounts for.

### MEASURED 2026-08-11: where the frame's CPU actually goes

!! **Superseded twice over. Every millisecond in this subsection and the
three that follow it was taken under VirtualGL *and* under a vblank-
locked swap.** They are kept because the reasoning they record is the
point -- see "IT WAS THE DISPLAY" for what the numbers actually are. The
draw counts and the operating-point table below are still correct.

The instrument was built (`cpu ours / bgfx::frame / outside` on the frame
line) and run. **First: name the operating point.** Culling settings
change the draw count by 5x, and the three configurations measured on
this model and camera are all "the baseline" to somebody:

| config | draws |
|---|---|
| culling off (the audit's validation row) | 30578 |
| hardware oracle, ttl 1e6 / confirm 2 (**the default**) | 12850 |
| software oracle, tight bounds (`sw/1/250000/0/1/0/2/100/1`) | 6485 |

The superseded "30577 draws" headline was the **culling-off validation
row**, on top of being audit-inflated. The 6485 quoted above is the
aggressive software-oracle row, not the default.

At the **default** config, audit off, converged (medians of 10 windows):

| term | ms | share |
|---|---|---|
| frame | 50.61 | |
| ours (all of `render()`) | 35.70 | 70.5% |
| -- our C++ before bgfx | **19.34** | **38.2%** |
| -- bgfx::frame (backend) | 16.37 | 32.3% |
| outside (Coin + Qt + app) | 14.97 | 29.6% |
| gpu | 22.14 | |

Two things fall out, and both cut against the plan as written:

**1. Coin is not the problem.** "Outside" is 15 ms of 50 -- real, but the
smallest of the three terms. The ~44 ms with no instrument was mostly
**ours**, not Coin's.

**2. Our own per-draw C++ (19.3 ms) is LARGER than bgfx's backend
submit (16.4 ms)** -- and it is barely per-draw at all. Fitting the two
draw counts:

```
our C++       = 15.56 ms fixed + 0.294 us/draw
bgfx::frame   =  5.34 ms fixed + 0.859 us/draw
```

At the default config that makes our C++ **15.6 ms fixed against 3.8 ms
per-draw: 80% of it does not scale with draws at all.**

**The bgfx fit is validated on a third point from an independent run.**
It predicts 10.91 ms of backend submit at 6485 draws; the earlier
software-oracle run measured **10.77 ms** -- 1.3% out, across a different
run, a different culling mechanism and a different day.

! The `our C++` fit has only two points and they differ in more than draw
count: the culling-off row also skips the cull, which should *lower* its
fixed cost. So 15.56 ms is if anything an **under**-estimate of the fixed
term. It needs a third point before it is quoted as a coefficient.

**Consequence for the phases.** Phase 2 was scoped as "do less per
draw". The per-draw part of our C++ is 3.8 ms of a 50 ms frame; the
**fixed** part is 15.6 ms. Whatever that fixed work is -- the scene walk,
the cull, the group rebuild -- it is now the single largest CPU item in
the renderer, and it is not addressed anywhere in this plan. **Find it
before building phase 2.**

### FOUND: the fixed cost is the post-submit pass region, and it is flat

The 15.6 ms was chased with a second instrument, `render cpu phases:`,
which partitions `render()` exactly (the parts sum to `ours` to within
0.02 ms). At the default config, 12850 draws in a 50.6 ms frame:

| term | ms | share of frame |
|---|---|---|
| pre (setup, pass marking, instance groups) | 1.90 | 3.8% |
| cull (frustum + occlusion) | 0.41 | 0.8% |
| main submit loop | 3.99 | 7.9% |
| **post (the passes after the main loop)** | **12.95** | **25.6%** |
| bgfx::frame (backend) | 16.51 | 32.6% |
| outside (Coin + Qt + app) | 14.79 | 29.2% |

**Two hypotheses died here, which is why the instrument was worth
building rather than reasoning from the code:**

- ** The cull is not the cost.** 0.41 ms. It walks every row each frame
  and it is still under 1% of the frame. Every intuition that put the
  cull near the top was wrong.
- ** The main submit loop is not the cost either.** 4.0 ms. It walks all
  17727 rows and evaluates predicates per row, which looked like the
  obvious 15 ms; it is a quarter of that.

**`post` is the fixed cost, and it is flat to the point of being
diagnostic**: 13.00 / 12.95 / 12.84 ms at 12849 / 12850 / 30578 draws --
a 2.4x change in draw count moves it by 1%. Nothing about it depends on
what survives culling.

** RETRACTED, and the retraction is the lesson.** This document first
explained the 13 ms as "eight more `for (const auto &draw : scene)`
loops between the main submit loop and `bgfx::frame()`, ~142k row visits
a frame that culling cannot reduce". **That explanation is wrong.**
Reading the guards instead of counting the loops:

- `groundReflActive || waterReflActive` gates one,
- `twoPass` / `sceneTwoPass` (hidden-line) gates three,
- `hl.show && hl.sceneOutline` gates two,

and **none of those are active on this camera** -- the `render passes:`
line lists 14 live views and no reflection, hidden-line or outline pass
among them. Only **three** full scans actually run each frame (the two
on-top passes, plus a ninth found later inside `submitSectionCaps`), and
all three short-circuit per row on a cheap predicate
(`numclipplanes > 0`, `material.ontop`). Three scans of 17727 cheap
tests is well under a millisecond, not 13.

X **Counting loops in the source is not measuring them.** The
measurement (post = 12.95 ms, flat) stands; the *cause* is unknown and
the region has been subdivided (`caps / effects / sel / tail`, with an
`unattr` residual that must come out ~0 or the split is not to be
believed) rather than explained again from reading.

`pre`, by contrast, scales properly: 1.90 ms at 12850 draws against
6.38 ms at 30578, because the instance-group work is per visible member.

### NAMED: it is the Qt <-> bgfx GL context switch

The subdivision was run, and the first result was that the four spans
accounted for **0.6 ms of the 12.9** -- `unattr` came back at 12.34. The
residual earned its place immediately: `post` is *derived*
(`ours - pre - submitloop - bgfx::frame`), so it also contains everything
**after** `bgfx::frame()` returns, which no checkpoint covered. The cost
was never in the pass region at all.

Instrumenting the hand-off around `bgfx::frame()` closes it (`unattr`
now -0.11 to +0.05, so the split is complete):

| span | ms | what it is |
|---|---|---|
| `widget->doneCurrent()` | **5.15** | release the Qt GL context |
| `_BGFXLib.makeCurrent()` | **7.16** | make bgfx's context current |
| `widget->makeCurrent()` | 0.08 | take Qt's back afterwards |
| `view->blit(...)` | 0.03 | copy bgfx's target into the widget |
| everything in the pass region | 0.64 | caps, effects, sel, tail |

**12.3 ms/frame -- 24% of the frame -- is one GL context switch.** It is
flat in draw count because a context switch does not care what the scene
contains, which is exactly the signature the fixed cost had.

Note what this does to the earlier reasoning: the comment on
`timedBgfxFrame` excludes these calls from the bgfx figure on the grounds
that they are "Qt's cost, not bgfx's". That was correct, and it is
precisely why they needed a number of their own -- excluded from one
bucket and never given another, they were invisible in every readout.

**Why the switch exists, and what removes it.** bgfx owns a GL context
separate from the Qt widget's, so a frame must go Qt (Coin) -> bgfx ->
Qt. The blit itself is free (0.03 ms); it is the two context
transitions that cost. => This is the same architectural knot the Vulkan
section reaches: **while Coin composites into the viewport, the frame
pays a context round-trip.** Removing it is worth 24% of the frame on
its own, before any submission work.

### MEASURED NATIVELY: it WAS the harness, and the correction above was wrong

The monitor was reconnected (DP-2, 1920x1080), so the missing arm became
possible: the same model, camera and culling row, on the real X session,
**no VirtualGL and no Xvfb**. Both checks passed -- the log says
`NVIDIA GeForce RTX 3060/PCIe/SSE2` and the run painted (frames 30-31
per window, against the zero-frames trap).

Default config, 12850 draws, converged medians of 10 windows:

| term | VirtualGL | **native** | native share |
|---|---|---|---|
| frame | 50.6 | **33.3** | |
| `doneCurrent` | 5.15 | **0.28** | |
| `_BGFXLib.makeCurrent` | 7.16 | **0.15** | |
| whole post region | 12.95 | **1.15** | 3% |
| bgfx::frame | 16.4 | **10.96** | 33% |
| our C++ (pre+cull+submitloop+post) | 19.3 | **6.79** | 20% |
| **outside (Coin + Qt + app)** | 14.8 | **15.56** | **47%** |
| gpu | 22.1 | 19.3 | |

**The 12.3 ms context switch was 95% VirtualGL.** Natively it is 0.66 ms.
The faker also inflated `bgfx::frame` by ~5.4 ms. `outside` is the one
term it did not touch (14.8 vs 15.6) -- which is the tell: the costs that
moved are the ones that cross the GL boundary.

**This document's previous section, which downgraded the artifact worry,
was wrong.** The reasoning it used -- "a uniform VirtualGL tax would
also hit `widget->makeCurrent()`, which is 90x cheaper" -- was sound
about *uniform* but wrong about the conclusion: the faker taxes
`context->makeCurrent(offscreen)` specifically, which is precisely the
residue that section listed as untested. **The flagged unknown was the
whole answer.** An argument that narrows a doubt is not an argument that
removes it.

### What the native numbers do to everything above

- X **"Coin is not the problem" is REVERSED.** `outside` is **47% of the
  native frame** -- the largest single term. It only looked small because
  VirtualGL had inflated everything around it.
- X **The "15.6 ms of fixed cost" chase was chasing the harness.** The
  post region is 1.15 ms natively. The cull (0.41 ms) and the submit
  loop (3.65 ms) survive as measured; the thing they were being compared
  against does not.
- * **The honest native split is: Coin+Qt 47%, bgfx backend 33%, our own
  C++ 20%.** Draw submission proper (`bgfx::frame`, 11 ms) is the second
  target, and the first one is not in this document at all.
- At 30578 draws `pre` rises to 8.73 ms (the instance-group work is per
  visible member) and `bgfx::frame` to 20.09 ms, so both still scale as
  the fits said.

**!! Every bullet in this subsection is wrong. `outside` is a wait, not
work -- see the next section. The reversal was itself reversed.**

!! **The rule this cost the most to learn, twice.** Every number on this
page before today came from `vglrun` on Xvfb. It is the right recipe when
the monitor is off ([[gpu-tests-monitor-off]] -- and it *is* the real
GPU), but **it is not a frame-cost harness**: it moved the frame 50.6 ->
33.3 ms and one span by 19x. Frame timings need the native session with
the monitor on; ask for the monitor rather than measuring around it.

! Native runs report `wait submit` / `wait render` as garbage
(3.6e13 ms). Under VirtualGL they read 0.00. Do not quote those two
fields from a native run until that is understood.

## IT WAS THE DISPLAY: `outside` is idle, and it was inflating everything

`outside` was 47% of the native frame and the largest single term, so it
got the instrument it had never had: six spans in
`View3DInventorViewer::renderScene()`, later four more in
`QuarterWidget::paintEvent()`, draining onto the frame line beside an
**unattributed remainder** (`Render::FrameOutside`).

The Gui code in it costs **0.76 ms**, and the remainder was 12.3 ms:

| span | ms/frame |
|---|---|
| Coin's whole `actualRedraw()` traversal | **0.18** |
| background root | 0.25 |
| paint event before the redraw | 0.16 |
| overlay captures | 0.10 |
| `QGraphicsView::paintEvent` (Qt's own painting) | 0.03 |
| chrome (axis cross, navicube, items, fps) | 0.03 |
| foreground root, delay queue, paint tail | 0.00 |
| **unattributed** | **12.3** |

So the sharp question this section was opened to ask -- *is Coin
traversing the whole scene graph for nothing at render-cache mode 3?* --
is answered **no, decisively**. Coin's composite is 0.5% of the frame.

### What the remainder is: three arms

The remainder is not in the paint event at all, and it is not work.

1. **The harness's own sleep -- REFUTED.** `spin()` slept 5 ms per
   redraw, between two `bgfx::frame` calls, i.e. inside `outside` by
   construction. `FC_SPIN_SLEEP=0` changed **nothing** (frame 33.3,
   outside 13.5). That null result is itself the clue: a wait that
   *absorbs* a 5 ms sleep without growing is elastic, so it is waiting
   on a deadline rather than doing work.
2. **`__GL_SYNC_TO_VBLANK=0` -- no change, and the control mattered.**
   Nothing moved. But the knob demonstrably works on this box:
   `glxgears` goes **59.8 -> 12984 fps** under it. Without that positive
   control the null would have read as "not vsync", which is the wrong
   conclusion -- the variable simply does not reach the swap Qt makes
   for the composited top-level window.
3. **Move the vblank grid -- CONFIRMED.** `xrandr --rate 119.88` on the
   same model, camera, culling row and binary:

| | 60 Hz | 119.88 Hz | swap interval 0 |
|---|---|---|---|
| frame | 33.34 | **25.10** | **20.40** |
| gpu | 19.39 | 19.52 | 19.82 |
| draws | 12850 | 12849 | 12849 |
| `bgfx::frame` | 10.30 | 10.34 | 9.92 |
| outside | 12.9 | 7.0 | **4.5** |

  33.33 ms is exactly two refresh intervals at 60 Hz; 25.02 ms is
  exactly three at 119.88 Hz. The GPU, the draws and the backend do not
  move. **The frame was waiting for the display.**

`FC_SWAP_INTERVAL` (read in `preAppSetup()` into the default
`QSurfaceFormat`, before `QApplication` exists) sets it where Qt reads
it. It is deliberately an environment variable and not a preference: it
is a measurement knob, and no user session should inherit a busy loop.

### The baseline, with the wait removed

Default culling row, 12849 draws, native, `FC_SWAP_INTERVAL=0`,
`FC_SPIN_SLEEP=0`, audit off, converged medians:

| term | ms | share of frame |
|---|---|---|
| **frame** | **20.40** | |
| **gpu** | **19.82** | **97%** |
| CPU total | ~16.0 | 78% |
| -- `bgfx::frame` (backend submit) | 9.92 | 49% |
| -- our own C++ | 6.02 | 30% |
| ---- pre (instance grouping) | 1.47 | |
| ---- submit loop | 3.17 | |
| ---- post region | 1.00 | |
| ---- cull | 0.34 | |
| -- all of Coin + Qt + overlays | 0.76 | 4% |
| CPU idle, waiting on the GPU | ~3.8 | 19% |

**This scene is GPU-bound.** `frame` (20.40) sits 3% above `gpu`
(19.82), and the CPU finishes 4 ms early every frame. The per-phase
chain is also complete for the first time: its `unattr` is **0.02 ms**.

**What this does to the plan.** Phases 1 and 3 spend CPU time the frame
does not have to give: even a *free* submission path would move 20.4 ms
to 19.8. The lever is the GPU half, and sec "primitives explain it better
than draws" says which one -- fewer triangles, not fewer draws.

### !! The wait was also inflating the CPU numbers, by 1.5-2x

Same binary, same row, vsync on vs swap interval 0:

| span | vsync on | swap 0 | ratio |
|---|---|---|---|
| cull | 0.68 | 0.34 | **2.0x** |
| submit loop | 5.9 | 3.17 | 1.9x |
| pre | 2.18 | 1.47 | 1.5x |
| `bgfx::frame` | 10.5 | 9.92 | 1.06x |
| ours (all of `render()`) | 20.6 | 15.9 | 1.3x |

The cull issues no GL calls, so driver back-pressure cannot explain it
doubling. The governor can: this box runs `ondemand` on `acpi-cpufreq`,
idling at **1400 MHz** against **2724 MHz** busy -- a 1.95x ratio
against the cull's measured 2.0x. **A process that sleeps a third of
every frame is measured at the slow clock.**

So `outside` did not merely fail to be work: while it existed it made
every CPU span beside it read ~1.5-2x too expensive. The submit loop's
"3.65 ms", the cull's "0.41 ms" and both fitted coefficients above were
all taken that way.

### The method, again

Four wrong answers, and now a fifth and sixth, all with the same shape:
**a term nobody had instrumented was assigned a cause by argument.** The
cull, the submit loop, the eight scans, the context switch, "Coin is the
problem", "the harness's sleep is the problem". What ended it each time
was measuring the residue -- and here, one control:
`__GL_SYNC_TO_VBLANK=0` produced *exactly* the same null result as the
sleep did, and only `glxgears` distinguished "the mechanism is absent"
from "the knob missed". **A null result from an unvalidated knob is not
evidence.**

### MEASURED: the GPU half is geometry-bound, and fill is ~nothing

The frame is 97% GPU and the GPU is one pass: `opaque` is **19.58 of
19.66 ms** (top of 15; nothing else reaches 0.05). No shadow, AO or post
pass is on this camera, so there is no pass-level lever -- what costs is
the geometry going through that one pass.

Which half of it -- vertices or pixels? Ablated with `FC_VIEW_SCALE=0.5`
(the 3D subwindow scaled from its maximized size, so `ViewFit` keeps the
framing), same model, camera, culling row and binary:

| | 1791x880 | 891x410 | change |
|---|---|---|---|
| pixels | 1.576 M | 0.365 M | **4.31x fewer** |
| **gpu** | 19.7 | **19.05** | **-3.3%** |
| draws | 12850 | 12850 | 0 |
| prims | 39.60 M | 36.05 M | -9.0% |
| **gpu per primitive** | 0.498 ns | **0.529 ns** | **+6%** |
| ours (CPU) | 15.9 | 15.6 | control, flat |

**Four times fewer pixels bought 3.3%, and per-primitive cost went
*up*.** Fill is a rounding error on this scene; GPU time tracks
primitives. That agrees with [[draw-call-is-the-cost-unit]], which found
16x pixels = 0% at a different operating point.

**=> The lever is fewer TRIANGLES.** Not fewer draws (phase 3 -- and the
CPU could not use them anyway), not fewer pixels (resolution scaling),
not cheaper submission (phases 1-2). Far-field proxies / LOD is the
workstream this points at, and its existing measurements are gated on
**draw** counts (phase 1: "42893 instances -> 2277 draws @64px"), which
is the unit just shown not to cost anything here. **Re-gate it on
primitives before building on it.**

! **Open, and not explained**: prims fell 9% at an identical draw count
when the viewport shrank. Something in the pipeline is already
screen-size dependent per draw. The instancing readout is byte-identical
across the arms, so it is not that. It cannot rescue fill -- per-primitive
cost rose -- but it should be found, because whatever it is, it is
already trading triangles for screen size and that is the mechanism this
section is asking for.

! The subwindow scale preserves the frame's aspect, not the viewport's
(2.035 -> 2.173), because the window chrome does not scale with it. That
is the likeliest source of the 9%, and it is a reason to size the
viewport directly if this ablation is repeated.

### The GPU half: primitives explain it better than draws

Across the same two points, GPU cost per *draw* varies 27% (1.72 vs
1.25 us) while GPU cost per *primitive* varies 13% (0.545 vs 0.472 ns).
Not conclusive from two points, but it leans the way the 5165
primitives-per-draw ratio already suggested: **at this operating point
the GPU is closer to geometry-bound than draw-bound**, so merging draws
(phase 3) would not buy the GPU half, and fewer triangles would.

### An aside the culling workstream should have

The software-oracle row culls twice as much (6485 vs 12850 draws) and
produces a **slower** frame: 54.78 ms against 50.61 ms, a gap far outside
either spread. Its extra CPU cull cost exceeds everything the removed
draws save. ** Culling harder is currently a net loss on this model.**

**TRAP: the per-draw GPU claim needs re-testing at this operating point.**
The draw-call note measured GPU cost as per-draw *state*, not fill or
triangles. The converged frame pushes **33.5 M primitives across 6485
draws -- 5165 per draw**, about 1.7 Gtri/s over 19.6 ms, which is near
this box's raster throughput. If the GPU half is geometry-bound here,
then merging draws (phase 3) does nothing for it and fewer *triangles*
(far-field proxies / LOD) is the lever. That is a measurement, not a
conclusion -- but it must be taken before phase 3 is justified on GPU
grounds.

WARNING: nothing here needed building to find out. **Read `render frame:`
before designing anything, and state which convergence state and which
audit setting a number came from** -- Phase 0 exists because this
document was first written without it, and this correction exists
because the numbers that replaced the estimate carried neither label.

## ⭐⭐ The constraint that shapes everything: one route, both tiers

**The same route must run on desktop and in the browser.** This is a
direction decision, not a technical one — running FreeCAD from one
codebase across browser, mobile and desktop is the project's first goal,
and a submission path that exists only on desktop forks the thing the
project is for.

What that rules out, and why each is genuinely unavailable rather than
merely inconvenient:

All of the following was **verified against the vendored tree**
(`src/3rdParty/bgfx`, bgfx `e3c8f29c` 2026-07-23) rather than assumed:

- ⛔ **Compute-shader culling on WebGL2.** WebGL2 is GLES 3.0;
  `BGFX_CAPS_COMPUTE` needs GLES >= 3.1 (`renderer_gl.cpp`), and the wasm
  build pins `BGFX_OPENGLES_VERSION 30`. Permanent, not a version away.
- ⛔ **Multi-draw indirect on WebGL2.** `BGFX_CAPS_DRAW_INDIRECT` requires
  `ARB/EXT_multi_draw_indirect`; none exist in WebGL2. bgfx does have a
  real `glMultiDrawElementsIndirect` path — gated off on web.
- ⛔ **`WEBGL_multi_draw`** exists in browsers but **bgfx does not expose
  it anywhere** (no such extension string in the tree). Adding it would
  be a web-only path, which is the fork this constraint forbids.
- ⛔⛔ **`bgfx::Encoder` multi-threading on web.** `BGFX_CONFIG_MULTITHREADED`
  is hard-coded to 0 for Emscripten, `maxEncoders` is clamped to 1, and
  `bx`'s thread/mutex/semaphore bodies are compiled out entirely
  (`BX_CONFIG_SUPPORTS_THREADING` 0). It cannot be forced on. Our own
  `src/3rdParty/bgfx/CMakeLists.txt` already disables it for Emscripten.
- ⛔ **`gl_InstanceID` / `bgfx::setInstanceCount`.** `BGFX_CAPS_VERTEX_ID`
  is *explicitly excluded* on Emscripten, so the attribute-free
  auto-instancing route is closed; a per-object index must ride a real
  `i_dataN` attribute.
- ⛔ **Uniform buffers.** bgfx's GL backend has no UBO path at all —
  uniforms are `glUniform4fv` per uniform per draw. And bgfx's own source
  warns that uniform *arrays* are much slower under Wasm because they
  marshal across to JS. ⇒ **a per-object data texture read with
  `texelFetch`, not a uniform array.**

### ⚠️ Correction: bgfx WebGPU *is* usable from Emscripten now

This document first asserted that bgfx's WebGPU backend is "Dawn-native
and unusable through Emscripten". **That has been false since
2026-06-28** — Emscripten support is merged upstream (bgfx PR #3795),
`renderer_webgpu.cpp` carries a real emdawnwebgpu path, and our own
`bgfx.cmake` already links `--use-port=emdawnwebgpu`. The backend is one
of bgfx's most actively developed. The claim was stale, and it is
repeated in several other docs (`RendererPlan.md`, `RoadMap.md`,
`ViewerUIResearch.md`) which should be corrected as they are touched.

**It changes less than it sounds like, and the reason is specific:**

- ⛔ **Multi-draw indirect is still not available on the web.** It is not
  core WebGPU — only a Chrome flag behind `enable-unsafe-webgpu` — and
  bgfx *emulates* it there by looping N `drawIndirect` calls. That is N
  encoder calls and N draws: **no reduction in the cost unit at all.**
- ⇒ WebGPU would not have helped the thing this document is about.
- ⭐ What it *does* unlock, uniquely, is **compute shaders**, which WebGL2
  can never have — meaning GPU-side culling that compacts an instance
  buffer into one instanced draw, needing no MDI.
- ⚠️ But adopting it means **two** browser paths and two shader packs
  (WGSL + ESSL): iOS < 26, Android < 12 and Firefox-on-Linux still need
  WebGL2. That cuts directly against the one-route constraint, so it is a
  *separate, time-boxed spike*, not part of this plan.

⇒ Everything below is built out of what **both** tiers have today:
instanced arrays (`BGFX_CAPS_INSTANCING` is unconditionally true on
GLES3), `texelFetch`, and ordinary indexed draws.

## The param

`Render_SubmitRoute` (`RenderParams.py` → `RenderDebug`-style view
property override, same pattern as `Render_Occlusion*`):

| value | route | runs on |
|---|---|---|
| 0 | **Classic** — today's per-draw submit | desktop + web |
| 1 | **Encoded** — the same submits, spread over bgfx encoders | desktop + web (1 encoder) |
| 2 | **Batched** — merged geometry, per-object data indexed | desktop + web |

⭐ The point of the param is not a desktop/web switch — every value must
work on both. It is so the routes can be *measured against each other on
one camera in one run*, the way `cull_audit.py` measures occlusion arms,
and so a regression has a one-property bisect.

## Phase 0 -- where the frame's time goes (mostly answered now)

⛔⛔ **The rule this workstream has already paid for twice**: §12.16 built
a mechanism on an unchecked premise, and §12.19's first run divided by
the wrong denominator. The ~24 ms figure is a *per-draw microbenchmark
rate multiplied by a draw count*. Before any of it is optimized, measure
the real frame:

**Corrected** (`render frame:`, converged + audit off): **6485 draws,
1.66 us submit and 3.03 us GPU per draw, 10.8 ms backend submit against
a 54.8 ms frame.** The heading's "32 ms" was the harness measuring
itself; see the RESOLVED section.

Still open, and each of these redirects the phases below:

1. **Which passes the draws belong to. ANSWERED.**: with the audit
   off, `opaque` is essentially the whole frame -- 9.5 ms of the 10.4 ms
   backend CPU and 18.2 ms of the 18.4 ms GPU. Everything else
   (background, sectioncap, the numbered overlay passes) is under 0.5 ms
   apiece. There is **no secondary pass worth deleting**: the hoped-for
   "a pass contributing thousands of draws for a small visual effect"
   does not exist in this scene. Shadow, AO and OIT are not live on this
   camera.

   ` render passes (top 8 of 14, totals 10.19/18.38): opaque 9.31/18.17
   pass80 0.47/0.03 background 0.17/0.07 pass81 0.15/0.09 ... `
2. **ANSWERED -- the ~44 ms is measured, and it is mostly ours.** Built
   as `cpu ours / bgfx::frame / outside` on the frame line and run: at
   the default config our own C++ is 19.3 ms, the bgfx backend 16.4 ms,
   and everything that is not this renderer (Coin's composite, Qt, the
   app) only 15.0 ms. Coin was not the problem.

   **The successor question, and the new priority: what is the 15.6 ms
   of FIXED cost inside our C++?** It does not scale with draw count --
   only 0.294 us/draw does. Candidates in order of suspicion: the
   per-frame scene walk, the cull itself, `buildInstanceGroups()`, and
   whatever else runs once per frame over all 17727 rows rather than
   over the survivors. **Nothing in this plan addresses it, and it is
   now the largest single CPU item in the renderer.**
3. The **fill / line / point** mix -- sec 12.19 found ~2787 point and ~2800
   line draws among 8388 rows, and they do not cost the same.

**This still points away from phase 3, and now more sharply**: the
backend submit it would reduce is 10.8 ms of 54.8, and at 5165
primitives per draw the GPU half may be geometry-bound rather than
draw-bound. Measure (2) and the geometry-bound question before choosing.

### Phase 0.5 — how much does the instancing we already have collapse?

⚠️ An earlier draft of this plan listed a bug here: "instancing never
engages at render cache 3 when Render Type is Default". **That was fixed
on 2026-08-07** (`a06f4c2938`, an ancestor of this branch) — the gate in
`ViewProviderExt.cpp::shapeInstancingActive()` asks the live backend
(`Render::Renderer::activeCount()` / `instancingHint()`) instead of the
preference string, with an observer rebuilding Part visuals when a
backend attaches. There is no bug to fix here.

The open question is the *quantitative* one, and nothing currently
answers it: **there is no instancing readout anywhere in the renderer.**
The benchmark log reports `scene consumed: 5954 draws, 1242 meshes` at
load and `indexed 17727 of 17727 draws` at cull time, and nothing in
between says how many submits the two instancing layers actually
collapse:

- **Part-side (Coin/TShape) instancing** — `shapeInstancingActive()`,
  identical tessellations shared across placements;
- **renderer-side grouping** — `buildInstanceGroups()`, identical
  geometry content *and* identical material folded into one instanced
  submit.

✅ **BUILT AND MEASURED** — `render instancing:`, on the frame-timing
cadence:

```
render instancing: 960 groups (495 usable, 465 singleton) over 17727 rows
  | 495 submits replaced 5432 draws | refused 0 groups / 0 draws
  | thinned to one: 0 draws
```

⭐⭐ **Instancing is working, and it is already the biggest lever in the
renderer**: 495 instanced submits stand in for 5432 scene rows. Nothing
is refused by the backend, and no group is thinned to a single visible
member by culling.

What that leaves, and it is the honest scope of everything below:

- **465 singleton groups** — geometry that occurs once. No instancing
  layer can ever batch these; only *merging distinct meshes* (phase 3)
  can.
- **~12300 of the 17727 rows are in no usable group at all.** That, not
  the 5432, is phase 3's target.
- The residual is still large: 30577 draws a frame after all of it.

⭐ Worth stating plainly because it nearly cost a phase: the note that
recorded the old bug also recorded its fix, in a later section, and an
earlier draft of this plan proposed re-fixing it. Read the whole record
before planning work against it — and prefer a readout to a belief.

## Phase 1 — Encoded: spread the same submits across cores

XX **Demoted again, harder: the frame is GPU-bound.** At the default
row the CPU already finishes ~4 ms before the GPU does, so spreading
submission across cores buys nothing a measurement could see -- a *free*
submit path moves the frame 20.4 -> 19.8 ms. Revisit only if a scene is
found whose CPU exceeds its GPU.

⚠️⚠️ **Demoted, and the honest label is desktop-only.** The plan first
claimed the browser would "run the identical code with one encoder". It
would — `begin()` returns a valid encoder and `end()` is a no-op — but
the verification found this buys the browser *exactly nothing*:
`BGFX_CONFIG_MULTITHREADED` is 0 on Emscripten, `maxEncoders` is clamped
to 1, `bx`'s threading is compiled out, and bgfx's free functions
(`bgfx::submit` and friends) **already route to encoder 0**. So adopting
`Encoder` is a no-op on web by construction.

**Demoted again by the 2026-08-11 correction.** The claim here was "32 ms
of desktop CPU divided by 8 cores is the single largest number on this
page". It was not 32 ms, it is **10.8 ms**, and that 10.8 ms is the
**backend render-thread** time (`cpuTimeBegin/End`), which is not what
`Encoder` parallelizes -- Encoder splits the *API-side* building of the
draw-item array, which lives in the unmeasured ~44 ms. So this phase's
prize is unknown rather than large, and it cannot be sized until the
~44 ms is broken down. It remains desktop-only, and it still does nothing
for the GPU half on either tier.

- N encoders, one per worker, partitioning the draw list by contiguous
  range; reuse `Render::physicalCoreCount()`/`occluderWorkers()` so the
  renderer sizes its pools one way.
- ⚠️ **Ordering is the whole risk.** bgfx sorts by view and sort key, so
  encoder assignment must not change order within a view. Sorted
  transparency depends on per-draw depth keys — keep it on one encoder
  until proven safe.
- ⚠️ Do **not** try to force `BGFX_CONFIG_MULTITHREADED` on for
  Emscripten: it compiles references to `bx::Mutex`/`Thread` that link to
  nothing.

**Gate**: **pixel-identical** to Classic, and the submit timer falls by
roughly the worker count on desktop. ⚠️ Read timings from a min/med/max
spread, never a last sample (§12.14, corrected in §12.15).

## Phase 2 — do less per draw (the phase that helps the browser)

Phase 1 buys the browser nothing, because the browser has one thread.
This one helps both.

- **Cache the recipe.** Most of what the 466-line `submit()` computes per
  draw is a pure function of the draw and does not change between frames:
  program selection, texture routing, the bgfx state word, unpacked
  colors. Compute once into a `SubmitRecipe`, invalidate on the scene
  version that already exists (`cullSceneVersion`).
- **Collapse the uniform sets.** Six `setUniform` calls per draw become
  one vec4-array set.
- ⭐ This is the *incremental-by-default* shape the project already
  applies elsewhere: never rebuild per frame what changed once.

**Gate**: pixel-identical, and measured on **both** tiers — the browser
number is the one this phase exists for.

## Phase 3 — Batched: merge distinct meshes into one draw

X **Same GPU-bound caveat as phase 1 for its CPU half** -- and its GPU
half was already argued away by the primitives-not-draws measurement
above. On this scene it has no side left to win on. It stays written
down because a draw-bound scene would change that, not because this one
is waiting for it.

The structural fix, and the only one that *removes* per-draw cost instead
of dividing it. Phases 1-2 make each draw cheaper or more parallel;
17727 draws remain 17727 draws.

- **Megabuffer per state bucket.** Concatenate the vertices and indices
  of many distinct meshes into one large VB/IB, keyed by the state that
  cannot vary within a draw (program, textures, blend, depth, cull).
- **Per-vertex object id.** One extra integer attribute naming which
  object a vertex belongs to.
- **Per-object data indexed, not bound.** Model matrix, diffuse, flags in
  a **data texture read with `texelFetch`** — verified available in
  bgfx's ESSL path, and integer samplers (`USAMPLER2D`) with it. ⛔ **Not
  a uniform array**: bgfx's GL backend has no UBO support at all, and its
  own source warns uniform arrays marshal Wasm→JS and are much slower
  there. ⇒ This is §12.17's "materials must become bindless first", in
  the one form both tiers support.
- ⚠️ **The object index must ride a real instance attribute** (`i_dataN`,
  always `vec4 float` — there is no integer instance attribute, so pack
  the index as a float, exact to 2^24). `gl_InstanceID` is unavailable:
  `BGFX_CAPS_VERTEX_ID` is explicitly excluded on Emscripten.
- ⚠️ **Two hard budgets**: instance data is capped at 16 vec4 (256-byte
  stride), and instance attributes consume ordinary vertex attribute
  slots — WebGL2 guarantees only **16 total**, and a 4x4 matrix alone is
  4 of them. A design that wants matrix + color + index + mesh attributes
  has to be counted against 16 before it is written.
- **Culling has to keep working.** The cull mask removes *objects*, and a
  batch is one draw, so either compact the index buffer per frame from
  the visible set (a memcpy of ranges into a transient IB) or collapse
  hidden objects in the vertex shader (`w = 0`). ⚠️ The second wastes
  vertex work and is what makes a naive batcher slower than what it
  replaced; measure both, and remember the occlusion pass exists to
  *remove* work, not to move it to the GPU.
- ⛔ **Not every draw qualifies.** Clip planes, user shaders, water /
  glass / fire / cloud, on-top, autozoom, per-face outline — the same
  exclusions `instancableDraw()` already lists. Those keep the Classic
  route. **Measure the qualifying share before building**: if it is 60%
  of draws, the ceiling of this phase is 60%.

**Gate**: pixel-identical, the draw count actually falls, and the frame
is faster on both tiers.

## Phase 4 — the two free levers (try before any of the above)

Both are bgfx-native, cost no portability, and were found by reading the
vendored backend rather than by design:

- **`bgfx::setViewMode(Sequential)`.** The default sorts draws by
  program/state to minimize backend state changes. Our scene is already
  grouped by material, so the sort may be pure cost — or it may be
  earning its keep. One call, measurable in an afternoon, either way.
- **The `setTransform` cache-index form.** `setTransform(mtx)` returns a
  cache index that `setTransform(cache, num)` re-references; we call the
  copying form everywhere, paying a 64-byte matrix copy per draw.
  Instanced groups and repeated placements are the obvious users.

⭐ These belong first in the order of work: they are hours, they help
both tiers, and they change the baseline every later phase is measured
against.

## Traps carried in from the occlusion work

- ⚠️⚠️ **A pixel compare needs a converged scene.** The same file measures
  0 / 108 / 616 differing pixels on framing and timing alone. Converge,
  re-fit, then compare.
- ⚠️ **Never bench while a build runs**, and rebuild **all** targets after
  a header change — a stale sibling segfaulted a test binary once.
- ⚠️ Delete the harness output file before a re-run: an
  `until grep DONE` waiter fires instantly on the previous run's DONE.
- ⭐ Counts here should be deterministic like the culler's; only timings
  need the min/median/max treatment.

## RESOLVED: the suspect baseline was the harness measuring itself

**Settled 2026-08-11 from the existing run logs -- no new run, no new
instrument.** The question was why `ViewDebugScene` cost ~15 ms CPU and
~16 ms GPU in runs where "nothing should have turned it on":

```
render passes (cpu/gpu ms, top 8 of 16, totals 26.46/34.99):
  debugscene 15.41/16.52   opaque 9.95/18.26   pass79 0.57/0.03  ...
```

**The cull audit turned it on, and the harness turned the audit on.**
`scripts/cull_audit.py` hard-coded `RenderDebug_CullAudit = True` until
`64cb4090d9`; `idPassRender` is `(viewMode == 11 || cullAudit)`, so the
id pass ran in **every frame of every run ever taken**. The note that
recorded the question also recorded, one section later, the `FC_NO_AUDIT=1`
that fixes it -- the flag had simply never been exercised.

The claim in the superseded text that "the run had the cull audit off and
produced zero audit lines" was **false**: the log for that run carries 84
`render cull audit:` lines. Nothing about the attribution was wrong. The
instrument was correct and was reporting a pass that was genuinely running.

Proof, from the two runs side by side (converged tails): turning the
audit off removes **17728 draws**, which is the 17727-row scene draw list
to within one, and takes the frame from 80.4 ms to 54.8 ms. The table at
the top of this document is that comparison.

**What this cost, and the rule it earns:**

- Every number in the first draft of this plan was inflated ~3x on
  submit and ~2x on GPU.
- **A default-on instrument is worse than no instrument.** The audit
  defaulted on because it was the only consumer of the harness; the
  moment a second consumer (frame timing) appeared, the default became a
  silent multiplier on every number the second consumer produced.
- **`os.environ.get("FC_NO_AUDIT", "") in ("", "0")` reads as "off by
  default" and means the opposite** -- unset gives `""`, which is in the
  tuple, which enables the audit. The polarity of an env-var default is
  worth reading twice.
- The earlier fix in this family was real and is kept: per-view stats are
  resolved to a pass **in the frame the sample was taken**, never by raw
  bgfx view id, because `idMap` is rebuilt each frame from the live set.

⚠️ The instrument already had one bug of this family and it is fixed:
per-view stats were accumulated by **raw bgfx view id**, but `idMap` is
rebuilt every frame from which passes are live, so a window mixing
frames with different live sets attributed one pass's milliseconds to
another. It now resolves id → pass **in the frame the sample was taken**
and only for passes whose mark is set.

`scripts/cull_audit.py` gained **`FC_NO_AUDIT=1`** for exactly this
reason: the audit re-renders every scene draw into the id image, so a
frame timing taken with it on is a measurement of the measuring
apparatus. Cull numbers need it on; frame timings need it off; the two
cannot come from one row.

## Backends: what Vulkan would and would not bring

Researched against the vendored tree (`renderer_vk.cpp` /
`renderer_gl.cpp`). **Verdict: do not add the Vulkan backend now.
Uncomment nothing at the `typeMap` entry.**

### The blocker is worse than "unsupported"

`renderer_vk.cpp` reinterprets `platformData.context` as a **`VkDevice`**:

```c
if (NULL != g_platformData.context) { m_device = m_externalDevice = (VkDevice)g_platformData.context; }
```

We put a GLX context handle in that field. Under `RendererType::Vulkan`
that is a type-confused pointer — immediate UB, not a graceful failure.
And bgfx implements **no GL↔VK interop** (no `EXT_memory_object` /
`external_memory` in either backend), so the whole export/import/sync
path would be ours to write and maintain against upstream.

### bgfx forfeits the Vulkan wins that matter

- **One render thread, every backend.** No `std::thread`, no
  `vkCmdExecuteCommands`, no secondary command buffers in
  `renderer_vk.cpp`; even `sort()` runs inside the backend.
  `bgfx::Encoder` parallelizes only the building of the unsorted
  draw-item array — **Vulkan does not change what Encoder can do.**
- **No bindless.** `VK_EXT_descriptor_indexing` is not used anywhere.
  The one feature that would collapse thousands of material-varied draws
  into a few indirect batches is not implemented.
- ⇒ The two largest wins in NVIDIA's CAD comparison — multi-threaded
  recording and command-buffer *reuse* — are architecturally
  unavailable; bgfx rebuilds the command stream every frame.

### The caps delta is ~nothing on our hardware

GL 4.6 on this box already exposes indirect draw, indirect count and
compute. Vulkan adds variable-rate shading and external textures, and
nothing that reduces draw cost.

### The per-draw mechanism difference is real, and second-order

GL commits uniforms through a **hash-map lookup per 4-float register**
and rebinds attributes per draw; Vulkan memcpys into a scratch buffer
and reuses descriptor sets via **dynamic offsets**. Estimated **2-3x on
the submit half** — 32 ms → ~12-18 ms. ⚠️ That multiplier is *inference*
from mechanism plus NVIDIA's hand-written CAD sample (7.8 → 1.8 ms at
44k draws); no bgfx-specific VK-vs-GL draw-cost benchmark appears to
exist. Vulkan also **adds** ~15 MB/frame of uniform writes, because it
re-uploads whole constant blocks where GL skips unchanged uniforms.

### Interop, if it were ever wanted

Vulkan offscreen + `VK_KHR_external_memory_fd` → `GL_EXT_memory_object_fd`
works on **Linux+NVIDIA** (the extensions are present on this box) and
has a Windows path. ⛔ **macOS is dead**: Apple GL is frozen at 4.1 and
never shipped `EXT_external_objects`.

### ⇒ When Vulkan becomes cheap

When Coin no longer composites into the viewport — i.e. when the
renderer owns the whole view — the blocker evaporates and Vulkan is a
small incremental step. It should be a *consequence* of that project,
not its justification. Keep the `spirv` shader profile building
meanwhile so the option stays cheap to exercise.

## ⭐ The free lever nobody has pulled

`bgfx::renderFrame()` is called immediately before `bgfx::Init` **on the
same thread**, which puts bgfx in **single-threaded mode**: no API/render
thread overlap, so our ~30 ms of submission and the GUI thread are
serialized today. Moving the render thread off the GUI thread overlaps
them. Backend-independent, cheap, and it improves the GL path we already
ship. ⚠️ Verify what it does to the Qt/Coin co-existence — Coin issues GL
on the GUI thread, and that is exactly the constraint that made
single-threaded mode the safe default in the first place.
