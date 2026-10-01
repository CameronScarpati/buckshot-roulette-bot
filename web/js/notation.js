// Position notation: parse, print and check. The grammar is the one the
// command line advisor reads (engine/Notation.h), for example
//   p1=2/4[mg,saw] p2=4/4[beer,cuff] tube=2L3B turn=p1 known=p1:0L
// Offsets count from 0, the chamber. A known shell is named by the type it
// fires as. A seat's items are written in the order it received them, which
// is the order of its tray here: copies of an item next to each other are one
// choice, and copies apart are separate ones. Printing follows the engine:
// flags in its order, and the tube always with both counts (tube=1L0B, never
// tube=1L). listcigs, phoned= and dealer= are kept as read and printed back;
// the table does not show them.

import { ITEM_TOKENS, ITEMS } from './vocab.js';

const SEATS = ['p1', 'p2'];
export const MAX_SHELLS = 8;
export const MAX_CHARGES = 8;
export const TRAY = 8;

export function blankSeat() {
  return { charges: 4, max: 4, items: Array(TRAY).fill(null) };
}

export function emptyPosition() {
  return {
    seats: [blankSeat(), blankSeat()],
    live: 2,
    blank: 3,
    turn: 'p1',
    cuffed: null,
    skipped: null,
    restraintUsed: false,
    sawed: false,
    inverted: false,
    known: { p1: {}, p2: {} },
    listCigs: false,
    phoned: { p1: [], p2: [] },
    dealer: null,
  };
}

export function clonePosition(pos) {
  return {
    ...pos,
    seats: pos.seats.map((s) => ({ ...s, items: [...s.items] })),
    known: { p1: { ...pos.known.p1 }, p2: { ...pos.known.p2 } },
    phoned: { p1: [...pos.phoned.p1], p2: [...pos.phoned.p2] },
  };
}

/** A seat's tray with its items packed from slot 1, in the same order. */
export function packTray(items) {
  const held = items.filter(Boolean);
  return [...held, ...Array(TRAY - held.length).fill(null)];
}

