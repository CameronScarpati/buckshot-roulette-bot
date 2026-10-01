# The study table

A web page that runs this repository's engine and solver in the browser, compiled to
WebAssembly. It has three modes: watch the solver play the Dealer, play the Dealer yourself
with the solver's ranking on request, or ask for the odds in any position you write down.

![Watch: the solver ranks its moves before each turn against the Dealer](../docs/images/web-watch.jpg)

The page is static files and the compiled engine. It talks to no server, sets no cookies,
stores nothing and requests nothing from any other site. Every path in it is relative, so it
works from a folder on any static host, including a project page under a sub-path.

## Building

The engine for the page needs [Emscripten](https://emscripten.org/) (built here with 6.0.10)
and CMake 3.16 or newer. From the repository root, with the Emscripten environment loaded
(`source <emsdk>/emsdk_env.sh`):

```sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build-web --target web_engine
```

This writes `build-web/web/engine.mjs` (an ES module of about 28 KB) and
`build-web/web/engine.wasm` (about 320 KB). Only an Emscripten build reaches `web/`, so the
native build and its tests are unchanged by it.

## Serving it locally

The page loads the engine from `js/`, next to `engine-worker.js`. Copy the two built files
there and serve the folder:

```sh
cp build-web/web/engine.mjs build-web/web/engine.wasm web/js/
python3 -m http.server 8000 --directory web
```

Then open <http://localhost:8000/>. The engine runs in a module Worker, which browsers do not
start from a `file://` page, so the folder has to be served. To publish the page, copy `web/`
with the two built files in `web/js/` to the host; `bridge.cpp`, `CMakeLists.txt`, `test/` and
this file are not needed there. `.github/workflows/pages.yml` does this on every push to `main`
and publishes the folder with GitHub Pages.

## The three modes

**Watch.** The solver takes seat 1 against the Dealer's script. Before each of its moves the
page asks for a ranking and shows the top three, and the next step plays the best one. Step
through the round or let it play at one of three speeds. The note on the table names the
Dealer's last action and the rule behind it. The spectator view shows what each seat has
seen privately, such as a shell a Magnifying Glass or a Burner Phone showed it.

**Play.** You take seat 1 against the Dealer's script. Pick up the shotgun and choose a
target, or press an item in your tray. Adrenaline lets you choose an item in the Dealer's
tray to take and use at once, or take nothing. Hint asks the solver to rank your moves. The
keys are 1 to 8 for the tray, G for the shotgun, D and Y for the targets, H for a hint and
Esc to put an item back. A switch adds the Dealer's rule for each of its actions to the log.

Both modes deal a round from a game and a seed, and the same game and seed always deal the
same round. In Double or Nothing either mode can open the current position in Advise.

**Advise.** Write a position on the table: charges, items in each tray, the shells and what
each seat knows about them, sawed, inverted, handcuffs and whose turn it is. Or paste a line
of notation or a whole `advisor` command line. Then ask for the odds against the Dealer's
script or against the solver at its best, looking through 0 or 1 reloads. The page shows the
notation and the matching command line, ready to copy.

## What the solver assumes

Every answer lists its assumptions under "Assumptions behind these numbers". The ones that
come from the page are these.

- **Watch and Play** rank seat 1's moves against the Dealer's script under the rules of the
  game being played. The search looks through no reloads: a round that would go on past the
  shells in the tube is scored by each seat's share of the charges left. It stops after
  2,000,000 new positions.
- **Advise** uses the Double or Nothing rules, with the maximum charges seat 1 has in the
  written position. It looks through 0 or 1 reloads, as chosen; the command line advisor can
  look through more. It stops after 1,600,000 new positions.
- A search that reaches its limit says so above the moves: "The search stopped early; these
  chances may be off."

The rules themselves, with their sources, are in [docs/RULES.md](../docs/RULES.md).

## How the page reaches the engine

`bridge.cpp` puts five functions over the engine and the solver with embind. Each returns
JSON text, either `{"ok": value}` or `{"error": sentence}`. The engine holds one round at a
time.

| Function | Answer |
|---|---|
| `newRound(mode, seed, charges, seat)` | View. `mode` is `don`, `story1`, `story2` or `story3`; `seed` is 0 to 4294967295; `charges` is 2, 3 or 4 in Double or Nothing, or 0 for the seed to draw it; `seat` is `human` or `solver`. |
| `act(id, slot)` | Step. Plays seat 1's move `id` from `view.legal`. `slot` names the tray slot it spends: a copy of the item, or for an Adrenaline move any Adrenaline. -1 spends the copy the move's own `slot` names, or the first Adrenaline. |
| `advance()` | Step. The next step that is not a person's choice: a load, one pass of the Dealer's turn, or with the solver in seat 1 the move `rank()` puts first. |
| `rank()` | Ranking of seat 1's moves in the round. Its ids are those of `view.legal`. |
| `advise(notation, opponent, reloads)` | Ranking for a written position, against `dealer` or `solver`, through 0 or 1 reloads. Its ids are places in the ranking. The round is not touched. |

`js/engine-worker.js` runs the module in a Worker, and `js/bridge.js` is the page's only way
to it: `newRound({mode, seed, charges, seat})`, `act(id, slot)`, `advance()`, `rank()` and
`advise(notation, {opponent, reloads})`, each returning a Promise. Calls run one at a time in
the order made, and an error rejects with its sentence. Watch and Play each keep a record of
their round's calls (`js/session.js`), and a mode that finds the engine holding the other
round replays its own from that record, with no search.

The values, briefly. Seats are `p1` (seat 1, at the bottom) and `p2` (the Dealer). Items are
the advisor's tokens: `mg`, `beer`, `cig`, `cuff`, `saw`, `phone`, `adr`, `inv` and `med`.

- **View**: `mode`, `stageLabel`, `seed`, `seats` (two Seats), `tube`, `toMove` (a seat, or
  null between turns), `over`, `winner`, `loadNumber`, `legal` (seat 1's Moves, empty unless
  it is to choose) and `notation`, seat 1's view of the position in the advisor's notation.
- **Seat**: `id`, `name`, `charges`, `max`, `faded` (healing does nothing at this many charges
  or fewer; 0 when it always works), `items` (the eight tray slots, a token or null), `hand`
  (the same items in the order they were picked up) and `restraint` (null, `cuffed` or
  `lost a turn`). A seat the turn passed over as the tube ran out stays `cuffed` until the
  reload takes the handcuffs off, since it loses no turn to them.
- **Tube**: `live`, `blank` and `total` count the shells by the type they were loaded as;
  `sawed`; `shells` lists each shell from the chamber on with its `offset`, its type once a
  seat has seen it (`known`, or null) and the seats that know it (`knownBy`).
- **Move**: `id`, `kind` (`shoot` or `item`), `label`, and as they apply `item`, `target` and
  `slot`, the tray slot the item comes from. A seat holding copies of an item in more than one
  run has one move per run, each named by its slot. A steal has `from` and `target` set to the
  seat it takes from, `item` set to the item taken and `slot` set to that seat's slot.
  Adrenaline used alone has `item` set to `adr` and no target.
- **Step**: `events` and the new `view`. Events are `load` (`live`, `blank`, `dealt` per seat,
  `first`, and `freed`, the seats the reload takes the handcuffs off), `shot` (`by`, `target`,
  `shell`, `damage`, the charges it took), `item` (`by`, `item`, `target` when it has one,
  `text`), `learned` (`by`, `offset`, `shell`), `rule` (the Dealer's reason, `text`), `skip`
  (`seat`) and `over` (`winner`). An Adrenaline that takes an item is an `adr` event with a
  target, followed by the event of the item taken.
- **Ranking**: `mover`, `opponent`, `refused` (null, or why there is nothing to rank),
  `moves` (best first, each `id`, `label` and `win`, the chance of being the last seat
  standing), `stopped` (null, or the sentence above) and `assumptions`.

`js/contract.js` checks each of these shapes as the page reads them.

## Testing the engine for the page

`test/smoke.mjs` runs the compiled engine under Node 22 or newer. It plays seeded rounds in
every mode, checks that a seed always deals the same round and that ranking leaves a round
alone, checks every value against the shapes above (and with `js/contract.js`), and compares
`advise()` with the native advisor's `--json` answer.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target advisor
node web/test/smoke.mjs build-web/web build/advisor
```

The `web` job in `.github/workflows/build.yml` installs Emscripten 6.0.10 and runs the build
commands above and these three on every push to `main` and every pull request against it.
