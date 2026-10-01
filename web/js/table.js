// The table: the Dealer's side at the top, the player's side at the bottom,
// the shotgun and the shells across the middle. One render function serves all
// three modes; the options say what is interactive and what may be revealed.

import { ITEMS, esc, listJoin } from './vocab.js';
import { boltSvg, gunSvg, itemSvg, ui } from './icons.js';

/** Attach the delegated listeners once. Each render swaps the handler. */
export function mountTable(root) {
  root.addEventListener('click', (e) => {
    const el = e.target.closest('[data-act]');
    if (!el || !root.contains(el)) return;
    if (el.matches('input, select')) return;
    root._onAction?.(el.dataset.act, el.dataset, el, e);
  });
  root.addEventListener('change', (e) => {
    const el = e.target.closest('[data-act]');
    if (!el || !el.matches('input, select')) return;
    root._onAction?.(el.dataset.act, el.dataset, el, e);
  });
  root.addEventListener('keydown', (e) => {
    root._onKey?.(e);
  });
}

/** Turn a bridge View into the table model. */
export function modelFromView(view) {
  const byId = Object.fromEntries(view.seats.map((s) => [s.id, s]));
  return {
    info: [view.stageLabel, view.loadNumber ? `Load ${view.loadNumber}` : 'Not loaded', `Seed ${view.seed}`],
    seats: byId,
    tube: view.tube,
    toMove: view.toMove,
    over: view.over,
    winner: view.winner,
  };
}

function readout(seat, label) {
  const bolts = [];
  for (let i = 1; i <= seat.max; i += 1) {
    const on = i <= seat.charges;
    const faded = i <= seat.faded;
    bolts.push(`<span class="bolt${on ? ' is-on' : ''}${faded ? ' is-faded' : ''}">${boltSvg()}</span>`);
  }
  const fadedNote = seat.faded ? `, and a seat on its last ${seat.faded === 1 ? 'charge' : `${seat.faded} charges`} cannot heal` : '';
  const low = seat.charges <= 1 ? ' is-low' : '';
  return `
    <div class="readout${low}" role="img" aria-label="${esc(label)}: ${seat.charges} of ${seat.max} charges${fadedNote}">
      <span class="readout__bolts">${bolts.join('')}</span>
      <span class="readout__num"><b>${seat.charges}</b><span>/${seat.max}</span></span>
    </div>`;
}

function stepper(act, seatId, value, label) {
  const seatAttr = seatId ? ` data-seat="${seatId}"` : '';
  return `
    <span class="stepper" role="group" aria-label="${esc(label)}">
      <button type="button" class="stepper__btn" data-act="${act}-"${seatAttr} data-key="${act}-${seatId}-dec" aria-label="${esc(label)}: one fewer">&minus;</button>
      <output class="stepper__val">${value}</output>
      <button type="button" class="stepper__btn" data-act="${act}+"${seatAttr} data-key="${act}-${seatId}-inc" aria-label="${esc(label)}: one more">+</button>
    </span>`;
}