/** Parse notation text. Returns { ok, errors, pos }, with every error found. */
export function parseNotation(text) {
  const pos = emptyPosition();
  pos.live = 0;
  pos.blank = 0;
  const errors = [];
  const given = { p1: false, p2: false, tube: false, turn: false, dir: false, dealer: false, phonedp1: false, phonedp2: false };
  // Allow spaces inside item brackets: "[saw, beer]".
  const src = String(text ?? '').replace(/\[[^\]]*\]/g, (m) => m.replace(/\s+/g, ''));
  const tokens = src.trim().split(/\s+/).filter(Boolean);
  if (tokens.length === 0) return { ok: false, errors: ['The position is empty.'], pos };
  const knownTokens = [];

  for (const raw of tokens) {
    const tok = raw.toLowerCase();
    const eq = tok.indexOf('=');
    const key = eq < 0 ? tok : tok.slice(0, eq);
    const value = eq < 0 ? '' : tok.slice(eq + 1);

    if (/^p\d+$/.test(key)) {
      const n = Number(key.slice(1));
      if (n < 1 || n > 2) {
        errors.push(`This page has two seats, so ${key} cannot be used.`);
        continue;
      }
      if (given[key]) {
        errors.push(`${key} is given twice.`);
        continue;
      }
      given[key] = true;
      const m = /^(\d+)(?:\/(\d+))?(?:\[([^\]]*)\]?)?$/.exec(value);
      if (!m) {
        errors.push(`Charges must look like ${key}=3/4, not "${raw}".`);
        continue;
      }
      const s = pos.seats[n - 1];
      s.charges = Number(m[1]);
      s.max = m[2] === undefined ? s.charges : Number(m[2]);
      s.items = Array(TRAY).fill(null);
      if (s.max < 1 || s.max > MAX_CHARGES) errors.push(`${key} needs a maximum of 1 to ${MAX_CHARGES} charges.`);
      if (s.charges > s.max) errors.push(`${key} holds more charges than its maximum.`);
      const list = (m[3] ?? '').split(',').filter(Boolean);
      if (list.length > TRAY) errors.push(`${key} holds ${list.length} items, and a tray has ${TRAY} slots.`);
      let slot = 0;
      for (const item of list) {
        if (item === 'jam' || item === 'rem') {
          errors.push(`The ${item === 'jam' ? 'Jammer' : 'Remote'} (${item}) is a multiplayer item, and this page has two seats.`);
        } else if (!ITEM_TOKENS.includes(item)) {
          errors.push(`No item is called "${item}". The items are ${ITEM_TOKENS.join(', ')}.`);
        } else if (slot < TRAY) {
          s.items[slot] = item;
          slot += 1;
        }
      }
    } else if (key === 'tube') {
      if (given.tube) {
        errors.push('tube is given twice.');
        continue;
      }
      given.tube = true;
      const m = /^(\d+)l(\d+)b$/.exec(value);
      if (!m) {
        errors.push(`Write the tube with both counts, as in tube=2L3B or tube=1L0B, not "${raw}".`);
        continue;
      }
      pos.live = Number(m[1]);
      pos.blank = Number(m[2]);
      if (pos.live + pos.blank > MAX_SHELLS) errors.push(`A tube holds at most ${MAX_SHELLS} shells.`);
    } else if (key === 'turn') {
      if (given.turn) {
        errors.push('turn is given twice.');
        continue;
      }
      given.turn = true;
      if (value === 'p1' || value === 'p2') pos.turn = value;
      else errors.push('turn must name a seat, as in turn=p1.');
    } else if (key === 'cuffed' || key === 'skipped') {
      const list = value.split(',').filter(Boolean);
      if (list.length !== 1 || !SEATS.includes(list[0])) {
        errors.push(`${key} must name one seat, as in ${key}=p2.`);
        continue;
      }
      if (key === 'cuffed') pos.cuffed = list[0];
      else pos.skipped = list[0];
    } else if (tok === 'restraintused') {
      pos.restraintUsed = true;
    } else if (tok === 'sawed') {
      pos.sawed = true;
    } else if (tok === 'inverted') {
      pos.inverted = true;
    } else if (key === 'known') {
      knownTokens.push({ raw, value });
    } else if (tok === 'listcigs') {
      pos.listCigs = true;
    } else if (key === 'phoned') {
      // A seat used a Burner Phone this load at these tube sizes, and the
      // reader never saw where it looked.
      const m = /^(p[12])@(\d+(?:,\d+)*)$/.exec(value);
      if (!m) {
        errors.push(`phoned must look like phoned=p2@5 or phoned=p2@5,4, not "${raw}".`);
        continue;
      }
      if (given[`phoned${m[1]}`]) {
        errors.push(`phoned is given twice for ${m[1]}.`);
        continue;
      }
      given[`phoned${m[1]}`] = true;
      const sizes = m[2].split(',').map(Number);
      if (sizes.some((n) => n < 2 || n > MAX_SHELLS)) errors.push(`A phone read names a tube of 2 to ${MAX_SHELLS} shells.`);
      pos.phoned[m[1]] = sizes.sort((a, b) => b - a);
    } else if (key === 'dealer') {
      // The Dealer's memory partway through its turn. The engine checks it.
      if (given.dealer) {
        errors.push('dealer is given twice.');
        continue;
      }
      given.dealer = true;
      if (!/^[a-z:0-9,]+$/.test(value)) errors.push(`dealer must look like dealer=seen or dealer=seen,med, not "${raw}".`);
      else pos.dealer = value.replace('believes:b', 'believes:B');
    } else if (key === 'dir') {
      // Turn direction only matters with more than two seats.
      if (given.dir) errors.push('dir is given twice.');
      else if (!['cw', 'ccw', '+', '-'].includes(value)) errors.push('dir must be cw or ccw.');
      given.dir = true;
    } else {
      errors.push(`Unknown token "${raw}".`);
    }
  }
  if (!given.p1) errors.push('Seat 1 is missing, as in p1=4/4[saw].');
  if (!given.p2) errors.push('Seat 2 is missing, as in p2=4/4[mg].');
  if (!given.tube) errors.push('The tube is missing, as in tube=2L3B.');

  const total = pos.live + pos.blank;
  for (const { raw, value } of knownTokens) {
    const colon = value.indexOf(':');
    const seat = colon < 0 ? '' : value.slice(0, colon);
    if (!SEATS.includes(seat)) {
      errors.push(`"${raw}" must name a seat, as in known=p1:0L.`);
      continue;
    }
    for (const fact of value.slice(colon + 1).split(',').filter(Boolean)) {
      const k = /^(\d+)([lb])$/.exec(fact);
      if (!k) {
        errors.push(`A known shell looks like 0L or 2B, not "${fact}".`);
        continue;
      }
      const off = Number(k[1]);
      const kind = k[2] === 'l' ? 'live' : 'blank';
      if (given.tube && off >= total) {
        errors.push(`Offset ${off} is past the end of the tube, which holds ${total}.`);
        continue;
      }
      pos.known[seat][off] = kind;
    }
  }
  return { ok: errors.length === 0, errors: [...new Set(errors)], pos };
}

