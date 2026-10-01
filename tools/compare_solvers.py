#!/usr/bin/env python3
"""Differential test: the C++ solver against the independent Python oracle.

Two implementations written from the same rules, sharing no code, have to agree
on every value. Where they do not, one of them is wrong, and the position is
printed so it can be argued about.

    python3 tools/compare_solvers.py --advisor build/advisor
    python3 tools/compare_solvers.py --advisor build/advisor --random 200 --seed 7
    python3 tools/compare_solvers.py --advisor build/advisor --opponent dealer --mode story

Exit status is non-zero when any position disagrees by more than the tolerance.
"""

from __future__ import annotations

import argparse
import json
import random
import re
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

# Positions for --opponent dealer: two seats, p1 advised, p2 the scripted
# Dealer. Most have p2 to move at the root, where neither side lists moves and
# the value is the Dealer turn's value. Each is checked in the mode given by
# --mode, so each runs under both of the Dealer's brains across the two modes.
DEALER_POSITIONS = [
    # Hand-checkable values.
    "p1=1/1 p2=1/1 tube=1L1B turn=p2",
    "p1=1/1 p2=1/1 tube=2L1B turn=p2",
    "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p1:1B,2B known=p2:1B,2B",
    "p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1",
    "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:1B",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed",
    # Each scanned item held by the Dealer.
    "p1=2/2 p2=2/2[mg] tube=1L2B turn=p2",
    "p1=2/2 p2=1/2[cig] tube=2L2B turn=p2",
    "p1=2/2 p2=2/4[med] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[beer] tube=1L2B turn=p2",
    "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[saw] tube=2L1B turn=p2",
    "p1=2/2 p2=2/2[mg,saw] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[phone] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[mg,inv] tube=1L2B turn=p2",
    # Each scanned item taken from p1 with an Adrenaline.
    "p1=2/2[mg] p2=2/2[adr] tube=1L2B turn=p2",
    "p1=2/2[cig] p2=1/2[adr] tube=2L2B turn=p2",
    "p1=2/2[med] p2=2/4[adr] tube=2L2B turn=p2",
    "p1=2/2[beer] p2=2/2[adr] tube=1L2B turn=p2",
    "p1=2/2[cuff] p2=2/2[adr] tube=2L2B turn=p2",
    "p1=2/2[saw] p2=2/2[adr] tube=2L1B turn=p2",
    "p1=2/2[mg,saw] p2=2/2[adr,adr] tube=2L2B turn=p2",
    "p1=2/2[phone] p2=2/2[adr] tube=2L2B turn=p2",
    "p1=2/2[inv] p2=2/2[mg,adr] tube=1L2B turn=p2",
    # Expired Medicine blocked by reachable cigarettes, by the item list left
    # after the last Adrenaline is spent, at one charge, and after one use.
    "p1=2/2[cig] p2=3/4[adr,med] tube=2L2B turn=p2",
    "p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2",
    "p1=2/2 p2=1/4[med] tube=2L2B turn=p2",
    "p1=2/2 p2=2/5[med,med] tube=2L2B turn=p2",
    # Handcuffs refused: p1 already cuffed, p1 skipped at the root, one shell.
    "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2 cuffed=p1",
    "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p1 cuffed=p1",
    "p1=2/2 p2=2/2[cuff] tube=1L0B turn=p2",
    # Deduction from the chamber seen by both, from the counts, and from the
    # offsets the Dealer has seen, with those facts written for both seats.
    "p1=2/2 p2=2/2 tube=1L1B turn=p2 known=p1:0L known=p2:0L",
    "p1=2/2 p2=2/2 tube=2L0B turn=p2",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p1:1L,2L known=p2:1L,2L",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p1:0B known=p2:0B",
    # Dealer memory only p2 has, which the root redraws.
    "p1=2/2 p2=2/2[mg] tube=2L2B turn=p2 known=p2:1L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B",
    "p1=2/2 p2=2/2[beer] tube=1L3B turn=p2 known=p2:1B,2L",
    # A cuffed p2 skipped at the root, and p1 to move at the root.
    "p1=2/2 p2=2/2[cuff] tube=2L1B turn=p2 cuffed=p2",
    "p1=2/2[saw] p2=2/2[mg,beer] tube=2L2B turn=p1",
    "p1=1/2[cuff,mg] p2=2/2[cig,saw] tube=1L2B turn=p1",
    "p1=2/2[beer] p2=2/2[mg] tube=2L2B turn=p1 known=p2:0L",
    # Sawed barrel, a pending inversion, and longer item chains.
    "p1=2/2 p2=2/2[mg] tube=2L1B turn=p2 sawed",
    "p1=1/1 p2=1/1 tube=1L1B turn=p2 inverted",
    "p1=2/2 p2=2/2[mg,beer] tube=1L3B turn=p2",
    "p1=2/2 p2=2/2[mg,beer,inv] tube=1L3B turn=p2",
    "p1=2/2 p2=2/2[phone] tube=1L2B turn=p2",
    "p1=2/3[cig,saw] p2=2/3[adr,cuff,phone] tube=2L2B turn=p2",
]