function slotHtml(seatId, i, token, o) {
  const it = token ? ITEMS[token] : null;
  const name = o.names[seatId];
  const inner = token
    ? `<span class="slot__icon">${itemSvg(token)}</span><span class="slot__label">${esc(it.short)}</span>`
    : '';
  const key = `slot-${seatId}-${i}`;
  if (o.edit) {
    return `<button type="button" class="slot slot--edit${token ? '' : ' is-empty'}" data-act="edit-slot" data-seat="${seatId}" data-slot="${i}" data-key="${key}"
      aria-label="${esc(name)}, slot ${i + 1}: ${token ? esc(it.name) : 'empty'}. Choose an item.">${inner || '<span class="slot__plus" aria-hidden="true">+</span>'}</button>`;
  }
  const steal = o.stealSlots?.[seatId]?.[i];
  if (steal) {
    const shortcut = steal.key ? ` aria-keyshortcuts="${steal.key}"` : '';
    return `<button type="button" class="slot is-steal" data-act="move" data-id="${esc(steal.id)}" data-key="${key}"${shortcut} aria-label="${esc(steal.label)}">${inner}<span class="slot__ring" aria-hidden="true"></span></button>`;
  }
  if (o.playSeat === seatId && token) {
    const enabled = o.slotEnabled?.(i);
    const selected = o.selected?.kind === 'slot' && o.selected.slot === i;
    return `<button type="button" class="slot slot--mine${selected ? ' is-selected' : ''}" data-act="slot" data-slot="${i}" data-key="${key}"
      aria-keyshortcuts="${i + 1}" aria-pressed="${selected}" aria-label="${esc(it.name)}, key ${i + 1}. ${esc(it.does)}${enabled ? '' : ' Not useful right now.'}" title="${esc(it.name)}: ${esc(it.does)}"${enabled ? '' : ' disabled'}>${inner}<span class="slot__key" aria-hidden="true">${i + 1}</span></button>`;
  }
  if (!token) return '<div class="slot is-empty" aria-hidden="true"></div>';
  return `<div class="slot" role="img" aria-label="${esc(it.name)}" title="${esc(it.name)}: ${esc(it.does)}">${inner}</div>`;
}

/** The note on the Dealer's side: its last action and the rule behind it. */
/** The spent shells of this load. Entries are 'live', 'blank' or 'unknown', with ' inverted' when the Inverter flipped the shell first. */
function spentHtml(list) {
  const kinds = list.map((k) => k.split(' ')[0]);
  const count = (k) => kinds.filter((x) => x === k).length;
  const flipped = list.filter((k) => k.endsWith(' inverted')).length;
  const label = [
    `Spent this load: ${count('live')} live, ${count('blank')} blank`,
    count('unknown') ? `, ${count('unknown')} not named` : '',
    flipped ? `. ${flipped === 1 ? 'One was' : `${flipped} were`} inverted before it left, so the counts follow the shells as loaded` : '',
  ].join('');
  const pile = list
    .map((k) => {
      const kind = k.split(' ')[0];
      const inv = k.endsWith(' inverted');
      return `<i class="spent__shell spent__shell--${kind}${inv ? ' is-inverted' : ''}">${kind === 'live' ? 'L' : kind === 'blank' ? 'B' : '?'}</i>`;
    })
    .join('');
  return `<div class="spent" role="img" aria-label="${esc(label)}.">
         <span class="spent__label" aria-hidden="true">Spent</span>
         <span class="spent__pile" aria-hidden="true">${pile}</span>
         ${flipped ? `<span class="spent__note" aria-hidden="true">${flipped} inverted</span>` : ''}
       </div>`;
}

export function dnoteHtml(d) {
  if (!d) return '';
  const last = d.last || 'The Dealer has not acted yet this round.';
  const why = d.showWhy && d.last ? d.why || 'No rule text came with this action.' : '';
  return `
    <div class="dnote" role="group" aria-label="The Dealer's last action">
      <p class="dnote__row"><span class="dnote__k">Last</span><span class="dnote__v">${esc(last)}</span></p>
      ${why ? `<p class="dnote__row dnote__row--why"><span class="dnote__k">Why</span><span class="dnote__v">${esc(why)}</span></p>` : ''}
    </div>`;
}

/** Update the Dealer's note in place while events play, without a full render. */
export function patchDnote(root, d) {
  const el = root.querySelector('.dnote');
  if (!el) return;
  const tmp = document.createElement('div');
  tmp.innerHTML = dnoteHtml(d);
  const next = tmp.firstElementChild;
  if (next) el.replaceWith(next);
}

