"""tools/regress/gate.py against a stand-in launcher: a replay scenario runs for its own length on a
fresh memory card, compare knows about it, a bad --replay stops the capture before anything runs, and
so does a launcher the host can't start, with a message rather than a traceback.

    python3 -m unittest discover -s tools/regress/tests

DREAM_GATE_PY names another gate.py to test, which is how a change is shown to fail first."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import pathlib
import shlex
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
# of the same scenario compare identical. write_launcher() makes it a program the host can start.
LAUNCHER = r'''
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


def write_launcher(folder: pathlib.Path, script: pathlib.Path) -> None:
    """LAUNCHER as jgr_boot, a program this host starts by name, running this interpreter. A script
    with a #! line is one only on a POSIX host; Windows starts a file by its extension, so there it
    is a batch file (which the gate's arguments pass through: paths, digits, '@' and ',')."""
    script.write_text(LAUNCHER)
    folder.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        (folder / "jgr_boot.cmd").write_text(f'@"{sys.executable}" "{script}" %*\n')
    else:
        exe = folder / "jgr_boot"
        exe.write_text(f'#!/bin/sh\nexec {shlex.quote(sys.executable)} {shlex.quote(str(script))} "$@"\n')
        exe.chmod(exe.stat().st_mode | stat.S_IXUSR)


@contextlib.contextmanager
def windows_error_mode_of_a_shell():
    """On Windows, this process given the error mode a shell gives the programs it starts (pwsh's on
    CI's runner: 0x8003, critical-error, fault and open-file boxes off). Starting a file that isn't a
    program then fails at once (WinError 216). CTest gives its tests Windows' default mode, 0, and
    with it CreateProcess first puts up a "not compatible" box and waits for someone to close it; on
    CI's runner nobody can, so the test hung there until CTest's timeout. Restored afterwards."""
    if os.name != "nt":
        yield
        return
    import ctypes
    kernel32 = ctypes.windll.kernel32
    before = kernel32.GetErrorMode()
    kernel32.SetErrorMode(before | 0x8003)
    try:
        yield
    finally:
        kernel32.SetErrorMode(before)


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
        write_launcher(self.build / "games" / "jgr", self.tmp / "standin.py")
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

    def test_a_launcher_this_host_cannot_start_stops_the_capture_with_a_message(self):
        # The name alone, as a POSIX script with no execute bit or an extensionless file on
        # Windows (which this test's own stand-in was, on the windows-x64 job: WinError 193); and a
        # file this host would start by its name that isn't a program at all.
        nt = os.name == "nt"
        cases = {
            "name only": ("jgr_boot", False),
            "not a program": ("jgr_boot.exe" if nt else "jgr_boot", True),
        }
        for case, (file, startable) in cases.items():
            with self.subTest(case):
                build = self.tmp / case.replace(" ", "-")
                exe = build / "games" / "jgr" / file
                exe.parent.mkdir(parents=True)
                exe.write_bytes(b"not a program\n")
                if startable and not nt:
                    exe.chmod(exe.stat().st_mode | stat.S_IXUSR)
                label = f"broken-{build.name}"
                with windows_error_mode_of_a_shell():
                    rc, _, err = self.run_gate("capture", label, "--build", str(build),
                                               "--config", str(self.config), "--game", "jgr")
                self.assertEqual(rc, 2, err)
                self.assertIn("jgr_boot", err)
                self.assertFalse((self.gate.STORE / label / "attract.hash").exists())

    def test_two_replays_may_not_share_a_name(self):
        rc, _, err = self.capture("before", "--replay", "tutorial", str(self.recording), "60",
                                  "--replay", "tutorial", str(self.recording), "61")
        self.assertEqual(rc, 2)
        self.assertIn("tutorial", err)


if __name__ == "__main__":
    unittest.main()
