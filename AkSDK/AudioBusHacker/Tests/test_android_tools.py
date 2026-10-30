"""Exercise package metadata and version-safe Unreal deployment without an NDK."""
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("android_tools", SOURCE / "scripts/build_android.py")
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)
VERSION = (2025, 1, 10, 9233)


class AndroidToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.args = SimpleNamespace(output=self.root / "Output", abis=["arm64-v8a"], configs=["Profile"],
                                    wwise_plugin=self.root / "WwiseSoundEngine")
        shutil.copy2(SOURCE / "bundle_template.json", self.root / "bundle_template.json")
        self.relative = tools.relative_outputs("arm64-v8a", "Profile")[0]
        artifact = self.args.output / "stage" / self.relative
        artifact.parent.mkdir(parents=True)
        artifact.write_bytes(b"Android-static-library-fixture")
        header = self.args.output / "stage/SDK/include/AK/Plugin/AudioBusHackerFXFactory.h"
        header.parent.mkdir(parents=True)
        header.write_text("callback header", encoding="utf-8")
        self.manifest = {"files": {self.relative.as_posix(): tools.digest(artifact)}}
        (self.args.output / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def make_plugin(self, version):
        plugin = self.args.wwise_plugin
        plugin.mkdir()
        (plugin / "WwiseSoundEngine.uplugin").write_text("{}", encoding="utf-8")
        include = plugin / "ThirdParty/include/AK/Plugin"
        include.mkdir(parents=True)
        macros = ("MAJOR", "MINOR", "SUBMINOR", "BUILD")
        (include.parent / "AkWwiseSDKVersion.h").write_text(
            "\n".join(f"#define AK_WWISESDK_VERSION_{key} {value}" for key, value in zip(macros, version)),
            encoding="utf-8")

    def test_first_package_creates_valid_bundle_and_archive(self):
        with patch.object(tools, "ROOT", self.root):
            tools.package(self.args, VERSION, self.manifest)
        bundle = json.loads((self.root / "bundle.json").read_text(encoding="utf-8"))
        package = self.root / bundle["files"][0]["sourceName"]
        self.assertEqual(bundle["version"], dict(zip(("year", "major", "minor", "build"), VERSION)))
        self.assertEqual(bundle["files"][0]["sha1"], tools.digest(package, "sha1"))
        with tarfile.open(package) as archive:
            self.assertEqual(archive.extractfile(self.relative.as_posix()).read(), b"Android-static-library-fixture")

    def test_repack_preserves_windows_entry_without_duplicate_android_entries(self):
        with patch.object(tools, "ROOT", self.root):
            tools.package(self.args, VERSION, self.manifest)
            bundle = json.loads((self.root / "bundle.json").read_text(encoding="utf-8"))
            windows = {"id": "Windows-package", "sourceName": "Windows-package"}
            bundle["files"].append(windows)
            (self.root / "bundle.json").write_text(json.dumps(bundle), encoding="utf-8")
            tools.package(self.args, VERSION, self.manifest)
        final = json.loads((self.root / "bundle.json").read_text(encoding="utf-8"))
        self.assertEqual(len(final["files"]), 2)
        self.assertIn(windows, final["files"])

    def test_deploy_rejects_wrong_sdk_before_copying_libraries(self):
        self.make_plugin((2023, 1, 6, 8555))
        with self.assertRaisesRegex(RuntimeError, "SDK version differs"):
            tools.deploy(self.args, VERSION)
        self.assertFalse((self.args.wwise_plugin / "ThirdParty/Android").exists())

    def test_deploy_uses_unreal_layout_and_static_library_only(self):
        self.make_plugin(VERSION)
        tools.deploy(self.args, VERSION)
        installed = self.args.wwise_plugin / "ThirdParty/Android/arm64-v8a/Profile/lib/libAudioBusHackerFX.a"
        self.assertEqual(installed.read_bytes(), b"Android-static-library-fixture")
        self.assertFalse(list(self.args.wwise_plugin.rglob("*.so")))


if __name__ == "__main__":
    unittest.main()