ITEMS = ["mg", "beer", "cig", "cuff", "saw", "phone", "inv", "med"]
DEALER_ITEMS = {
    "don": ["mg", "beer", "cig", "cuff", "saw", "phone", "adr", "inv", "med"],
    "story": ["mg", "beer", "cig", "cuff", "saw"],
}
TUBES = ["1L1B", "1L2B", "2L1B", "2L2B", "1L3B", "3L1B", "1L0B", "0L2B", "2L3B"]
# The advisor's name for each --mode; the oracle takes the name as given.
ADVISOR_MODES = {"don": "don", "story": "story2"}


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


def random_dealer_positions(count: int, seed: int, mode: str) -> list[str]:
    """Two-seat positions for the Dealer opponent. Shell facts are read off one
    hidden shuffle of the tube, so facts written for p1 and for p2 agree."""
    rng = random.Random(seed)
    pool = DEALER_ITEMS[mode]
    out = []
    while len(out) < count:
        tube = rng.choice(TUBES)
        live = int(tube.split("L")[0])
        blank = int(tube.split("L")[1][:-1])
        if live + blank == 0:
            continue
        shells = ["L"] * live + ["B"] * blank
        rng.shuffle(shells)
        maxhp = rng.randint(1, 3)
        hp1 = rng.randint(1, maxhp)
        hp2 = rng.randint(1, maxhp)
        held = []
        for seat in range(2):
            items = [rng.choice(pool) for _ in range(rng.randint(0, 3))]
            held.append("[" + ",".join(items) + "]" if items else "")
        turn = rng.choice(["p1", "p2"])
        extras = []
        if rng.random() < 0.15:
            extras.append("sawed")
        # Dealer memory, which the root redraws unless p1 has seen the same
        # shell, and p1's own facts, some of them shared with the Dealer.
        if rng.random() < 0.35:
            seen = sorted(rng.sample(range(len(shells)), rng.randint(1, min(2, len(shells)))))
            extras.append("known=p2:" + ",".join(f"{i}{shells[i]}" for i in seen))
            if rng.random() < 0.5:
                both = [i for i in seen if rng.random() < 0.5] or seen[:1]
                extras.append("known=p1:" + ",".join(f"{i}{shells[i]}" for i in both))
        elif rng.random() < 0.20:
            i = rng.randrange(len(shells))
            extras.append(f"known=p1:{i}{shells[i]}")
        roll = rng.random()
        if roll < 0.10:
            extras.append("cuffed=" + ("p2" if turn == "p1" else "p1"))
        elif roll < 0.15:
            extras.append("cuffed=" + turn)
        out.append(
            f"p1={hp1}/{maxhp}{held[0]} p2={hp2}/{maxhp}{held[1]} "
            f"tube={tube} turn={turn} " + " ".join(extras)
        )
    return out


def model_flags(mode: str, opponent: str, advisor: bool) -> list[str]:
    """--mode and --opponent for one solver. A flag at its default is left out,
    so the default run sends each solver the same argv as before either flag
    existed."""
    flags = []
    if mode != "don":
        flags += ["--mode", ADVISOR_MODES[mode] if advisor else mode]
    if opponent != "solver":
        flags += ["--opponent", opponent]
    return flags


