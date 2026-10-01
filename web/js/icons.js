// Every drawing on the page: items, the shotgun, charge bolts, small UI icons.
// Drawn here from simple shapes; nothing is taken from the game.

const ITEM_ART = {
  // Magnifying Glass: brass rim, pale lens, dark handle.
  mg: `
    <path d="M30 30 L42 42" stroke="#3b2414" stroke-width="7" stroke-linecap="round"/>
    <path d="M29 29 L33 33" stroke="#c9a457" stroke-width="6" stroke-linecap="round"/>
    <circle cx="20" cy="20" r="12.5" fill="#8fc6dc" fill-opacity=".38" stroke="#d8b466" stroke-width="4"/>
    <path d="M13 16.5 A8 8 0 0 1 19 11.5" stroke="#fff" stroke-opacity=".75" stroke-width="2.2" fill="none" stroke-linecap="round"/>`,
  // Beer: a can with a paper band.
  beer: `
    <rect x="14" y="8" width="20" height="34" rx="4" fill="#8e2a1e"/>
    <rect x="14" y="8" width="20" height="5" rx="2.2" fill="#cfd3d8"/>
    <rect x="14" y="38" width="20" height="4" rx="2" fill="#a8adb3"/>
    <rect x="14" y="19" width="20" height="10" fill="#ead9ac"/>
    <rect x="18" y="22.5" width="12" height="3" rx="1.5" fill="#8e2a1e"/>
    <rect x="16.5" y="13" width="3" height="25" fill="#fff" fill-opacity=".16"/>`,
  // Cigarettes: a pack with two showing.
  cig: `
    <rect x="17" y="5" width="5" height="14" rx="1" fill="#f7f3ea"/>
    <rect x="17" y="5" width="5" height="4.5" rx="1" fill="#d39a52"/>
    <rect x="24" y="8" width="5" height="11" rx="1" fill="#f7f3ea"/>
    <rect x="24" y="8" width="5" height="4" rx="1" fill="#d39a52"/>
    <rect x="12" y="15" width="24" height="28" rx="2.5" fill="#efe9dd"/>
    <rect x="12" y="15" width="24" height="9" rx="2.5" fill="#b3282a"/>
    <rect x="12" y="21" width="24" height="3" fill="#b3282a"/>
    <rect x="16" y="30" width="16" height="2.4" rx="1.2" fill="#b3282a" fill-opacity=".6"/>`,
  // Handcuffs: two rings and a chain.
  cuff: `
    <circle cx="14.5" cy="29" r="9" fill="none" stroke="#c7cdd5" stroke-width="4"/>
    <circle cx="33.5" cy="29" r="9" fill="none" stroke="#c7cdd5" stroke-width="4"/>
    <rect x="9" y="17" width="11" height="6" rx="2" fill="#9aa3ad"/>
    <rect x="28" y="17" width="11" height="6" rx="2" fill="#9aa3ad"/>
    <path d="M19 18 C21 12 27 12 29 18" fill="none" stroke="#8b939c" stroke-width="2.6" stroke-dasharray="3 1.6"/>`,
  // Hand Saw: toothed blade and a wooden handle.
  saw: `
    <path d="M5 33 L30 16 L35 23 L9 38 Z" fill="#cfd5dc"/>
    <path d="M9 38 l1.5 -3.4 l2.2 1.9 l1.4 -3.5 l2.2 1.9 l1.4 -3.5 l2.2 1.9 l1.4 -3.5 l2.2 1.9 l1.4 -3.5 l2.2 1.9 l1.4 -3.5 l2.2 1.9 l1 -2.4" fill="none" stroke="#8d969f" stroke-width="1.4"/>
    <path d="M28 12 L38 6 C41 5 44 7 44 10 L44 22 C44 25 41 27 38 26 L33 24 Z" fill="#8a5631"/>
    <ellipse cx="38.5" cy="14.5" rx="2.8" ry="4.6" fill="#2a1b10" transform="rotate(-30 38.5 14.5)"/>`,
  // Burner Phone: a small handset with a lit screen.
  phone: `
    <rect x="28" y="2" width="3" height="8" rx="1.2" fill="#5a6168"/>
    <rect x="15" y="7" width="18" height="36" rx="3.5" fill="#2b2f34" stroke="#59616a" stroke-width="1.4"/>
    <rect x="18" y="11" width="12" height="9" rx="1" fill="#79d897"/>
    <g fill="#9aa3ac">
      <circle cx="19.5" cy="25" r="1.4"/><circle cx="24" cy="25" r="1.4"/><circle cx="28.5" cy="25" r="1.4"/>
      <circle cx="19.5" cy="29.5" r="1.4"/><circle cx="24" cy="29.5" r="1.4"/><circle cx="28.5" cy="29.5" r="1.4"/>
      <circle cx="19.5" cy="34" r="1.4"/><circle cx="24" cy="34" r="1.4"/><circle cx="28.5" cy="34" r="1.4"/>
    </g>`,
  // Adrenaline: a syringe at an angle.
  adr: `
    <g transform="rotate(40 24 24)">
      <rect x="22.8" y="1.5" width="2.4" height="7" fill="#a9b6bf"/>
      <rect x="18" y="7" width="12" height="3" rx="1" fill="#a9b6bf"/>
      <rect x="19.5" y="10" width="9" height="25" rx="2" fill="#e8f0f4" fill-opacity=".9" stroke="#9fb0bb" stroke-width="1.2"/>
      <rect x="20.6" y="19" width="6.8" height="15" rx="1" fill="#f0a42e"/>
      <path d="M24 35 V45" stroke="#dfe5e9" stroke-width="1.6"/>
    </g>`,
  // Inverter: a small box with plus, minus and a swap arrow.
  inv: `
    <rect x="7" y="11" width="34" height="26" rx="6" fill="#36475b" stroke="#7189a3" stroke-width="1.6"/>
    <path d="M13 24 h8 M17 20 v8" stroke="#f2f4f6" stroke-width="2.6" stroke-linecap="round"/>
    <path d="M28 24 h8" stroke="#f2f4f6" stroke-width="2.6" stroke-linecap="round"/>
    <path d="M17 33 C21 37 27 37 31 33" fill="none" stroke="#f0b55e" stroke-width="1.8"/>
    <path d="M31 33 l-3.6 0.4 l1.6 -3" fill="#f0b55e"/>`,
  // Expired Medicine: an amber pill bottle with a cross on the label.
  med: `
    <rect x="13" y="7" width="22" height="8" rx="2" fill="#f3f0e9"/>
    <rect x="15" y="14" width="18" height="29" rx="3.5" fill="#c4781f"/>
    <rect x="15" y="22" width="18" height="13" fill="#f2e7cd"/>
    <path d="M24 24.5 v8 M20 28.5 h8" stroke="#b3282a" stroke-width="2.6" stroke-linecap="round"/>
    <rect x="17" y="15" width="2.5" height="26" fill="#fff" fill-opacity=".18"/>`,
};

