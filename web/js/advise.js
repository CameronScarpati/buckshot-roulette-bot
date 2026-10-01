// ADVISE: set up any position on the table and ask for the odds. The table
// becomes an editor; the panel holds the notation, the options and the answer.

import { itemSvg } from './icons.js';
import { chanceText, readTube } from './knowledge.js';
import { clonePosition, formatCommand, formatNotation, packTray, parseNotation, readPaste, validatePosition } from './notation.js';
import { renderRanking } from './ranking.js';
import * as session from './session.js';
import { app, reducedMotion } from './state.js';
import { focusIn, renderTable } from './table.js';
import { ITEMS, ITEM_TOKENS, esc } from './vocab.js';

const DEFAULT = 'p1=2/4[mg,saw] p2=4/4[beer,cuff] tube=2L3B turn=p1';

const EXAMPLES = [
  { label: 'Glass and saw against beer and cuffs', text: DEFAULT },
  { label: 'You have seen a live chamber', text: 'p1=3/4[cig,saw] p2=2/4[mg] tube=2L2B turn=p1 known=p1:0L' },
  { label: 'Only the Dealer has seen the chamber', text: 'p1=4/4[mg,phone] p2=3/4[saw] tube=3L2B turn=p1 known=p2:0L' },
  { label: 'Last shell, one charge each', text: 'p1=1/4[inv] p2=1/4[adr,med] tube=1L0B turn=p1' },
  {
    label: 'A command line: the other seat to move, against the solver',
    text: './build/advisor --position "p1=3/4[saw] p2=3/4[mg,beer] tube=2L2B turn=p2" --reloads 0 --opponent solver',
  },
];

const BLOCK_DEALER =
  'The Dealer is to move and follows its script, so it has no choice to rank. Choose the solver as the opponent, or give the turn to you.';

