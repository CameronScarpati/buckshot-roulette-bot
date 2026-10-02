// A ranking as a list of moves with win-chance bars, best first. Every bar
// carries a tick at even odds. The best row says how far it leads the next
// move; every other row says how far it trails the best. A move that only
// spends an item carries a note saying so. A search that stopped at its node
// limit says so above the list.

import { esc, pct, points } from './vocab.js';

/**
 * o.title, o.sub     heading and one line under it
 * o.limit            show at most this many moves
 * o.busy             a busy message, shown instead of the list
 * o.busySince        start time (ms) for an elapsed-seconds counter
 * o.empty            text when there is no ranking to show
 * o.assumptions      'open' | 'closed' | 'none'
 * o.next             tag on the first row before it is played (watch)
 * o.playedId         id of the move that was played: tagged Played
 * o.stale            the position changed since this answer
 */
export function renderRanking(el, r, o = {}) {
  clearInterval(el._timer);
  const titleId = `${el.id}-title`;
  const head = `
    <div class="rank__head">
      <h2 class="card__title" id="${titleId}" tabindex="-1">${esc(o.title ?? 'Ranking')}</h2>
      ${o.sub ? `<p class="rank__sub">${esc(o.sub)}</p>` : ''}
    </div>`;
  el.setAttribute('aria-labelledby', titleId);
  if (o.busy) {
    el.setAttribute('aria-busy', 'true');
    const timer = o.busySince ? '<span class="busy__time" aria-hidden="true">0 s</span>' : '';
    el.innerHTML = `${head}
      <div class="busy" role="status"><span class="spinner" aria-hidden="true"></span><span>${esc(o.busy)}</span>${timer}</div>
      <ol class="rank__list rank__list--ghost" aria-hidden="true">${'<li class="rank__item"><div class="rank__line"><span class="ghost-line"></span></div><div class="bar"><span class="bar__fill"></span></div></li>'.repeat(3)}</ol>`;
    if (o.busySince) {
      const out = el.querySelector('.busy__time');
      const tick = () => {
        out.textContent = `${Math.floor((Date.now() - o.busySince) / 1000)} s`;
      };
      tick();
      el._timer = setInterval(tick, 500);
    }
    return;
  }
  el.removeAttribute('aria-busy');
  if (!r) {
    el.innerHTML = `${head}<p class="rank__empty">${esc(o.empty ?? 'Nothing to show yet.')}</p>`;
    return;
  }
  if (r.refused) {
    el.innerHTML = `${head}<p class="rank__refused">${esc(r.refused)}</p>`;
    return;
  }
  if (!r.moves.length) {
    el.innerHTML = `${head}<p class="rank__empty">${esc(o.empty ?? 'No move to rank.')}</p>`;
    return;
  }
  const moves = o.limit ? r.moves.slice(0, o.limit) : r.moves;
  const top = r.moves[0].win;
  const items = moves
    .map((m, i) => {
      const best = i === 0;
      const tie = !best && Math.abs(m.win - top) < 5e-5;
      let gap = '';
      if (best && r.moves.length > 1) {
        const lead = top - r.moves[1].win;
        gap = lead < 5e-5 ? 'Level with the next move.' : `+${points(lead)} points over the next move.`;
      } else if (!best) {
        gap = tie ? 'Level with the best.' : `${points(top - m.win)} points behind the best.`;
      }
      if (m.note) gap = gap ? `${gap} ${m.note}` : m.note;
      const tags = [
        best ? '<span class="tag tag--best">Best</span>' : '',
        best && o.next && !o.playedId ? `<span class="tag tag--plays">${esc(o.next)}</span>` : '',
        o.playedId && m.id === o.playedId ? '<span class="tag tag--played">Played</span>' : '',
      ].join('');
      return `
        <li class="rank__item${best ? ' is-best' : ''}${o.playedId && m.id === o.playedId ? ' is-played' : ''}">
          <div class="rank__line">
            <span class="rank__label">${esc(m.label)}${tags ? ` <span class="rank__tags">${tags}</span>` : ''}</span>
            <span class="rank__val"><span class="sr-only">chance to win </span>${pct(m.win)}</span>
          </div>
          <div class="bar" aria-hidden="true"><span class="bar__fill" style="width:${Math.max(0.8, m.win * 100).toFixed(2)}%"></span><span class="bar__even"></span></div>
          ${gap ? `<p class="rank__gap">${gap}</p>` : ''}
        </li>`;
    })
    .join('');
  const more = o.limit && r.moves.length > o.limit ? ` ${r.moves.length - o.limit} more move${r.moves.length - o.limit === 1 ? '' : 's'} rank lower.` : '';
  const foot = `<p class="rank__foot"><span class="even-key" aria-hidden="true"></span>The tick on each bar marks even odds, 50%.${more}</p>`;
  const stale = o.stale ? '<p class="rank__stale" role="status">The position has changed since this answer. Ask again to update it.</p>' : '';
  const stopped = r.stopped ? `<p class="rank__stopped">${esc(r.stopped)}</p>` : '';
  let assumptions = '';
  if (o.assumptions !== 'none' && r.assumptions?.length) {
    assumptions = `
      <details class="assume"${o.assumptions === 'open' ? ' open' : ''}>
        <summary>Assumptions behind these numbers (${r.assumptions.length})</summary>
        <ul>${r.assumptions.map((a) => `<li>${esc(a)}</li>`).join('')}</ul>
      </details>`;
  }
  el.innerHTML = `${head}${stale}${stopped}<ol class="rank__list${o.stale ? ' is-stale' : ''}">${items}</ol>${foot}${assumptions}`;
}
