// Checks for the values the bridge returns (README.md describes them). Each
// function returns a list of problems, empty when the value has the shape the
// page reads. The page's browser checks run them on every value the bridge
// returns.

import { ITEM_TOKENS } from './vocab.js';

const SEATS = ['p1', 'p2'];
const MODES = ['don', 'story1', 'story2', 'story3'];
const EVENT_KEYS = {
  load: ['kind', 'live', 'blank', 'dealt', 'first', 'freed'],
  shot: ['kind', 'by', 'target', 'shell', 'damage'],
  item: ['kind', 'by', 'item', 'target', 'text'],
  learned: ['kind', 'by', 'offset', 'shell', 'private'],
  rule: ['kind', 'by', 'text'],
  skip: ['kind', 'seat'],
  over: ['kind', 'winner'],
};

const isInt = (n) => Number.isInteger(n);
const isObj = (o) => o !== null && typeof o === 'object' && !Array.isArray(o);

function extraKeys(obj, allowed, where, out) {
  if (!isObj(obj)) return;
  for (const k of Object.keys(obj)) if (!allowed.includes(k)) out.push(`${where} has a field the page does not know: ${k}.`);
}

/** Plain voice: no em-dashes and no contractions in any text shown to people. */
export function checkVoice(text, where) {
  const out = [];
  if (typeof text !== 'string') return out;
  if (/[\u2014\u2013]/.test(text)) out.push(`${where} uses a dash: "${text}".`);
  if (/n[’']t\b|\b(I|you|we|they|it|that|there|he|she)[’'](re|ve|ll|d|m|s)\b/i.test(text)) out.push(`${where} uses a contraction: "${text}".`);
  return out;
}