/**
 * Read pasted text: a position, or a whole advisor command line such as
 *   ./build/advisor --position "p1=4/4 p2=4/4 tube=2L2B turn=p1" --reloads 1 --opponent dealer
 * Returns { notation, opponent, reloads, notes, errors }. Options the page
 * does not use are listed in notes; anything it cannot read is an error.
 */
export function readPaste(text) {
  let src = String(text ?? '')
    .replace(/[“”]/g, '"')
    .replace(/[‘’]/g, "'")
    .replace(/\\\r?\n/g, ' ')
    .trim()
    .replace(/^\$\s*/, '');
  const out = { notation: src, opponent: null, reloads: null, notes: [], errors: [] };
  const isCommand = /(^|\s)(--position|-p)(=|\s|$)/.test(src) || /^\S*advisor(\s|$)/.test(src);
  if (!isCommand) return out;
  out.notation = null;
  const { words, unclosed } = shellWords(src);
  if (unclosed) out.errors.push('A quote in the command line is not closed.');
  let i = words[0] && !words[0].startsWith('-') ? 1 : 0;
  for (; i < words.length; i += 1) {
    let flag = words[i];
    let inline = null;
    const eq = flag.startsWith('--') ? flag.indexOf('=') : -1;
    if (eq > 0) {
      inline = flag.slice(eq + 1);
      flag = flag.slice(0, eq);
    }
    const value = () => {
      if (inline !== null) return inline;
      if (i + 1 < words.length) {
        i += 1;
        return words[i];
      }
      return null;
    };
    if (flag === '--position' || flag === '-p') {
      const v = value();
      if (!v || !v.trim()) out.errors.push('--position needs a position after it, in quotes.');
      else out.notation = v;
    } else if (flag === '--reloads') {
      const v = value();
      if (!/^\d+$/.test(v ?? '')) out.errors.push('--reloads needs a whole number: 0 or 1.');
      else if (Number(v) > 1) {
        out.reloads = 1;
        out.notes.push(`--reloads ${v} looks further than this page does, so reloads is set to 1.`);
      } else out.reloads = Number(v);
    } else if (flag === '--opponent') {
      const v = (value() ?? '').toLowerCase();
      if (v === 'solver' || v === 'dealer') out.opponent = v;
      else out.errors.push(`--opponent takes solver or dealer, not "${v}".`);
    } else if (flag === '--seat') {
      const v = value();
      if (v !== '1') out.notes.push(`This page advises seat 1, so --seat ${v ?? ''} is ignored.`);
    } else if (flag === '--mode') {
      const v = value();
      if (v !== 'don') out.notes.push(`This page asks under Double or Nothing rules, so --mode ${v ?? ''} is ignored.`);
    } else if (flag === '--json') {
      // The page always reads the answer as data.
    } else if (flag.startsWith('-')) {
      const v = inline ?? (i + 1 < words.length && !words[i + 1].startsWith('-') ? words[(i += 1)] : null);
      out.notes.push(`This page ignores ${flag}${v ? ` ${v}` : ''}.`);
    } else {
      out.errors.push(`Cannot read "${flag}" in the command line. Put the position in quotes after --position.`);
    }
  }
  if (out.notation === null && !out.errors.some((e) => e.startsWith('--position'))) {
    out.errors.push('The command line has no --position "..." to read.');
  }
  return out;
}