function seatHtml(seatId, m, o) {
  const s = m.seats[seatId];
  const name = o.names[seatId];
  const turn = m.toMove === seatId && !m.over;
  const badges = [];
  if (turn) badges.push('<span class="badge badge--turn">To move</span>');
  if (s.restraint === 'cuffed') badges.push(`<span class="badge badge--cuff">${ui.cuffSmall}Cuffed</span>`);
  if (s.restraint === 'lost a turn') badges.push(`<span class="badge badge--cuff">${ui.cuffSmall}Lost a turn</span>`);
  if (m.over && m.winner === seatId) badges.push('<span class="badge badge--win">Winner</span>');

  let editor = '';
  if (o.edit) {
    const r = o.edit.restraint[seatId] ?? '';
    editor = `
      <div class="plate__edit">
        ${stepper('ch', seatId, s.charges, `${name} charges`)}
        <span class="plate__of">of</span>
        ${stepper('max', seatId, s.max, `${name} maximum charges`)}
        <label class="mini-select"><span class="sr-only">${esc(name)} restraint</span>
          <select data-act="restraint" data-seat="${seatId}" data-key="restraint-${seatId}">
            <option value=""${r === '' ? ' selected' : ''}>Not cuffed</option>
            <option value="cuffed"${r === 'cuffed' ? ' selected' : ''}>Cuffed</option>
            <option value="skipped"${r === 'skipped' ? ' selected' : ''}>Lost a turn</option>
          </select>
        </label>
      </div>`;
  }

  const targets = o.targets?.[seatId] ?? [];
  const targetHtml = targets.length
    ? `<div class="plate__targets">${targets
        .map((t, k) => {
          const shortcut = t.key ? ` aria-keyshortcuts="${t.key}"` : '';
          const keyMark = t.key ? `<kbd class="target-btn__key" aria-hidden="true">${t.key}</kbd>` : '';
          return `<button type="button" class="target-btn" data-act="move" data-id="${esc(t.id)}" data-key="target-${seatId}-${k}"${shortcut}>${ui.cross}<span>${esc(t.label)}</span>${keyMark}</button>`;
        })
        .join('')}</div>`
    : '';

  const items = s.items.map((t, i) => slotHtml(seatId, i, t, o));
  const held = s.items.filter(Boolean).map((t) => ITEMS[t].name);
  const trayLabel = `${name}'s tray: ${held.length ? listJoin(held) : 'empty'}`;
  const note = seatId === 'p2' ? dnoteHtml(o.dnote) : '';
  return `
    <section class="seat seat--${seatId}${turn ? ' is-turn' : ''}${targets.length ? ' is-targetable' : ''}" aria-label="${esc(name)}'s side">
      <div class="plate">
        <div class="plate__row">
          <span class="plate__name">${esc(name)}</span>
          ${badges.join('')}
        </div>
        <div class="plate__main">
          ${readout(s, name)}
          ${targetHtml}
        </div>
        ${editor}
      </div>
      <div class="tray tray--a" role="group" aria-label="${esc(trayLabel)}">${items.slice(0, 4).join('')}</div>
      <div class="tray tray--b" role="group" aria-label="${esc(name)}'s tray, slots 5 to 8">${items.slice(4).join('')}</div>
      ${note}
    </section>`;
}

function shellHtml(sh, o) {
  const r = o.reveal(sh);
  const kind = r.kind ?? 'unknown';
  const glyph = r.kind === 'live' ? 'L' : r.kind === 'blank' ? 'B' : '?';
  const by = r.by ?? [];
  const seenBy = by.map((s) => o.names[s]);
  const pos = sh.offset === 0 ? 'Shell 1, the chamber' : `Shell ${sh.offset + 1}`;
  let how = '';
  if (r.how === 'deduced') how = ', settled by the counts';
  else if (seenBy.length) how = `, seen by ${listJoin(seenBy)}`;
  const label = `${pos}: ${r.kind ?? 'unknown'}${how}`;
  const chips = by.map((s) => `<span class="seen seen--${s}" title="Seen by ${esc(o.names[s])}">${esc(o.seenMark[s])}</span>`).join('');
  const mark = r.how === 'deduced' ? '<span class="seen seen--deduced" title="Settled by the counts">=</span>' : '';
  const body = `
      <span class="shell__body"><span class="shell__glyph">${glyph}</span></span>
      <span class="shell__head"></span>
      <span class="shell__pos" aria-hidden="true">${sh.offset === 0 ? 'ch' : sh.offset + 1}</span>
      <span class="shell__seen" aria-hidden="true">${chips}${mark}</span>`;
  const cls = `shell shell--${kind}${r.how === 'deduced' ? ' shell--deduced' : ''}${sh.offset === 0 ? ' is-chamber' : ''}`;
  if (o.edit) {
    return `<li><button type="button" class="${cls} shell--edit" data-act="edit-shell" data-offset="${sh.offset}" data-key="shell-${sh.offset}" aria-label="${esc(label)}. Change what is known.">${body}</button></li>`;
  }
  return `<li class="${cls}" aria-label="${esc(label)}">${body}</li>`;
}

