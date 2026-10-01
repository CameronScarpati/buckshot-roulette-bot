// App-wide state and small helpers shared by the modes.

import { GAMES } from './vocab.js';

export const app = {
  mode: null, // 'watch' | 'play' | 'advise'
};

export function reducedMotion() {
  return window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false;
}

export function wait(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// The bridge holds one round. Calls are queued so two modes, or a click and an
// autoplay step, never interleave inside it.
let chain = Promise.resolve();
export function call(fn) {
  const next = chain.then(fn);
  chain = next.catch(() => {});
  return next;
}

/** Fill a round form's Game select. */
export function fillGameSelect(select, value = 'don') {
  select.innerHTML = GAMES.map((g) => `<option value="${g.value}"${g.value === value ? ' selected' : ''}>${g.label}</option>`).join('');
}

/** Read a round form: mode, charges and seed. A bad seed draws a new one. */
export function readRoundForm(form) {
  const game = GAMES.find((g) => g.value === form.elements.game.value) ?? GAMES[0];
  const field = form.elements.seed;
  let seed = Number.parseInt(String(field.value).trim(), 10);
  if (!Number.isFinite(seed) || seed < 0) {
    seed = Math.floor(Math.random() * 100000);
    field.value = String(seed);
  }
  return { mode: game.mode, seed, charges: game.charges };
}

/** The note under a round form that says what the chosen game deals. */
export function syncGameNote(form) {
  const game = GAMES.find((g) => g.value === form.elements.game.value) ?? GAMES[0];
  const note = form.querySelector('.round__note');
  if (note) note.textContent = game.note;
}

/** Put a round's settings back into its form. */
export function writeRoundForm(form, settings) {
  const value = settings.mode === 'don' && settings.charges ? `don:${settings.charges}` : settings.mode;
  form.elements.game.value = value;
  form.elements.seed.value = String(settings.seed);
  syncGameNote(form);
}

/**
 * Keep a CSS variable equal to the height of whichever of these bars is
 * showing and fixed to the screen (the phone dock), or 0px when none is.
 * Returns the sync function, to call after anything that may show or hide one.
 */
export function trackHeight(els, cssVar) {
  const set = () => {
    let h = 0;
    for (const el of els) {
      if (!el || el.getClientRects().length === 0) continue;
      if (getComputedStyle(el).position !== 'fixed') continue;
      h = Math.max(h, Math.ceil(el.getBoundingClientRect().height));
    }
    document.documentElement.style.setProperty(cssVar, `${h}px`);
  };
  if ('ResizeObserver' in window) {
    const ro = new ResizeObserver(set);
    for (const el of els) if (el) ro.observe(el);
  }
  window.addEventListener('resize', set);
  set();
  return set;
}