export function itemSvg(token) {
  const art = ITEM_ART[token];
  if (!art) return '';
  return `<svg viewBox="0 0 48 48" class="item-svg item-svg--${token}" aria-hidden="true" focusable="false">${art}</svg>`;
}

export function boltSvg() {
  return '<svg viewBox="0 0 16 24" aria-hidden="true" focusable="false"><path d="M10.2 1 L2 13.6 H7.3 L5.7 23 L14 9.6 H8.6 Z"/></svg>';
}

/** The shotgun, lying across the table, muzzle to the right. */
export function gunSvg() {
  return `
<svg class="gun-svg" viewBox="0 0 640 132" aria-hidden="true" focusable="false">
  <defs>
    <linearGradient id="gw" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#9a6a40"/><stop offset=".45" stop-color="#6e4325"/><stop offset="1" stop-color="#3f2413"/>
    </linearGradient>
    <linearGradient id="gm" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#7c838b"/><stop offset=".4" stop-color="#3c4249"/><stop offset="1" stop-color="#1b1f23"/>
    </linearGradient>
    <linearGradient id="gb" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#a3aab2"/><stop offset=".35" stop-color="#555c64"/><stop offset="1" stop-color="#22272c"/>
    </linearGradient>
    <radialGradient id="gf" cx=".3" cy=".5" r=".7">
      <stop offset="0" stop-color="#fff6d6"/><stop offset=".35" stop-color="#ffc35a"/><stop offset="1" stop-color="#ff6a2a" stop-opacity="0"/>
    </radialGradient>
    <filter id="gs" x="-10%" y="-60%" width="120%" height="220%"><feGaussianBlur stdDeviation="7"/></filter>
  </defs>
  <ellipse class="gun-shadow" cx="320" cy="104" rx="300" ry="13" fill="#000" fill-opacity=".55" filter="url(#gs)"/>
  <!-- stock -->
  <path d="M8 58 C8 50 13 46 22 46 L178 40 L216 44 L216 84 L178 86 L30 100 C16 101 8 95 8 87 Z" fill="url(#gw)"/>
  <path d="M4 55 C4 51 7 48 11 48 L18 48 L18 98 L11 98 C7 98 4 95 4 91 Z" fill="#171110"/>
  <g stroke="#2b170b" stroke-opacity=".55" stroke-width="1.2" fill="none">
    <path d="M30 60 C80 55 130 52 200 50"/><path d="M34 74 C90 70 140 66 205 64"/><path d="M40 88 C90 84 150 78 205 76"/>
  </g>
  <!-- receiver -->
  <rect x="206" y="38" width="120" height="47" rx="7" fill="url(#gm)"/>
  <rect x="246" y="46" width="42" height="12" rx="3" fill="#0f1113"/>
  <rect x="212" y="41" width="108" height="3" rx="1.5" fill="#fff" fill-opacity=".14"/>
  <!-- trigger and guard -->
  <path d="M234 84 C234 106 280 106 280 84" fill="none" stroke="#262a2f" stroke-width="5"/>
  <path d="M256 84 q3 9 -3 14" stroke="#16191c" stroke-width="4" fill="none" stroke-linecap="round"/>
  <!-- full barrel -->
  <g class="gun-long">
    <rect x="320" y="63" width="244" height="13" rx="6" fill="url(#gm)"/>
    <rect x="320" y="45" width="300" height="15" rx="3" fill="url(#gb)"/>
    <rect x="606" y="42.5" width="16" height="20" rx="2.5" fill="#24292e"/>
    <circle cx="612" cy="43" r="2.4" fill="#e3c97d"/>
  </g>
  <!-- sawed barrel -->
  <g class="gun-short">
    <rect x="320" y="63" width="160" height="13" rx="6" fill="url(#gm)"/>
    <path d="M320 45 H500 L503 48.5 L499 52 L504 56 L500 60 H320 Z" fill="url(#gb)"/>
    <g fill="#d9b27a" fill-opacity=".7"><circle cx="509" cy="56" r="1.2"/><circle cx="514" cy="50" r="1"/><circle cx="512" cy="61" r=".9"/></g>
  </g>
  <!-- pump -->
  <rect x="352" y="57" width="118" height="27" rx="9" fill="url(#gw)"/>
  <g stroke="#2a160a" stroke-opacity=".7" stroke-width="2">
    <path d="M368 61 v19"/><path d="M380 61 v19"/><path d="M392 61 v19"/><path d="M404 61 v19"/><path d="M416 61 v19"/><path d="M428 61 v19"/><path d="M440 61 v19"/><path d="M452 61 v19"/>
  </g>
  <!-- muzzle flash, shown on a live shot -->
  <g class="gun-flash gun-flash--long"><path d="M622 52 L660 36 L646 52 L668 54 L646 58 L660 72 Z" fill="url(#gf)"/><circle cx="626" cy="53" r="12" fill="url(#gf)"/></g>
  <g class="gun-flash gun-flash--short"><path d="M504 52 L542 36 L528 52 L550 54 L528 58 L542 72 Z" fill="url(#gf)"/><circle cx="508" cy="53" r="12" fill="url(#gf)"/></g>
</svg>`;
}