export function createAdvise({ table, panel }) {
  const $ = (sel) => panel.querySelector(sel);
  const paste = $('#a-paste');
  const pasteErr = $('#a-paste-errors');
  const pasteNotes = $('#a-paste-notes');
  const notationOut = $('#a-notation');
  const commandOut = $('#a-command');
  const copyStatus = { notation: $('#a-copied'), command: $('#a-copied-cmd') };
  const problems = $('#a-problems');
  const btnAsk = $('#a-ask');
  const askWhy = $('#a-ask-why');
  const warn = $('#a-reload-warn');
  const answerEl = $('#a-answer');
  const examples = $('#a-examples');
  const dlgItem = document.getElementById('dlg-item');
  const dlgShell = document.getElementById('dlg-shell');

  const st = {
    pos: parseNotation(DEFAULT).pos,
    answer: null,
    answeredFor: null,
    busy: false,
    busySince: 0,
    gen: 0,
    editSlot: null,
    editShell: null,
  };

  const opponent = () => panel.querySelector('input[name="a-opp"]:checked')?.value ?? 'dealer';
  const reloads = () => Number(panel.querySelector('input[name="a-reloads"]:checked')?.value ?? 0);
  const names = () => ({ p1: 'You', p2: opponent() === 'dealer' ? 'Dealer' : 'Opponent' });
  const marks = () => ({ p1: 'Y', p2: opponent() === 'dealer' ? 'D' : 'O' });
  const total = () => st.pos.live + st.pos.blank;
  const notation = () => formatNotation(st.pos);
  const command = () => formatCommand(notation(), { opponent: opponent(), reloads: reloads() });
  const askKey = () => `${notation()}|${opponent()}|${reloads()}`;

  function setRadio(name, value) {
    const el = panel.querySelector(`input[name="${name}"][value="${value}"]`);
    if (el) el.checked = true;
  }

  /** Why Ask is off, or '' when it is on. */
  function blocker() {
    if (st.busy) return '';
    const list = validatePosition(st.pos);
    if (list.length) return `Ask is off until the position is fixed: ${list.length === 1 ? 'one problem is' : `${list.length} problems are`} listed above.`;
    if (st.pos.turn === 'p2' && opponent() === 'dealer') return BLOCK_DEALER;
    return '';
  }

  /** The position as a table model. */
  function model() {
    const p = st.pos;
    const seat = (id, i) => {
      const s = p.seats[i];
      const restraint = p.cuffed === id ? 'cuffed' : p.skipped === id ? 'lost a turn' : null;
      return { id, name: names()[id], charges: s.charges, max: s.max, faded: 0, items: s.items, restraint };
    };
    const shells = [];
    for (let off = 0; off < total(); off += 1) {
      const by = ['p1', 'p2'].filter((s) => p.known[s][off]);
      const known = by.length ? p.known[by[0]][off] : null;
      shells.push({ offset: off, known, knownBy: by });
    }
    return {
      info: ['Position editor', `${total()} shell${total() === 1 ? '' : 's'}`, `Reloads ${reloads()}`],
      seats: { p1: seat('p1', 0), p2: seat('p2', 1) },
      tube: { live: p.live, blank: p.blank, total: total(), sawed: p.sawed, shells },
      toMove: p.turn,
      over: false,
      winner: null,
    };
  }

  function renderBoard() {
    if (app.mode !== 'advise') return;
    const p = st.pos;
    const m = model();
    const mover = p.turn;
    const read = readTube(m.tube, (sh) => sh.knownBy.includes(mover), p.inverted);
    const moverView = mover === 'p1' ? 'Your view' : `${names().p2}’s view`;
    const anyDeduced = read.shells.some((s, i) => s.how === 'deduced' && !m.tube.shells[i].known);
    renderTable(table, m, {
      context: 'advise',
      names: names(),
      seenMark: marks(),
      reveal: (sh) => {
        if (sh.known) return { kind: sh.known, by: sh.knownBy, how: 'seen' };
        const r = read.shells[sh.offset];
        return r.how === 'deduced' ? { kind: r.kind, by: [], how: 'deduced' } : { kind: null, by: [], how: null };
      },
      chance: validatePosition(p).length ? null : { text: chanceText(read.chance), label: moverView },
      legend: `Select a shell to set what it is and who has seen it: ${marks().p1} for you, ${marks().p2} for the ${names().p2 === 'Dealer' ? 'Dealer' : 'opponent'}.${anyDeduced ? ' A shell marked = is settled by the counts for the seat to move.' : ''}`,
      edit: {
        restraint: { p1: p.cuffed === 'p1' ? 'cuffed' : p.skipped === 'p1' ? 'skipped' : '', p2: p.cuffed === 'p2' ? 'cuffed' : p.skipped === 'p2' ? 'skipped' : '' },
        sawed: p.sawed,
        inverted: p.inverted,
        restraintUsed: p.restraintUsed,
      },
      onAction,
    });
  }

  function renderSide() {
    notationOut.textContent = notation();
    commandOut.innerHTML = `<span class="nw">./build/advisor</span> <span class="nw">--position</span> "${esc(notation())}" <span class="nw">--reloads ${reloads()}</span> <span class="nw">--opponent ${opponent()}</span>`;
    const list = validatePosition(st.pos);
    problems.hidden = list.length === 0;
    problems.innerHTML = list.length
      ? `<p class="problems__title">Fix this before asking:</p><ul>${list.map((x) => `<li>${esc(x)}</li>`).join('')}</ul>`
      : '';
    const why = blocker();
    btnAsk.disabled = st.busy || Boolean(why);
    btnAsk.setAttribute('aria-busy', String(st.busy));
    btnAsk.querySelector('.btn__label').textContent = st.busy ? 'Working' : 'Ask for the odds';
    askWhy.textContent = why;
    askWhy.hidden = !why;
    warn.hidden = reloads() === 0;
    renderAnswer();
  }

  function renderAnswer() {
    const mover = st.pos.turn;
    const base = {
      title: 'Answer',
      sub: mover === 'p1' ? 'Your moves, best first, by your chance of winning the round.' : `The ${names().p2 === 'Dealer' ? 'Dealer' : 'opponent'}’s moves, best first, by its chance of winning the round.`,
    };
    if (st.busy) {
      return renderRanking(answerEl, null, {
        ...base,
        busy: reloads() ? 'Working through the next load as well. This can take a while.' : 'Working.',
        busySince: st.busySince,
      });
    }
    const stale = Boolean(st.answer) && st.answeredFor !== askKey();
    renderRanking(answerEl, st.answer, {
      ...base,
      assumptions: 'open',
      stale,
      empty: 'Set up the position on the table, then ask for the odds.',
    });
  }

  function render() {
    renderBoard();
    renderSide();
  }

  function clamp(n, lo, hi) {
    return Math.max(lo, Math.min(hi, n));
  }

  function trimKnown() {
    for (const s of ['p1', 'p2']) {
      for (const off of Object.keys(st.pos.known[s])) if (Number(off) >= total()) delete st.pos.known[s][off];
    }
  }

  function onAction(act, data, el) {
    const p = st.pos;
    const seat = data.seat === 'p2' ? p.seats[1] : p.seats[0];
    switch (act) {
      case 'ch-':
        seat.charges = clamp(seat.charges - 1, 1, seat.max);
        break;
      case 'ch+':
        seat.charges = clamp(seat.charges + 1, 1, seat.max);
        break;
      case 'max-':
        seat.max = clamp(seat.max - 1, 1, 8);
        seat.charges = Math.min(seat.charges, seat.max);
        break;
      case 'max+':
        seat.max = clamp(seat.max + 1, 1, 8);
        break;
      case 'live-':
        p.live = Math.max(0, p.live - 1);
        trimKnown();
        break;
      case 'live+':
        if (total() < 8) p.live += 1;
        break;
      case 'blank-':
        p.blank = Math.max(0, p.blank - 1);
        trimKnown();
        break;
      case 'blank+':
        if (total() < 8) p.blank += 1;
        break;
      case 'restraint': {
        const id = data.seat;
        if (p.cuffed === id) p.cuffed = null;
        if (p.skipped === id) p.skipped = null;
        if (el.value === 'cuffed') p.cuffed = id;
        if (el.value === 'skipped') p.skipped = id;
        break;
      }
      case 'sawed':
        p.sawed = el.checked;
        break;
      case 'inverted':
        p.inverted = el.checked;
        break;
      case 'restraintused':
        p.restraintUsed = el.checked;
        break;
      case 'turn':
        p.turn = el.value === 'p2' ? 'p2' : 'p1';
        break;
      case 'edit-slot':
        openItemDialog(data.seat, Number(data.slot));
        return;
      case 'edit-shell':
        openShellDialog(Number(data.offset));
        return;
      default:
        return;
    }
    render();
  }

  // Item dialog: nine items and "Empty the slot".
  function openItemDialog(seatId, slot) {
    st.editSlot = { seatId, slot };
    const current = st.pos.seats[seatId === 'p2' ? 1 : 0].items[slot];
    dlgItem.querySelector('.dlg__title').textContent = `${names()[seatId]}, slot ${slot + 1}`;
    const grid = dlgItem.querySelector('.dlg__items');
    grid.innerHTML = ITEM_TOKENS.map(
      (t) => `<button type="submit" value="${t}" class="pick${t === current ? ' is-current' : ''}" aria-pressed="${t === current}">
        <span class="pick__icon">${itemSvg(t)}</span><span class="pick__name">${esc(ITEMS[t].name)}</span><span class="pick__does">${esc(ITEMS[t].does)}</span></button>`,
    ).join('');
    dlgItem.querySelector('[value="none"]').disabled = !current;
    dlgItem.returnValue = '';
    dlgItem.showModal();
    (grid.querySelector('.is-current') ?? grid.querySelector('button'))?.focus();
  }

  dlgItem.addEventListener('close', () => {
    const e = st.editSlot;
    st.editSlot = null;
    if (!e) return;
    const v = dlgItem.returnValue;
    let at = e.slot;
    if (v && v !== 'cancel') {
      // A written position lists a seat's items in order, so the tray keeps
      // them packed from slot 1, as the engine reads them.
      const seat = st.pos.seats[e.seatId === 'p2' ? 1 : 0];
      seat.items[e.slot] = v === 'none' ? null : v;
      const before = seat.items.slice(0, e.slot).filter(Boolean).length;
      seat.items = packTray(seat.items);
      at = v === 'none' ? Math.min(e.slot, seat.items.filter(Boolean).length) : before;
      render();
    }
    focusIn(table, `[data-key="slot-${e.seatId}-${at}"]`);
  });

  // Shell dialog: unknown, live or blank, and which seats have seen it.
  function openShellDialog(off) {
    st.editShell = off;
    const p = st.pos;
    const kind = p.known.p1[off] ?? p.known.p2[off] ?? 'unknown';
    dlgShell.querySelector('.dlg__title').textContent = off === 0 ? 'Shell 1, the chamber' : `Shell ${off + 1}`;
    const f = dlgShell.querySelector('form');
    f.elements.kind.value = kind;
    f.elements.seenP1.checked = Boolean(p.known.p1[off]);
    f.elements.seenP2.checked = Boolean(p.known.p2[off]);
    dlgShell.querySelector('[data-name="p1"]').textContent = names().p1;
    dlgShell.querySelector('[data-name="p2"]').textContent = names().p2 === 'Dealer' ? 'The Dealer' : 'The opponent';
    syncShellDialog();
    dlgShell.returnValue = '';
    dlgShell.showModal();
    f.querySelector(`input[name="kind"][value="${kind}"]`)?.focus();
  }

  function syncShellDialog() {
    const f = dlgShell.querySelector('form');
    const unknown = f.elements.kind.value === 'unknown';
    f.elements.seenP1.disabled = unknown;
    f.elements.seenP2.disabled = unknown;
    dlgShell.querySelector('.dlg__seen').classList.toggle('is-off', unknown);
  }
  dlgShell.querySelector('form').addEventListener('change', syncShellDialog);

  dlgShell.addEventListener('close', () => {
    const off = st.editShell;
    st.editShell = null;
    if (off === null) return;
    if (dlgShell.returnValue === 'save') {
      const f = dlgShell.querySelector('form');
      const kind = f.elements.kind.value;
      const p = st.pos;
      delete p.known.p1[off];
      delete p.known.p2[off];
      if (kind !== 'unknown') {
        let a = f.elements.seenP1.checked;
        const b = f.elements.seenP2.checked;
        if (!a && !b) a = true; // A known shell has been seen by someone; default to you.
        if (a) p.known.p1[off] = kind;
        if (b) p.known.p2[off] = kind;
      }
      render();
    }
    focusIn(table, `[data-key="shell-${off}"]`);
  });

  // Paste: a position, or a whole advisor command line. Every error found is
  // listed; nothing is filled until the text reads cleanly.
  function showList(el, title, list) {
    el.hidden = list.length === 0;
    el.innerHTML = list.length ? `<p class="errors__title">${esc(title)}</p><ul>${list.map((x) => `<li>${esc(x)}</li>`).join('')}</ul>` : '';
  }

  function fillFrom(text) {
    const r = readPaste(text);
    const errors = [...r.errors];
    let parsed = null;
    if (r.notation !== null) {
      parsed = parseNotation(r.notation);
      errors.push(...parsed.errors);
    }
    const unique = [...new Set(errors)];
    if (unique.length || !parsed) {
      showList(pasteErr, `Nothing was filled. ${unique.length === 1 ? 'One problem' : `${unique.length} problems`} in the text:`, unique);
      showList(pasteNotes, 'Also noted:', r.notes);
      return false;
    }
    showList(pasteErr, '', []);
    showList(pasteNotes, 'Filled. Also noted:', r.notes);
    st.pos = clonePosition(parsed.pos);
    if (r.opponent) setRadio('a-opp', r.opponent);
    if (r.reloads !== null) setRadio('a-reloads', String(r.reloads));
    render();
    return true;
  }

  $('#a-paste-form').addEventListener('submit', (e) => {
    e.preventDefault();
    if (fillFrom(paste.value)) paste.value = '';
  });
  paste.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      $('#a-paste-form').requestSubmit();
    }
  });

  examples.innerHTML = EXAMPLES.map(
    (x, i) => `<li><button type="button" class="linkish" data-ex="${i}">${esc(x.label)}</button><code>${esc(x.text)}</code></li>`,
  ).join('');
  examples.addEventListener('click', (e) => {
    const b = e.target.closest('[data-ex]');
    if (!b) return;
    fillFrom(EXAMPLES[Number(b.dataset.ex)].text);
  });

  // Copy.
  async function copy(text, what) {
    let ok = false;
    try {
      await navigator.clipboard.writeText(text);
      ok = true;
    } catch {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.setAttribute('readonly', '');
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      try {
        ok = document.execCommand('copy');
      } catch {
        ok = false;
      }
      ta.remove();
    }
    const out = copyStatus[what];
    out.textContent = ok ? `Copied the ${what}.` : `Could not copy. Select the ${what} and copy it by hand.`;
    clearTimeout(out._t);
    out._t = setTimeout(() => (out.textContent = ''), 3000);
  }
  $('#a-copy-notation').addEventListener('click', () => copy(notation(), 'notation'));
  $('#a-copy-command').addEventListener('click', () => copy(command(), 'command'));

  panel.addEventListener('change', (e) => {
    if (e.target.matches('input[name="a-opp"], input[name="a-reloads"]')) render();
  });

  async function ask() {
    if (st.busy || blocker()) return;
    st.gen += 1;
    const gen = st.gen;
    const key = askKey();
    st.busy = true;
    st.busySince = Date.now();
    renderSide();
    try {
      const r = await session.advise(notation(), { opponent: opponent(), reloads: reloads() });
      if (gen !== st.gen) return;
      st.answer = r;
      st.answeredFor = key;
    } catch (err) {
      if (gen !== st.gen) return;
      st.answer = { mover: st.pos.turn, opponent: opponent(), moves: [], stopped: null, assumptions: [], refused: `The engine stopped: ${err?.message ?? err}` };
      st.answeredFor = key;
    } finally {
      if (gen === st.gen) {
        st.busy = false;
        renderSide();
        const title = document.getElementById('a-answer-title');
        if (app.mode === 'advise' && title) {
          title.focus({ preventScroll: true });
          const r = answerEl.getBoundingClientRect();
          if (r.top < 0 || r.top > window.innerHeight - 80) answerEl.scrollIntoView({ block: 'start', behavior: reducedMotion() ? 'auto' : 'smooth' });
        }
      }
    }
  }
  btnAsk.addEventListener('click', ask);

  // Ctrl+Enter or Cmd+Enter asks from anywhere in Advise.
  document.addEventListener('keydown', (e) => {
    if (app.mode !== 'advise' || e.key !== 'Enter' || !(e.ctrlKey || e.metaKey)) return;
    if (e.target.closest?.('dialog')) return;
    e.preventDefault();
    ask();
  });

  return {
    enter() {
      render();
    },
    leave() {},
    /** Fill the editor with a written position from a round, against the Dealer's script. */
    open(text) {
      setRadio('a-opp', 'dealer');
      setRadio('a-reloads', '0');
      fillFrom(text);
    },
    state: st,
  };
}
