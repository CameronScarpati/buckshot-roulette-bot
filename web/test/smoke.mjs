// Checks the WebAssembly engine that the web page runs.
//
//   node web/test/smoke.mjs <folder holding engine.mjs> <advisor binary> [rounds]
//
// It checks the first load of five pinned seeds, plays the given number of
// seeded rounds (50 by default) in each mode with the solver in seat 1,
// replays each round to check that a seed always gives the same round and
// that advance() plays the move rank() puts first, plays rounds with and
// without rankings to check that rank() leaves the round alone, checks every
// value against the shapes the page reads, checks the errors and refusals,
// and compares advise() at reloads 0 with the native advisor's --json answer
// to 1e-9.

import { execFileSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const [folder, advisor, roundsArg] = process.argv.slice(2);
if (!folder || !advisor) {
  console.error('usage: node web/test/smoke.mjs <folder holding engine.mjs> <advisor> [rounds]');
  process.exit(2);
}
const ROUNDS = roundsArg === undefined ? 50 : Number(roundsArg);
const { default: createEngine } = await import(pathToFileURL(resolve(folder, 'engine.mjs')).href);

let failures = 0;
let checks = 0;
// How often each kind of move and event was met, so that a quiet run cannot
// pass by never meeting a steal or a skipped turn.
const seen = {};
const meet = (what) => {
  seen[what] = (seen[what] ?? 0) + 1;
};
function expect(condition, message) {
  checks += 1;
  if (!condition) {
    failures += 1;
    if (failures <= 40) console.error(`FAIL ${message}`);
  }
}

// ---------------------------------------------------------------------------
// The engine, as the worker calls it.
// ---------------------------------------------------------------------------

function wrap(module) {
  const answer = (text) => {
    const value = JSON.parse(text);
    const keys = Object.keys(value);
    expect(
      keys.length === 1 && (keys[0] === 'ok' || keys[0] === 'error'),
      `an answer is {ok} or {error}: ${text.slice(0, 120)}`,
    );
    return value;
  };
  return {
    newRound: (mode, seed, charges, seat) => answer(module.newRound(mode, seed, charges, seat)),
    act: (id, slot = -1) => answer(module.act(id, slot)),
    advance: () => answer(module.advance()),
    rank: () => answer(module.rank()),
    advise: (text, opponent, reloads) => answer(module.advise(text, opponent, reloads)),
  };
}

// ---------------------------------------------------------------------------
// Shapes.
// ---------------------------------------------------------------------------

const SEATS = ['p1', 'p2'];
const MODES = ['don', 'story1', 'story2', 'story3'];
const TOKENS = ['mg', 'beer', 'cig', 'cuff', 'saw', 'phone', 'adr', 'inv', 'med'];
const isInt = (n) => Number.isInteger(n);
const isObj = (o) => o !== null && typeof o === 'object' && !Array.isArray(o);

function keysWithin(object, allowed, where) {
  for (const key of Object.keys(object)) {
    expect(allowed.includes(key), `${where} has an unexpected field ${key}`);
  }
}

function voice(text, where) {
  if (typeof text !== 'string') return;
  expect(!/[\u2013\u2014]/.test(text), `${where} uses a dash: ${text}`);
  expect(
    !/n[’']t\b|\b(I|you|we|they|it|that|there|he|she)[’'](re|ve|ll|d|m|s)\b/i.test(text),
    `${where} runs two words together: ${text}`,
  );
}

function sameItems(a, b) {
  return [...a].sort().join() === [...b].sort().join();
}

function checkMove(move, view, where) {
  keysWithin(move, ['id', 'kind', 'label', 'item', 'target', 'slot', 'from'], where);
  expect(typeof move.id === 'string' && /^\d+$/.test(move.id), `${where}.id is ${move.id}`);
  expect(typeof move.label === 'string' && move.label.length > 0, `${where}.label is empty`);
  voice(move.label, `${where}.label`);
  const [mine, theirs] = view.seats;
  if (move.kind === 'shoot') {
    meet(move.target === 'p1' ? 'shoot yourself' : 'shoot the other seat');
    expect(SEATS.includes(move.target), `${where} shoots no seat`);
    expect(move.item === undefined && move.slot === undefined, `${where} is a shot with an item`);
    return;
  }
  expect(move.kind === 'item', `${where}.kind is ${move.kind}`);
  expect(TOKENS.includes(move.item), `${where}.item is ${move.item}`);
  expect(isInt(move.slot) && move.slot >= 0 && move.slot < 8, `${where}.slot is ${move.slot}`);
  if (move.from !== undefined) {
    meet('steal');
    expect(move.from === 'p2' && move.target === 'p2', `${where} steals from ${move.from}`);
    expect(theirs.items[move.slot] === move.item, `${where} steals from an empty slot`);
    expect(
      /^Use Adrenaline to take /.test(move.label),
      `${where} is a steal labelled ${move.label}`,
    );
    expect(move.item !== 'adr', `${where} steals an Adrenaline`);
  } else if (move.item === 'adr') {
    meet('Adrenaline alone');
    expect(move.label === 'Use Adrenaline and take nothing', `${where} is labelled ${move.label}`);
    expect(move.target === undefined, `${where} aims an Adrenaline used alone`);
    expect(mine.items[move.slot] === 'adr', `${where} names a slot without an Adrenaline`);
  } else {
    meet(`use ${move.item}`);
    expect(mine.items[move.slot] === move.item, `${where} names a slot without its item`);
    expect((move.item === 'cuff') === (move.target !== undefined), `${where} has a stray target`);
  }
}

function checkView(view) {
  if (!isObj(view)) {
    expect(false, 'the view is not an object');
    return;
  }
  keysWithin(
    view,
    [
      'mode',
      'stageLabel',
      'seed',
      'seats',
      'tube',
      'toMove',
      'over',
      'winner',
      'loadNumber',
      'legal',
      'notation',
    ],
    'view',
  );
  expect(MODES.includes(view.mode), `view.mode is ${view.mode}`);
  expect(
    typeof view.stageLabel === 'string' && view.stageLabel.length > 0,
    'view.stageLabel is empty',
  );
  expect(isInt(view.seed) && view.seed >= 0, 'view.seed is not a whole number');
  expect(
    typeof view.notation === 'string' && /^p1=/.test(view.notation),
    `view.notation is ${view.notation}`,
  );
  expect(Array.isArray(view.seats) && view.seats.length === 2, 'view.seats is not two seats');
  view.seats.forEach((seat, i) => {
    const where = `view.seats[${i}]`;
    keysWithin(
      seat,
      ['id', 'name', 'charges', 'max', 'faded', 'items', 'hand', 'restraint'],
      where,
    );
    expect(seat.id === SEATS[i], `${where}.id is ${seat.id}`);
    expect(typeof seat.name === 'string' && seat.name.length > 0, `${where}.name is empty`);
    expect(isInt(seat.max) && seat.max >= 1, `${where}.max is ${seat.max}`);
    expect(
      isInt(seat.charges) && seat.charges >= 0 && seat.charges <= seat.max,
      `${where}.charges`,
    );
    expect(isInt(seat.faded) && seat.faded >= 0 && seat.faded < seat.max, `${where}.faded`);
    expect(
      Array.isArray(seat.items) && seat.items.length === 8,
      `${where}.items is not eight slots`,
    );
    expect(
      seat.items.every((t) => t === null || TOKENS.includes(t)),
      `${where}.items has a bad token`,
    );
    expect(Array.isArray(seat.hand) && seat.hand.every((t) => TOKENS.includes(t)), `${where}.hand`);
    expect(
      sameItems(
        seat.hand,
        seat.items.filter((t) => t !== null),
      ),
      `${where}.hand and items disagree`,
    );
    expect([null, 'cuffed', 'lost a turn'].includes(seat.restraint), `${where}.restraint`);
  });
  const tube = view.tube;
  keysWithin(tube, ['live', 'blank', 'total', 'sawed', 'shells'], 'view.tube');
  expect(isInt(tube.live) && isInt(tube.blank) && tube.live >= 0 && tube.blank >= 0, 'tube counts');
  expect(tube.live + tube.blank === tube.total && tube.total <= 8, 'tube total');
  expect(typeof tube.sawed === 'boolean', 'tube.sawed');
  expect(Array.isArray(tube.shells) && tube.shells.length === tube.total, 'tube.shells');
  tube.shells.forEach((shell, i) => {
    keysWithin(shell, ['offset', 'known', 'knownBy'], `view.tube.shells[${i}]`);
    expect(shell.offset === i, `shell ${i} offset`);
    expect([null, 'live', 'blank'].includes(shell.known), `shell ${i} known`);
    expect(
      Array.isArray(shell.knownBy) && shell.knownBy.every((s) => SEATS.includes(s)),
      `shell ${i} knownBy`,
    );
    expect((shell.known === null) === (shell.knownBy.length === 0), `shell ${i} known and knownBy`);
  });
  expect([null, 'p1', 'p2'].includes(view.toMove), `view.toMove is ${view.toMove}`);
  expect(typeof view.over === 'boolean', 'view.over');
  expect(view.over === (view.winner !== null), 'view.over and view.winner disagree');
  expect(!view.over || view.toMove === null, 'a move is due after the round is over');
  expect(tube.total > 0 || view.toMove === null, 'a move is due with an empty tube');
  expect(isInt(view.loadNumber) && view.loadNumber >= 0, 'view.loadNumber');
  expect(Array.isArray(view.legal), 'view.legal');
  const shouldList = !view.over && view.toMove === 'p1' && tube.total > 0;
  expect(shouldList === view.legal.length > 0, 'view.legal is listed when seat 1 is to move');
  view.legal.forEach((move, i) => {
    expect(move.id === String(i), `view.legal[${i}].id is ${move.id}`);
    checkMove(move, view, `view.legal[${i}]`);
  });
}

const EVENT_KEYS = {
  load: ['kind', 'live', 'blank', 'dealt', 'first'],
  shot: ['kind', 'by', 'target', 'shell', 'damage'],
  item: ['kind', 'by', 'item', 'target', 'text'],
  learned: ['kind', 'by', 'offset', 'shell', 'private'],
  rule: ['kind', 'by', 'text'],
  skip: ['kind', 'seat'],
  over: ['kind', 'winner'],
};

function checkEvent(event, before, after) {
  const keys = EVENT_KEYS[event.kind];
  expect(keys !== undefined, `an event has kind ${event.kind}`);
  if (keys === undefined) return;
  meet(`${event.kind} event`);
  keysWithin(event, keys, `${event.kind} event`);
  switch (event.kind) {
    case 'load': {
      expect(event.live + event.blank >= 2 && event.live >= 1 && event.blank >= 1, 'load counts');
      expect(SEATS.includes(event.first), 'load.first');
      const most = { don: 5, story1: 0, story2: 2, story3: 4 }[after.mode];
      const least = { don: 2, story1: 0, story2: 2, story3: 4 }[after.mode];
      for (const [i, seat] of SEATS.entries()) {
        const dealt = event.dealt[seat];
        expect(
          Array.isArray(dealt) && dealt.every((t) => TOKENS.includes(t)),
          `load.dealt.${seat}`,
        );
        const room = 8 - before.seats[i].hand.length;
        expect(
          dealt.length <= Math.min(most, room),
          `${after.mode} deals ${dealt.length} to ${seat}`,
        );
        expect(
          dealt.length >= Math.min(least, room),
          `${after.mode} deals ${dealt.length} to ${seat}`,
        );
      }
      break;
    }
    case 'shot':
      expect(SEATS.includes(event.by) && SEATS.includes(event.target), 'shot seats');
      expect(['live', 'blank'].includes(event.shell), 'shot.shell');
      expect(isInt(event.damage) && event.damage >= 0 && event.damage <= 2, 'shot.damage');
      expect(event.shell === 'live' || event.damage === 0, 'a blank dealt damage');
      break;
    case 'item':
      expect(SEATS.includes(event.by), 'item.by');
      expect(TOKENS.includes(event.item), `item.item is ${event.item}`);
      expect(event.target === undefined || SEATS.includes(event.target), 'item.target');
      expect(typeof event.text === 'string' && event.text.trim().length > 0, 'item.text');
      expect(
        event.item !== 'beer' || /\b(live|blank)\b/.test(event.text),
        'a Beer names its shell',
      );
      voice(event.text, 'item.text');
      break;
    case 'learned':
      expect(SEATS.includes(event.by), 'learned.by');
      expect(isInt(event.offset) && event.offset >= 0 && event.offset <= 7, 'learned.offset');
      expect(['live', 'blank'].includes(event.shell), 'learned.shell');
      expect(event.private === true, 'learned.private');
      break;
    case 'rule':
      expect(event.by === 'p2', 'rule.by');
      expect(typeof event.text === 'string' && event.text.trim().length > 0, 'rule.text');
      voice(event.text, 'rule.text');
      break;
    case 'skip':
      expect(SEATS.includes(event.seat), 'skip.seat');
      break;
    case 'over':
      expect(SEATS.includes(event.winner), 'over.winner');
      break;
    default:
  }
}

/** Checks a result of act() or advance() taken from `before`. */
function checkStep(result, before) {
  keysWithin(result, ['events', 'view'], 'a step');
  expect(Array.isArray(result.events), 'step events');
  for (const event of result.events) checkEvent(event, before, result.view);
  checkView(result.view);
  // The damage a shot reports is what its target lost, so the charges add up
  // unless something else in the step changed them.
  const changers = new Set(['cig', 'med']);
  if (
    !result.events.some((e) => e.kind === 'load' || (e.kind === 'item' && changers.has(e.item)))
  ) {
    for (const [i, seat] of SEATS.entries()) {
      const lost = result.events
        .filter((e) => e.kind === 'shot' && e.target === seat)
        .reduce((sum, e) => sum + e.damage, 0);
      expect(
        before.seats[i].charges - result.view.seats[i].charges === lost,
        `${seat} lost charges no shot reported`,
      );
    }
  }
}

function checkRanking(ranking, view) {
  keysWithin(ranking, ['mover', 'opponent', 'refused', 'moves', 'assumptions'], 'ranking');
  expect(SEATS.includes(ranking.mover), 'ranking.mover');
  expect(['solver', 'dealer'].includes(ranking.opponent), 'ranking.opponent');
  expect(ranking.refused === null || typeof ranking.refused === 'string', 'ranking.refused');
  voice(ranking.refused, 'ranking.refused');
  expect(Array.isArray(ranking.moves), 'ranking.moves');
  expect((ranking.refused === null) === ranking.moves.length > 0, 'refused and moves agree');
  ranking.moves.forEach((move, i) => {
    keysWithin(move, ['id', 'label', 'win'], `ranking.moves[${i}]`);
    expect(typeof move.id === 'string' && move.id.length > 0, 'ranking move id');
    expect(typeof move.label === 'string' && move.label.length > 0, 'ranking move label');
    voice(move.label, 'ranking move label');
    expect(typeof move.win === 'number' && move.win >= 0 && move.win <= 1, `win is ${move.win}`);
    expect(i === 0 || move.win <= ranking.moves[i - 1].win, 'the ranking is best first');
  });
  expect(Array.isArray(ranking.assumptions), 'ranking.assumptions');
  ranking.assumptions.forEach((sentence) => voice(sentence, 'an assumption'));
  if (view !== null && ranking.refused === null) {
    // In a round every legal move is ranked once, under its own id and label.
    const ids = ranking.moves.map((m) => m.id).sort();
    expect(
      ids.join() ===
        view.legal
          .map((m) => m.id)
          .sort()
          .join(),
      'the ranking covers view.legal',
    );
    for (const move of ranking.moves) {
      expect(view.legal[Number(move.id)]?.label === move.label, `ranked label ${move.label}`);
    }
  }
}

// ---------------------------------------------------------------------------
// The pinned first loads.
// ---------------------------------------------------------------------------

const engine = wrap(await createEngine());

const PINNED = {
  1: 'p1=3/3[phone,inv] p2=3/3[beer,cig] tube=2L3B turn=p1',
  2: 'p1=2/2[mg,inv,cuff] p2=2/2[cig,cuff,mg] tube=2L3B turn=p1',
  4: 'p1=3/3[cig,cuff,phone,med,saw] p2=3/3[adr,mg,saw,med,cig] tube=4L4B turn=p1',
  6: 'p1=4/4[cig,inv,mg,cig,cuff] p2=4/4[beer,mg,phone,inv,inv] tube=1L1B turn=p1',
  7: 'p1=2/2[inv,cuff,cuff] p2=2/2[med,med,phone] tube=4L4B turn=p1',
};
for (const [seed, first] of Object.entries(PINNED)) {
  const start = engine.newRound('don', Number(seed), 0, 'human').ok;
  checkView(start);
  expect(start.loadNumber === 0 && start.tube.total === 0 && start.toMove === null, 'a new round');
  const load = engine.advance().ok;
  checkStep(load, start);
  expect(load.view.notation === first, `seed ${seed} loads ${load.view.notation}`);
  const ranking = engine.rank().ok;
  checkRanking(ranking, load.view);
}

// ---------------------------------------------------------------------------
// Rounds with the solver in seat 1, each played twice: once with advance()
// for the solver's moves and once with act() on the move rank() puts first.
// ---------------------------------------------------------------------------

function playRound(mode, seed, useAct, firstDecisions) {
  const steps = [];
  let view = engine.newRound(mode, seed, 0, 'solver').ok;
  checkView(view);
  steps.push(JSON.stringify(view));
  let first = true;
  for (let n = 0; n < 2000 && !view.over; n += 1) {
    let result;
    if (view.toMove === 'p1') {
      const ranking = engine.rank().ok;
      checkRanking(ranking, view);
      steps.push(JSON.stringify(ranking));
      if (first && firstDecisions !== null) firstDecisions.push(view.notation);
      first = false;
      result = useAct ? engine.act(ranking.moves[0].id) : engine.advance();
    } else {
      result = engine.advance();
    }
    expect(result.ok !== undefined, `${mode} seed ${seed}: ${result.error}`);
    if (result.ok === undefined) break;
    checkStep(result.ok, view);
    steps.push(JSON.stringify(result.ok));
    view = result.ok.view;
  }
  expect(view.over, `${mode} seed ${seed} ends`);
  const after = engine.advance().ok;
  expect(after.events.length === 0 && after.view.over, 'advance after the end changes nothing');
  return { steps, winner: view.winner };
}

const started = performance.now();
const donDecisions = [];
const record = {};
for (const mode of MODES) {
  const wins = { p1: 0, p2: 0 };
  for (let seed = 1; seed <= ROUNDS; seed += 1) {
    const one = playRound(mode, seed, seed % 2 === 0, mode === 'don' ? donDecisions : null);
    const two = playRound(mode, seed, seed % 2 !== 0, null);
    const same =
      one.steps.length === two.steps.length && one.steps.every((s, i) => s === two.steps[i]);
    expect(same, `${mode} seed ${seed} plays the same round twice`);
    wins[one.winner] += 1;
  }
  record[mode] = wins;
}
// A person in seat 1 picks moves by a fixed rule. Asking for a ranking before
// each pick must not change the round, since rank() draws nothing from the
// round's random numbers.
function playByRule(mode, seed, askFirst) {
  const steps = [];
  let view = engine.newRound(mode, seed, 0, 'human').ok;
  for (let n = 0; n < 2000 && !view.over; n += 1) {
    let result;
    if (view.toMove === 'p1') {
      if (askFirst) checkRanking(engine.rank().ok, view);
      result = engine.act(view.legal[(n * 7) % view.legal.length].id);
    } else {
      result = engine.advance();
    }
    expect(result.ok !== undefined, `${mode} seed ${seed} by rule: ${result.error}`);
    if (result.ok === undefined) break;
    checkStep(result.ok, view);
    steps.push(JSON.stringify(result.ok));
    view = result.ok.view;
  }
  expect(view.over, `${mode} seed ${seed} by rule ends`);
  return steps;
}
for (const mode of MODES) {
  for (let seed = 1; seed <= 10; seed += 1) {
    const plain = playByRule(mode, seed, false);
    const asked = playByRule(mode, seed, true);
    const same = plain.length === asked.length && plain.every((s, i) => s === asked[i]);
    expect(same, `${mode} seed ${seed}: a ranking changed the round`);
  }
}
const roundSeconds = (performance.now() - started) / 1000;
const MET = [
  'shoot yourself',
  'shoot the other seat',
  'steal',
  'Adrenaline alone',
  ...TOKENS.filter((t) => t !== 'adr').map((t) => `use ${t}`),
  ...Object.keys(EVENT_KEYS).map((kind) => `${kind} event`),
];
for (const what of MET) expect((seen[what] ?? 0) > 0, `the rounds never met: ${what}`);
const metInRounds = MET.map((what) => `${what} ${seen[what] ?? 0}`).join(', ');

// ---------------------------------------------------------------------------
// Errors and refusals.
// ---------------------------------------------------------------------------

const DEALER_HAS_NO_CHOICE =
  'The Dealer is to move and follows its script, so it has no choice to rank.';
const NOTHING_TO_RANK = 'There is no move to rank right now.';

{
  const fresh = wrap(await createEngine());
  expect(fresh.act('0').error === 'No round has been started.', 'act with no round');
  expect(fresh.advance().error === 'No round has been started.', 'advance with no round');
  expect(fresh.rank().ok.refused === NOTHING_TO_RANK, 'rank with no round');
}
expect(engine.newRound('endless', 1, 0, 'human').error !== undefined, 'an unknown mode');
for (const seed of [-1, 1.5, 2 ** 32, Number.NaN]) {
  expect(engine.newRound('don', seed, 0, 'human').error !== undefined, `seed ${seed}`);
}
for (const charges of [1, 5, -3]) {
  expect(engine.newRound('don', 1, charges, 'human').error !== undefined, `charges ${charges}`);
}
expect(engine.newRound('don', 1, 0, 'nobody').error !== undefined, 'an unknown seat');
for (const charges of [2, 3, 4]) {
  const view = engine.newRound('don', 9, charges, 'human').ok;
  expect(
    view.seats.every((s) => s.max === charges),
    `Double or Nothing on ${charges} charges`,
  );
}
expect(engine.newRound('story3', 9, 2, 'human').ok.seats[0].max === 5, 'story 3 sets its charges');

{
  let view = engine.newRound('don', 3, 0, 'human').ok;
  expect(engine.rank().ok.refused === NOTHING_TO_RANK, 'rank before the first load');
  expect(engine.act('0').error !== undefined, 'act before the first load');
  view = engine.advance().ok.view;
  expect(view.toMove === 'p1', 'seed 3 starts with seat 1');
  expect(
    engine.advance().error === 'Seat 1 is to move. Call act with one of view.legal.',
    'advance on a person in seat 1',
  );
  for (const id of ['', 'x', '-1', '1.0', String(view.legal.length), '0000']) {
    expect(engine.act(id).error !== undefined, `act with id ${JSON.stringify(id)}`);
  }
  const item = view.legal.find(
    (m) => m.kind === 'item' && m.item !== 'adr' && m.from === undefined,
  );
  if (item !== undefined) {
    const empty = view.seats[0].items.indexOf(null);
    expect(engine.act(item.id, empty).error !== undefined, 'act with an empty tray slot');
    expect(engine.act('0', 0).error !== undefined, 'a shot given a tray slot');
    const played = engine.act(item.id, item.slot).ok;
    checkStep(played, view);
    expect(played.view.seats[0].items[item.slot] === null, 'the move empties the slot it names');
  }
  // Shoot the Dealer until it is the Dealer's turn, then ask for a ranking.
  for (let n = 0; n < 40; n += 1) {
    const current = engine.advance();
    if (current.ok !== undefined && current.ok.view.toMove === 'p2') {
      const ranking = engine.rank().ok;
      expect(
        ranking.refused === DEALER_HAS_NO_CHOICE && ranking.mover === 'p2',
        'rank on the Dealer',
      );
      expect(engine.act('0').error !== undefined, 'act on the Dealer');
      break;
    }
    if (current.ok !== undefined && current.ok.view.over) break;
    if (current.error !== undefined) engine.act('1');
  }
}

{
  const ranking = engine.advise('p1=3/4[saw] p2=3/4[mg,beer] tube=2L2B turn=p2', 'dealer', 0).ok;
  expect(
    ranking.refused ===
      `${DEALER_HAS_NO_CHOICE} Choose the solver as the opponent, or give the turn to seat 1.`,
    'advise on the Dealer',
  );
  const deep = engine.advise('p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1', 'solver', 2).ok;
  expect(deep.refused !== null && deep.moves.length === 0, 'advise past one reload');
  const bad = engine.advise('p1=3/4 p2=9/4 tube=2L2B turn=p1', 'solver', 0).ok;
  expect(/^The position cannot be used: /.test(bad.refused), 'advise on a bad position');
  expect(
    engine.advise('p1=3/4 p2=3/4 tube=2L2B turn=p1', 'nobody', 0).error !== undefined,
    'advise opponent',
  );
  const cuffed = engine.advise(
    'p1=3/4[saw] p2=3/4[mg,adr] tube=2L2B turn=p1 cuffed=p1',
    'dealer',
    0,
  ).ok;
  expect(
    cuffed.refused !== null && cuffed.mover === 'p2',
    'advise with seat 1 cuffed against the Dealer',
  );
  for (const r of [deep, bad, ranking, cuffed]) checkRanking(r, null);
}

// ---------------------------------------------------------------------------
// advise() against the native advisor.
// ---------------------------------------------------------------------------

function native(text, opponent, seat) {
  const args = ['--position', text, '--reloads', '0', '--json', '--seat', String(seat + 1)];
  if (opponent === 'dealer') args.push('--opponent', 'dealer');
  try {
    return JSON.parse(
      execFileSync(advisor, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }),
    );
  } catch {
    return null;
  }
}

