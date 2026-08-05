// The touch loupe's mark: a ring where the pick actually is, tied back to
// the fingertip it belongs to by a short leader.
//
// The loupe lifts its pick ~20px above the contact point so the fingertip
// stops covering the target (main.cpp kLoupeLift). The highlight alone
// cannot explain that offset: it says what was hit, and says nothing at
// all when the lifted point is over empty space or between two edges —
// exactly when you need to know which way to move. The ring is drawn
// whether or not anything was hit; the leader is what makes it read as
// the user's own pointer rather than a floating dot.
import { Show } from 'solid-js';

export interface LoupeMark {
  x: number;
  y: number;
  fromX: number;
  fromY: number;
}

export function LoupeOverlay(props: { mark: () => LoupeMark | null }) {
  return (
    <Show when={props.mark()}>
      {(m) => (
        // Every stroke is drawn twice — a dark wide pass under a light
        // narrow one — because this floats over a model of any colour and
        // a single-colour mark disappears into half of them.
        <svg class="fc-loupe" aria-hidden="true">
          <line class="fc-loupe-leader-halo"
                x1={m().fromX} y1={m().fromY} x2={m().x} y2={m().y} />
          <line class="fc-loupe-leader"
                x1={m().fromX} y1={m().fromY} x2={m().x} y2={m().y} />
          <circle class="fc-loupe-ring-halo" cx={m().x} cy={m().y} r="11" />
          <circle class="fc-loupe-ring" cx={m().x} cy={m().y} r="11" />
          <circle class="fc-loupe-dot" cx={m().x} cy={m().y} r="1.5" />
        </svg>
      )}
    </Show>
  );
}