export function checkView(v) {
  const out = [];
  if (!isObj(v)) return ['The view is not an object.'];
  extraKeys(v, ['mode', 'stageLabel', 'seed', 'seats', 'tube', 'toMove', 'over', 'winner', 'loadNumber', 'legal', 'notation'], 'view', out);
  if (!MODES.includes(v.mode)) out.push(`view.mode is ${v.mode}.`);
  if (typeof v.stageLabel !== 'string' || !v.stageLabel) out.push('view.stageLabel is empty.');
  if (!isInt(v.seed) || v.seed < 0) out.push('view.seed is not a whole number.');
  if (!Array.isArray(v.seats) || v.seats.length !== 2) out.push('view.seats does not hold two seats.');
  else {
    v.seats.forEach((s, i) => {
      const w = `view.seats[${i}]`;
      extraKeys(s, ['id', 'name', 'charges', 'max', 'faded', 'items', 'hand', 'restraint'], w, out);
      if (s.id !== SEATS[i]) out.push(`${w}.id is ${s.id}, expected ${SEATS[i]}.`);
      if (typeof s.name !== 'string' || !s.name) out.push(`${w}.name is empty.`);
      if (!isInt(s.max) || s.max < 1) out.push(`${w}.max is ${s.max}.`);
      if (!isInt(s.charges) || s.charges < 0 || s.charges > s.max) out.push(`${w}.charges is ${s.charges} of ${s.max}.`);
      if (!isInt(s.faded) || s.faded < 0 || s.faded >= s.max) out.push(`${w}.faded is ${s.faded}.`);
      if (!Array.isArray(s.items) || s.items.length !== 8) out.push(`${w}.items is not eight slots.`);
      else s.items.forEach((t, k) => t !== null && !ITEM_TOKENS.includes(t) && out.push(`${w}.items[${k}] is ${t}.`));
      // The hand is the same items in the order the seat got them.
      if (!Array.isArray(s.hand) || s.hand.some((t) => !ITEM_TOKENS.includes(t))) out.push(`${w}.hand is not a list of items.`);
      else if (Array.isArray(s.items) && [...s.hand].sort().join() !== s.items.filter((t) => t !== null).sort().join()) {
        out.push(`${w}.hand and ${w}.items hold different items.`);
      }
      if (![null, 'cuffed', 'lost a turn'].includes(s.restraint)) out.push(`${w}.restraint is ${s.restraint}.`);
    });
  }
  const t = v.tube;
  if (!isObj(t)) out.push('view.tube is missing.');
  else {
    extraKeys(t, ['live', 'blank', 'total', 'sawed', 'shells'], 'view.tube', out);
    if (!isInt(t.live) || !isInt(t.blank) || t.live < 0 || t.blank < 0) out.push('view.tube counts are not whole numbers.');
    if (t.live + t.blank !== t.total) out.push(`view.tube: ${t.live} live and ${t.blank} blank do not make ${t.total}.`);
    if (t.total > 8) out.push('view.tube holds more than eight shells.');
    if (typeof t.sawed !== 'boolean') out.push('view.tube.sawed is not a boolean.');
    if (!Array.isArray(t.shells) || t.shells.length !== t.total) out.push('view.tube.shells does not match the total.');
    else {
      t.shells.forEach((sh, i) => {
        const w = `view.tube.shells[${i}]`;
        extraKeys(sh, ['offset', 'known', 'knownBy'], w, out);
        if (sh.offset !== i) out.push(`${w}.offset is ${sh.offset}.`);
        if (![null, 'live', 'blank'].includes(sh.known)) out.push(`${w}.known is ${sh.known}.`);
        if (!Array.isArray(sh.knownBy) || sh.knownBy.some((s) => !SEATS.includes(s)) || new Set(sh.knownBy).size !== sh.knownBy.length) {
          out.push(`${w}.knownBy is not a list of seats.`);
        } else if ((sh.known === null) !== (sh.knownBy.length === 0)) out.push(`${w}: known and knownBy disagree.`);
      });
    }
  }
  if (![null, 'p1', 'p2'].includes(v.toMove)) out.push(`view.toMove is ${v.toMove}.`);
  if (typeof v.over !== 'boolean') out.push('view.over is not a boolean.');
  if (![null, 'p1', 'p2'].includes(v.winner)) out.push(`view.winner is ${v.winner}.`);
  if (v.over !== (v.winner !== null)) out.push('view.over and view.winner disagree.');
  if (v.over && v.toMove !== null) out.push('view.toMove is set after the round is over.');
  if (!isInt(v.loadNumber) || v.loadNumber < 0) out.push('view.loadNumber is not a whole number.');
  if (isObj(t) && t.total === 0 && v.toMove !== null) out.push('view.toMove is set while the tube is empty.');
  if (typeof v.notation !== 'string' || !/(^| )tube=/.test(v.notation)) out.push('view.notation is not a written position.');
  if (!Array.isArray(v.legal)) out.push('view.legal is not a list.');
  else {
    const ids = new Set();
    v.legal.forEach((m, i) => {
      const w = `view.legal[${i}]`;
      extraKeys(m, ['id', 'kind', 'label', 'item', 'target', 'slot', 'from'], w, out);
      if (typeof m.id !== 'string' || !m.id) out.push(`${w}.id is empty.`);
      if (ids.has(m.id)) out.push(`${w}.id ${m.id} repeats.`);
      ids.add(m.id);
      if (!['shoot', 'item'].includes(m.kind)) out.push(`${w}.kind is ${m.kind}.`);
      if (m.kind === 'shoot' && !SEATS.includes(m.target)) out.push(`${w} shoots no seat.`);
      if (m.kind === 'item' && !ITEM_TOKENS.includes(m.item)) out.push(`${w}.item is ${m.item}.`);
      if (m.target !== undefined && !SEATS.includes(m.target)) out.push(`${w}.target is ${m.target}.`);
      if (m.kind === 'shoot' && m.slot !== undefined) out.push(`${w} shoots from a slot.`);
      if (m.kind === 'item' && (!isInt(m.slot) || m.slot < 0 || m.slot > 7)) out.push(`${w}.slot is ${m.slot}.`);
      if (m.from !== undefined && (m.from !== 'p2' || m.kind !== 'item' || m.target !== 'p2')) out.push(`${w}.from is ${m.from}.`);
      // The slot holds the item the move spends: the Adrenaline for a move
      // made with it alone, and for a steal the item in the Dealer's tray.
      if (m.kind === 'item' && isInt(m.slot) && Array.isArray(v.seats) && v.seats.length === 2) {
        const tray = v.seats[m.from === 'p2' ? 1 : 0].items ?? [];
        if (tray[m.slot] !== m.item) out.push(`${w}.slot ${m.slot} does not hold ${m.item}.`);
      }
      if (typeof m.label !== 'string' || !m.label) out.push(`${w}.label is empty.`);
      out.push(...checkVoice(m.label, `${w}.label`));
    });
    const shouldList = !v.over && v.toMove === 'p1' && isObj(t) && t.total > 0;
    if (!shouldList && v.legal.length) out.push('view.legal lists moves while seat 1 is not to move.');
    if (shouldList && !v.legal.length) out.push('view.legal is empty while seat 1 is to move.');
  }
  return out;
}

