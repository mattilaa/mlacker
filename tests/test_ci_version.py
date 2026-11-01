"""CI and packaging must label the same binary with the same version."""
import importlib.util
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ci_version", REPO / "scripts/ci_version.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class CiVersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mlacker-ci-version-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "version.txt").write_text("0.3.1\n")
        (self.root / "mlang.toml").write_text('[package]\nname = "mlacker"\nversion = "0.3.1"\n[dependencies]\n')

    def test_source_tag_and_ci_build(self):
        self.assertEqual(ci.resolve(self.root), "0.3.1")
        self.assertEqual(ci.resolve(self.root, "v0.3.1"), "0.3.1")
        self.assertEqual(ci.resolve(self.root, "v0.3.1.17"), "0.3.1.17")
        self.assertEqual(ci.resolve(self.root, build_number="42"), "0.3.1.42")

    def test_rejects_mismatched_or_malformed_tag(self):
        for tag in ("v0.3.0", "v0.3.01", "v0.3.1.01", "v0.3.1-rc1", "0.3.1", "v0.3.1.4.5"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                ci.resolve(self.root, tag)
        with self.assertRaises(ValueError):
            ci.resolve(self.root, "v0.3.1", "42")
        for build in ("-1", "01", "", "1\nTAG=invalid"):
            with self.assertRaises(ValueError):
                ci.resolve(self.root, build_number=build)

    def test_manifest_must_match_source_version(self):
        (self.root / "mlang.toml").write_text('[package]\nversion = "0.3.0"\n')
        with self.assertRaises(ValueError):
            ci.resolve(self.root)

    def test_github_outputs_match_cmake_environment(self):
        env = dict(os.environ)
        for variable in ("GITHUB_ENV", "GITHUB_OUTPUT", "GITHUB_STEP_SUMMARY"):
            env[variable] = str(self.root / variable)
        result = subprocess.run(["python3", str(REPO / "scripts/ci_version.py"), "--build-number", "42", "--github"],
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        base = ci.source_version(REPO)
        self.assertEqual((self.root / "GITHUB_ENV").read_text(), f"MLACKER_RELEASE_VERSION={base}.42\n")
        self.assertEqual((self.root / "GITHUB_OUTPUT").read_text(), f"version={base}.42\ntag=v{base}.42\n")

    def test_packager_rejects_a_different_binary_version(self):
        binary = self.root / "mlacker"
        binary.write_text('#!/bin/sh\nprintf "mlacker 0.3.1.7\\n"\n')
        binary.chmod(0o755)
        result = subprocess.run(["bash", str(REPO / "packaging/macos/build_pkg.sh"), "--version", "0.3.1"],
                                env=dict(os.environ, MLACKER_BIN=str(binary)), text=True, capture_output=True)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("differs from binary 0.3.1.7", result.stderr)


if __name__ == "__main__":
    unittest.main()
