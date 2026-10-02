// What one reader can tell about the shells: the ones it has seen, the ones
// the counts settle, and the chance that the chamber fires live.
//
// The view's live and blank counts are public counts, by the type each shell
// was loaded as. A known shell is named by the type it fires as. An Inverter
// changes what the chamber fires as but not the counts, so the page tracks the
// flip itself (see inversion() below) and undoes it when it counts.

import { flip } from './vocab.js';

/**
 * tube      the view's tube
 * canSee    (shell) => whether this reader has seen the shell
 * inverted  whether the chamber was flipped since it was loaded
 * Returns { shells: [{offset, kind, how: 'seen'|'deduced'|null, by}], chance, remLive, remBlank }
 */
export function readTube(tube, canSee, inverted = false) {
  let seenLive = 0;
  let seenBlank = 0;
  const shells = tube.shells.map((sh) => {
    if (sh.known && canSee(sh)) {
      const drawn = sh.offset === 0 && inverted ? flip(sh.known) : sh.known;
      if (drawn === 'live') seenLive += 1;
      else seenBlank += 1;
      return { offset: sh.offset, kind: sh.known, how: 'seen' };
    }
    return { offset: sh.offset, kind: null, how: null };
  });
  const remLive = tube.live - seenLive;
  const remBlank = tube.blank - seenBlank;
  if (remLive >= 0 && remBlank >= 0) {
    for (const s of shells) {
      if (s.how) continue;
      let drawn = null;
      if (remLive === 0 && remBlank > 0) drawn = 'blank';
      else if (remBlank === 0 && remLive > 0) drawn = 'live';
      if (!drawn) continue;
      s.kind = s.offset === 0 && inverted ? flip(drawn) : drawn;
      s.how = 'deduced';
    }
  }
  let chance = null;
  if (shells.length) {
    const ch = shells[0];
    if (ch.kind) chance = ch.kind === 'live' ? 1 : 0;
    else if (remLive + remBlank > 0) {
      const p = remLive / (remLive + remBlank);
      chance = inverted ? 1 - p : p;
    }
  }
  return { shells, chance, remLive, remBlank };
}

/**
 * Follow the Inverter through a list of events. Returns the new flag: an
 * Inverter flips it, and a shot, a Beer or a new load clears it.
 */
export function inversion(flag, events) {
  let on = flag;
  for (const ev of events) {
    if (ev.kind === 'load' || ev.kind === 'shot') on = false;
    else if (ev.kind === 'item' && ev.item === 'beer') on = false;
    else if (ev.kind === 'item' && ev.item === 'inv') on = !on;
  }
  return on;
}

/** The chamber chance as chip text, or null when there is no chamber. */
export function chanceText(chance) {
  if (chance === null) return null;
  if (chance >= 1) return '100%';
  if (chance <= 0) return '0%';
  return `${Math.round(chance * 100)}%`;
}