export function checkEvent(ev) {
  const out = [];
  if (!isObj(ev)) return ['An event is not an object.'];
  const keys = EVENT_KEYS[ev.kind];
  if (!keys) return [`Unknown event kind ${ev.kind}.`];
  const w = `${ev.kind} event`;
  extraKeys(ev, keys, w, out);
  const seat = (v) => SEATS.includes(v);
  switch (ev.kind) {
    case 'load':
      if (!isInt(ev.live) || !isInt(ev.blank) || ev.live + ev.blank < 1) out.push(`${w} has bad counts.`);
      if (!isObj(ev.dealt) || !SEATS.every((s) => Array.isArray(ev.dealt[s]) && ev.dealt[s].every((x) => ITEM_TOKENS.includes(x)))) out.push(`${w}.dealt is not two item lists.`);
      if (!seat(ev.first)) out.push(`${w}.first is ${ev.first}.`);
      if (!Array.isArray(ev.freed) || !ev.freed.every(seat)) out.push(`${w}.freed is not a list of seats.`);
      break;
    case 'shot':
      if (!seat(ev.by) || !seat(ev.target)) out.push(`${w} has bad seats.`);
      if (!['live', 'blank'].includes(ev.shell)) out.push(`${w}.shell is ${ev.shell}.`);
      if (!isInt(ev.damage) || ev.damage < 0 || ev.damage > 2) out.push(`${w}.damage is ${ev.damage}.`);
      if (ev.shell === 'blank' && ev.damage !== 0) out.push(`${w}: a blank dealt damage.`);
      break;
    case 'item':
      if (!seat(ev.by)) out.push(`${w}.by is ${ev.by}.`);
      if (!ITEM_TOKENS.includes(ev.item)) out.push(`${w}.item is ${ev.item}.`);
      if (ev.target !== undefined && !seat(ev.target)) out.push(`${w}.target is ${ev.target}.`);
      if (typeof ev.text !== 'string' || !ev.text.trim()) out.push(`${w} for ${ev.item} has no text.`);
      if (ev.item === 'beer' && !/\b(live|blank)\b/i.test(ev.text ?? '')) out.push(`${w}: the Beer text does not name the shell.`);
      out.push(...checkVoice(ev.text, `${w}.text`));
      break;
    case 'learned':
      if (!seat(ev.by)) out.push(`${w}.by is ${ev.by}.`);
      if (!isInt(ev.offset) || ev.offset < 0 || ev.offset > 7) out.push(`${w}.offset is ${ev.offset}.`);
      if (!['live', 'blank'].includes(ev.shell)) out.push(`${w}.shell is ${ev.shell}.`);
      if (ev.private !== true) out.push(`${w}.private is not true.`);
      break;
    case 'rule':
      if (ev.by !== 'p2') out.push(`${w}.by is ${ev.by}.`);
      if (typeof ev.text !== 'string' || !ev.text.trim()) out.push(`${w} has no text.`);
      out.push(...checkVoice(ev.text, `${w}.text`));
      break;
    case 'skip':
      if (!seat(ev.seat)) out.push(`${w}.seat is ${ev.seat}.`);
      break;
    case 'over':
      if (!seat(ev.winner)) out.push(`${w}.winner is ${ev.winner}.`);
      break;
    default:
  }
  return out;
}

