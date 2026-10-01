// The page's one way to the engine: the C++ engine and solver compiled to
// WebAssembly, running in a module Worker (engine-worker.js) so that a search
// never holds up the page. Every method returns a Promise. Requests go to the
// worker one at a time, in the order they were made, so a slow search
// finishes before the next call starts.

export const engineLabel = 'C++ engine and solver, compiled to WebAssembly';

let worker = null;
let nextId = 1;
let pending = null;
let queue = Promise.resolve();

function fail(sentence) {
  if (pending === null) return;
  const { reject } = pending;
  pending = null;
  reject(new Error(sentence));
}

function startWorker() {
  worker = new Worker(new URL('./engine-worker.js', import.meta.url), { type: 'module' });
  worker.onmessage = (event) => {
    const reply = event.data ?? {};
    if (pending === null || reply.id !== pending.id) return;
    const { resolve, reject } = pending;
    pending = null;
    if (Object.hasOwn(reply, 'error')) reject(new Error(reply.error));
    else resolve(reply.ok);
  };
  // The worker script or the engine could not be loaded. The next call tries
  // again with a new worker.
  worker.onerror = (event) => {
    event.preventDefault?.();
    worker?.terminate();
    worker = null;
    fail('The engine could not be loaded.');
  };
  worker.onmessageerror = () => fail('The engine sent an answer the page could not read.');
}

function send(method, args) {
  return new Promise((resolve, reject) => {
    if (worker === null) startWorker();
    const id = nextId++;
    pending = { id, resolve, reject };
    try {
      worker.postMessage({ id, method, args });
    } catch {
      fail('The engine takes plain values only.');
    }
  });
}

function call(method, ...args) {
  const run = () => send(method, args);
  const answer = queue.then(run, run);
  queue = answer.catch(() => {});
  return answer;
}

/** Start a round: {mode, seed, charges, seat}. Resolves to its View. */
export const newRound = (options) => call('newRound', options);

/**
 * Play seat 1's move `moveId`, an id from `view.legal`. `slot`, when given,
 * names the slot in seat 1's tray that the move spends: the item's own slot,
 * or for any Adrenaline move the Adrenaline clicked. Without it the move
 * spends the copy its `slot` names, or the first Adrenaline. Resolves to
 * {events, view}.
 */
export const act = (moveId, slot) => call('act', moveId, slot ?? null);

/** Take the next step that is not seat 1's choice. Resolves to {events, view}. */
export const advance = () => call('advance');

/** Rank seat 1's moves in the round. Resolves to a Ranking. */
export const rank = () => call('rank');

/** Rank the moves in a written position: {opponent, reloads}. Resolves to a Ranking. */
export const advise = (notation, options) => call('advise', notation, options);
