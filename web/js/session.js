// One bridge, two rounds. Watch and Play each keep a record of their round:
// the settings it was dealt with and every call that changed it. The bridge
// holds one round at a time; when a mode needs it back, its round is replayed
// from the record. A replay makes no search: a solver move in Watch is
// recorded by its move id and replayed with act(), the Dealer's turns and the
// loads with advance(). This relies on the engine giving the same round for
// the same seed and the same calls, which rank() does not change.

import * as bridge from './bridge.js';
import { call } from './state.js';

const records = { watch: null, play: null };
let holder = null;

export function hasRound(mode) {
  return Boolean(records[mode]);
}

/** Replay a mode's round if the bridge holds another. Returns the replayed view or null. */
async function ensure(mode, onRestore) {
  if (holder === mode) return null;
  const rec = records[mode];
  if (!rec) throw new Error('There is no round to restore.');
  holder = null;
  onRestore?.(0, rec.steps.length);
  let view = await bridge.newRound(rec.settings);
  for (let i = 0; i < rec.steps.length; i += 1) {
    const s = rec.steps[i];
    const res = s.type === 'act' ? await bridge.act(s.id, s.slot) : await bridge.advance();
    view = res.view;
    if (i % 4 === 3) onRestore?.(i + 1, rec.steps.length);
  }
  holder = mode;
  onRestore?.(rec.steps.length, rec.steps.length);
  return view;
}

/** Deal a new round for a mode. */
export function newRound(mode, settings) {
  return call(async () => {
    holder = null;
    const view = await bridge.newRound(settings);
    records[mode] = { settings: { ...settings }, steps: [] };
    holder = mode;
    return view;
  });
}

/**
 * Play seat 1's move. The slot names which copy of the item to use; null lets
 * the engine use the first copy of the move's run. Resolves to {events, view, restored}.
 */
export function act(mode, id, slot, hooks = {}) {
  return call(async () => {
    const restored = await ensure(mode, hooks.onRestore);
    const res = await bridge.act(id, slot ?? null);
    records[mode].steps.push({ type: 'act', id, slot: slot ?? null });
    return { ...res, restored };
  });
}

/** Let the engine take the next step: a load, a Dealer pass, or the solver's move. */
export function advance(mode, hooks = {}) {
  return call(async () => {
    const restored = await ensure(mode, hooks.onRestore);
    const res = await bridge.advance();
    records[mode].steps.push({ type: 'advance' });
    return { ...res, restored };
  });
}

/** Rank seat 1's moves in a mode's round. Resolves to {ranking, restored}. */
export function rank(mode, hooks = {}) {
  return call(async () => {
    const restored = await ensure(mode, hooks.onRestore);
    const ranking = await bridge.rank();
    return { ranking, restored };
  });
}

/** Odds for a written position. Stateless: the round the bridge holds stays. */
export function advise(notation, options) {
  return call(() => bridge.advise(notation, options));
}

/** Compare a replayed view with the one on screen. */
export function sameView(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}
