// PLAY: you take seat 1 against the Dealer. Pick up the shotgun or an item
// from your tray, then choose a target. Only legal moves are offered. Keys:
// 1 to 8 pick an item, G the shotgun, D and Y choose the Dealer or you, H asks
// for a hint, Escape puts it back.

import { playEvents } from './director.js';
import { chanceText, inversion, readTube } from './knowledge.js';
import { Log, nextDealerNote } from './log.js';
import { renderRanking } from './ranking.js';
import * as session from './session.js';
import { app, fillGameSelect, readRoundForm, reducedMotion, syncGameNote, wait, writeRoundForm } from './state.js';
import { focusIn, modelFromView, patchDnote, renderTable } from './table.js';
import { ITEMS, esc, pct } from './vocab.js';

const PACE = 700;
const DEALER_PAUSE = 650;

export function createPlay({ table, panel, openInAdvise }) {
  // The round row sits under the table, outside the panel.
  const $ = (sel) => panel.querySelector(sel) ?? document.querySelector(sel);
  const status = $('#p-status');
  const btnHint = $('#p-hint');
  const reasons = $('#p-reasons');
  const form = $('#p-form');
  const btnReplay = $('#p-replay');
  const btnAdvise = $('#p-to-advise');
  const rankEl = $('#p-rank');
  const log = new Log($('#p-log'));
  log.ctx = { human: true, rules: false, secrets: false };

  const st = {
    view: null,
    settings: null,
    sel: null, // {kind:'gun'} | {kind:'slot', slot, token}
    hint: null,
    hintBusy: false,
    busy: false,
    dealerPaused: false,
    restoring: '',
    spent: [],
    inv: false,
    dnote: null,
    gen: 0,
  };

  const myTurn = () => Boolean(st.view && !st.view.over && st.view.toMove === 'p1' && st.view.tube.total > 0 && !st.busy);
  const legal = () => (myTurn() ? st.view.legal : []);
  const alive = (gen) => () => gen === st.gen;
  const visible = () => app.mode === 'play';
  const ctx = () => ({ human: true, rules: reasons.checked, secrets: false });
  const dealerDue = () => Boolean(st.view && !st.view.over && st.view.toMove !== 'p1');
  const dnoteNow = () => ({ ...(st.dnote ?? { last: '', why: '' }), showWhy: reasons.checked });

  /**
   * The moves that can spend the item in slot i of your tray, each with the
   * slot to pass to act() as `use`. Every Adrenaline move may spend the
   * Adrenaline clicked. A type held in one run of adjacent copies has one
   * move, and any of its copies may be spent. A type held in more than one
   * run has a move per run, each naming the slot of its run's first copy: a
   * click on that slot picks its run, and a click on another copy offers
   * every run, each spending the copy its label names.
   */
  function movesForSlot(i) {
    const token = st.view.seats[0].items[i];
    if (!token) return [];
    const items = legal().filter((m) => m.kind === 'item');
    if (token === 'adr') return items.filter((m) => m.from === 'p2' || m.item === 'adr').map((m) => ({ ...m, use: i }));
    const own = items.filter((m) => m.item === token && !m.from);
    if (new Set(own.map((m) => m.slot)).size <= 1) return own.map((m) => ({ ...m, use: i }));
    const exact = own.filter((m) => m.slot === i);
    if (exact.length) return exact.map((m) => ({ ...m, use: i }));
    return own.map((m) => ({ ...m, use: null }));
  }

  /** The target buttons for the current selection, with their keys. */
  function selectionTargets() {
    const out = { targets: { p1: [], p2: [] }, steal: { p2: {} }, bar: '' };
    if (!st.sel) return out;
    if (st.sel.kind === 'gun') {
      for (const m of legal().filter((x) => x.kind === 'shoot')) out.targets[m.target].push({ ...m, use: null });
      out.bar = '<b>Shotgun in hand.</b> Choose a target: the Dealer (D) or yourself (Y).';
    } else {
      const token = st.sel.token;
      const moves = movesForSlot(st.sel.slot);
      const it = ITEMS[token];
      if (token === 'adr') {
        // A steal is shown on the Dealer's slot it takes from; Adrenaline
        // used alone is shown on your side.
        for (const m of moves) {
          if (m.from === 'p2') out.steal.p2[m.slot] = m;
          else out.targets.p1.push(m);
        }
        const steals = Object.keys(out.steal.p2).length;
        out.bar = steals
          ? `<b>${esc(it.name)}.</b> Choose an item in the Dealer’s tray to take and use now, or use it and take nothing.`
          : `<b>${esc(it.name)}.</b> Nothing in the Dealer’s tray can be taken. You can use it and take nothing.`;
      } else {
        for (const m of moves) out.targets[m.target ?? 'p1'].push(m);
        out.bar = `<b>${esc(it.name)}.</b> ${esc(it.does)}`;
      }
    }
    // D reaches the first choice on the Dealer's side, Y the first on yours.
    const firstSteal = Object.keys(out.steal.p2).map(Number).sort((a, b) => a - b)[0];
    if (out.targets.p2[0]) out.targets.p2[0].key = 'D';
    else if (firstSteal !== undefined) out.steal.p2[firstSteal].key = 'D';
    if (out.targets.p1[0]) out.targets.p1[0].key = 'Y';
    return out;
  }

  /** The slot to pass with move `id` for the current selection, or null. */
  function useFor(id) {
    const sel = selectionTargets();
    const shown = [...sel.targets.p1, ...sel.targets.p2, ...Object.values(sel.steal.p2)];
    return shown.find((m) => m.id === id)?.use ?? null;
  }

  function hintLine() {
    if (!st.hint || st.hint.refused || !st.hint.moves.length || !myTurn()) return '';
    const best = st.hint.moves[0];
    return `Hint: ${best.label}, ${pct(best.win)}`;
  }

  function statusText() {
    const v = st.view;
    if (st.restoring) return st.restoring;
    if (!v) return 'Dealing a round.';
    if (v.over) return v.winner === 'p1' ? 'You win the round.' : 'The Dealer wins the round.';
    if (v.tube.total === 0) return 'Loading the shotgun.';
    if (v.toMove === 'p2') return 'The Dealer is moving.';
    if (st.busy) return 'Your move is playing out.';
    if (st.sel?.kind === 'gun') return 'Shotgun in hand. Choose a target.';
    if (st.sel?.token === 'adr') return 'Adrenaline in hand. Choose an item in the Dealer’s tray, or take nothing.';
    if (st.sel) return `${ITEMS[st.sel.token].name} in hand. Choose where to use it.`;
    return 'Your turn. Pick up the shotgun or an item.';
  }

  function syncStatus() {
    const hint = hintLine();
    status.innerHTML = `<span class="status__main">${esc(statusText())}</span>${hint ? `<span class="status__hint">${esc(hint)}</span>` : ''}`;
    btnHint.disabled = !myTurn() || st.hintBusy;
    btnReplay.disabled = !st.settings;
    // Advise reads positions under Double or Nothing rules only.
    btnAdvise.hidden = st.view?.mode !== 'don';
    btnAdvise.disabled = !myTurn();
  }

  function render() {
    if (app.mode !== 'play') return;
    if (!st.view) {
      syncStatus();
      renderHint();
      return;
    }
    const v = st.view;
    const sel = selectionTargets();
    const moves = legal();
    const items = v.seats.find((s) => s.id === 'p1').items;
    const read = readTube(v.tube, (sh) => sh.knownBy.includes('p1'), st.inv);
    const anySeen = read.shells.some((s) => s.how === 'seen');
    const anyDeduced = read.shells.some((s) => s.how === 'deduced');
    const legend = [anySeen ? 'Y marks a shell you have seen.' : '', anyDeduced ? 'A shell marked = is settled by the counts.' : ''].filter(Boolean).join(' ');
    let note = '';
    if (!v.over && v.toMove !== 'p1') note = v.tube.total === 0 ? 'Loading the shotgun.' : 'The Dealer is moving.';
    renderTable(table, modelFromView(v), {
      context: 'play',
      names: { p1: 'You', p2: 'Dealer' },
      seenMark: { p1: 'Y', p2: 'D' },
      reveal: (sh) => {
        const r = read.shells[sh.offset];
        return { kind: r.kind, how: r.how, by: r.how === 'seen' ? ['p1'] : [] };
      },
      inverted: st.inv,
      chance: { text: chanceText(read.chance), label: 'Your view' },
      dnote: dnoteNow(),
      legend,
      playSeat: 'p1',
      slotEnabled: (i) => Boolean(items[i]) && movesForSlot(i).length > 0,
      gunButton: moves.some((m) => m.kind === 'shoot'),
      selected: st.sel,
      targets: sel.targets,
      stealSlots: sel.steal,
      selectionBar: sel.bar,
      spent: st.spent,
      note,
      overlay: v.over
        ? {
            title: v.winner === 'p1' ? 'You win the round.' : 'The Dealer wins the round.',
            text: `Seed ${v.seed}, ${v.loadNumber} load${v.loadNumber === 1 ? '' : 's'}. A new round uses the next seed.`,
            actions: [
              { act: 'new-round', label: 'New round', primary: true },
              { act: 'replay', label: 'Replay this seed' },
            ],
          }
        : null,
      onAction,
    });
    syncStatus();
    renderHint();
  }

  function renderHint() {
    const base = { title: 'Hint', sub: 'The solver ranks your moves by your chance to win.' };
    if (st.hintBusy) return renderRanking(rankEl, null, { ...base, busy: 'Weighing your moves.' });
    renderRanking(rankEl, myTurn() ? st.hint : null, {
      ...base,
      limit: 5,
      assumptions: 'closed',
      empty: myTurn() ? 'Press Hint, or H, to see how the solver ranks your moves.' : 'Hints are available on your turn.',
    });
  }

  /** Bring the first target into view on a small screen, clear of the dock. */
  function revealTargets() {
    const el = focusIn(table, '.target-btn, .slot.is-steal');
    if (!el) return;
    const r = el.getBoundingClientRect();
    const dock = Number.parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--dock-h')) || 0;
    if (r.top < 0 || r.bottom > window.innerHeight - dock) {
      el.scrollIntoView({ block: 'nearest', behavior: reducedMotion() ? 'auto' : 'smooth' });
    }
  }

  function cancel() {
    const was = st.sel;
    st.sel = null;
    render();
    if (was?.kind === 'gun') focusIn(table, '[data-key="gun"]');
    else if (was) focusIn(table, `[data-key="slot-p1-${was.slot}"]`);
  }

  function pickGun() {
    if (!myTurn() || !legal().some((m) => m.kind === 'shoot')) return;
    st.sel = st.sel?.kind === 'gun' ? null : { kind: 'gun' };
    render();
    if (st.sel) revealTargets();
  }

  function pickSlot(i) {
    if (!myTurn()) return;
    const token = st.view.seats[0].items[i];
    if (!token || !movesForSlot(i).length) return;
    if (st.sel?.kind === 'slot' && st.sel.slot === i) {
      cancel();
      return;
    }
    st.sel = { kind: 'slot', slot: i, token };
    render();
    revealTargets();
  }

  function onAction(act, data) {
    if (act === 'new-round') return nextSeed();
    if (act === 'replay') return replay();
    if (!myTurn()) return;
    if (act === 'gun') return pickGun();
    if (act === 'slot') return pickSlot(Number(data.slot));
    if (act === 'cancel') return cancel();
    if (act === 'move') doMove(data.id, useFor(data.id));
  }

  /** Press the button a key names, if the table shows it. */
  function pressKey(key) {
    const el = table.querySelector(`[aria-keyshortcuts="${key}"]`);
    if (el && !el.disabled) {
      el.click();
      return true;
    }
    return false;
  }

  const hooks = (gen) => ({
    onRestore: (done, total) => {
      if (gen !== st.gen) return;
      st.restoring = done < total ? `Restoring this round: replaying step ${done} of ${total}.` : '';
      syncStatus();
    },
  });

  function checkRestored(view) {
    if (!view || !st.view) return;
    if (!session.sameView(view, st.view)) {
      log.note('The replayed round does not match the one on screen. The table now follows the engine.');
      st.view = view;
    }
  }

  function onEvent(ev) {
    st.dnote = nextDealerNote(st.dnote, ev, ctx());
    if (visible()) patchDnote(table, dnoteNow());
  }

  async function apply(res, gen) {
    checkRestored(res.restored);
    st.restoring = '';
    await playEvents(res.events, { table, log, spent: st.spent, ctx: ctx(), pace: PACE, alive: alive(gen), visible, onEvent });
    if (gen !== st.gen) return;
    st.inv = inversion(st.inv, res.events);
    st.view = res.view;
    render();
  }

  /** The Dealer's turns and the loads, one step at a time with a pause. Stops when Play is left. */
  async function dealerTurns(gen) {
    let guard = 0;
    st.dealerPaused = false;
    while (dealerDue() && guard < 400) {
      guard += 1;
      if (app.mode !== 'play') {
        st.dealerPaused = true;
        return;
      }
      await wait(DEALER_PAUSE);
      if (gen !== st.gen) return;
      if (app.mode !== 'play') {
        st.dealerPaused = true;
        return;
      }
      const res = await session.advance('play', hooks(gen));
      if (gen !== st.gen) return;
      await apply(res, gen);
      if (gen !== st.gen) return;
    }
  }

  async function runDealer(gen) {
    st.busy = true;
    render();
    try {
      await dealerTurns(gen);
    } catch (err) {
      if (gen === st.gen) log.note(`The engine stopped: ${err?.message ?? err}`);
    } finally {
      if (gen === st.gen) {
        st.busy = false;
        render();
        if (st.view?.over && app.mode === 'play') focusIn(table, '[data-key="result-new-round"]');
      }
    }
  }

  async function doMove(id, slot) {
    const gen = st.gen;
    st.sel = null;
    st.hint = null;
    st.busy = true;
    render();
    try {
      const res = await session.act('play', id, slot, hooks(gen));
      if (gen !== st.gen) return;
      await apply(res, gen);
      if (gen !== st.gen) return;
      await dealerTurns(gen);
    } catch (err) {
      if (gen === st.gen) log.note(`The engine stopped: ${err?.message ?? err}`);
    } finally {
      if (gen === st.gen) {
        st.busy = false;
        render();
        if (app.mode === 'play') {
          if (st.view?.over) focusIn(table, '[data-key="result-new-round"]');
          else if (myTurn() && !table.contains(document.activeElement)) focusIn(table, '[data-key="gun"]');
        }
      }
    }
  }

  async function hint() {
    if (!myTurn() || st.hintBusy) return;
    const gen = st.gen;
    st.hintBusy = true;
    render();
    try {
      const { ranking, restored } = await session.rank('play', hooks(gen));
      if (gen !== st.gen) return;
      checkRestored(restored);
      st.hint = ranking;
    } catch (err) {
      if (gen === st.gen) log.note(`The engine stopped while ranking: ${err?.message ?? err}`);
    } finally {
      if (gen === st.gen) {
        st.hintBusy = false;
        st.restoring = '';
        render();
      }
    }
    // On a narrow screen the hint sits below the table; the dock repeats its best move.
  }

  async function newRound(settings) {
    st.gen += 1;
    const gen = st.gen;
    st.sel = null;
    st.hint = null;
    st.spent = [];
    st.inv = false;
    st.dnote = null;
    st.busy = true;
    st.dealerPaused = false;
    log.clear();
    st.view = null;
    render();
    try {
      const view = await session.newRound('play', { ...settings, seat: 'human' });
      if (gen !== st.gen) return;
      st.settings = { ...settings };
      st.view = view;
      render();
      await dealerTurns(gen);
    } catch (err) {
      if (gen === st.gen) log.note(`The engine stopped: ${err?.message ?? err}`);
    } finally {
      if (gen === st.gen) {
        st.busy = false;
        render();
      }
    }
  }

  function fromForm() {
    newRound(readRoundForm(form));
  }

  function nextSeed() {
    const seed = form.elements.seed;
    seed.value = String((Number(st.settings?.seed ?? seed.value) || 0) + 1);
    fromForm();
  }

  function replay() {
    if (!st.settings) return;
    writeRoundForm(form, st.settings);
    newRound(st.settings);
  }

  btnHint.addEventListener('click', hint);
  btnAdvise.addEventListener('click', () => {
    if (myTurn() && st.view.mode === 'don') openInAdvise?.(st.view.notation);
  });
  btnReplay.addEventListener('click', replay);
  reasons.addEventListener('change', () => {
    log.setContext(ctx());
    render();
  });
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    fromForm();
  });
  fillGameSelect(form.elements.game, 'don');
  form.elements.game.addEventListener('change', () => syncGameNote(form));
  syncGameNote(form);

  const docKey = (e) => {
    if (app.mode !== 'play' || e.ctrlKey || e.metaKey || e.altKey || e.defaultPrevented) return;
    if (e.key === 'Escape') {
      if (st.sel) {
        e.preventDefault();
        cancel();
      }
      return;
    }
    if (e.target.closest?.('input, select, textarea, [contenteditable], dialog')) return;
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    let done = false;
    if (/^[1-8]$/.test(k)) {
      pickSlot(Number(k) - 1);
      done = true;
    } else if (k === 'g') {
      pickGun();
      done = true;
    } else if (k === 'd') done = pressKey('D');
    else if (k === 'y') done = pressKey('Y');
    else if (k === 'h') {
      hint();
      done = true;
    }
    if (done) e.preventDefault();
  };

  return {
    enter() {
      document.addEventListener('keydown', docKey);
      log.setContext(ctx());
      if (!session.hasRound('play')) {
        fromForm();
        return;
      }
      render();
      // The Dealer stops when Play is left and picks up where it was.
      if (dealerDue() && !st.busy) runDealer(st.gen);
    },
    leave() {
      st.sel = null;
      document.removeEventListener('keydown', docKey);
    },
    state: st,
  };
}
