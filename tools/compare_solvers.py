#!/usr/bin/env python3
"""Differential test: the C++ solver against the independent Python oracle.

Two implementations written from the same rules, sharing no code, have to agree
on every value. Where they do not, one of them is wrong, and the position is
printed so it can be argued about.

    python3 tools/compare_solvers.py --advisor build/advisor
    python3 tools/compare_solvers.py --advisor build/advisor --random 200 --seed 7

Exit status is non-zero when any position disagrees by more than the tolerance.
"""

from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ORACLE = REPO / "tools" / "oracle" / "oracle.py"

# Positions chosen to exercise one mechanism each, in the notation both sides
# accept. Keep these small: the oracle is written for clarity, not for speed.
FIXED_POSITIONS = [
    "p1=1/1 p2=1/1 tube=1L1B turn=p1",
    "p1=1/1 p2=1/1 tube=1L0B turn=p1",
    "p1=1/1 p2=2/2 tube=1L0B turn=p1 sawed",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1",
    "p1=1/2 p2=2/2 tube=1L2B turn=p1",
    "p1=2/2[saw] p2=2/2 tube=1L1B turn=p1",
    "p1=1/1[cuff] p2=1/1 tube=1L1B turn=p1",
    "p1=1/1[mg] p2=1/1 tube=1L1B turn=p1",
    "p1=1/2[cig] p2=2/2 tube=1L1B turn=p1",
    "p1=1/2[med] p2=1/1 tube=1L0B turn=p1",
    "p1=1/1[inv] p2=1/1 tube=1L3B turn=p1",
    "p1=2/2[beer] p2=2/2 tube=1L2B turn=p1",
    "p1=2/2[phone] p2=2/2 tube=2L2B turn=p1",
    "p1=2/2[adr] p2=2/2[saw] tube=1L1B turn=p1",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:0L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:1B",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2",
    "p1=1/2 p2=1/2 tube=2L1B turn=p1 sawed",
    "p1=2/3[saw,beer] p2=2/3[cuff] tube=2L2B turn=p1",
    "p1=1/1 p2=1/1 tube=1L1B turn=p1 cuffed=p2",
    # The seat to move cannot see what the other seat has: both solvers have to
    # choose from the mover's own information state rather than from the
    # position as it really is.
    "p1=1/1 p2=1/1 tube=1L1B turn=p2 known=p1:0L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p1:1L",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p1:0B,2L",
    "p1=2/2[saw] p2=2/2 tube=2L2B turn=p2 known=p1:0L",
    # The other way round: p2 has looked at a shell and the advised seat has
    # not. Neither solver may read it, and neither may forget that p2 can, so
    # both have to average over the ways it could have fallen.
    "p1=1/1 p2=1/1 tube=1L1B turn=p1 known=p2:0L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p2:1L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B",
    "p1=2/2[saw] p2=2/2[beer] tube=1L2B turn=p1 known=p2:2L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:0L known=p2:2B",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:1B,3L",
    # The third story stage's faded band, which only bites under --heal-floor 2:
    # a seat on one charge there has no normal charges left, so healing cannot
    # reach it and any hit is fatal. Under the default floor these are ordinary
    # positions, so both runs are worth having.
    "p1=1/5[cig] p2=2/5 tube=1L1B turn=p1",
    "p1=1/5[med] p2=2/5 tube=1L1B turn=p1",
    "p1=2/5[cig,med] p2=1/5 tube=1L1B turn=p1",
    "p1=1/5[cig] p2=1/5[med] tube=2L1B turn=p2",
]

ITEMS = ["mg", "beer", "cig", "cuff", "saw", "phone", "inv", "med"]
TUBES = ["1L1B", "1L2B", "2L1B", "2L2B", "1L3B", "3L1B", "1L0B", "0L2B", "2L3B"]


