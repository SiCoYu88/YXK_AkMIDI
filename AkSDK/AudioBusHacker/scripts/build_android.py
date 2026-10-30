"""Build, verify, package and deploy AudioBusHacker without modifying the Wwise SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
ABIS = ("arm64-v8a", "x86_64")
CONFIGS = ("Debug", "Profile", "Release")
CALLBACKS = ("SetAudioBusHackerCallbacks", "SetAudioBusHackerVisualizationCallback")


def run(*args):
    command = [str(arg) for arg in args]
    print("[RUN] " + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True)


def digest(path, algorithm="sha256"):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest() if hasattr(hashlib, "file_digest") else hash_stream(stream, algorithm)


def hash_stream(stream, algorithm):
    value = hashlib.new(algorithm)
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        value.update(block)
    return value.hexdigest()


def sdk_version(sdk):
    header = sdk / "include/AK/AkWwiseSDKVersion.h"
    text = header.read_text(encoding="utf-8-sig")
    values = []
    for part in ("MAJOR", "MINOR", "SUBMINOR", "BUILD"):
        match = re.search(r"^#define\s+AK_WWISESDK_VERSION_" + part + r"\s+(\d+)", text, re.M)
        if not match:
            raise RuntimeError(f"SDK version macro {part} missing from {header}")
        values.append(int(match[1]))
    return tuple(values)


def source_hashes():
    files = [ROOT / "Android/CMakeLists.txt", ROOT / "AudioBusHackerConfig.h"]
    files += sorted((ROOT / "SoundEnginePlugin").glob("*.cpp"))
    files += sorted((ROOT / "SoundEnginePlugin").glob("*.h"))
    files.append(ROOT / "Tests/AndroidSmoke.cpp")
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in files}


def tool_path(candidates, description):
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate).resolve()
    raise RuntimeError(f"{description} not found. Install it or specify its command-line path.")


def ndk_tool(ndk, name):
    hosts = {"win32": "windows-x86_64", "linux": "linux-x86_64", "darwin": "darwin-x86_64"}
    return ndk / "toolchains/llvm/prebuilt" / hosts[sys.platform] / "bin" / (name + (".exe" if os.name == "nt" else ""))


def relative_outputs(abi, config):
    directory = Path("SDK") / f"Android_{abi}" / config
    return (directory / "lib/libAudioBusHackerFX.a", directory / "bin/libAudioBusHacker.so")


def build(args, version):
    cmake = tool_path([args.cmake, shutil.which("cmake")], "CMake >= 3.22")
    ninja = tool_path([args.ninja, shutil.which("ninja"), args.sdk.parent / "Tools/Win32/bin/ninja.exe"], "Ninja")
    if not (args.ndk / "build/cmake/android.toolchain.cmake").is_file():
        raise RuntimeError(f"Android NDK toolchain not found: {args.ndk}")
    for abi in args.abis:
        for config in args.configs:
            directory = args.output / "build" / abi / config
            run(cmake, "-S", ROOT / "Android", "-B", directory, "-G", "Ninja",
                f"-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}",
                f"-DCMAKE_TOOLCHAIN_FILE={(args.ndk / 'build/cmake/android.toolchain.cmake').as_posix()}",
                f"-DWWISE_SDK={args.sdk.as_posix()}", f"-DANDROID_ABI={abi}",
                f"-DANDROID_PLATFORM=android-{args.api}", "-DANDROID_STL=c++_static",
                "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", f"-DABH_CONFIGURATION={config}",
                f"-DCMAKE_BUILD_TYPE={'Debug' if config == 'Debug' else 'RelWithDebInfo' if config == 'Profile' else 'Release'}",
                "-DABH_BUILD_TESTS=ON")
            run(cmake, "--build", directory, "--parallel")
            run(cmake, "--install", directory, "--prefix", args.output / "stage")
    files = [str(path.as_posix()) for abi in args.abis for config in args.configs for path in relative_outputs(abi, config)]
    files.append("SDK/include/AK/Plugin/AudioBusHackerFXFactory.h")
    manifest = {"wwise_version": list(version), "abis": args.abis, "configurations": args.configs,
                "android_api": args.api, "ndk": (args.ndk / "source.properties").read_text(),
                "sources": source_hashes(),
                "files": {path: digest(args.output / "stage" / path) for path in files}}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def verify(args, version):
    manifest = json.loads((args.output / "manifest.json").read_text(encoding="utf-8"))
    if manifest["wwise_version"] != list(version):
        raise RuntimeError("Output Wwise version does not match --sdk; rebuild before packaging/deployment.")
    if manifest.get("sources") != source_hashes():
        raise RuntimeError("Source files have changed since the last build; rebuild before packaging/deployment.")
    for relative, expected_hash in manifest["files"].items():
        if digest(args.output / "stage" / relative) != expected_hash:
            raise RuntimeError(f"Output hash mismatch: {relative}; rebuild.")
    for abi in args.abis:
        for config in args.configs:
            for relative in relative_outputs(abi, config):
                path = args.output / "stage" / relative
                if manifest["files"].get(relative.as_posix()) != digest(path):
                    raise RuntimeError(f"Missing or changed output: {path}; rebuild.")
                nm = [str(ndk_tool(args.ndk, "llvm-nm")), "--defined-only"]
                if path.suffix == ".so":
                    nm.append("--dynamic")
                symbols = subprocess.check_output(nm + [str(path)], text=True)
                defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
                if not set(CALLBACKS).issubset(defined):
                    raise RuntimeError(f"Visualization callback API missing from {path}")
                if path.suffix == ".so":
                    headers = subprocess.check_output([str(ndk_tool(args.ndk, "llvm-readelf")), "-h", "-l", str(path)], text=True)
                    machine = "AArch64" if abi == "arm64-v8a" else "Advanced Micro Devices X86-64"
                    if machine not in headers:
                        raise RuntimeError(f"Wrong ELF architecture: {path}")
                    for line in headers.splitlines():
                        if line.strip().startswith("LOAD") and int(line.split()[-1], 16) < 16384:
                            raise RuntimeError(f"ELF segment is not 16 KB aligned: {path}")
                print(f"[OK] {abi} {config}: {path.name}, C callback symbols and ELF checks")
            test = args.output / "build" / abi / config / "AudioBusHackerAndroidTest"
            if not test.is_file():
                raise RuntimeError(f"Static consumer link test missing: {test}")
    header = args.output / "stage/SDK/include/AK/Plugin/AudioBusHackerFXFactory.h"
    if digest(header) != digest(ROOT / "SoundEnginePlugin/AudioBusHackerFXFactory.h"):
        raise RuntimeError("Factory header is stale; rebuild.")
    if manifest["files"].get("SDK/include/AK/Plugin/AudioBusHackerFXFactory.h") != digest(header):
        raise RuntimeError("Factory header differs from build manifest; rebuild.")
    return manifest


def package(args, version, manifest):
    major, minor, patch, build_number = version
    filename = f"AudioBusHacker_v{major}.{minor}.{patch}_Build{build_number}_SDK.Android.tar.xz"
    path = ROOT / filename
    bundle_path = ROOT / "bundle.json"
    expected = dict(zip(("year", "major", "minor", "build"), version))
    if bundle_path.is_file():
        bundle = json.loads(bundle_path.read_text(encoding="utf-8"))
    else:
        template = (ROOT / "bundle_template.json").read_text(encoding="utf-8")
        template = re.sub(r"(?m)^\s*//.*$", "", template)
        for key, value in expected.items():
            template = template.replace("${" + key + "}", str(value))
        bundle = json.loads(template)
        bundle["files"] = []
    if bundle["version"] != expected:
        raise RuntimeError("bundle.json has a different Wwise version. Generate the matching Windows bundle first.")
    with tarfile.open(path, "w:xz") as archive:
        for relative in manifest["files"]:
            archive.add(args.output / "stage" / relative, arcname=relative, recursive=False)
        archive.add(args.output / "manifest.json", arcname="SDK/AudioBusHacker.Android.json")
    with tarfile.open(path) as archive:
        members = {member.name: member for member in archive.getmembers()}
        if not set(manifest["files"]).issubset(members):
            raise RuntimeError("Android package is incomplete.")
        unpacked_size = sum(member.size for member in members.values())
    entry = {"groups": [{"groupId": "Packages", "groupValueId": "SDK"},
                         {"groupId": "DeploymentPlatforms", "groupValueId": "Android"}],
             "id": filename, "licenses": [], "name": "SDK.Android.AudioBusHacker",
             "sha1": digest(path, "sha1"), "size": path.stat().st_size,
             "sourceName": filename, "uncompressedSize": unpacked_size}
    bundle["files"] = [item for item in bundle["files"] if item.get("sourceName", item.get("id")) != filename] + [entry]
    bundle_path.write_text(json.dumps(bundle, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"[OK] {path}; bundle.json updated, existing Windows packages retained")


def deploy(args, version):
    plugin = args.wwise_plugin.resolve()
    if not (plugin / "WwiseSoundEngine.uplugin").is_file():
        raise RuntimeError("--wwise-plugin must point to the WwiseSoundEngine plugin directory.")
    third_party = plugin / "ThirdParty"
    if sdk_version(third_party) != version:
        raise RuntimeError("Unreal Wwise SDK version differs from the built library; deployment cancelled.")
    # Copy only the static library: shared/static copies have independent callback state.
    for abi in args.abis:
        for config in args.configs:
            relative = relative_outputs(abi, config)[0]
            target = third_party / "Android" / abi / config / "lib/libAudioBusHackerFX.a"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(args.output / "stage" / relative, target)
            print(f"[OK] Deployed {target}")
    shutil.copy2(args.output / "stage/SDK/include/AK/Plugin/AudioBusHackerFXFactory.h",
                 third_party / "include/AK/Plugin/AudioBusHackerFXFactory.h")
    shutil.copy2(args.output / "manifest.json", third_party / "Android/AudioBusHacker.Android.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("all", "build", "verify", "package", "deploy"), nargs="?", default="all")
    sdk_default = os.environ.get("WWISESDK") or str(Path(os.environ.get("WWISEROOT", "H:/Audiokinetic/2025.1.10.9233")) / "SDK")
    parser.add_argument("--sdk", type=Path, default=Path(sdk_default))
    parser.add_argument("--ndk", type=Path, default=Path(os.environ.get("NDKROOT") or os.environ.get("NDK_ROOT", "")))
    parser.add_argument("--output", type=Path, default=ROOT / "Output/Android")
    parser.add_argument("--abis", nargs="+", choices=ABIS, default=list(ABIS))
    parser.add_argument("--configs", nargs="+", choices=CONFIGS, default=list(CONFIGS))
    parser.add_argument("--api", type=int, default=28, help="Android API level (default: 28)")
    parser.add_argument("--cmake")
    parser.add_argument("--ninja")
    parser.add_argument("--wwise-plugin", type=Path, help="Optional deployment destination; static library only")
    args = parser.parse_args()
    args.sdk, args.ndk, args.output = args.sdk.resolve(), args.ndk.resolve(), args.output.resolve()
    if args.action == "deploy" and not args.wwise_plugin:
        parser.error("deploy requires --wwise-plugin")
    if args.api < 21:
        parser.error("64-bit Android requires --api >= 21")
    version = sdk_version(args.sdk)
    print("Wwise " + ".".join(map(str, version)), flush=True)
    if args.action in ("build", "all"):
        build(args, version)
    manifest = verify(args, version)
    if args.action in ("package", "all"):
        package(args, version, manifest)
    if args.action in ("deploy", "all") and args.wwise_plugin:
        deploy(args, version)
    print("[SUCCESS] Android " + args.action)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        sys.exit(1)
