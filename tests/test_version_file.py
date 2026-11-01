"""Exercise version resolution in isolated CMake trees without the audio SDK."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class VersionFileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mlacker-version-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        (self.root / "cmake").mkdir()
        shutil.copy2(REPO / "cmake/MlackerVersion.cmake", self.root / "cmake/MlackerVersion.cmake")
        (self.root / "version.txt").write_text("0.3.1\n")
        (self.root / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.19)
include(cmake/MlackerVersion.cmake)
project(VersionFixture VERSION "${MLACKER_VERSION}" LANGUAGES NONE)
file(WRITE "${CMAKE_BINARY_DIR}/reported-version.txt" "${PROJECT_VERSION}\\n")
add_custom_target(version COMMAND "${CMAKE_COMMAND}" -E cat "${CMAKE_BINARY_DIR}/reported-version.txt")
''')
        self.env = dict(os.environ)
        self.env.pop("MLACKER_RELEASE_VERSION", None)
        self.env.pop("MLACKER_BUILD_NUMBER", None)

    def configure(self, *options, success=True):
        result = subprocess.run(["cmake", "-S", str(self.root), "-B", str(self.build), *options],
                                text=True, capture_output=True, env=self.env)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (self.build / "reported-version.txt").read_text().strip()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stderr

    def test_local_counter_persists_and_build_does_not_increment_it(self):
        self.assertEqual(self.configure(), "0.3.1.1")
        self.assertEqual(self.configure(), "0.3.1.2")
        self.build_version()
        self.assertEqual((self.build / "reported-version.txt").read_text(), "0.3.1.2\n")
        self.assertEqual((self.build / ".mlacker_build_number").read_text(), "2\n")

    def build_version(self):
        result = subprocess.run(["cmake", "--build", str(self.build), "--target", "version"],
                                text=True, capture_output=True, env=self.env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_version_edit_reconfigures_build(self):
        self.configure()
        (self.root / "version.txt").write_text("0.3.2\n")
        self.build_version()
        self.assertEqual((self.build / "reported-version.txt").read_text(), "0.3.2.2\n")

    def test_frozen_and_explicit_build_numbers(self):
        self.assertEqual(self.configure("-DMLACKER_AUTO_INCREMENT_BUILD=OFF", "-DMLACKER_VERSION_BUILD=8"), "0.3.1.8")
        self.assertEqual(self.configure("-DMLACKER_BUILD_NUMBER=42"), "0.3.1.42")
        self.assertEqual(self.configure(), "0.3.1.42")
        self.assertFalse((self.build / ".mlacker_build_number").exists())

    def test_release_version_is_exact_and_does_not_consume_local_counter(self):
        self.assertEqual(self.configure(), "0.3.1.1")
        self.assertEqual(self.configure("-DMLACKER_RELEASE_VERSION=0.3.1"), "0.3.1")
        self.assertEqual(self.configure(), "0.3.1")
        self.assertEqual(self.configure("-DMLACKER_RELEASE_VERSION=0.3.1.17"), "0.3.1.17")
        self.assertEqual(self.configure("-DMLACKER_RELEASE_VERSION="), "0.3.1.2")

    def test_environment_overrides_on_first_configure(self):
        self.env["MLACKER_BUILD_NUMBER"] = "23"
        self.assertEqual(self.configure(), "0.3.1.23")
        self.env["MLACKER_BUILD_NUMBER"] = "24"
        self.assertEqual(self.configure(), "0.3.1.23")
        self.assertEqual(self.configure("-DMLACKER_BUILD_NUMBER=24"), "0.3.1.24")

    def test_invalid_versions_and_conflicting_overrides(self):
        for version in ("0.3", "v0.3.1", "0.3.01", "0.3.1.1", "0.3.1-rc1", ""):
            with self.subTest(version=version):
                (self.root / "version.txt").write_text(version + "\n")
                self.assertIn("version.txt must contain", self.configure(success=False))
        (self.root / "version.txt").write_text("0.3.1\n")
        for release in ("0.3.2", "0.3.1.01", "invalid"):
            self.configure(f"-DMLACKER_RELEASE_VERSION={release}", success=False)
        self.configure("-DMLACKER_RELEASE_VERSION=0.3.1", "-DMLACKER_BUILD_NUMBER=4", success=False)
        self.configure("-DMLACKER_RELEASE_VERSION=", "-DMLACKER_BUILD_NUMBER=04", success=False)


if __name__ == "__main__":
    unittest.main()