def random_positions(count: int, seed: int) -> list[str]:
    rng = random.Random(seed)
    out = []
    while len(out) < count:
        tube = rng.choice(TUBES)
        live = int(tube.split("L")[0])
        blank = int(tube.split("L")[1][:-1])
        if live + blank == 0:
            continue
        maxhp = rng.randint(1, 3)
        hp1 = rng.randint(1, maxhp)
        hp2 = rng.randint(1, maxhp)
        held = []
        for seat in range(2):
            items = [rng.choice(ITEMS) for _ in range(rng.randint(0, 2))]
            held.append("[" + ",".join(items) + "]" if items else "")
        turn = rng.choice(["p1", "p2"])
        extras = []
        if rng.random() < 0.15:
            extras.append("sawed")
        if rng.random() < 0.20 and live + blank >= 2:
            offset = rng.randrange(live + blank)
            kind = "L" if (live > 0 and rng.random() < live / (live + blank)) else "B"
            # Half the time the shell belongs to the seat that is not being
            # advised, which is the case the two solvers have to average over.
            watcher = "p1" if rng.random() < 0.5 else "p2"
            extras.append(f"known={watcher}:{offset}{kind}")
        if rng.random() < 0.10:
            extras.append("cuffed=" + ("p2" if turn == "p1" else "p1"))
        out.append(
            f"p1={hp1}/{maxhp}{held[0]} p2={hp2}/{maxhp}{held[1]} "
            f"tube={tube} turn={turn} " + " ".join(extras)
        )
    return out


def run_advisor(binary: str, position: str, seat: int, reloads: int,
                heal_floor: int = 1) -> dict:
    result = subprocess.run(
        [binary, "--position", position, "--seat", str(seat), "--reloads", str(reloads),
         "--heal-floor", str(heal_floor), "--json"],
        capture_output=True,
        text=True,
        timeout=600,
    )
    if result.returncode != 0:
        raise RuntimeError(f"advisor failed on {position}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def run_oracle(position: str, seat: int, reloads: int, heal_floor: int = 1) -> dict:
    result = subprocess.run(
        [sys.executable, str(ORACLE), "--position", position, "--seat", str(seat),
         "--reloads", str(reloads), "--heal-floor", str(heal_floor)],
        capture_output=True,
        text=True,
        timeout=900,
    )
    if result.returncode != 0:
        raise RuntimeError(f"oracle failed on {position}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def compare(position: str, seat: int, reloads: int, binary: str, tolerance: float,
            heal_floor: int = 1) -> list[str]:
    problems = []
    mine = run_advisor(binary, position, seat, reloads, heal_floor)
    theirs = run_oracle(position, seat, reloads, heal_floor)

    if abs(mine["value"] - theirs["value"]) > tolerance:
        problems.append(
            f"value {mine['value']:.6f} against {theirs['value']:.6f}"
        )

    mine_actions = {entry["action"]: entry["value"] for entry in mine["actions"]}
    their_actions = {entry["action"]: entry["value"] for entry in theirs["actions"]}
    only_mine = sorted(set(mine_actions) - set(their_actions))
    only_theirs = sorted(set(their_actions) - set(mine_actions))
    if only_mine:
        problems.append("moves only the C++ solver offers: " + ", ".join(only_mine))
    if only_theirs:
        problems.append("moves only the oracle offers: " + ", ".join(only_theirs))
    for action in sorted(set(mine_actions) & set(their_actions)):
        gap = abs(mine_actions[action] - their_actions[action])
        if gap > tolerance:
            problems.append(
                f"{action}: {mine_actions[action]:.6f} against "
                f"{their_actions[action]:.6f}, apart by {gap:.6f}"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--advisor", default=str(REPO / "build" / "advisor"))
    parser.add_argument("--reloads", type=int, default=0)
    parser.add_argument("--tolerance", type=float, default=1e-9)
    parser.add_argument("--random", type=int, default=0, help="extra random positions")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--seat", type=int, default=1)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--heal-floor", type=int, default=1,
                        help="charges below which healing does nothing, for the "
                             "third story stage's faded band (default 1)")
    args = parser.parse_args()

    if not Path(args.advisor).exists():
        print(f"no advisor at {args.advisor}; build it first", file=sys.stderr)
        return 2
    if not ORACLE.exists():
        print(f"no oracle at {ORACLE}", file=sys.stderr)
        return 2

    positions = list(FIXED_POSITIONS)
    if args.random:
        positions += random_positions(args.random, args.seed)

    failures = 0
    for position in positions:
        try:
            problems = compare(position, args.seat, args.reloads, args.advisor,
                               args.tolerance, args.heal_floor)
        except Exception as error:  # noqa: BLE001 - report and keep going
            print(f"FAIL {position}\n     {error}")
            failures += 1
            continue
        if problems:
            failures += 1
            print(f"FAIL {position}")
            for problem in problems:
                print(f"     {problem}")
        elif not args.quiet:
            print(f"ok   {position}")

    print(f"\n{len(positions) - failures} of {len(positions)} positions agree")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
