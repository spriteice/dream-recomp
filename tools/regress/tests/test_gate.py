"""tools/regress/gate.py against a stand-in launcher: a replay scenario runs for its own length on a
fresh memory card, compare knows about it, and a bad --replay stops the capture before anything runs.

    python3 -m unittest discover -s tools/regress/tests

DREAM_GATE_PY names another gate.py to test, which is how a change is shown to fail first."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import pathlib
import stat
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
GATE_PY = pathlib.Path(os.environ.get("DREAM_GATE_PY", HERE.parent / "gate.py")).resolve()

# The stand-in launcher: takes the launcher's arguments, writes the hash and report files the gate
# reads, makes the card when asked (as the real one formats a new file), refuses a recording that
# isn't there, and leaves its arguments beside the hash for the test to read. The hash content
# depends on what drives the run (seconds, presses, recording), never on the paths, so two captures
# of the same scenario compare identical.
LAUNCHER = r'''#!/usr/bin/env python3
import hashlib, json, os, pathlib, sys, time
argv = sys.argv[1:]
valued = {"--config", "--rtc-seed", "--max-seconds", "--press", "--write-hash", "--report", "--replay-input", "--vmu"}
opts, i = {}, 0
while i < len(argv):
    if argv[i] in valued:
        opts[argv[i]] = argv[i + 1]; i += 2
    else:
        opts[argv[i]] = True; i += 1
note = {"argv": argv, "cwd": os.getcwd()}
if "--vmu" in opts:
    card = pathlib.Path(opts["--vmu"])
    note["card_existed"] = card.exists()
    card.write_bytes(b"card")
if "--replay-input" in opts and not pathlib.Path(opts["--replay-input"]).is_file():
    sys.stderr.write("no recording\n"); sys.exit(1)
drives = [opts.get(k, "") for k in ("--max-seconds", "--press", "--replay-input")]
seconds = int(opts["--max-seconds"])
h = pathlib.Path(opts["--write-hash"])
lines = ["# frame cycles hash writes"]
for f in range(seconds * 2):
    lines.append(f"{f} 0 {hashlib.sha256((repr(drives) + str(f)).encode()).hexdigest()[:16]} 1")
h.write_text("\n".join(lines) + "\n")
pathlib.Path(opts["--report"]).write_text(
    f"stop: time limit\nguest: {seconds}.000 s, {seconds * 2} frames\nhost: {time.time()} s (1.0x real time)\n"
    f"interrupts delivered: {seconds * 7}, traps: 0\n")
h.with_suffix(".argv.json").write_text(json.dumps(note))
'''


def load_gate():
    spec = importlib.util.spec_from_file_location("gate_under_test", GATE_PY)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod   # dataclasses look the module up by name
    spec.loader.exec_module(mod)
    return mod


class GateTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="gate-test-"))
        self.gate = load_gate()
        self.gate.STORE = self.tmp / ".regress"
        self.build = self.tmp / "build"
        exe = self.build / "games" / "jgr" / "jgr_boot"
        exe.parent.mkdir(parents=True)
        exe.write_text(LAUNCHER)
        exe.chmod(exe.stat().st_mode | stat.S_IXUSR)
        self.config = self.tmp / "jgr.toml"
        self.config.write_text("[game]\n")
        self.recording = self.tmp / "tutorial.txt"
        self.recording.write_text("# frame buttons\n0 ffff 0 0 128 128 128 128\n")

    def run_gate(self, *args: str):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = self.gate.main(list(args))
        return rc, out.getvalue(), err.getvalue()

    def capture(self, label: str, *extra: str):
        return self.run_gate("capture", label, "--build", str(self.build), "--config", str(self.config),
                             "--game", "jgr", *extra)

    def argv_of(self, label: str, name: str) -> dict:
        return json.loads((self.gate.STORE / label / f"{name}.argv.json").read_text())

    def test_a_replay_scenario_runs_for_its_own_length_on_a_fresh_card(self):
        rc, out, err = self.capture("before", "--replay", "tutorial", str(self.recording), "1110")
        self.assertEqual(rc, 0, err)
        store = self.gate.STORE / "before"
        for name in ("attract", "gameplay", "tutorial"):
            self.assertTrue((store / f"{name}.hash").exists(), name)
            self.assertTrue((store / f"{name}.counters").exists(), name)
        tut = self.argv_of("before", "tutorial")["argv"]
        self.assertIn("--max-seconds", tut)
        self.assertEqual(tut[tut.index("--max-seconds") + 1], "1110")
        self.assertEqual(tut[tut.index("--replay-input") + 1], str(self.recording.resolve()))
        self.assertEqual(tut[tut.index("--vmu") + 1], str(store / "tutorial.card.bin"))
        self.assertTrue((store / "tutorial.card.bin").exists())
        for name in ("attract", "gameplay"):
            argv = self.argv_of("before", name)["argv"]
            self.assertEqual(argv[argv.index("--max-seconds") + 1], "40", name)
            self.assertNotIn("--vmu", argv, name)
            self.assertNotIn("--replay-input", argv, name)
        self.assertIn("tutorial", out)
        self.assertIn("frames=2221", out)   # the gate counts the hash file's lines, header included
        # The counters drop the timing line, as for the built-in scenarios.
        self.assertNotIn("real time", (store / "tutorial.counters").read_text())

    def test_a_card_left_by_the_last_capture_is_removed_first(self):
        store = self.gate.STORE / "before"
        store.mkdir(parents=True)
        (store / "tutorial.card.bin").write_bytes(b"a save from last time")
        rc, _, err = self.capture("before", "--replay", "tutorial", str(self.recording), "60")
        self.assertEqual(rc, 0, err)
        self.assertFalse(self.argv_of("before", "tutorial")["card_existed"])

    def test_compare_covers_the_replay_scenario_and_misses_it_loudly(self):
        self.assertEqual(self.capture("before", "--replay", "tutorial", str(self.recording), "60")[0], 0)
        self.assertEqual(self.capture("after", "--replay", "tutorial", str(self.recording), "60")[0], 0)
        rc, out, err = self.run_gate("compare", "before", "after")
        self.assertEqual(rc, 0, err)
        self.assertRegex(out, r"tutorial\s+identical")
        # The same scenario driven differently is a change, not a match.
        self.assertEqual(self.capture("longer", "--replay", "tutorial", str(self.recording), "61")[0], 0)
        rc, out, _ = self.run_gate("compare", "before", "longer")
        self.assertEqual(rc, 1)
        self.assertRegex(out, r"tutorial\s+CHANGED")
        # A capture without the replay has nothing to compare it with: MISSING, never identical.
        self.assertEqual(self.capture("plain")[0], 0)
        rc, out, err = self.run_gate("compare", "before", "plain")
        self.assertEqual(rc, 1)
        self.assertRegex(err, r"tutorial\s+MISSING")
        self.assertRegex(out, r"attract\s+identical")
        self.assertRegex(out, r"gameplay\s+identical")

    def test_a_bad_replay_stops_the_capture_before_anything_runs(self):
        cases = [
            ("tutorial", str(self.tmp / "missing.txt"), "60"),   # no recording
            ("attract", str(self.recording), "60"),              # a built-in scenario's name
            ("tutorial", str(self.recording), "sixty"),          # not a number
            ("tutorial", str(self.recording), "0"),              # not a length
            ("../tutorial", str(self.recording), "60"),          # not a file name
        ]
        for name, file, seconds in cases:
            rc, _, err = self.capture("before", "--replay", name, file, seconds)
            self.assertEqual(rc, 2, (name, file, seconds))
            self.assertIn(name, err)
            self.assertFalse((self.gate.STORE / "before").exists(), (name, file, seconds))

    def test_two_replays_may_not_share_a_name(self):
        rc, _, err = self.capture("before", "--replay", "tutorial", str(self.recording), "60",
                                  "--replay", "tutorial", str(self.recording), "61")
        self.assertEqual(rc, 2)
        self.assertIn("tutorial", err)


if __name__ == "__main__":
    unittest.main()