def run_advisor(binary: str, position: str, seat: int, reloads: int,
                heal_floor: int = 1, mode: str = "don", opponent: str = "solver") -> dict:
    result = subprocess.run(
        [binary, "--position", position, "--seat", str(seat), "--reloads", str(reloads),
         "--heal-floor", str(heal_floor), "--json"] + model_flags(mode, opponent, True),
        capture_output=True,
        text=True,
        timeout=600,
    )
    if result.returncode != 0:
        raise RuntimeError(f"advisor failed on {position}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def run_oracle(position: str, seat: int, reloads: int, heal_floor: int = 1,
               mode: str = "don", opponent: str = "solver") -> dict:
    result = subprocess.run(
        [sys.executable, str(ORACLE), "--position", position, "--seat", str(seat),
         "--reloads", str(reloads), "--heal-floor", str(heal_floor)]
        + model_flags(mode, opponent, False),
        capture_output=True,
        text=True,
        timeout=900,
    )
    if result.returncode != 0:
        raise RuntimeError(f"oracle failed on {position}: {result.stderr.strip()}")
    return json.loads(result.stdout)


# The C++ advisor names the victim of a stolen Handcuffs or Jammer ("steal
# Handcuffs from p2 and use it on p3"); the oracle gives one text per steal,
# "steal Handcuffs from p2 and use it", valued at the best target for the seat
# to move. Both styles are documented, so the advisor's rows are folded into
# the oracle's form before the two lists are matched.
STEAL_TARGET = re.compile(r" on p\d+$")


def fold_steal_targets(actions: list[dict], seat: int) -> dict[str, float]:
    """Advisor rows keyed by the oracle's action text.

    The seat to move never reads "shoot p<its own number>" (it reads "shoot
    self"), so the advised seat is to move exactly when no row shoots it. Its
    folded steals keep the largest value; another mover's keep the smallest.
    """
    labels = {entry["action"] for entry in actions}
    pick = min if f"shoot p{seat}" in labels else max
    folded: dict[str, float] = {}
    for entry in actions:
        label = entry["action"]
        if label.startswith("steal "):
            label = STEAL_TARGET.sub("", label)
        if label in folded:
            folded[label] = pick(folded[label], entry["value"])
        else:
            folded[label] = entry["value"]
    return folded


def compare(position: str, seat: int, reloads: int, binary: str, tolerance: float,
            heal_floor: int = 1, mode: str = "don", opponent: str = "solver") -> list[str]:
    problems = []
    mine = run_advisor(binary, position, seat, reloads, heal_floor, mode, opponent)
    theirs = run_oracle(position, seat, reloads, heal_floor, mode, opponent)

    if abs(mine["value"] - theirs["value"]) > tolerance:
        problems.append(
            f"value {mine['value']:.6f} against {theirs['value']:.6f}"
        )

    mine_actions = fold_steal_targets(mine["actions"], seat)
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
    parser.add_argument("--opponent", choices=("solver", "optimal", "dealer"),
                        default="solver",
                        help="opponent model passed to both solvers: solver "
                             "(default), the minimising opponent, which the older "
                             "name optimal also selects; or dealer, the scripted "
                             "Dealer as p2, which checks DEALER_POSITIONS and needs "
                             "--seat 1")
    parser.add_argument("--mode", choices=("don", "story"), default="don",
                        help="don (default) or story; story is sent to the advisor "
                             "as story2 and to the oracle as story")
    args = parser.parse_args()
    if args.opponent == "optimal":
        args.opponent = "solver"
    if args.opponent == "dealer" and args.seat != 1:
        parser.error("--opponent dealer advises p1 only: use --seat 1")

    if not Path(args.advisor).exists():
        print(f"no advisor at {args.advisor}; build it first", file=sys.stderr)
        return 2
    if not ORACLE.exists():
        print(f"no oracle at {ORACLE}", file=sys.stderr)
        return 2

    if args.opponent == "dealer":
        positions = list(DEALER_POSITIONS)
        if args.random:
            positions += random_dealer_positions(args.random, args.seed, args.mode)
    else:
        positions = list(FIXED_POSITIONS)
        if args.random:
            positions += random_positions(args.random, args.seed)

    failures = 0
    for position in positions:
        try:
            problems = compare(position, args.seat, args.reloads, args.advisor,
                               args.tolerance, args.heal_floor, args.mode, args.opponent)
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
