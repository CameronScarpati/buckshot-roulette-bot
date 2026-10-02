// Runs the engine in a module Worker, so that a long search never holds up the
// page. The engine is engine.mjs and engine.wasm, built from web/bridge.cpp
// (see web/CMakeLists.txt) and served from this folder.
//
// Each message is {id, method, args} and gets one reply, {id, ok} with the
// value or {id, error} with a sentence. web/js/bridge.js sends one message at
// a time.

import createEngine from './engine.mjs';

let loading = null;

/** The engine, started on the first call and again after a failure. */
function engine() {
  if (loading === null) {
    loading = createEngine();
    loading.catch(() => {
      loading = null;
    });
  }
  return loading;
}

// The engine takes plain values. Anything else is passed on as a value the
// engine refuses with its own sentence, rather than one it would misread.
const text = (value) => (typeof value === 'string' ? value : '');
const whole = (value, otherwise) =>
  Number.isInteger(value) && Math.abs(value) < 2 ** 31 ? value : otherwise;
const id = (value) => (Number.isInteger(value) ? String(value) : text(value));

const calls = {
  newRound: (m, options = {}) =>
    m.newRound(
      text(options.mode),
      typeof options.seed === 'number' ? options.seed : Number.NaN,
      options.charges === null || options.charges === undefined ? 0 : whole(options.charges, 1),
      text(options.seat),
    ),
  act: (m, moveId, slot) =>
    m.act(id(moveId), slot === null || slot === undefined ? -1 : whole(slot, -2)),
  advance: (m) => m.advance(),
  rank: (m) => m.rank(),
  advise: (m, notation, options = {}) =>
    m.advise(text(notation), text(options.opponent), whole(options.reloads, -1)),
};

self.onmessage = async (event) => {
  const { id: callId, method, args } = event.data ?? {};
  const call = Object.hasOwn(calls, method) ? calls[method] : null;
  if (call === null) {
    self.postMessage({ id: callId, error: `The engine has no call named ${String(method)}.` });
    return;
  }
  let m;
  try {
    m = await engine();
  } catch {
    self.postMessage({ id: callId, error: 'The engine could not be loaded.' });
    return;
  }
  let answer;
  try {
    answer = JSON.parse(call(m, ...(Array.isArray(args) ? args : [])));
  } catch (problem) {
    // A WebAssembly trap, such as running out of memory, leaves the engine
    // unusable, so the next call starts a fresh one, with no round.
    if (problem instanceof WebAssembly.RuntimeError) loading = null;
    self.postMessage({
      id: callId,
      error:
        problem instanceof WebAssembly.RuntimeError
          ? 'The engine failed and starts again with no round. Start a new round.'
          : 'The engine could not answer.',
    });
    return;
  }
  if (Object.hasOwn(answer, 'error')) {
    self.postMessage({ id: callId, error: answer.error });
  } else {
    self.postMessage({ id: callId, ok: answer.ok });
  }
};
