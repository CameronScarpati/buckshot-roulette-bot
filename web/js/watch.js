// WATCH: the solver (seat 1) plays the Dealer. Before each solver move the
// panel shows its three best moves; the move it plays keeps a Played tag while
// the Dealer answers. The Dealer's last action and its rule sit on the table.

import { playEvents } from './director.js';
import { ui } from './icons.js';
import { chanceText, inversion, readTube } from './knowledge.js';
import { pct } from './vocab.js';
import { Log, nextDealerNote } from './log.js';
import { renderRanking } from './ranking.js';
import * as session from './session.js';
import { app, fillGameSelect, readRoundForm, syncGameNote, wait, writeRoundForm } from './state.js';
import { modelFromView, patchDnote, renderTable } from './table.js';

const SPEEDS = {
  slow: { gap: 2000, pace: 1000 },
  normal: { gap: 1100, pace: 650 },
  fast: { gap: 380, pace: 300 },
};

export function createWatch({ table, panel, openInAdvise }) {
  // The round row sits under the table, outside the panel.
  const $ = (sel) => panel.querySelector(sel) ?? document.querySelector(sel);
  const btnPlay = $('#w-play');
  const btnStep = $('#w-step');
  const spectator = $('#w-spectator');
  const form = $('#w-form');
  const btnReplay = $('#w-replay');
  const btnAdvise = $('#w-to-advise');
  const status = $('#w-status');
  const next = $('#w-next');
  const rankEl = $('#w-rank');
  const log = new Log($('#w-log'));

  const st = {
    view: null,
    settings: null,
    ranking: null, // the solver's ranking for the move it is about to play
    played: null, // {ranking, id}: its last decision, kept while the Dealer moves
    rankBusy: false,
    playing: false,
    busy: false,
    restoring: '',
    spent: [],
    inv: false,
    dnote: null,
    gen: 0,
  };

  const speed = () => SPEEDS[panel.querySelector('input[name="w-speed"]:checked')?.value ?? 'normal'];
  const alive = (gen) => () => gen === st.gen;
  const visible = () => app.mode === 'watch';
  const ctx = () => ({ human: false, rules: true, secrets: spectator.checked });
  const solverToMove = () => Boolean(st.view && !st.view.over && st.view.toMove === 'p1' && st.view.tube.total > 0);

  /** What the reader may see: shells both seats know, or every shell a seat knows in spectator view. */
  function canSee(sh) {
    if (spectator.checked) return sh.knownBy.length > 0;
    return sh.knownBy.includes('p1') && sh.knownBy.includes('p2');
  }

  function tableOptions() {
    const v = st.view;
    const secrets = spectator.checked;
    const read = readTube(v.tube, canSee, st.inv);
    const viewLabel = secrets ? 'Spectator view' : 'Public view';
    const over = v.over
      ? {
          title: v.winner === 'p1' ? 'The solver wins the round.' : 'The Dealer wins the round.',
          text: `Seed ${v.seed}, ${v.loadNumber} load${v.loadNumber === 1 ? '' : 's'}. A new round uses the next seed.`,
          actions: [
            { act: 'new-round', label: 'New round', primary: true },
            { act: 'replay', label: 'Replay this seed' },
          ],
        }
      : null;
    let note = '';
    if (!v.over && v.tube.total === 0) note = v.loadNumber ? 'The tube is empty. The next step reloads it.' : 'The next step loads the shotgun.';
    const anyDeduced = read.shells.some((s) => s.how === 'deduced');
    const legend = [
      secrets ? 'Spectator view: S marks a shell the solver has seen, D one the Dealer has seen.' : 'Public view: a shell shows once both seats have seen it.',
      anyDeduced ? 'A shell marked = is settled by the counts.' : '',
    ]
      .filter(Boolean)
      .join(' ');
    return {
      context: 'watch',
      names: { p1: 'Solver', p2: 'Dealer' },
      seenMark: { p1: 'S', p2: 'D' },
      reveal: (sh) => {
        const r = read.shells[sh.offset];
        return { kind: r.kind, how: r.how, by: r.how === 'seen' ? sh.knownBy : [] };
      },
      inverted: st.inv,
      chance: { text: chanceText(read.chance), label: viewLabel },
      dnote: { ...(st.dnote ?? { last: '', why: '' }), showWhy: true },
      legend,
      spent: st.spent,
      overlay: over,
      note,
      onAction: (act) => {
        if (act === 'new-round') nextSeed();
        if (act === 'replay') replay();
      },
    };
  }

  function renderRank() {
    const v = st.view;
    const base = { title: 'The solver weighs', sub: 'Its top three moves, by its chance to win the round.' };
    if (!v) return renderRanking(rankEl, null, { ...base, empty: 'Dealing a round.' });
    if (st.rankBusy) return renderRanking(rankEl, null, { ...base, busy: 'Weighing the moves.' });
    if (st.ranking && solverToMove()) {
      return renderRanking(rankEl, st.ranking, { ...base, limit: 3, next: 'Plays next', assumptions: 'closed' });
    }
    if (st.played) {
      return renderRanking(rankEl, st.played.ranking, {
        ...base,
        sub: v.over ? 'Its last decision in this round.' : 'Its last decision, kept here while the Dealer answers.',
        limit: 3,
        playedId: st.played.id,
        assumptions: 'closed',
      });
    }
    let empty = 'Press Step to see the solver weigh its moves.';
    if (v.over) empty = 'The round is over.';
    else if (v.tube.total === 0) empty = 'The tube is empty. The next step loads it.';
    else if (v.toMove === 'p2') empty = 'The Dealer is to move. It follows its script and does not weigh moves. Its rule shows on the table.';
    return renderRanking(rankEl, null, { ...base, empty });
  }

  function statusText() {
    const v = st.view;
    if (st.restoring) return st.restoring;
    if (!v) return 'Dealing a round.';
    if (v.over) return v.winner === 'p1' ? 'The solver wins the round.' : 'The Dealer wins the round.';
    const head = st.playing ? 'Playing. ' : '';
    if (v.tube.total === 0) return `${head}${v.loadNumber ? 'The tube is empty. The next step reloads it.' : 'The next step loads the shotgun.'}`;
    if (v.toMove === 'p2') return `${head}The Dealer is to move. The next step plays its action.`;
    if (st.rankBusy) return `${head}The solver is weighing its moves.`;
    return `${head}The solver is to move. The next step plays its best move.`;
  }

  function syncControls() {
    const over = Boolean(st.view?.over);
    btnPlay.setAttribute('aria-pressed', String(st.playing));
    btnPlay.innerHTML = st.playing ? `${ui.pauseIcon}<span>Pause</span>` : `${ui.playIcon}<span>Play</span>`;
    btnPlay.disabled = !st.view || over;
    btnStep.disabled = !st.view || st.playing || st.busy || st.rankBusy || over;
    btnReplay.disabled = !st.settings;
    // Advise reads positions under Double or Nothing rules only.
    btnAdvise.hidden = st.view?.mode !== 'don';
    btnAdvise.disabled = !solverToMove() || st.busy;
    status.textContent = statusText();
    next.textContent = nextText();
  }

  /** One short line for the phone dock: the move the solver plays next, or the status. */
  function nextText() {
    const best = st.ranking?.moves?.[0];
    if (!st.restoring && best && !st.rankBusy && solverToMove()) return `Plays next: ${best.label}, ${pct(best.win)}`;
    return statusText().replace(/^Playing\. /, '');
  }

  function render() {
    if (app.mode !== 'watch' || !st.view) {
      syncControls();
      return;
    }
    renderTable(table, modelFromView(st.view), tableOptions());
    renderRank();
    syncControls();
  }

  const hooks = (gen) => ({
    onRestore: (done, total) => {
      if (gen !== st.gen) return;
      st.restoring = done < total ? `Restoring this round: replaying step ${done} of ${total}.` : '';
      syncControls();
    },
  });

  /** After a replay, check the engine gave back the round on screen. */
  function checkRestored(view) {
    if (!view || !st.view) return;
    if (!session.sameView(view, st.view)) {
      log.note('The replayed round does not match the one on screen. The table now follows the engine.');
      st.view = view;
    }
  }

  async function fetchRank(gen) {
    if (!solverToMove() || app.mode !== 'watch') return;
    st.rankBusy = true;
    renderRank();
    syncControls();
    try {
      const { ranking, restored } = await session.rank('watch', hooks(gen));
      if (gen !== st.gen) return;
      checkRestored(restored);
      st.ranking = ranking;
    } catch (err) {
      if (gen !== st.gen) return;
      st.ranking = null;
      log.note(`The engine stopped while ranking: ${err?.message ?? err}`);
      st.playing = false;
    } finally {
      if (gen === st.gen) {
        st.rankBusy = false;
        st.restoring = '';
        renderRank();
        syncControls();
      }
    }
  }

  async function step() {
    if (st.busy || st.rankBusy || !st.view || st.view.over) return;
    const gen = st.gen;
    st.busy = true;
    syncControls();
    try {
      let res;
      if (solverToMove()) {
        if (!st.ranking) await fetchRank(gen);
        if (gen !== st.gen) return;
        const best = st.ranking && !st.ranking.refused ? st.ranking.moves[0] : null;
        if (best) {
          st.played = { ranking: st.ranking, id: best.id };
          st.ranking = null;
          renderRank();
          res = await session.act('watch', best.id, null, hooks(gen));
        } else {
          res = await session.advance('watch', hooks(gen));
        }
      } else {
        res = await session.advance('watch', hooks(gen));
      }
      if (gen !== st.gen) return;
      checkRestored(res.restored);
      st.restoring = '';
      await playEvents(res.events, {
        table,
        log,
        spent: st.spent,
        ctx: ctx(),
        pace: speed().pace,
        alive: alive(gen),
        visible,
        onEvent: (ev) => {
          st.dnote = nextDealerNote(st.dnote, ev, ctx());
          if (visible()) patchDnote(table, { ...st.dnote, showWhy: true });
        },
      });
      if (gen !== st.gen) return;
      st.inv = inversion(st.inv, res.events);
      st.view = res.view;
      render();
      await fetchRank(gen);
    } catch (err) {
      if (gen !== st.gen) return;
      log.note(`The engine stopped: ${err?.message ?? err}`);
      st.playing = false;
    } finally {
      if (gen === st.gen) {
        st.busy = false;
        st.restoring = '';
        syncControls();
      }
    }
  }

  async function loop() {
    const gen = st.gen;
    while (st.playing && gen === st.gen && app.mode === 'watch' && st.view && !st.view.over) {
      await step();
      if (!st.playing || gen !== st.gen) break;
      await wait(speed().gap);
    }
    if (gen === st.gen) {
      st.playing = false;
      syncControls();
    }
  }

  function setPlaying(on) {
    if (on && (!st.view || st.view.over)) return;
    st.playing = on;
    syncControls();
    if (on) loop();
  }

  async function newRound(settings) {
    st.gen += 1;
    const gen = st.gen;
    st.playing = false;
    st.busy = true;
    st.ranking = null;
    st.played = null;
    st.spent = [];
    st.inv = false;
    st.dnote = null;
    st.view = null;
    log.clear();
    syncControls();
    try {
      const view = await session.newRound('watch', { ...settings, seat: 'solver' });
      if (gen !== st.gen) return;
      st.settings = { ...settings };
      st.view = view;
      render();
    } catch (err) {
      if (gen !== st.gen) return;
      log.note(`The engine could not deal the round: ${err?.message ?? err}`);
    } finally {
      if (gen === st.gen) {
        st.busy = false;
        syncControls();
      }
    }
    if (gen === st.gen && st.view && st.view.tube.total === 0 && !st.view.over) await step();
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

  btnPlay.addEventListener('click', () => setPlaying(!st.playing));
  btnStep.addEventListener('click', () => step());
  btnReplay.addEventListener('click', replay);
  btnAdvise.addEventListener('click', () => {
    if (solverToMove() && !st.busy && st.view.mode === 'don') openInAdvise?.(st.view.notation);
  });
  spectator.addEventListener('change', () => {
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
  syncControls();

  // P plays or pauses, S steps.
  const docKey = (e) => {
    if (app.mode !== 'watch' || e.ctrlKey || e.metaKey || e.altKey || e.defaultPrevented) return;
    if (e.target.closest?.('input, select, textarea, [contenteditable], dialog')) return;
    const k = e.key.toLowerCase();
    if (k === 'p') {
      e.preventDefault();
      setPlaying(!st.playing);
    } else if (k === 's' && !btnStep.disabled) {
      e.preventDefault();
      step();
    }
  };
  document.addEventListener('keydown', docKey);

  return {
    enter() {
      log.setContext(ctx());
      if (!session.hasRound('watch')) {
        fromForm();
        return;
      }
      // The round is kept: show it as it was. The bridge gets it back by
      // replay only when the next call needs it.
      render();
      if (solverToMove() && !st.ranking && !st.busy) fetchRank(st.gen);
    },
    leave() {
      st.playing = false;
      syncControls();
    },
    state: st,
  };
}
