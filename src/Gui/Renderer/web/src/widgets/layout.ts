// A layout spec, planned (docs/Sandbox.md 7.22, G7, W1): the host's
// `Layout::spec()` turned into what a DOM view places without deciding
// anything for itself.
//
// Pure, like protocol.ts, and for the same reason: the gate asserts the plan
// over the recorded corpus in node, with no DOM in the way. The view maps a
// plan onto flex and CSS grid; nothing here emits CSS, so the rules below
// stay checkable.
//
// What the recorded corpus actually contains (six panels: Pad, OrthoArray, a
// CAM op, Sketcher, an SVG picture, a QMessageBox):
//
//   classes     QVBoxLayout 53, QHBoxLayout 24, QGridLayout 25, QFormLayout 2
//   item kinds  widget 250, nested layout 24, spacer 9
//   placement   pos 153 (145 four-wide, 8 two-wide), align 2
//   extras      none
//
// Two consequences worth stating, because both are easy to get wrong:
//
//   - `pos` comes TWO wide as well as four. `Layout::addWidget` writes
//     [row, column, rowSpan, columnSpan], but `Layout::addRow` -- a form's
//     row -- writes [row, column] alone. A span read straight out of pos[2]
//     is `undefined` on those, so the spans default to 1 here rather than in
//     the view.
//   - separator, stretch and spacing ITEMS never appear in a panel: they are
//     tool-bar shapes, and the tool bars are a different subscription. They
//     are planned anyway, because the wire can carry them, but nothing in
//     the corpus gates them -- said plainly rather than counted as covered.

import type { LayoutItem, LayoutSpec } from './protocol.ts';
import { refId } from './protocol.ts';

/// How a view lays the items out. `form` is a grid of label/field rows: the
/// host builds it with `addRow`, and the two-wide `pos` is what says so.
export type PlanKind = 'stack' | 'grid' | 'form';

export type ItemKind =
  | 'widget'
  | 'layout'
  | 'action'
  | 'separator'
  | 'stretch'
  | 'spacing'
  | 'spacer';

export interface SpacerPlan {
  width: number;
  height: number;
  hPolicy: number;
  vPolicy: number;
}

export interface PlannedItem {
  kind: ItemKind;
  /// The model a `widget` or an `action` item names.
  id?: string;
  /// A nested layout, planned in turn.
  layout?: LayoutPlan;
  /// Grid placement; absent when the parent is a stack.
  row?: number;
  column?: number;
  rowSpan?: number;
  columnSpan?: number;
  /// A stretch or spacing item's number, and a widget's stretch factor.
  value?: number;
  align?: number;
  spacer?: SpacerPlan;
}

export interface LayoutPlan {
  kind: PlanKind;
  /// For a stack: which way it runs.
  direction?: 'row' | 'column';
  /// The class as the host named it, kept for a view that wants to be exact
  /// and for the gate to report an unknown one.
  className: string;
  name: string;
  items: PlannedItem[];
  /// [left, top, right, bottom], or null when the host sent none.
  margins: number[] | null;
  spacing: number | null;
  /// Grid extent, so a view can size the track lists without a second pass.
  columns?: number;
  rows?: number;
}

const STACKS: Record<string, 'row' | 'column'> = {
  QVBoxLayout: 'column',
  QHBoxLayout: 'row',
  QBoxLayout: 'column',
};

/// Which of the seven shapes an item is. The host writes exactly one of the
/// keys, and in this precedence (`Layout::spec`), so the first hit wins; an
/// item with none of them is a spacer, which is how the host spells it.
export function itemKind(item: LayoutItem): ItemKind {
  if (item.widget !== undefined) return 'widget';
  if (item.layout !== undefined) return 'layout';
  if (item.action !== undefined) return 'action';
  if (item.separator) return 'separator';
  if (item.stretch !== undefined && item.pos === undefined && item.widget === undefined) {
    // a bare stretch item; a stretch that rides a widget is a factor, below
    return 'stretch';
  }
  if (item.spacing !== undefined) return 'spacing';
  return 'spacer';
}

function planItem(item: LayoutItem, grid: boolean): PlannedItem {
  const kind = itemKind(item);
  const out: PlannedItem = { kind };

  if (kind === 'widget') out.id = refId(item.widget) ?? undefined;
  else if (kind === 'action') out.id = refId(item.action) ?? undefined;
  else if (kind === 'layout') out.layout = planLayout(item.layout);
  else if (kind === 'stretch') out.value = item.stretch;
  else if (kind === 'spacing') out.value = item.spacing;
  else if (kind === 'spacer') {
    const [width = 0, height = 0, hPolicy = 0, vPolicy = 0] = item.spacer ?? [];
    out.spacer = { width, height, hPolicy, vPolicy };
  }

  // Placement. The spans default to 1: a form's row sends [row, column]
  // only, and reading pos[2] there would be undefined.
  if (grid && item.pos && item.pos.length >= 2) {
    out.row = item.pos[0];
    out.column = item.pos[1];
    out.rowSpan = item.pos.length > 2 ? item.pos[2] : 1;
    out.columnSpan = item.pos.length > 3 ? item.pos[3] : 1;
  }
  // A stretch factor on a widget or a nested layout is placement, not a
  // shape -- the host sends both on the same item.
  if (kind !== 'stretch' && item.stretch !== undefined) out.value = item.stretch;
  if (item.align !== undefined) out.align = item.align;
  return out;
}

/// Plan one layout. An unknown class falls back to a vertical stack rather
/// than throwing: a panel with one unfamiliar container should still render,
/// and the gate reports the class so it can be added deliberately.
export function planLayout(spec: LayoutSpec | undefined): LayoutPlan {
  const className = spec?.class ?? '';
  const direction = STACKS[className];
  const kind: PlanKind =
    className === 'QGridLayout' ? 'grid' : className === 'QFormLayout' ? 'form' : 'stack';
  const grid = kind !== 'stack';

  const items = (spec?.items ?? []).map((item) => planItem(item, grid));

  const plan: LayoutPlan = {
    kind,
    className,
    name: spec?.name ?? '',
    items,
    margins: spec?.margins ?? null,
    spacing: spec?.spacing ?? null,
  };
  if (kind === 'stack') plan.direction = direction ?? 'column';
  if (grid) {
    let rows = 0;
    let columns = 0;
    for (const item of items) {
      if (item.row === undefined || item.column === undefined) continue;
      rows = Math.max(rows, item.row + (item.rowSpan ?? 1));
      columns = Math.max(columns, item.column + (item.columnSpan ?? 1));
    }
    plan.rows = rows;
    plan.columns = columns;
  }
  return plan;
}

/// Whether a class is one this module plans deliberately. The gate asserts
/// it over the corpus, so a fifth container shows up as a failure rather
/// than as a silently vertical panel.
export function isKnownLayoutClass(className: string): boolean {
  return className in STACKS || className === 'QGridLayout' || className === 'QFormLayout';
}
