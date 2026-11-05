"""`dcdisc new-game` and `dcdisc doctor`, on the synthetic disc.

new-game writes the two files a title needs and nothing else. The things worth pinning are the ones
that would be silently wrong rather than loudly broken: the boot filename comes from IP.BIN rather
than being assumed, and the generated config is the shape the C++ loader accepts.
"""

import ntpath
import os

from dcdisc.cli import main
from dcdisc.project import _relpath_or_abspath, _slug


def test_slug_from_a_title():
    assert _slug("CHARGEN BLAST") == "chargenblast"
    assert _slug("MSR - Metropolis Street Racer") == "msrmetropolisstreetracer"
    assert _slug("!!!") == "game"  # never an empty directory name


def test_new_game_writes_config_and_build_file(gdi_path, tmp_path):
    games = tmp_path / "games"
    rc = main(["new-game", gdi_path, "synth", "--dir", str(games), "--no-extract"])
    assert rc == 0
    toml = (games / "synth" / "synth.toml").read_text()
    # Read off IP.BIN, not assumed. The synthetic disc's boot file happens to be the usual name;
    # a title whose IP.BIN says otherwise (Charge 'N Blast says 1ST_READ.US) must follow it.
    assert 'path = "extracted/fs/1ST_READ.BIN"' in toml
    assert 'id = "synth"' in toml
    assert "load_address = 0x8C010000" in toml
    assert "link_address = 0x0C010000" in toml
    assert "entry = 0x8C010000" in toml
    cmake = (games / "synth" / "CMakeLists.txt").read_text()
    assert "dream_add_game(synth" in cmake
    # --no-extract writes the files and nothing else.
    assert not (games / "synth" / "extracted").exists()


def test_new_game_refuses_to_overwrite(gdi_path, tmp_path):
    games = tmp_path / "games"
    assert main(["new-game", gdi_path, "synth", "--dir", str(games), "--no-extract"]) == 0
    # A second run must not quietly discard a config someone has since edited by hand.
    assert main(["new-game", gdi_path, "synth", "--dir", str(games), "--no-extract"]) == 2
    assert main(["new-game", gdi_path, "synth", "--dir", str(games), "--no-extract",
                 "--force"]) == 0


def test_new_game_rejects_a_bad_id(gdi_path, tmp_path):
    assert main(["new-game", gdi_path, "../escape", "--dir", str(tmp_path), "--no-extract"]) == 2


def test_doctor_runs_and_reports(capsys):
    # It reports rather than asserting an environment, so the check is that it produces the table
    # and an exit code, on whatever machine happens to be running it.
    rc = main(["doctor"])
    out = capsys.readouterr().out
    assert "Required" in out and "Optional" in out
    assert "CMake 3.24+" in out
    assert rc in (0, 1)


def test_new_game_title_override_and_brief(gdi_path, tmp_path, capsys):
    games = tmp_path / "games"
    rc = main(["new-game", gdi_path, "synth", "--dir", str(games), "--no-extract",
               "--title", "My Own Name", "--brief"])
    assert rc == 0
    toml = (games / "synth" / "synth.toml").read_text()
    assert 'title = "My Own Name"' in toml
    assert 'dream_add_game(synth TITLE "My Own Name")' in (
        games / "synth" / "CMakeLists.txt").read_text()
    # --brief is for a caller that is about to run those steps itself.
    assert "next:" not in capsys.readouterr().out


def test_relpath_or_abspath_across_windows_drives():
    # Windows has no relative path from one drive to another, and ntpath.relpath raises rather than
    # answering. ntpath is the real Windows path logic, so this runs the same on every host.
    assert _relpath_or_abspath(r"E:\games\jgr\jgr.toml", r"D:\work", ntpath) == \
        r"E:\games\jgr\jgr.toml"
    # On one drive the answer stays relative, as before.
    assert _relpath_or_abspath(r"E:\games\jgr\jgr.toml", r"E:\games", ntpath) == \
        r"jgr\jgr.toml"


def test_new_game_when_no_relative_path_exists(gdi_path, tmp_path, monkeypatch, capsys):
    # The game folder, the disc and the working directory on three different Windows drives: no
    # path is relative to any other. new-game must still write both files and name them in full.
    def no_relative_path(path, start=None):
        raise ValueError("path is on mount 'C:', start on mount 'D:'")

    monkeypatch.setattr(os.path, "relpath", no_relative_path)
    games = tmp_path / "games"
    rc = main(["new-game", gdi_path, "synth", "--dir", str(games), "--no-extract"])
    out = capsys.readouterr().out
    monkeypatch.undo()
    assert rc == 0
    toml_path = games / "synth" / "synth.toml"
    assert f"wrote {toml_path}" in out
    assert f"wrote {games / 'synth' / 'CMakeLists.txt'}" in out
    assert f"--config {toml_path} --window" in out
    # The config loader joins disc.image onto the config's folder, and an absolute path survives
    # that join, so the disc is named in full.
    image = os.path.abspath(gdi_path).replace(os.sep, "/")
    assert f'image = "{image}"' in toml_path.read_text()
