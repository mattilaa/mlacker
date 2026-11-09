"""Beast's audible note starts must line up with its numbered musical rows."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("align_beast", ROOT / "scripts/align_beast_projects.py")
beast = importlib.util.module_from_spec(spec)
spec.loader.exec_module(beast)


class BeastAlignmentTests(unittest.TestCase):
    def test_projects_follow_beats_and_matrix_bar_boundaries(self):
        for name, (step, bpm) in beast.CONFIG.items():
            with self.subTest(project=name):
                project = beast.Project(ROOT / f"examples/shadow_of_the_beast_{name}.mlaproj/Project.mlack")
                self.assertEqual(project.view[0], bpm)
                self.assertEqual(len(project.patterns) % 4, 0)
                notes = 0
                for index, pattern in enumerate(project.patterns):
                    self.assertEqual(pattern["id"], index + 1)
                    self.assertEqual(len(pattern["rows"]) % 16, 0)
                    self.assertLessEqual(len(pattern["rows"]), 64)
                    self.assertIn(" bars ", pattern["name"])
                    channel = pattern["tracks"][0][0][6] - 1
                    self.assertEqual(channel, index % 4)
                    for row_index, row in enumerate(pattern["rows"]):
                        self.assertEqual(row[0], f"{row_index + 1:03}")
                        self.assertEqual(row[6], "7D")
                        self.assertLessEqual(len(bytes.fromhex(row[7])), 80)
                        found, micros, snapshot, frames = beast.decode_packet("7D " + row[7])
                        self.assertEqual(found, channel)
                        self.assertEqual(micros, round(beast.Fraction(15_000_000, bpm) / step))
                        if row_index == 0 and snapshot is None:
                            # A pattern without a snapshot must initialize its voice itself.
                            self.assertTrue({1, 2, 3, 4, 5}.issubset({op for op, _ in beast.operations(frames[0])}))
                        for frame_index, frame in enumerate(frames):
                            if any(op == 5 for op, _ in beast.operations(frame)):
                                self.assertEqual(frame_index, 0, "a note starts between musical rows")
                                notes += 1
                self.assertEqual(notes, 3513 if name == "intro" else 1849)
                for bar, pattern_id in enumerate(project.song):
                    self.assertEqual(pattern_id, bar // 4 * 4 + 1 if bar % 4 == 0 else 0)
                grid = next(i for i, value in enumerate(project.extensions) if value[1] == "MATRIX_GRID")
                self.assertEqual(project.extensions[grid + 1][1], 16)
                matrix = next(i for i, value in enumerate(project.extensions) if value[1] == "SONG_MATRIX")
                position = matrix + 1
                self.assertEqual(project.extensions[position][1], 3)
                position += 1
                for lane in range(1, 4):
                    self.assertEqual(project.extensions[position][1], len(project.song))
                    position += 1
                    for bar in range(len(project.song)):
                        expected = bar // 4 * 4 + lane + 1 if bar % 4 == 0 else 0
                        self.assertEqual(project.extensions[position][1], expected)
                        position += 1

    def test_snapshots_preserve_playing_block_and_reload_loop(self):
        registers = beast.Registers()
        registers.apply(bytes.fromhex('01 01 00 00 02 02 00 00 03 2C 03 00 04 20 05'))
        registers.apply(bytes.fromhex('01 05 00 00 02 06 00 00'))
        restored = beast.Registers()
        restored.apply(registers.snapshot())
        self.assertEqual(restored.values, registers.values)
        self.assertEqual(restored.latched, registers.latched)
        self.assertTrue(restored.active)

    def test_compaction_preserves_dma_events(self):
        registers = beast.Registers()
        frame = bytes.fromhex('04 00 03 2C 03 00 06 05 05')
        self.assertEqual(registers.apply(frame, compact=True), bytes.fromhex('06 05 05'))


if __name__ == "__main__":
    unittest.main()
