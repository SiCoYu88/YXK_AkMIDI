# AudioBusHacker / AkAudioSampler Android

当前 Android 构建入口为 `build_android.bat`（跨平台底层入口为
`python scripts/build_android.py`），使用 `Android/CMakeLists.txt`。不使用仓库中遗留的
2023.1 Android `.mk`，也不修改 Wwise SDK 安装目录。

## 环境与构建

需要 Python 3.8+、CMake 3.22+、Ninja、匹配项目版本的 Wwise SDK 头文件和 Android NDK。
优先使用 `PATH` 中的工具，Ninja 也可使用 Wwise 自带版本；可通过 `--cmake` / `--ninja`
指定路径。`PYTHON_EXE` 可指定批处理使用的 Python。

从 AudioBusHacker 根目录执行：

```bat
build_android.bat all --sdk "H:\Audiokinetic\2025.1.10.9233\SDK" --ndk "C:\Users\YourName\AppData\Local\Android\Sdk\ndk\27.2.12479018"
```

可分步执行 `build`、`verify`、`package`、`deploy`。默认构建两种 64 位架构
`arm64-v8a`、`x86_64` 的 Debug/Profile/Release。可用 `--abis arm64-v8a` 和
`--configs Profile Release` 缩小范围，后续 verify/package/deploy 必须使用相同参数。
默认 Android API 28，可用 `--api` 指定项目的最低版本。Wwise 自身的最低版本要求仍需满足。

默认输出在 `Output/Android`，`--output` 可更改目录：

```text
build/<ABI>/<Config>/libAudioBusHackerFX.a
build/<ABI>/<Config>/libAudioBusHacker.so
build/<ABI>/<Config>/AudioBusHackerAndroidTest
stage/SDK/Android_<ABI>/<Config>/lib/libAudioBusHackerFX.a
stage/SDK/Android_<ABI>/<Config>/bin/libAudioBusHacker.so
stage/SDK/include/AK/Plugin/AudioBusHackerFXFactory.h
manifest.json
```

静态库使用 PIC；共享库和测试程序启用 16 KB LOAD 段对齐。Release 定义 `AK_OPTIMIZED`，
仍保留可视化回调。Debug/Profile 保留断言支持。

verify 校验构建版本、产物哈希、两个 C 回调符号、共享库 ELF 架构及 LOAD 段对齐，并确认
静态消费者测试程序成功链接。package 使用构建清单中的文件生成
`AudioBusHacker_v2025.1.10_Build9233_SDK.Android.tar.xz`（版本取自 SDK 头文件），
将 Factory 头和构建清单一并打包，更新同版本 `bundle.json`，保留 Windows 包记录。
首次构建没有 bundle 时从模板生成；如果已有 bundle 版本不同，先生成对应版本的 Windows bundle。

## Unreal 接入

```bat
build_android.bat deploy --wwise-plugin "D:\YourGame\Plugins\WwiseSoundEngine"
```

也可在 `all` 命令中加 `--wwise-plugin` 完成构建后部署。部署校验目标插件的
`ThirdParty/include/AK/AkWwiseSDKVersion.h`，版本必须与构建一致。
静态库从 Wwise SDK 的 `Android_<ABI>` 布局转换为 Unreal 的
`ThirdParty/Android/<ABI>/<Config>/lib/libAudioBusHackerFX.a`；同时复制 Factory 头和清单。

AkAudioSampler 按目标架构显式链接该库，并通过 `AK_STATIC_LINK_PLUGIN(AudioBusHackerFX)`
保留静态工厂注册。Android 直接获取同一静态实例的回调接口，Windows 仍从 DLL 获取接口。
库缺失时，Unreal 构建规则提前终止并显示缺失路径。

| Unreal 配置 | Wwise 目录 |
| --- | --- |
| Debug / DebugGame | Debug |
| Development | Profile |
| Test / Shipping | Release |

Android 不部署共享库到 Unreal，避免与静态实例重复。若目标项目此前手工部署过
`libAudioBusHacker.so`，需检查其动态插件配置，确保 SoundEngine 使用静态注册实例。
旧 2023.1 Android 包不能与 Wwise 2025.1.10 混用。

承载 AkAudioSampler 的父级 `.uplugin` 仍需声明 Niagara 插件依赖，按
`AkPlugins/AkAudioSampler/README.md` 配置。该声明警告与本次链接缺失是两个问题。

## 设备验证

每个构建都编译静态消费者测试：验证插件注册、静音电平、1 kHz 正弦波频谱，以及解除回调后
不再接收快照。编译成功只代表链接通过，运行结果必须在匹配 ABI 的设备或模拟器上确认。
例如从 AudioBusHacker 根目录在 arm64 设备执行：

```bat
adb push Output/Android/build/arm64-v8a/Profile/AudioBusHackerAndroidTest /data/local/tmp/AudioBusHackerAndroidTest
adb shell chmod 755 /data/local/tmp/AudioBusHackerAndroidTest
adb shell /data/local/tmp/AudioBusHackerAndroidTest
```

预期打印 `PASS: static registration, silence, tone spectrum and callback unregister`，返回码 0。

Unreal 游戏还需在设备上确认：启动日志出现
`Using statically linked AudioBusHacker visualization API (Android).`；目标 Wwise Bus 安装
AudioBusHacker，播放音频时查询到有效快照；停止播放后 Niagara 按超时设置清零。
CPU/GPU Niagara 的可用性还取决于目标设备和 Niagara 系统本身的移动端配置。

## 验证记录

2026-10-30：Wwise SDK 2025.1.10.9233、NDK r27c / Clang 18.0.3、Android API 28。
arm64-v8a / x86_64 的 Debug/Profile/Release 静态库、共享库和静态消费者测试程序均编译、
链接成功；两个 C 接口、共享库架构和 16 KB 段对齐校验通过，Android SDK 包已生成。
当前未连接设备，未执行设备测试或完整 Unreal APK 打包。
AkAudioSampler 构建规则已使用 UE 5.8 的 UnrealBuildTool 程序集编译通过；4 项工具测试覆盖
首次生成 bundle、保留 Windows 包记录、版本不匹配时拒绝部署，以及 Unreal 静态库部署布局。
