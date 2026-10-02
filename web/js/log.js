// The log: every event as a plain sentence. Rule events attach to the action
// they explain. What a seat learned privately is shown only to a spectator.

import { ITEMS, esc, listJoin } from './vocab.js';

/**
 * ctx.human    true in play mode (seat 1 is "you")
 * ctx.rules    show the Dealer's rule text
 * ctx.secrets  show what each seat learned privately
 */
function words(ctx) {
  const you = ctx.human;
  const subj = (s) => (s === 'p1' ? (you ? 'You' : 'The solver') : 'The Dealer');
  const obj = (s) => (s === 'p1' ? (you ? 'you' : 'the solver') : 'the Dealer');
  const plural = (s) => s === 'p1' && you;
  const verb = (s, base, third) => (plural(s) ? base : third ?? `${base}s`);
  const self = (s) => (plural(s) ? 'yourself' : 'itself');
  const tag = (s) => (s === 'p1' ? (you ? 'You' : 'Solver') : 'Dealer');
  return { subj, obj, plural, verb, self, tag };
}

function charges(n) {
  return `${n} charge${n === 1 ? '' : 's'}`;
}

function itemNames(list) {
  return listJoin(list.map((t) => ITEMS[t]?.name ?? t));
}

/** The sentence after "X uses the Item." */
function itemResult(ev, ctx) {
  const w = words(ctx);
  // In Watch the reader is a spectator: private results show only through
  // learned events, so the page writes these sentences itself.
  if (!ctx.human) {
    if (ev.item === 'mg') return `Only ${w.obj(ev.by)} sees the shell in the chamber.`;
    if (ev.item === 'phone') return `The phone names one later shell to ${w.obj(ev.by)} only.`;
    if (ev.item === 'inv') return 'The chamber is flipped.';
  }
  return ev.text ?? '';
}

/** Describe one event. Returns {who, cls, html} or null to skip it. */
export function describe(ev, ctx, loadNo) {
  const w = words(ctx);
  switch (ev.kind) {
    case 'load': {
      const parts = [`${ev.live} live and ${ev.blank} blank shell${ev.live + ev.blank === 1 ? '' : 's'} go in, in an order nobody sees.`];
      for (const s of ['p1', 'p2']) {
        const got = ev.dealt?.[s] ?? [];
        if (got.length) parts.push(`${w.subj(s)} ${w.verb(s, 'get')} ${itemNames(got)}.`);
      }
      // A seat still in handcuffs when the tube ran out loses no turn to them.
      for (const s of ev.freed ?? []) parts.push(`The reload takes the handcuffs off ${w.obj(s)}.`);
      parts.push(`${w.subj(ev.first)} ${w.verb(ev.first, 'move')} first.`);
      return { who: `Load ${loadNo}`, cls: 'load', html: esc(parts.join(' ')) };
    }
    case 'shot': {
      const isSelf = ev.by === ev.target;
      const head = `${w.subj(ev.by)} ${w.verb(ev.by, 'shoot')} ${isSelf ? w.self(ev.by) : w.obj(ev.target)}.`;
      let tail;
      if (ev.shell === 'live') {
        const loser = isSelf ? (w.plural(ev.by) ? 'you' : 'it') : w.obj(ev.target);
        tail = `The shell is <strong class="t-live">live</strong>: ${loser} ${w.verb(ev.target, 'lose')} ${charges(ev.damage)}.`;
      } else {
        tail = isSelf
          ? `The shell is <strong class="t-blank">blank</strong>, so ${w.plural(ev.by) ? 'you keep' : 'it keeps'} the turn.`
          : 'The shell is <strong class="t-blank">blank</strong>.';
      }
      return { who: w.tag(ev.by), cls: `shot by-${ev.by}`, html: `${esc(head)} ${tail}` };
    }
    case 'item': {
      const name = ITEMS[ev.item]?.name ?? ev.item;
      const head = ev.item === 'adr' ? `${w.subj(ev.by)} ${w.verb(ev.by, 'use')} Adrenaline.` : `${w.subj(ev.by)} ${w.verb(ev.by, 'use')} the ${name}.`;
      const tail = itemResult(ev, ctx);
      return { who: w.tag(ev.by), cls: `item by-${ev.by}`, html: esc(tail ? `${head} ${tail}` : head) };
    }
    case 'learned': {
      if (!ctx.secrets) return null;
      const where = ev.offset === 0 ? 'shell 1, the chamber,' : `shell ${ev.offset + 1}`;
      const s = `Seen only by ${w.obj(ev.by)}: ${where} is ${ev.shell}.`;
      return { who: 'Private', cls: `learned by-${ev.by}`, html: esc(s) };
    }
    case 'skip': {
      const s = `${w.subj(ev.seat)} ${w.plural(ev.seat) ? 'are' : 'is'} handcuffed and ${w.verb(ev.seat, 'lose')} this turn.`;
      return { who: w.tag(ev.seat), cls: `skip by-${ev.seat}`, html: esc(s) };
    }
    case 'over': {
      const s = `${w.subj(ev.winner)} ${w.verb(ev.winner, 'win')} the round.`;
      return { who: 'Result', cls: 'over', html: `<strong>${esc(s)}</strong>` };
    }
    case 'note':
      return { who: 'Note', cls: 'note', html: esc(ev.text) };
    default:
      return null;
  }
}

