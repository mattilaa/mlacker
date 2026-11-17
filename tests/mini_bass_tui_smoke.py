"""Load, play and resave the generated Mla Mini example through mlacker."""
import json
from pathlib import Path
import shutil
import sys
import tempfile

from session_tui_smoke import Terminal


def main():
    source = Path(__file__).resolve().parents[1] / "examples/mini_bass.mlaproj"
    with tempfile.TemporaryDirectory(prefix="mlacker-mini-") as folder:
        project = Path(folder) / source.name
        shutil.copytree(source, project)
        path = project / "Project.mlack"
        doc = json.loads(path.read_text())
        for record in doc["records"]:
            if record[0] == "string" and record[1].endswith("MlaMini.vst3"):
                record[1] = str(Path(sys.argv[2]).resolve())
        strings = [v for k, v in doc["records"] if k == "string"]
        for expected in ("Classic bass", "Ladder sweep", "Legato glide", "cc:74", "cc:71", "4.50"):
            assert expected in strings, expected
        assert sum(v.endswith("MlaMini.vst3") for v in strings) == 2
        path.write_text(json.dumps(doc))
        for _ in range(2):
            tui = Terminal(str(project))
            try:
                frame = tui.read(1)
                assert b"Classic bass" in frame, frame[-6000:]
                assert b"Cannot load" not in frame, frame[-6000:]
                frame = tui.send_until(b" ", b"PLAY", timeout=4)
                assert b"PLAY" in frame, frame[-6000:]
                frame = tui.send_until(b" ", b"STOP", timeout=4)
                assert b"STOP" in frame, frame[-6000:]
                frame = tui.send_until(b"\x13", b"Saved project:", timeout=10)
                assert b"Saved project:" in frame, frame[-6000:]
            finally:
                tui.close()
            saved = json.loads(path.read_text())
            assert [v for k, v in saved["records"] if k == "string"] == strings
    print("PASS: Mla Mini bass example plays and survives save/reopen")


if __name__ == "__main__":
    main()