export const ui = {
  watch:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12Z" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="12" cy="12" r="3.2" fill="currentColor"/></svg>',
  play:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="12" cy="12" r="2.2" fill="currentColor"/><path d="M12 1.5v5M12 17.5v5M1.5 12h5M17.5 12h5" stroke="currentColor" stroke-width="1.8"/></svg>',
  advise:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="7" cy="7" r="3" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="17" cy="17" r="3" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M19 4 5 20" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
  playIcon: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 4.5v15l12-7.5z" fill="currentColor"/></svg>',
  pauseIcon:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="4.5" width="4.2" height="15" rx="1" fill="currentColor"/><rect x="13.8" y="4.5" width="4.2" height="15" rx="1" fill="currentColor"/></svg>',
  stepIcon:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 4.5v15l10-7.5z" fill="currentColor"/><rect x="16.5" y="4.5" width="3" height="15" rx="1" fill="currentColor"/></svg>',
  cross:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="7" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 2.5v6M12 15.5v6M2.5 12h6M15.5 12h6" stroke="currentColor" stroke-width="2"/></svg>',
  cuffSmall:
    '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="7" cy="14" r="4.6" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="17" cy="14" r="4.6" fill="none" stroke="currentColor" stroke-width="2"/><path d="M10 9.5c1-2.6 3-2.6 4 0" fill="none" stroke="currentColor" stroke-width="1.6"/></svg>',
  eye: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12s3.6-6.5 10-6.5S22 12 22 12s-3.6 6.5-10 6.5S2 12 2 12Z" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="12" r="2.8" fill="currentColor"/></svg>',
};
