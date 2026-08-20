import subprocess
from pathlib import Path


def treesync_bin() -> Path:
    return Path(__file__).resolve().parents[1] / "treesync"


def run_treesync(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(treesync_bin())] + args, check=True, capture_output=True, text=True)


def test_help_smoke() -> None:
    result = run_treesync(["--help"])
    assert "Usage: treesync" in result.stdout


def test_equal_dirs_have_no_diff(tmp_path: Path) -> None:
    src = tmp_path / "src"
    dst = tmp_path / "dst"
    src.mkdir()
    dst.mkdir()
    (src / "data.txt").write_text("abc\n", encoding="utf-8")
    (dst / "data.txt").write_text("abc\n", encoding="utf-8")

    result = run_treesync([str(src), str(dst)])

    assert result.stdout == ""


def test_exclude_ignores_source_and_destination_files_during_sync(tmp_path: Path) -> None:
    src = tmp_path / "src"
    dst = tmp_path / "dst"
    src.mkdir()
    dst.mkdir()
    (src / "data.txt").write_text("abc\n", encoding="utf-8")
    (src / ".dirdb").write_text("source cache\n", encoding="utf-8")
    (dst / "stale.txt").write_text("stale\n", encoding="utf-8")
    (dst / ".dirdb").write_text("destination cache\n", encoding="utf-8")
    (dst / "orphan").mkdir()
    (dst / "orphan" / ".dirdb").write_text("nested cache\n", encoding="utf-8")

    result = run_treesync(["--sync", "--exclude", ".dirdb", str(src), str(dst)])

    assert result.stdout == ""
    assert (dst / "data.txt").read_text(encoding="utf-8") == "abc\n"
    assert not (dst / "stale.txt").exists()
    assert (dst / ".dirdb").read_text(encoding="utf-8") == "destination cache\n"
    assert (dst / "orphan" / ".dirdb").read_text(encoding="utf-8") == "nested cache\n"


def test_exclude_can_be_specified_multiple_times(tmp_path: Path) -> None:
    src = tmp_path / "src"
    dst = tmp_path / "dst"
    src.mkdir()
    dst.mkdir()
    (src / "data.txt").write_text("abc\n", encoding="utf-8")
    (src / "data.tmp").write_text("tmp\n", encoding="utf-8")
    (dst / "cache.dirdb").write_text("cache\n", encoding="utf-8")

    result = run_treesync(["--sync", "-x", "*.tmp", "-x", "*.dirdb", str(src), str(dst)])

    assert result.stdout == ""
    assert (dst / "data.txt").read_text(encoding="utf-8") == "abc\n"
    assert not (dst / "data.tmp").exists()
    assert (dst / "cache.dirdb").read_text(encoding="utf-8") == "cache\n"