function chanceHtml(c) {
  if (!c || c.text === null || c.text === undefined) return '';
  return `
    <span class="chip chip--chance" title="The chance that the chamber fires live, from ${esc(c.label.toLowerCase())}">
      <span class="sr-only">Chance the chamber fires live, from ${esc(c.label.toLowerCase())}: ${esc(c.text)}</span>
      <span class="chip__k" aria-hidden="true">Chamber live</span>
      <b aria-hidden="true">${esc(c.text)}</b>
      <span class="chip__who" aria-hidden="true">${esc(c.label)}</span>
    </span>`;
}

function centreHtml(m, o) {
  const t = m.tube;
  const lifted = o.selected?.kind === 'gun';
  const gunLabel = `Shotgun${t.sawed ? ', sawed' : ''}`;
  const gun = o.gunButton
    ? `<button type="button" class="gun${lifted ? ' is-lifted' : ''}" data-act="gun" data-key="gun" aria-pressed="${lifted}" aria-keyshortcuts="G"
         aria-label="${gunLabel}, key G. Pick it up, then choose a target.">${gunSvg()}</button>`
    : `<div class="gun" role="img" aria-label="${gunLabel}">${gunSvg()}</div>`;

  const counts = o.edit
    ? [
        `<span class="count count--live count--edit"><i class="pip pip--live" aria-hidden="true">L</i>${stepper('live', '', t.live, 'Live shells')}<span>live</span></span>`,
        `<span class="count count--blank count--edit"><i class="pip pip--blank" aria-hidden="true">B</i>${stepper('blank', '', t.blank, 'Blank shells')}<span>blank</span></span>`,
      ]
    : [
        `<span class="count count--live"><i class="pip pip--live" aria-hidden="true">L</i><b>${t.live}</b> live</span>`,
        `<span class="count count--blank"><i class="pip pip--blank" aria-hidden="true">B</i><b>${t.blank}</b> blank</span>`,
      ];
  if (t.sawed && !o.edit) counts.push('<span class="tag tag--warn">Sawed: next shot deals 2</span>');
  if (o.inverted && !o.edit) counts.push('<span class="tag tag--inv">Chamber inverted</span>');
  counts.push(chanceHtml(o.chance));

  const shells = t.shells.length
    ? t.shells.map((sh) => shellHtml(sh, o)).join('')
    : '<li class="rack__empty">The tube is empty.</li>';

  const spent = o.spent?.length ? spentHtml(o.spent) : '';

  let editor = '';
  if (o.edit) {
    const e = o.edit;
    editor = `
      <div class="centre__edit">
        <label class="tog"><input type="checkbox" data-act="sawed" data-key="tog-sawed"${e.sawed ? ' checked' : ''}><span>Sawed</span></label>
        <label class="tog"><input type="checkbox" data-act="inverted" data-key="tog-inverted"${e.inverted ? ' checked' : ''}><span>Inverted</span></label>
        <label class="tog"><input type="checkbox" data-act="restraintused" data-key="tog-restraint"${e.restraintUsed ? ' checked' : ''}><span>Cuffs used this turn</span></label>
        <span class="edit-turn" role="radiogroup" aria-label="Seat to move">
          <span class="edit-label" aria-hidden="true">To move</span>
          <label class="tog"><input type="radio" name="tbl-turn" value="p1" data-act="turn" data-key="turn-p1"${m.toMove === 'p1' ? ' checked' : ''}><span>${esc(o.names.p1)}</span></label>
          <label class="tog"><input type="radio" name="tbl-turn" value="p2" data-act="turn" data-key="turn-p2"${m.toMove === 'p2' ? ' checked' : ''}><span>${esc(o.names.p2)}</span></label>
        </span>
      </div>`;
  }

  const legend = o.legend ? `<p class="tube__legend">${o.legend}</p>` : '';
  const selection = o.selectionBar
    ? `<div class="selbar" role="status">
         <span class="selbar__text">${o.selectionBar}</span>
         <button type="button" class="btn btn--ghost btn--small" data-act="cancel" data-key="cancel" aria-keyshortcuts="Escape">Put it back</button>
       </div>`
    : '';
  const overlay = o.overlay
    ? `<div class="result" role="group" aria-labelledby="result-title">
         <p class="result__kicker">Round over</p>
         <h2 class="result__title" id="result-title">${esc(o.overlay.title)}</h2>
         <p class="result__text">${esc(o.overlay.text)}</p>
         <div class="result__actions">${(o.overlay.actions ?? [])
           .map((a) => `<button type="button" class="btn${a.primary ? ' btn--primary' : ''}" data-act="${esc(a.act)}" data-key="result-${esc(a.act)}">${esc(a.label)}</button>`)
           .join('')}</div>
       </div>`
    : '';

  return `
    <section class="centre" aria-label="The shotgun and the shells">
      <div class="centre__info">${m.info.map((x) => `<span>${esc(x)}</span>`).join('')}</div>
      <div class="gunbay${t.sawed ? ' is-sawed' : ''}">${gun}</div>
      <div class="tube">
        <div class="tube__counts">${counts.join('')}</div>
        <div class="tube__row">
          <ol class="rack" aria-label="Shells in the tube, chamber first">${shells}</ol>
          ${spent}
        </div>
      </div>
      ${editor}
      ${selection || (o.note ? `<p class="centre__note">${o.note}</p>` : legend)}
      <div class="stamp" aria-hidden="true"></div>
      ${overlay}
    </section>`;
}

