# Thin client UI — pointer interaction and panel rules

What a mouse, pen or finger does in the WASM viewer, and how the DOM
panels (pill, property card, HUD) react. This is the behavioral contract;
the architecture and protocol live in `ThinClient.md`. Implementation:
gestures and picking in `src/Gui/Renderer/wasm/main.cpp`, panels in
`src/Gui/Renderer/web/src/`.

## Picking

- A pick raycasts the streamed scene **client-side** — no round trip. Hit
  priority mirrors the desktop's `SoFCUnifiedSelection`: Face < Edge <
  Vertex, a higher-priority element winning only when the hits are
  essentially coincident in depth.
- The pick radius is the streamed `ViewParams::PickRadius` in CSS pixels,
  scaled by device pixel ratio, floored at 15 CSS px for touch. A pen
  hover in the last 2 s keeps the fine mouse radius for the tap that
  follows.
- Mouse hover preselects (highlight). Pens get the same via pointer
  events (`pointerType === 'pen'`); fingers get preselection through the
  loupe (below).

## Selection (mouse)

A *sub-element selection* is one face/edge/vertex; a *whole-object
selection* highlights every pickable draw of the object and reports an
empty `sub` in the `fc:selection` event.

| Gesture | On | Result |
|---|---|---|
| Click | unselected element | Replace selection with that sub-element |
| Click | the already-selected sub-element | Promote: that object's **whole-object** selection |
| Click | any sub-element of a whole-selected object | Narrow back to that sub-element |
| Click | empty space | Clear the selection |
| Ctrl+click | element | Multi-select toggle: add the sub-element, or remove it if already selected. Adding a sub-element drops the same object's whole-object item (the two are mutually exclusive) |
| Ctrl+click | empty space | Keep the selection |
| Shift+click (with or without Ctrl) | any element | **Whole-object select**: drop the picked object's element items, put its whole-object item in their place, keep other objects' selections |
| Double-click | anywhere | Zoom to fit (the first click of the pair still picks) |

So a plain click cycles sub → whole → sub on the same spot. Shift is the
*explicit* promotion — needed under Ctrl, where re-clicking a selected
element means deselect, leaving no plain-click path to the whole object.

Selection is client-owned and instant; it is not synced per tap. A future
modeling operation submits the accumulated selection batched with the
operation (`ThinClient.md` on preview/commit).

## Camera

- Drag = orbit. Right-drag or Shift+drag = grab-pan (the scene follows
  the cursor). A **motionless** Shift+click is the whole-object select
  above; the 6 px slop decides which it was. Wheel = zoom.
- Touch: one finger orbits, two fingers pan (centroid) + pinch-zoom.
  Double-tap = zoom to fit.
- The NaviCube claims its pixels first: faces/corners orient, the arrow
  and roll buttons step, the corner dot flips sides.

## Touch selection: the loupe

A one-finger hold (~350 ms under 10 px slop) enters preselect instead of
orbit: the highlight tracks the finger (pick lifted 20 px above the
contact after the first movement, so the fingertip stops occluding the
target), lifting commits the highlighted element as the selection.
Cancelled by drag past slop before the threshold, a second finger, or
touchcancel. The committed tap is a plain click by the table above — so
tapping an already-selected sub-element promotes to the whole object on
touch too, where there is no Shift.

## Pill and property card

Selection UI is one thing that is small or large, not two panels:

- A pick shows the **pill** — one line, label + path (the path
  doc-qualifies only objects from outside the scene's home document; a
  multi-item selection is counted on the card head). Expanding is lazy:
  the card and its property fetch happen only on the expand button.
- The **card** replaces the pill in place (the pill's corner is handed
  to the card, and back on close). Its header is the pill's line plus
  `×`.
- Subjects: **Object** (the picked object's properties) and **View &
  document** (the 3D view's and the document's settings, one subject —
  a client sees one view and one document). The bottom-left menu opens
  the card on View & document; the segmented switcher in the card head
  moves between subjects. Switching subjects resets group/keyword
  navigation (one subject's groups mean nothing to another).
- **An open card follows the picks.** A new object's pick returns an
  open card to the Object subject — even from View & document, because
  a pick is a statement of interest. Re-picking another element of the
  *same* object leaves the card exactly as it is. A collapsed pill
  stays collapsed; expanding is a request the user has not made yet.
- **Selecting nothing** (click on empty space): an open Object card
  falls back to View & document rather than showing a stale object or
  closing — closing would throw away a panel the user opened.
- Editors commit on change/blur, dim until the backend acknowledges,
  and refetch on success *and* failure (a revert needs fresh
  descriptors).

## Panels in general

- The pill, the card and the HUD card are draggable (pointer capture),
  clamped into the viewport, re-clamped on resize, and each remembers
  its position per panel in `localStorage`.
- On narrow screens the card becomes a pinned bottom sheet; the menu
  button hides only while the sheet covers its corner.
- The HUD is a card too (header, `×`, drag, remembered position),
  toggled from the menu; a bare `fcviewer.html` without the UI bundle
  keeps the plain `#__hud` overlay.
- Keyboard shortcuts defer while any form control has focus.
