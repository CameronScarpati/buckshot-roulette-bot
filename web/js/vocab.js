// Shared words for the page: item names, what each item does, mode names.
// Plain voice: direct, factual, no contractions.

// The item tokens, in the engine's own item order.
export const ITEM_TOKENS = ['mg', 'beer', 'cig', 'cuff', 'saw', 'phone', 'adr', 'inv', 'med'];

export const ITEMS = {
  mg: { name: 'Magnifying Glass', short: 'Glass', does: 'See the shell in the chamber.' },
  beer: { name: 'Beer', short: 'Beer', does: 'Rack the chamber out. Everyone sees the shell.' },
  cig: { name: 'Cigarettes', short: 'Cigarettes', does: 'One charge back.', plural: true },
  cuff: { name: 'Handcuffs', short: 'Cuffs', does: 'The other seat skips its next turn.', plural: true },
  saw: { name: 'Hand Saw', short: 'Saw', does: 'The next shot deals two charges.' },
  phone: { name: 'Burner Phone', short: 'Phone', does: 'Learn the type of one later shell.' },
  adr: { name: 'Adrenaline', short: 'Adrenaline', does: 'Take an item from the other seat and use it now.' },
  inv: { name: 'Inverter', short: 'Inverter', does: 'Flip the shell in the chamber.' },
  med: { name: 'Expired Medicine', short: 'Medicine', does: 'Even odds: two charges back, or one lost.' },
};

export const MODES = {
  don: 'Double or Nothing',
  story1: 'Story, stage 1',
  story2: 'Story, stage 2',
  story3: 'Story, stage 3',
};

// The choices in a round form's Game select. Double or Nothing either draws
// its charges from the seed or uses a fixed count.
export const GAMES = [
  { value: 'don', mode: 'don', charges: null, label: 'Double or Nothing', note: 'Charges drawn from 2 to 4 by the seed. 2 to 5 items each load.' },
  { value: 'don:2', mode: 'don', charges: 2, label: 'Double or Nothing, 2 charges', note: '2 to 5 items each load.' },
  { value: 'don:3', mode: 'don', charges: 3, label: 'Double or Nothing, 3 charges', note: '2 to 5 items each load.' },
  { value: 'don:4', mode: 'don', charges: 4, label: 'Double or Nothing, 4 charges', note: '2 to 5 items each load.' },
  { value: 'story1', mode: 'story1', charges: null, label: 'Story, stage 1', note: '2 charges. No items.' },
  { value: 'story2', mode: 'story2', charges: null, label: 'Story, stage 2', note: '4 charges. 2 items each load.' },
  { value: 'story3', mode: 'story3', charges: null, label: 'Story, stage 3', note: '5 charges, and the last one cannot be healed. 4 items each load.' },
];

export const other = (seat) => (seat === 'p1' ? 'p2' : 'p1');
export const flip = (kind) => (kind === 'live' ? 'blank' : 'live');

export function esc(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

export function pct(win) {
  if (win >= 1) return '100%';
  if (win <= 0) return '0%';
  return `${(win * 100).toFixed(1)}%`;
}

/** Percentage points between two chances, one decimal. */
export function points(a) {
  return `${(Math.abs(a) * 100).toFixed(1)}`;
}

/** Join names as "A", "A and B", "A, B and C". */
export function listJoin(words) {
  if (words.length <= 1) return words.join('');
  return `${words.slice(0, -1).join(', ')} and ${words[words.length - 1]}`;
}