/**
 * Render the whole table into root.
 * o.context     'watch' | 'play' | 'advise'
 * o.names       {p1, p2} display names
 * o.seenMark    {p1, p2} one-letter marks under shells a seat has seen
 * o.reveal(sh)  -> {kind, by, how}  what this reader may see of a shell;
 *               how is 'seen', 'deduced' (settled by the counts) or null
 * o.inverted    the chamber was flipped since it was loaded
 * o.chance      {text, label} the chamber-live chip and whose view it uses
 * o.dnote       {last, why, showWhy} the note on the Dealer's side, or null
 * o.edit        position editor state (advise) or null
 * o.playSeat    seat whose tray is clickable (play), o.slotEnabled(i)
 * o.selected    {kind:'gun'} | {kind:'slot', slot}
 * o.targets     {p1:[{id,label,key?}], p2:[...]} target buttons on the plates
 * o.stealSlots  {p2: {slotIndex: {id,label,key?}}}
 * o.gunButton   whether the shotgun is a button
 * o.overlay     {title, text, actions:[{act,label,primary}]} at round end
 * o.onAction(act, data, el, event), o.onKey(event)
 */
export function renderTable(root, m, o) {
  const active = document.activeElement;
  const key = active && root.contains(active) ? active.dataset.key : null;
  root._onAction = o.onAction ?? null;
  root._onKey = o.onKey ?? null;
  root.dataset.context = o.context;
  root.innerHTML = `
    <div class="table__surface" aria-hidden="true"></div>
    ${seatHtml('p2', m, o)}
    ${centreHtml(m, o)}
    ${seatHtml('p1', m, o)}`;
  if (key) {
    const again = root.querySelector(`[data-key="${CSS.escape(key)}"]`);
    if (again && !again.disabled) again.focus({ preventScroll: true });
  }
}

/** Focus the first element matching a selector inside the table. */
export function focusIn(root, selector) {
  const el = root.querySelector(selector);
  if (el) el.focus({ preventScroll: true });
  return el;
}
