#!/usr/bin/env python3
"""Did that change alter what the guest does?

Every guest write is hashed per frame, so two runs with the same hash did bit-for-bit the same
thing. That is the only signal that reliably catches a translation change: frame counts, texture
counters, function boundaries and the unit tests all called a behaviour change clean during the
0x0C081E56 investigation, and only the hash disagreed.

    python3 tools/regress/gate.py capture before
    # ... change something, rebuild ...
    python3 tools/regress/gate.py capture after
    python3 tools/regress/gate.py compare before after

Two scenarios are always run, 40 s each: `attract`, which any build reaches, and `gameplay`, driven
with scripted presses into actual play. They exercise very different code and a change can be free
in one and costly in the other.

A play-through recorded with `--record-input` joins them as a replay scenario:

    python3 tools/regress/gate.py capture before --replay tutorial recordings/tutorial.txt 1110

runs `tutorial` for 1110 s of guest time with `--replay-input` on a memory card that did not exist
when the run started: the launcher formats a new one, as a player's first run does, and a card left
by an earlier capture is removed first, because a save on it would send the replay elsewhere. Give
the same --replay to every capture you mean to compare; compare reports a scenario one side lacks
as MISSING, never as a match.

Captures live in .regress/ at the repository root, which is gitignored. They are keyed to a disc
and a build, so they mean nothing on another machine.
"""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
STORE = REPO / ".regress"

# Pinned so two captures are comparable. --rtc-seed is not optional: without it the console clock
# comes from the host date and two runs of the same build diverge at about frame 24.
COMMON = ["--no-audio", "--rtc-seed", "1"]


@dataclasses.dataclass(frozen=True)
class Scenario:
    seconds: int                          # guest time, --max-seconds
    args: tuple[str, ...] = ()            # what drives the run
    replay: pathlib.Path | None = None    # a --record-input file, played back on a fresh memory card


SCENARIOS = {
    "attract": Scenario(40),
    "gameplay": Scenario(40, ("--press", "start@300,start@600,a@900,start@1200,start@1800,a@2400,start@3000")),
}
# A scenario names its files, so its name is a plain file name.
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\Z")
# The scenarios a capture ran, one name per line, so compare knows what to expect from it. A
# capture made before this file existed ran the built-in scenarios only.
MANIFEST = "scenarios"


class Usage(Exception):
    """A command line that can't be run as given."""


# Timings differ between runs by design and say nothing about behaviour. Dropped whole lines rather
# than substituted: an alternation like `host:|.*real time` matches the shorter branch first and
# leaves the rest of the line behind, which then differs between captures and fails the gate for no
# reason.
def strip_timings(report: str) -> str:
    drop = ("real time", "per second", "elapsed")
    return "\n".join(ln for ln in report.splitlines()
                      if not ln.startswith("host:") and not any(d in ln for d in drop))


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:16]


def replay_scenarios(specs: list[list[str]]) -> dict[str, Scenario]:
    """The --replay NAME FILE SECONDS arguments as scenarios, checked before anything runs."""
    out: dict[str, Scenario] = {}
    for name, file, seconds in specs:
        if not NAME.match(name):
            raise Usage(f"--replay {name}: a scenario name is letters, digits, '_', '.' and '-'")
        if name in SCENARIOS or name in out:
            raise Usage(f"--replay {name}: that scenario exists already")
        try:
            n = int(seconds)
        except ValueError:
            n = 0
        if n <= 0:
            raise Usage(f"--replay {name}: SECONDS is a whole number of guest seconds, not {seconds!r}")
        # Absolute, because the launcher runs with the repository as its working directory.
        path = pathlib.Path(file).resolve()
        if not path.is_file():
            raise Usage(f"--replay {name}: no recording at {path}")
        out[name] = Scenario(n, (), path)
    return out


def scenarios_of(label: str) -> set[str]:
    m = STORE / label / MANIFEST
    if not m.exists():
        return set(SCENARIOS)
    return {ln.strip() for ln in m.read_text().splitlines() if ln.strip()}


def launcher(folder: pathlib.Path, game: str) -> pathlib.Path | None:
    """The game's launcher in a build's folder, found as this host finds a program it can start: on
    Windows by the extensions in PATHEXT (jgr_boot.exe), elsewhere by the bare name with its execute
    bit set. A file that merely has the name isn't one: Windows can't start an extensionless file,
    and nothing starts one without its execute bit."""
    # A path with a folder in it, so which() looks there only, never in the working directory.
    found = shutil.which(str(folder / f"{game}_boot"))
    return pathlib.Path(found) if found else None


