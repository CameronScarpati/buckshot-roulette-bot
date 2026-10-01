// Plays a batch of events onto the table: one at a time, each with a short
// effect on the table and a line in the log. The table itself is redrawn from
// the new view once the batch is done.

import { ITEMS, esc } from './vocab.js';
import { itemSvg, ui } from './icons.js';
import { stampText } from './log.js';
import { reducedMotion, wait } from './state.js';

/** The shell a Beer racked out, read from its text: 'live', 'blank' or 'unknown'. */
export function beerShell(ev) {
  const text = ev.text ?? '';
  const live = /\blive\b/i.test(text);
  const blank = /\bblank\b/i.test(text);
  if (live && !blank) return 'live';
  if (blank && !live) return 'blank';
  return 'unknown';
}

/**
 * Track the spent shells of the current load from the events. A shell the
 * Inverter flipped before it left is stored as 'live inverted' or 'blank
 * inverted': the counts on the table follow the shells as loaded, so the
 * table says which spent shells changed type.
 */
export function updateSpent(spent, ev) {
  if (ev.kind === 'load') {
    spent.length = 0;
    spent.inverted = false;
    return;
  }
  if (ev.kind === 'item' && ev.item === 'inv') {
    spent.inverted = !spent.inverted;
    return;
  }
  let shell = null;
  if (ev.kind === 'shot') shell = ev.shell;
  else if (ev.kind === 'item' && ev.item === 'beer') shell = beerShell(ev);
  if (!shell) return;
  spent.push(spent.inverted && shell !== 'unknown' ? `${shell} inverted` : shell);
  spent.inverted = false;
}

/**
 * The text of the rule event that explains the action at events[i], or null.
 * A batch holds one action, and the bridge puts its rule event after the
 * events of that action (an Adrenaline and the item it took are one action),
 * so the rule is known while the action is still on the table.
 */
export function ruleAhead(events, i) {
  for (let j = i + 1; j < events.length; j += 1) {
    const e = events[j];
    if (e.kind === 'rule') return e.text;
    if (e.kind === 'shot' || e.kind === 'skip' || e.kind === 'load' || e.kind === 'over') return null;
  }
  return null;
}

function stampHtml(ev, ctx, loadNo) {
  if (ev.kind === 'shot') {
    const live = ev.shell === 'live';
    return `
      <span class="stamp__big stamp__big--${ev.shell}"><i class="pip pip--${ev.shell}">${live ? 'L' : 'B'}</i>${live ? 'Live' : 'Blank'}</span>
      <span class="stamp__small">${esc(stampText(ev, ctx))}</span>`;
  }
  if (ev.kind === 'item') {
    return `
      <span class="stamp__item">${itemSvg(ev.item)}</span>
      <span class="stamp__small">${esc(stampText(ev, ctx))}</span>`;
  }
  if (ev.kind === 'load') {
    const pips = [...Array(ev.live).fill('live'), ...Array(ev.blank).fill('blank')]
      .map((k) => `<i class="pip pip--${k}">${k === 'live' ? 'L' : 'B'}</i>`)
      .join('');
    return `
      <span class="stamp__kicker">Load ${loadNo}</span>
      <span class="stamp__pips">${pips}</span>
      <span class="stamp__small">${ev.live} live, ${ev.blank} blank. The order is hidden.</span>`;
  }
  if (ev.kind === 'skip') {
    return `<span class="stamp__item stamp__item--icon">${ui.cuffSmall}</span><span class="stamp__small">${esc(stampText(ev, ctx))}</span>`;
  }
  return '';
}

/**
 * Play events in order.
 * opts.table    the table root
 * opts.log      a Log instance
 * opts.spent    the spent-shell array for this round
 * opts.ctx      log context (human, rules, secrets)
 * opts.pace     ms per visible event
 * opts.alive    () => boolean, false stops early (the round was replaced)
 * opts.visible  () => boolean, false logs the rest without effects (mode left)
 * opts.onEvent  (ev, why) => void, called as each event plays, with the
 *               text of the rule event that explains it, or null
 */
export async function playEvents(events, opts) {
  for (let i = 0; i < events.length; i += 1) {
    const ev = events[i];
    if (!opts.alive()) return;
    opts.log.push(ev);
    updateSpent(opts.spent, ev);
    opts.onEvent?.(ev, ruleAhead(events, i));
    if (opts.visible && !opts.visible()) continue;
    const html = stampHtml(ev, opts.ctx, opts.log.loadCount());
    if (!html) continue;
    const root = opts.table;
    const stamp = root.querySelector('.stamp');
    root.dataset.fx = ev.kind;
    root.dataset.fxTarget = ev.target ?? ev.seat ?? '';
    root.dataset.fxShell = ev.shell ?? '';
    root.dataset.fxBy = ev.by ?? '';
    if (stamp) {
      stamp.className = `stamp stamp--${ev.kind}${ev.shell ? ` stamp--${ev.shell}` : ''} is-on`;
      stamp.innerHTML = html;
    }
    const hold = ev.kind === 'load' ? opts.pace * 1.6 : opts.pace;
    await wait(reducedMotion() ? Math.max(hold, 500) : hold);
    delete root.dataset.fx;
    delete root.dataset.fxTarget;
    delete root.dataset.fxShell;
    delete root.dataset.fxBy;
    if (stamp) stamp.classList.remove('is-on');
    await wait(Math.min(160, opts.pace / 4));
  }
}

/** The name of an item, for sentences. */
export function itemName(token) {
  return ITEMS[token]?.name ?? token;
}