function shellWords(s) {
  const words = [];
  let cur = '';
  let quote = null;
  let started = false;
  for (const ch of s) {
    if (quote) {
      if (ch === quote) quote = null;
      else cur += ch;
      continue;
    }
    if (ch === '"' || ch === "'") {
      quote = ch;
      started = true;
    } else if (/\s/.test(ch)) {
      if (started) words.push(cur);
      cur = '';
      started = false;
    } else {
      cur += ch;
      started = true;
    }
  }
  if (started) words.push(cur);
  return { words, unclosed: quote !== null };
}

function itemList(items) {
  return items.filter(Boolean).join(',');
}

function knownList(map) {
  return Object.keys(map)
    .map(Number)
    .sort((a, b) => a - b)
    .map((o) => `${o}${map[o] === 'live' ? 'L' : 'B'}`)
    .join(',');
}

/** Print a position the way the engine prints it. */
export function formatNotation(pos) {
  const parts = pos.seats.map((s, i) => {
    const items = itemList(s.items);
    return `${SEATS[i]}=${s.charges}/${s.max}${items ? `[${items}]` : ''}`;
  });
  parts.push(`tube=${pos.live}L${pos.blank}B`);
  parts.push(`turn=${pos.turn}`);
  if (pos.sawed) parts.push('sawed');
  if (pos.inverted) parts.push('inverted');
  if (pos.restraintUsed) parts.push('restraintused');
  if (pos.cuffed) parts.push(`cuffed=${pos.cuffed}`);
  if (pos.skipped) parts.push(`skipped=${pos.skipped}`);
  for (const seat of SEATS) {
    const list = knownList(pos.known[seat]);
    if (list) parts.push(`known=${seat}:${list}`);
  }
  if (pos.listCigs) parts.push('listcigs');
  for (const seat of SEATS) if (pos.phoned[seat].length) parts.push(`phoned=${seat}@${pos.phoned[seat].join(',')}`);
  if (pos.dealer) parts.push(`dealer=${pos.dealer}`);
  return parts.join(' ');
}

/** The advisor command line for a position. */
export function formatCommand(notation, { opponent, reloads }) {
  return `./build/advisor --position "${notation}" --reloads ${reloads} --opponent ${opponent}`;
}

/** Problems that make a position impossible, in plain sentences. */
export function validatePosition(pos) {
  const out = [];
  const label = { p1: 'Seat 1', p2: 'Seat 2' };
  pos.seats.forEach((s, i) => {
    const who = label[SEATS[i]];
    if (s.max < 1 || s.max > MAX_CHARGES) out.push(`${who} needs a maximum of 1 to ${MAX_CHARGES} charges.`);
    if (s.charges < 1) out.push(`${who} has no charges left, so the round is already over.`);
    if (s.charges > s.max) out.push(`${who} has more charges than its maximum.`);
  });
  const total = pos.live + pos.blank;
  if (total < 1) out.push('The tube needs at least one shell.');
  if (total > MAX_SHELLS) out.push(`The tube holds at most ${MAX_SHELLS} shells.`);
  const merged = {};
  for (const seat of SEATS) {
    for (const [o, v] of Object.entries(pos.known[seat])) {
      const off = Number(o);
      if (off >= total) out.push(`${label[seat]} knows shell ${off + 1}, but the tube holds ${total}.`);
      if (merged[off] && merged[off] !== v) out.push(`Shell ${off + 1} is named as both live and blank.`);
      merged[off] = v;
    }
  }
  const knownLive = Object.values(merged).filter((v) => v === 'live').length;
  const knownBlank = Object.values(merged).filter((v) => v === 'blank').length;
  if (knownLive > pos.live) out.push('More shells are marked live than the tube holds.');
  if (knownBlank > pos.blank) out.push('More shells are marked blank than the tube holds.');
  if (pos.inverted && merged[0]) {
    out.push('A chamber a seat has seen cannot also be marked inverted. Name the type it fires as and clear Inverted.');
  }
  if (pos.cuffed && pos.cuffed === pos.turn) out.push('The seat to move cannot be handcuffed.');
  if (pos.cuffed && pos.cuffed === pos.skipped) out.push('A seat cannot be cuffed and owed a turn at once.');
  return [...new Set(out)];
}

/** Count of items in a tray, for messages. */
export function itemCount(items) {
  return items.filter(Boolean).length;
}

export const ITEM_NAMES = Object.fromEntries(ITEM_TOKENS.map((t) => [t, ITEMS[t].name]));