/** Short sentence for the stamp that appears on the table. */
export function stampText(ev, ctx) {
  const w = words(ctx);
  if (ev.kind === 'shot') {
    const isSelf = ev.by === ev.target;
    const head = `${w.subj(ev.by)} ${w.verb(ev.by, 'shoot')} ${isSelf ? w.self(ev.by) : w.obj(ev.target)}`;
    if (ev.shell === 'live') return `${head}. ${['No charge', 'One charge', 'Two charges'][ev.damage] ?? charges(ev.damage)} lost.`;
    return isSelf ? `${head}. The turn stays.` : `${head}. Nothing happens.`;
  }
  if (ev.kind === 'item') return `${w.subj(ev.by)} ${w.verb(ev.by, 'use')} ${ev.item === 'adr' ? 'Adrenaline' : `the ${ITEMS[ev.item]?.name ?? ev.item}`}`;
  if (ev.kind === 'skip') return `${w.subj(ev.seat)} ${w.plural(ev.seat) ? 'lose' : 'loses'} the turn to the cuffs.`;
  return '';
}

/** The Dealer's last action in a few words, for the note on the table. */
export function dealerLine(ev, ctx) {
  const w = words(ctx);
  if (ev.kind === 'shot') {
    const whom = ev.target === 'p2' ? 'itself' : w.obj('p1');
    return `Shot ${whom}: ${ev.shell}${ev.damage ? `, ${charges(ev.damage)} lost` : ''}.`;
  }
  if (ev.kind === 'item' && ev.item === 'adr') return ev.target ? 'Used Adrenaline.' : 'Used Adrenaline and took nothing.';
  if (ev.kind === 'item') return `Used the ${ITEMS[ev.item]?.name ?? ev.item}.`;
  if (ev.kind === 'skip') return 'Lost its turn to the cuffs.';
  return '';
}

/**
 * The Dealer's note on the table after one more event: its last action in a
 * few words and the rule text that came with it. `why` is the text of the
 * rule event later in the batch that explains this action, so the note never
 * shows an action without its rule while the action plays. Adrenaline and the
 * item it took read as one action. An Adrenaline that takes an item names the
 * seat it takes from; one used alone names none.
 */
export function nextDealerNote(note, ev, ctx, why = null) {
  const cur = note ?? { last: '', why: '', adr: false };
  if (ev.kind === 'rule') return { ...cur, why: ev.text };
  const by = ev.kind === 'skip' ? ev.seat : ev.by;
  if (by !== 'p2' || !['shot', 'item', 'skip'].includes(ev.kind)) return cur;
  if (ev.kind === 'skip') return { last: dealerLine(ev, ctx), why: 'It was handcuffed, so it loses this turn.', adr: false };
  let last = dealerLine(ev, ctx);
  if (ev.kind === 'item' && cur.adr) {
    const whose = ctx.human ? 'your' : 'the solver’s';
    last = `Used Adrenaline to take ${whose} ${ITEMS[ev.item]?.name ?? ev.item}.`;
  }
  return { last, why: why ?? '', adr: ev.kind === 'item' && ev.item === 'adr' && Boolean(ev.target) };
}

export class Log {
  constructor(listEl) {
    this.el = listEl;
    this.events = [];
    this.ctx = { human: false, rules: true, secrets: false };
  }

  setContext(ctx) {
    this.ctx = { ...this.ctx, ...ctx };
    this.rebuild();
  }

  clear() {
    this.events = [];
    this.el.innerHTML = '';
  }

  push(ev) {
    this.events.push(ev);
    this.append(ev, this.loadCount());
    this.scroll();
  }

  note(text) {
    this.push({ kind: 'note', text });
  }

  loadCount() {
    return this.events.filter((e) => e.kind === 'load').length;
  }

  append(ev, loadNo) {
    if (ev.kind === 'rule') {
      if (!this.ctx.rules) return;
      const last = this.el.lastElementChild;
      if (!last) return;
      const why = document.createElement('p');
      why.className = 'log__why';
      why.innerHTML = `<span class="log__whylabel">Why</span> ${esc(ev.text)}`;
      last.querySelector('.log__body')?.appendChild(why);
      return;
    }
    const d = describe(ev, this.ctx, loadNo);
    if (!d) return;
    const li = document.createElement('li');
    li.className = `log__e log__e--${d.cls}`;
    li.innerHTML = `<span class="log__who">${esc(d.who)}</span><div class="log__body"><p>${d.html}</p></div>`;
    this.el.appendChild(li);
  }

  rebuild() {
    const live = this.el.getAttribute('aria-live');
    this.el.setAttribute('aria-live', 'off');
    this.el.innerHTML = '';
    let loads = 0;
    for (const ev of this.events) {
      if (ev.kind === 'load') loads += 1;
      this.append(ev, loads);
    }
    this.scroll();
    if (live) this.el.setAttribute('aria-live', live);
    else this.el.removeAttribute('aria-live');
  }

  scroll() {
    const box = this.el.closest('.log-scroll') ?? this.el;
    box.scrollTop = box.scrollHeight;
  }
}
