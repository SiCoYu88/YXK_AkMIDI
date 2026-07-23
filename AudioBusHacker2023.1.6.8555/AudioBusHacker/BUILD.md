# AudioBusHacker 编译指南

本文参考 Audiokinetic Wwise SDK 2025.1.4 的[“针对不同的 Wwise 平台构建工程”](https://www.audiokinetic.com/zh/public-library/2025.1.4_9062/?source=SDK&id=effectplugin_tools_building.html)说明，并记录本工程已验证的迁移和构建流程。

## 1. 当前目标

- Wwise：`2025.1.4.9062`
- 安装目录：`G:\Wwise2025.1.4.9062`
- Windows 工具集：Visual Studio 2022、MSVC v143（`vc170`）
- 架构：`x64`
- 已验证：SoundEngine Debug/Profile/Release、Authoring Debug/Release、Documentation
- bundle 版本：`2025.1.4.9062`

工程最初来自 Wwise 2023.1.6.8555。`vc170` 解决方案现已用 2025.1.4 Premake 重新生成；旧 2023 安装包保存在 `LegacyPackages`，不能安装到 Wwise 2025.1。

## 2. 环境要求

Wwise Launcher 中需要安装 Wwise `2025.1.4.9062` 的 Wwise Authoring、SDK 和 Windows SDK 平台包。Windows 侧还需要：

- Python 3
- Visual Studio 2022 或 Build Tools 2022
- “使用 C++ 的桌面开发”工作负载
- MSVC v143 C++ 工具集
- Windows 10/11 SDK

当前机器已验证 Visual Studio 2022 Community 和 MSBuild `17.14` 可用。系统 `py` 目前只发现 Python 2.7，因此必须安装 Python 3，或给 `$Python` 指定现有 Python 3 的绝对路径。

## 3. 构建环境

在 **Developer PowerShell for VS 2022** 中执行：

```powershell
Set-Location "H:\m2_wwise_test01\v_wwise\YXK_AkMIDI\AudioBusHacker2023.1.6.8555\AudioBusHacker"

$env:WWISEROOT = "G:\Wwise2025.1.4.9062"
$env:PYTHONUTF8 = "1"
$Python = "C:\Path\To\Python3\python.exe"
$Wp = Join-Path $env:WWISEROOT "Scripts\Build\Plugins\wp.py"
```

显式设置 `PYTHONUTF8=1` 是必要的，否则中文 Windows 上的 Wwise 2025.1.4 构建脚本可能在读取 `vswhere` 输出时发生 GBK 解码错误。

执行预检：

```powershell
& $Python --version
Test-Path $Wp
Test-Path (Join-Path $env:WWISEROOT "SDK\include\AK\SoundEngine\Common\AkTypes.h")
& $Python $Wp build -h
```

Python 必须为 3.x，两个 `Test-Path` 应返回 `True`。

## 4. 生成工程

`PremakePlugin.lua` 是工程配置来源；迁移 Wwise 版本或工程目录后，不应继续使用旧 `.sln`/`.vcxproj`。

```powershell
& $Python $Wp premake Windows_vc170 -t vc170 --disable-codesign
& $Python $Wp premake Authoring_Windows -t vc170 --disable-codesign
```

本地构建使用 `--disable-codesign`。发布签名构建应移除此参数，并按团队流程配置 `--signtool-path` 和证书。

生成后，项目文件中的包含、依赖、输出和 Factory 头复制路径都应指向 `G:\Wwise2025.1.4.9062`，不应再出现旧 `F:\Wwise_2023.1.6.8555`。

## 5. 编译 SoundEngine

先关闭正在运行的 Wwise Authoring，避免插件 DLL 被占用。

```powershell
# 调试版本
& $Python $Wp build Windows_vc170 -c Debug -x x64 -t vc170

# 日常联调
& $Python $Wp build Windows_vc170 -c Profile -x x64 -t vc170

# 发布版本
& $Python $Wp build Windows_vc170 -c Release -x x64 -t vc170
```

每条命令会同时生成动态 CRT 静态库、StaticCRT 静态库和共享 DLL。已验证产物包括：

- `G:\Wwise2025.1.4.9062\SDK\x64_vc170\Profile\lib\AudioBusHackerFX.lib`
- `G:\Wwise2025.1.4.9062\SDK\x64_vc170\Profile(StaticCRT)\lib\AudioBusHackerFX.lib`
- `G:\Wwise2025.1.4.9062\SDK\x64_vc170\Profile\bin\AudioBusHacker.dll`
- `G:\Wwise2025.1.4.9062\SDK\x64_vc170\Release\lib\AudioBusHackerFX.lib`
- `G:\Wwise2025.1.4.9062\SDK\x64_vc170\Release(StaticCRT)\lib\AudioBusHackerFX.lib`
- `G:\Wwise2025.1.4.9062\SDK\x64_vc170\Release\bin\AudioBusHacker.dll`

## 6. 编译 Authoring

Wwise 2025.1.4 的参数校验会接受 `Profile`，但该版本实际生成的 Authoring 解决方案只有 `Debug` 和 `Release`。不要使用 `Profile` 构建 Authoring。

Authoring Release 会链接 SoundEngine Profile 静态库，因此应先完成上一节的 Profile 构建：

```powershell
& $Python $Wp build Authoring_Windows -c Debug -x x64 -t vc170
& $Python $Wp build Authoring_Windows -c Release -x x64 -t vc170
```

已验证产物：

- `G:\Wwise2025.1.4.9062\Authoring\x64\Release\bin\Plugins\AudioBusHacker.dll`
- `G:\Wwise2025.1.4.9062\Authoring\x64\Release\bin\Plugins\AudioBusHacker.xml`
- `G:\Wwise2025.1.4.9062\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h`

该工程没有 MFC 对话框或资源，迁移时已移除空 GUI 类对 `PluginMFCWindows<>` 的依赖，无需额外安装 Visual Studio MFC 组件。

## 7. 构建 Property Help

Wwise 文档脚本需要 Python 包 `markdown` 和 `jinja2`：

```powershell
& $Python -m pip install markdown jinja2
& $Python $Wp build Documentation
```

HTML 会生成到 `WwisePlugin\res\Html`，并复制到：

```text
G:\Wwise2025.1.4.9062\Authoring\Data\Plugins\AudioBusHacker\Html
```

当前 `en`、`ja`、`ko`、`zh` 四种语言均已验证生成成功。

## 8. 验证

```powershell
$Artifacts = @(
    "G:\Wwise2025.1.4.9062\SDK\x64_vc170\Profile\lib\AudioBusHackerFX.lib",
    "G:\Wwise2025.1.4.9062\SDK\x64_vc170\Release\bin\AudioBusHacker.dll",
    "G:\Wwise2025.1.4.9062\Authoring\x64\Release\bin\Plugins\AudioBusHacker.dll",
    "G:\Wwise2025.1.4.9062\Authoring\x64\Release\bin\Plugins\AudioBusHacker.xml",
    "G:\Wwise2025.1.4.9062\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h"
)

Get-Item $Artifacts | Select-Object FullName, Length, LastWriteTime
```

随后启动 `G:\Wwise2025.1.4.9062` 下的 Wwise Authoring，确认可以添加 `AudioBusHacker`、编辑 Placeholder 属性并生成 SoundBank。

## 9. 打包

先完成 Profile、Release 和 Authoring Release 构建：

```powershell
$Version = "2025.1.4.9062"

& $Python $Wp package Windows_vc170 -v $Version
& $Python $Wp package Authoring_Windows -v $Version
& $Python $Wp generate-bundle -v $Version
```

生成文件：

- `AudioBusHacker_v2025.1.4_Build9062_SDK.Windows_vc170.tar.xz`
- `AudioBusHacker_v2025.1.4_Build9062_Authoring.Windows_x64.Release.tar.xz`
- `AudioBusHacker_v2025.1.4_Build9062_Authoring.Windows_x64.Debug.tar.xz`
- `bundle.json`

Wwise 2025 的 `generate-bundle` 会扫描工程根目录中的所有 `.tar.xz`。旧版包必须保存在 `LegacyPackages` 等子目录中，否则旧命名格式会使生成失败。

## 10. Android（尚未迁移验证）

当前安装中没有 Wwise 2025.1.4 Android SDK 平台包，因此仓库里的 Android `.mk` 仍属于 2023.1 生成物。通过 Wwise Launcher 补装 Android 平台后执行：

```powershell
& $Python $Wp premake Android
& $Python $Wp build Android -c Profile -x arm64-v8a
& $Python $Wp build Android -c Release -x arm64-v8a
& $Python $Wp package Android -v 2025.1.4.9062
```

NDK、JDK 和 Android SDK 版本必须以 Wwise 2025.1.4 Android 平台文档为准。

## 11. 常见问题

### `No suitable Python runtime found`

`py` 没有注册 Python 3。安装 Python 3，或使用 Python 3 可执行文件的绝对路径。

### `UnicodeDecodeError: 'gbk' codec can't decode ...`

在调用 `wp.py` 前设置 `$env:PYTHONUTF8 = "1"`。

### Authoring 的 `Profile|x64` 无效

Wwise 2025.1.4 实际只生成 Authoring Debug/Release。使用 Release；其 SoundEngine 依赖为 Profile。

### 找不到 `AK/Plugin/AkAudioBusHackerPlugin.h`

不要向 Wwise SDK 手工复制旧头文件。迁移后的回调 API 已包含在 `AudioBusHackerFXFactory.h` 中；重新生成并编译当前源码。

### `PluginMFCWindows` 未定义

旧空 GUI 类不需要 MFC。当前源码已移除该基类；若未来加入真正的 MFC 对话框，再安装 Visual Studio MFC 组件并恢复对应初始化。

### `ModuleNotFoundError: markdown` 或 `jinja2`

执行 `& $Python -m pip install markdown jinja2`。

### DLL 无法覆盖或 Wwise 无法加载

关闭 Wwise Authoring 后重新构建，并确认 DLL 来自 `2025.1.4.9062`、`x64` 和正确配置目录。

## 12. 最短执行清单

```powershell
Set-Location "H:\m2_wwise_test01\v_wwise\YXK_AkMIDI\AudioBusHacker2023.1.6.8555\AudioBusHacker"
$env:WWISEROOT = "G:\Wwise2025.1.4.9062"
$env:PYTHONUTF8 = "1"
$Python = "C:\Path\To\Python3\python.exe"
$Wp = Join-Path $env:WWISEROOT "Scripts\Build\Plugins\wp.py"

& $Python $Wp premake Windows_vc170 -t vc170 --disable-codesign
& $Python $Wp premake Authoring_Windows -t vc170 --disable-codesign
& $Python $Wp build Windows_vc170 -c Debug -x x64 -t vc170
& $Python $Wp build Windows_vc170 -c Profile -x x64 -t vc170
& $Python $Wp build Windows_vc170 -c Release -x x64 -t vc170
& $Python $Wp build Authoring_Windows -c Debug -x x64 -t vc170
& $Python $Wp build Authoring_Windows -c Release -x x64 -t vc170
& $Python -m pip install markdown jinja2
& $Python $Wp build Documentation
```
