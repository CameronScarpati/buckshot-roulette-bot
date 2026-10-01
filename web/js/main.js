// Entry point: mount the table, build the three modes and route between them
// with the URL hash (#watch, #play, #advise).

import * as bridge from './bridge.js';
import { createAdvise } from './advise.js';
import { createPlay } from './play.js';
import { ui } from './icons.js';
import { app, trackHeight } from './state.js';
import { mountTable } from './table.js';
import { createWatch } from './watch.js';

const MODES = ['watch', 'play', 'advise'];

for (const el of document.querySelectorAll('[data-icon]')) el.innerHTML = ui[el.dataset.icon] ?? '';

const table = document.getElementById('table');
mountTable(table);

const panels = Object.fromEntries(MODES.map((m) => [m, document.getElementById(`panel-${m}`)]));
const tabs = Object.fromEntries(MODES.map((m) => [m, document.getElementById(`tab-${m}`)]));

// Watch and Play hand seat 1's written view of a position to Advise.
const openInAdvise = (text) => {
  modes.advise.open(text);
  location.hash = 'advise';
};

const modes = {
  watch: createWatch({ table, panel: panels.watch, openInAdvise }),
  play: createPlay({ table, panel: panels.play, openInAdvise }),
  advise: createAdvise({ table, panel: panels.advise }),
};

const badge = document.getElementById('engine-badge');
if (badge) {
  if (bridge.engineLabel) {
    badge.textContent = bridge.engineLabel;
    badge.hidden = false;
  } else {
    badge.hidden = true;
  }
}

// On a phone the transport (Watch) and the status dock (Play) are fixed to the
// bottom of the screen; the page keeps its content clear of them.
const syncDock = trackHeight([panels.watch.querySelector('.transport'), panels.play.querySelector('.dock')], '--dock-h');

function setMode(mode, { focusTab = false } = {}) {
  if (!MODES.includes(mode)) mode = 'watch';
  if (app.mode === mode) return;
  if (app.mode) modes[app.mode].leave();
  app.mode = mode;
  document.body.dataset.mode = mode;
  for (const m of MODES) {
    const on = m === mode;
    tabs[m].setAttribute('aria-selected', String(on));
    tabs[m].tabIndex = on ? 0 : -1;
    panels[m].hidden = !on;
  }
  if (focusTab) tabs[mode].focus();
  modes[mode].enter();
  syncDock();
}

function modeFromHash() {
  const h = location.hash.replace(/^#/, '');
  return MODES.includes(h) ? h : 'watch';
}

for (const m of MODES) {
  tabs[m].addEventListener('click', (e) => {
    e.preventDefault();
    if (location.hash !== `#${m}`) location.hash = m;
    else setMode(m);
  });
}

document.querySelector('[role="tablist"]').addEventListener('keydown', (e) => {
  // Space selects a tab as Enter does, instead of scrolling the page.
  if (e.key === ' ' && e.target.closest('[role="tab"]')) {
    e.preventDefault();
    e.target.closest('[role="tab"]').click();
    return;
  }
  const i = MODES.indexOf(app.mode);
  let next = null;
  if (e.key === 'ArrowRight' || e.key === 'ArrowDown') next = MODES[(i + 1) % MODES.length];
  else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') next = MODES[(i + MODES.length - 1) % MODES.length];
  else if (e.key === 'Home') next = MODES[0];
  else if (e.key === 'End') next = MODES[MODES.length - 1];
  if (!next) return;
  e.preventDefault();
  history.replaceState(null, '', `#${next}`);
  setMode(next, { focusTab: true });
});

window.addEventListener('hashchange', () => setMode(modeFromHash()));
setMode(modeFromHash());