/** A move's kind and item, which the native text and the page's label both name. */
function nativeMove(text) {
  if (text === 'shoot self') return 'shoot self';
  if (text.startsWith('shoot ')) return 'shoot other';
  if (text === 'use Adrenaline') return 'adrenaline alone';
  const steal = /^steal (.+?)(?: #\d+)? from p\d and use it/.exec(text);
  if (steal) return `steal ${steal[1]}`;
  const use = /^use (.+?)(?: #\d+)?(?: on p\d)?$/.exec(text);
  return use ? `use ${use[1]}` : text;
}

function pageMove(label) {
  if (label === 'Shoot yourself') return 'shoot self';
  if (label.startsWith('Shoot ')) return 'shoot other';
  if (label === 'Use Adrenaline and take nothing') return 'adrenaline alone';
  const steal = /^Use Adrenaline to take (?:your|the \w+’s) (.+?)(?: in slot \d)?$/.exec(label);
  if (steal) return `steal ${steal[1]}`;
  const use = /^Use the (.+?)(?: in slot \d)?(?: on .+)?$/.exec(label);
  return use ? `use ${use[1]}` : label;
}

const FIXED = [
  'p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1',
  'p1=2/4[saw] p2=3/4[mg,beer] tube=2L2B turn=p1',
  'p1=4/4[mg] p2=4/4 tube=2L3B turn=p1',
  'p1=3/4[saw] p2=3/4[mg,beer] tube=2L2B turn=p2',
  'p1=3/4[saw] p2=3/4[mg,adr] tube=2L2B turn=p1 cuffed=p1',
  'p1=2/2[adr] p2=2/2[cig,beer] tube=1L2B turn=p1',
  'p1=2/3[cig,adr,cig] p2=2/3[adr,mg,cuff,saw] tube=3L2B turn=p1 known=p1:1L',
];
const positions = [...FIXED, ...donDecisions];
let compared = 0;
let refusedBoth = 0;
const adviseStarted = performance.now();
for (const text of positions) {
  for (const opponent of ['solver', 'dealer']) {
    const ours = engine.advise(text, opponent, 0).ok;
    checkRanking(ours, null);
    const turn = / turn=p2/.test(text) ? 1 : 0;
    if (opponent === 'dealer' && turn === 1) continue;
    let theirs = native(text, opponent, turn);
    if (theirs !== null && theirs.mover !== SEATS[turn] && opponent === 'solver') {
      theirs = native(text, opponent, SEATS.indexOf(theirs.mover));
    }
    if (theirs === null || theirs.refused || theirs.actions.length === 0) {
      expect(ours.refused !== null, `${opponent} ${text}: the advisor gives no ranking`);
      refusedBoth += 1;
      continue;
    }
    expect(ours.refused === null, `${opponent} ${text}: refused ${ours.refused}`);
    if (ours.refused !== null) continue;
    expect(ours.mover === theirs.mover, `${opponent} ${text}: mover ${ours.mover}`);
    expect(ours.moves.length === theirs.actions.length, `${opponent} ${text}: move count`);
    theirs.actions.forEach((action, i) => {
      const move = ours.moves[i];
      expect(
        move !== undefined && Math.abs(move.win - action.value) <= 1e-9,
        `${opponent} ${text}: row ${i}`,
      );
      expect(
        move !== undefined && pageMove(move.label) === nativeMove(action.action),
        `${opponent} ${text}: ${move?.label} is ${action.action}`,
      );
    });
    compared += 1;
  }
}
const adviseSeconds = (performance.now() - adviseStarted) / 1000;

console.log(`rounds: ${ROUNDS} a mode, each played twice, in ${roundSeconds.toFixed(1)} s`);
for (const mode of MODES) console.log(`  ${mode}: seat 1 won ${record[mode].p1} of ${ROUNDS}`);
console.log(`  met in the rounds: ${metInRounds}`);
console.log(
  `advise: ${compared} answers matched the advisor, ${refusedBoth} refused by both, in ${adviseSeconds.toFixed(1)} s`,
);
console.log(`${checks} checks, ${failures} failed`);
process.exit(failures === 0 ? 0 : 1);