/** A result of act() or advance(): {events, view}. */
export function checkStep(res) {
  const out = [];
  if (!isObj(res)) return ['The result is not an object.'];
  extraKeys(res, ['events', 'view'], 'result', out);
  if (!Array.isArray(res.events)) out.push('result.events is not a list.');
  else res.events.forEach((ev) => out.push(...checkEvent(ev)));
  out.push(...checkView(res.view));
  return out;
}

export function checkRanking(r, view = null) {
  const out = [];
  if (!isObj(r)) return ['The ranking is not an object.'];
  extraKeys(r, ['mover', 'opponent', 'refused', 'moves', 'stopped', 'assumptions'], 'ranking', out);
  if (!SEATS.includes(r.mover)) out.push(`ranking.mover is ${r.mover}.`);
  if (!['solver', 'dealer'].includes(r.opponent)) out.push(`ranking.opponent is ${r.opponent}.`);
  if (r.refused !== null && (typeof r.refused !== 'string' || !r.refused)) out.push('ranking.refused is neither null nor a sentence.');
  out.push(...checkVoice(r.refused, 'ranking.refused'));
  if (!Array.isArray(r.moves)) out.push('ranking.moves is not a list.');
  else {
    if (r.refused && r.moves.length) out.push('A refused ranking still lists moves.');
    if (!r.refused && !r.moves.length) out.push('ranking.moves is empty without a reason in refused.');
    r.moves.forEach((m, i) => {
      const w = `ranking.moves[${i}]`;
      extraKeys(m, ['id', 'label', 'win', 'note'], w, out);
      if (typeof m.id !== 'string' || !m.id) out.push(`${w}.id is empty.`);
      if (typeof m.label !== 'string' || !m.label) out.push(`${w}.label is empty.`);
      out.push(...checkVoice(m.label, `${w}.label`));
      if (typeof m.win !== 'number' || !(m.win >= 0 && m.win <= 1)) out.push(`${w}.win is ${m.win}.`);
      // A move that only spends an item goes after the moves it ties with, and
      // rounding can leave it a hair above them.
      if (i > 0 && m.win > r.moves[i - 1].win + 1e-9) out.push(`${w} ranks above a better move.`);
      if (m.note !== null && (typeof m.note !== 'string' || !m.note)) out.push(`${w}.note is neither null nor a sentence.`);
      out.push(...checkVoice(m.note, `${w}.note`));
    });
    if (view && Array.isArray(view.legal) && view.legal.length) {
      const legal = new Set(view.legal.map((m) => m.id));
      for (const m of r.moves) if (!legal.has(m.id)) out.push(`ranking move ${m.id} is not in view.legal.`);
    }
  }
  if (r.stopped !== null && (typeof r.stopped !== 'string' || !r.stopped)) out.push('ranking.stopped is neither null nor a sentence.');
  if (r.stopped && r.refused) out.push('A refused ranking says the search stopped.');
  out.push(...checkVoice(r.stopped, 'ranking.stopped'));
  if (!Array.isArray(r.assumptions) || r.assumptions.some((a) => typeof a !== 'string')) out.push('ranking.assumptions is not a list of sentences.');
  else r.assumptions.forEach((a, i) => out.push(...checkVoice(a, `ranking.assumptions[${i}]`)));
  return out;
}