def capture(label: str, build: str, config: str, game: str,
            replays: dict[str, Scenario] | None = None) -> int:
    folder = REPO / build / "games" / game
    exe = launcher(folder, game)
    if exe is None:
        print(f"no launcher {game}_boot in {folder} that this host can start", file=sys.stderr)
        return 2
    scenarios = {**SCENARIOS, **(replays or {})}
    out = STORE / label
    out.mkdir(parents=True, exist_ok=True)
    (out / MANIFEST).write_text("".join(f"{name}\n" for name in scenarios))
    for name, sc in scenarios.items():
        h, rep = out / f"{name}.hash", out / f"{name}.report"
        extra = list(sc.args)
        if sc.replay is not None:
            # A fresh card every time: the launcher formats a file that doesn't exist, as a player's
            # first run does, and a save the last capture left would send the replay elsewhere.
            card = out / f"{name}.card.bin"
            card.unlink(missing_ok=True)
            extra += ["--replay-input", str(sc.replay), "--vmu", str(card)]
        try:
            proc = subprocess.run(
                [str(exe), "--config", str(REPO / config), *COMMON, "--max-seconds", str(sc.seconds),
                 *extra, "--write-hash", str(h), "--report", str(rep)],
                capture_output=True, text=True, timeout=3600, cwd=str(REPO))
        except OSError as e:
            # Found but not a program this host runs: a build for another platform, or a broken file.
            print(f"  {label}/{name}: could not start {exe} -- {e}", file=sys.stderr)
            return 2
        if not h.exists() or h.stat().st_size == 0:
            tail = (proc.stderr or proc.stdout or "").strip().split("\n")[-1:]
            print(f"  {label}/{name}: NO OUTPUT -- {' '.join(tail)}", file=sys.stderr)
            return 2
        counters = strip_timings(rep.read_text()) if rep.exists() else ""
        (out / f"{name}.counters").write_text(counters)
        frames = sum(1 for _ in h.open())
        print(f"  {label:14} {name:9} frames={frames:<6} writehash={digest(h)} "
              f"counters={hashlib.sha256(counters.encode()).hexdigest()[:16]}")
    return 0


def compare(a: str, b: str) -> int:
    bad = False
    # Every scenario either capture ran: one that the other lacks is a missing capture below.
    names = scenarios_of(a) | scenarios_of(b)
    order = [n for n in SCENARIOS if n in names] + sorted(n for n in names if n not in SCENARIOS)
    for name in order:
        pa, pb = STORE / a / f"{name}.hash", STORE / b / f"{name}.hash"
        # Loudly, not silently. Comparing two absent files used to report a match, which is the
        # worst possible failure mode for a gate: it says "no regression" when it checked nothing.
        missing = [p for p in (pa, pb) if not p.exists()]
        for p in missing:
            shown = p.relative_to(REPO) if p.is_relative_to(REPO) else p
            print(f"  {name:9} MISSING {shown} -- capture it before comparing", file=sys.stderr)
        if missing:
            # Per scenario, not overall: one missing capture must not hide the other scenario's
            # result the way a shared flag did.
            bad = True
            continue
        ha, hb = digest(pa), digest(pb)
        ca = STORE / a / f"{name}.counters"
        cb = STORE / b / f"{name}.counters"
        same_c = ca.exists() and cb.exists() and ca.read_text() == cb.read_text()
        if ha == hb and same_c:
            print(f"  {name:9} identical")
        else:
            bad = True
            print(f"  {name:9} CHANGED  writehash {ha} -> {hb}"
                  f"{'' if same_c else '   counters differ too'}")
    if bad:
        print("\n  A changed hash is a fact; whether it matters is a judgement. Understand it "
              "before continuing.", file=sys.stderr)
    return 1 if bad else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture", help="run the scenarios and store the hashes")
    c.add_argument("label")
    c.add_argument("--build", default="build")
    c.add_argument("--config", default="games/crazytaxi/crazytaxi.toml")
    c.add_argument("--game", default="crazytaxi")
    c.add_argument("--replay", nargs=3, action="append", default=[],
                   metavar=("NAME", "FILE", "SECONDS"),
                   help="also run NAME: the --record-input FILE played back for SECONDS of guest "
                        "time on a memory card that did not exist (repeatable)")
    d = sub.add_parser("compare", help="compare two captures")
    d.add_argument("before")
    d.add_argument("after")
    a = ap.parse_args(argv)
    if a.cmd == "capture":
        try:
            replays = replay_scenarios(a.replay)
        except Usage as e:
            print(f"gate: {e}", file=sys.stderr)
            return 2
        return capture(a.label, a.build, a.config, a.game, replays)
    return compare(a.before, a.after)


if __name__ == "__main__":
    raise SystemExit(main())
