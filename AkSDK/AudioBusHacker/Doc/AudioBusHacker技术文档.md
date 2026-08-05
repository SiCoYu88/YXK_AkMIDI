# AudioBusHacker 技术文档

## 1. 文档范围

本文说明 AudioBusHacker 的代码结构、运行时数据流、公共回调 API、线程约束、编译、打包和验证方法。

本文以仓库当前源码为准：

- Wwise SDK：`2025.1.4.9062`
- Windows 工具集：Visual Studio 2022 / MSVC v143（`vc170`）
- Windows 架构：`x64`
- 插件类型：Wwise In-Place Effect
- Company ID：`64`
- Plugin ID：`15450`

`LegacyPackages` 中的 2023.1.6.8555 安装包只用于保留历史版本，不能安装到 Wwise 2025.1。

## 2. 功能概述

AudioBusHacker 插入 Wwise Bus 后读取该插入点的 `AkAudioBuffer`。当前实现提供两类接口：

1. 原始 PCM 回调：每个音频块回调一次，传出临时 `AkAudioBuffer*`。
2. 可视化汇总回调：约 25 Hz 输出固定大小的电平、波形、频谱和立体声相关度数据。

可视化分析不会修改音频样本。没有注册外部可视化回调、且 Wwise Monitor 不需要数据时，分析会自动旁路。

当前 `Placeholder` 属性来自 Wwise 插件模板，已写入 SoundBank 并支持 RTPC，但没有参与 DSP 或可视化计算。它不应被视为有效的业务控制参数。

## 3. 目录与职责

| 路径 | 职责 |
| --- | --- |
| `AudioBusHackerConfig.h` | Company ID 与 Plugin ID |
| `PremakePlugin.lua` | Wwise Premake 工程定义 |
| `SoundEnginePlugin/AudioBusHackerFX.*` | SoundEngine Effect、音频分析和回调发布 |
| `SoundEnginePlugin/AudioBusHackerFXParams.*` | 参数初始化、SoundBank 反序列化和 RTPC 更新 |
| `SoundEnginePlugin/AudioBusHackerFXFactory.h` | 静态注册、公共结构体和回调声明 |
| `WwisePlugin/AudioBusHackerPlugin.*` | Authoring 插件与 SoundBank 参数写入 |
| `WwisePlugin/AudioBusHacker.xml` | 插件元数据、平台能力和属性定义 |
| `WwisePlugin/Win32/AudioBusHackerPluginGUI.*` | 接收 Authoring Monitor 数据；当前未实现绘图控件 |
| `FactoryAssets/Manifest.xml` | Factory Assets 依赖声明 |
| `additional_artifacts.json` | 安装包附加文件映射 |
| `build_wwise_2025_1_4.bat` | 生成、编译、文档、打包和验证入口 |
| `Doc/VISUALIZATION.md` | 可视化数据协议的简明说明 |

## 4. 运行时架构

```text
Wwise Bus
   |
   v
AudioBusHackerFX::Execute(AkAudioBuffer*)
   |-- 原始 PCM 回调（每个音频块，可选）
   |
   `-- AnalyzeBuffer（仅在有消费者时）
          |-- 每声道 Peak / RMS
          |-- 128 段波形包络
          |-- 1024 帧历史 + Hann 窗
          |-- 64 个对数分布 Goertzel 频点
          `-- 前两个声道相关度
                    |
                    v 约 25 Hz
          AkAudioBusHackerVisualizationData
                    |-- 外部回调（所有配置）
                    `-- Wwise PostMonitorData（非 AK_OPTIMIZED）
```

`GetPluginInfo()` 将插件声明为 In-Place Effect，因此 `Execute()` 接收并传递同一块音频缓冲区。当前实现本身不改写样本，但旧的原始 PCM 回调能够访问可写的 `AkAudioBuffer*`，调用方必须自行保证不会破坏声音。

插件 XML 声明了 Audio Object 插入能力，但 SoundEngine 的 `GetPluginInfo()` 当前设置 `bCanProcessObjects = false`。在统一这两处能力声明并完成测试前，只把 Bus 插入视为已验证用法。

## 5. 核心代码

### 5.1 插件工厂

插件由 Company ID、Plugin ID 和 Effect 类型唯一标识：

```cpp
AK_IMPLEMENT_PLUGIN_FACTORY(
    AudioBusHackerFX,
    AkPluginTypeEffect,
    AudioBusHackerConfig::CompanyID,
    AudioBusHackerConfig::PluginID)
```

修改 ID 时必须同时更新：

- `AudioBusHackerConfig.h`
- `WwisePlugin/AudioBusHacker.xml`
- `FactoryAssets/Manifest.xml`

### 5.2 音频线程入口

`Execute()` 先执行兼容用的原始 PCM 回调，再按需执行汇总分析：

```cpp
void AudioBusHackerFX::Execute(AkAudioBuffer* io_pBuffer)
{
    const auto rawCallback = m_ABHExecCallback.load(std::memory_order_acquire);
    if (rawCallback)
        rawCallback(io_pBuffer);

    bool shouldAnalyze =
        m_visualizationCallback.load(std::memory_order_acquire) != nullptr;

#ifndef AK_OPTIMIZED
    if (!shouldAnalyze && m_pContext)
        shouldAnalyze = m_pContext->CanPostMonitorData();
#endif

    if (shouldAnalyze)
        AnalyzeBuffer(io_pBuffer);
}
```

实际源码还会在分析从关闭切换为开启时调用 `Reset()`，避免把旧历史数据带入新会话。

### 5.3 公共数据结构

公共定义位于 `SoundEnginePlugin/AudioBusHackerFXFactory.h`。编译或安装 SDK 包后，它会复制到：

```text
%WWISEROOT%\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h
```

主要字段如下：

| 字段 | 含义 |
| --- | --- |
| `uVersion` / `uStructSize` | 协议版本与结构体大小，用于兼容性检查 |
| `uSequence` | 当前插件实例的递增序号 |
| `uBusID` | `GetAudioNodeID()` 返回的 Bus ID |
| `uSampleRate` | 采样率 |
| `uChannelConfig` | 序列化后的 Wwise 声道配置 |
| `uNumChannels` | Bus 实际声道数 |
| `uAnalyzedChannels` | 实际分析声道数，最多 16 |
| `uFrames` | 当前快照累计的音频帧数 |
| `fChannelPeakDb[16]` | 每声道峰值，单位 dBFS |
| `fChannelRmsDb[16]` | 每声道 RMS，单位 dBFS |
| `fWaveformMin/Max[128]` | 单声道混合波形的最小/最大包络 |
| `fSpectrumDb[64]` | 对数分布频点的幅度，单位 dBFS |
| `fStereoCorrelation` | 前两个声道相关度，范围 `-1..1` |
| `fDownstreamGain` | 当前节点到输出设备的线性累计增益 |

无信号的 dB 值钳制为 `-120 dBFS`。频谱只在积累满 1024 个混合样本后有效，此前各频点均为 `-120 dBFS`。

### 5.4 注册汇总回调

```cpp
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/Plugin/AudioBusHackerFXFactory.h>

void OnAudioBusVisualization(
    const AkAudioBusHackerVisualizationData* in_pData)
{
    if (!in_pData ||
        in_pData->uVersion != AK_AUDIO_BUS_HACKER_VISUALIZATION_VERSION ||
        in_pData->uStructSize < sizeof(AkAudioBusHackerVisualizationData))
    {
        return;
    }

    // 回调运行在音频线程。这里只做固定大小复制并写入无锁队列。
    visualizationQueue.try_push(*in_pData);
}

void StartAudioBusVisualization()
{
    SetAudioBusHackerVisualizationCallback(&OnAudioBusVisualization);
}

void StopAudioBusVisualization()
{
    SetAudioBusHackerVisualizationCallback(nullptr);
}
```

注册函数当前固定返回整数 `8`，它不是可用的成功/失败状态码。调用方不要根据该返回值判断注册结果。

### 5.5 原始 PCM 回调

```cpp
void OnAudioBlock(AkAudioBuffer* io_pBuffer)
{
    // io_pBuffer 只在本次回调内有效。
}

SetAudioBusHackerCallbacks(&OnAudioBlock);
// 停止接收
SetAudioBusHackerCallbacks(nullptr);
```

原始 PCM 回调每个音频块触发一次，数据量和实时风险都高于汇总回调。新功能应优先使用 `SetAudioBusHackerVisualizationCallback()`。

## 6. 分析算法

### 6.1 Peak 与 RMS

每个发布周期内，各声道分别累计绝对峰值与平方和：

```text
peak = max(abs(sample))
rms  = sqrt(sum(sample * sample) / frameCount)
dBFS = max(-120, 20 * log10(linear))
```

非有限样本（NaN 或 Inf）按 0 处理。

### 6.2 波形

所有已分析声道先求平均得到 mono 样本，再按当前约 40 ms 周期映射到 128 个时间段。每段保存最小值与最大值，适合绘制竖线或填充包络。

### 6.3 频谱

频谱使用最近 1024 个 mono 样本、Hann 窗和 64 个 Goertzel 频点。频点从 20 Hz 到 `min(Nyquist, 20000 Hz)` 按对数分布：

```text
frequency(bin) = minHz * pow(maxHz / minHz, bin / 63.0)
```

这是固定频点分析，不是完整 FFT 频谱。若 UI 需要频带能量，应在消费端根据相邻频点做平滑或聚合，不要在音频回调中执行额外重计算。

### 6.4 立体声相关度

相关度只使用前两个声道：

```text
correlation = sum(L * R) / sqrt(sum(L^2) * sum(R^2))
```

结果钳制到 `-1..1`。单声道返回 `1`；少于两个有效声道且不是单声道时返回 `0`。

## 7. 实时线程约束

外部回调和原始 PCM 回调都由 Wwise 音频线程调用。回调中只允许固定成本、无阻塞操作，例如复制 POD 数据到预分配的无锁队列。

回调中禁止：

- 保存传入指针到回调结束后继续使用；
- 直接更新 Slate、UMG、ImGui 或其他 UI；
- 申请/释放堆内存；
- 获取互斥锁或等待事件；
- 写文件、访问网络或执行大量日志；
- 执行不可预测耗时的频谱或图形计算。

推荐的数据流是“音频线程复制快照 -> 无锁 SPSC 队列 -> 游戏/UI 线程消费”。停止 Wwise SoundEngine 或卸载模块前，应先把回调设置为 `nullptr`，防止回调进入已经销毁的对象或模块代码。

## 8. 参数与 SoundBank

Authoring 端 `GetBankParameters()` 将 `Placeholder` 写成一个 `Real32`。SoundEngine 端按相同顺序读取：

```cpp
// Authoring
in_dataWriter.WriteReal32(
    m_propertySet.GetReal32(in_guidPlatform, "Placeholder"));

// SoundEngine
RTPC.fPlaceholder = READBANKDATA(
    AkReal32, pParamsBlock, in_ulBlockSize);
```

若新增参数，需要同步修改：

1. `WwisePlugin/AudioBusHacker.xml` 中的属性与 `AudioEnginePropertyID`。
2. `AudioBusHackerFXParams.h` 中的参数 ID、结构体和 `NUM_PARAMS`。
3. `AudioBusHackerPlugin.cpp` 的 SoundBank 写入顺序。
4. `AudioBusHackerFXParams.cpp` 的读取顺序和 `SetParam()`。
5. 对应语言的 Property Help Markdown。

SoundBank 写入和读取顺序必须严格一致，否则会出现参数错位或 Bank 数据校验失败。

## 9. Windows 编译

### 9.1 环境要求

- Wwise Authoring、SDK 和 Windows SDK 平台包：`2025.1.4.9062`
- Visual Studio 2022 或 Build Tools 2022
- “使用 C++ 的桌面开发”工作负载
- MSVC v143 和 Windows 10/11 SDK
- Python 3
- 生成 Property Help 时需要 Python 包 `markdown`、`jinja2`

默认 Wwise 路径是 `G:\Wwise2025.1.4.9062`。安装在其他目录时，先设置 `WWISEROOT`。

### 9.2 一键构建

在插件根目录运行：

```bat
set "WWISEROOT=G:\Wwise2025.1.4.9062"
set "PYTHON_EXE=C:\Path\To\Python3\python.exe"
set "INSTALL_DOC_DEPS=1"
build_wwise_2025_1_4.bat all
```

无参数运行也默认执行 `all`。该流程依次执行：

1. 生成 Windows 与 Authoring 的 vc170 工程。
2. 编译 SoundEngine Debug/Profile/Release。
3. 编译 Authoring Debug/Release。
4. 生成四种语言的 Property Help。
5. 生成 SDK 与 Authoring 安装包和 `bundle.json`。
6. 检查关键产物、版本和包数量。

可以分步运行：

```bat
build_wwise_2025_1_4.bat premake
build_wwise_2025_1_4.bat build
build_wwise_2025_1_4.bat docs
build_wwise_2025_1_4.bat package
build_wwise_2025_1_4.bat verify
```

### 9.3 手工调用 Wwise 构建工具

在 Developer PowerShell for VS 2022 中执行：

```powershell
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

`PYTHONUTF8=1` 可避免中文 Windows 环境下读取工具输出时发生 GBK 解码错误。Authoring 工程只生成 Debug 和 Release；不要构建 Authoring Profile。Authoring Release 会链接 SoundEngine Profile 静态库，因此必须先完成 Profile 构建。

本地开发使用 `--disable-codesign`。正式签名构建应移除该参数，并按发布流程配置签名工具和证书。

### 9.4 主要产物

```text
%WWISEROOT%\SDK\x64_vc170\Debug\bin\AudioBusHacker.dll
%WWISEROOT%\SDK\x64_vc170\Profile\lib\AudioBusHackerFX.lib
%WWISEROOT%\SDK\x64_vc170\Profile(StaticCRT)\lib\AudioBusHackerFX.lib
%WWISEROOT%\SDK\x64_vc170\Release\bin\AudioBusHacker.dll
%WWISEROOT%\Authoring\x64\Debug\bin\Plugins\AudioBusHacker.dll
%WWISEROOT%\Authoring\x64\Release\bin\Plugins\AudioBusHacker.dll
%WWISEROOT%\Authoring\x64\Release\bin\Plugins\AudioBusHacker.xml
%WWISEROOT%\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h
```

工程根目录生成三个 2025.1.4.9062 安装包：

```text
AudioBusHacker_v2025.1.4_Build9062_SDK.Windows_vc170.tar.xz
AudioBusHacker_v2025.1.4_Build9062_Authoring.Windows_x64.Debug.tar.xz
AudioBusHacker_v2025.1.4_Build9062_Authoring.Windows_x64.Release.tar.xz
```

### 9.5 Android 状态

仓库中的 Android `.mk` 仍属于 2023.1 生成物。当前没有完成 Wwise 2025.1.4 Android 平台迁移和验证，不能把这些文件视为可发布产物。补装对应 Wwise Android 平台包后，必须重新 Premake、编译、部署并在真机验证。

## 10. 链接与集成

### 10.1 静态 SoundEngine 插件

将 `AudioBusHackerFX.lib` 加入应用的链接依赖，并包含安装后的 Factory 头。该头默认执行：

```cpp
AK_STATIC_LINK_PLUGIN(AudioBusHackerFX);
```

因此应确保它在最终应用的一个编译单元中以默认方式包含。不要在多个不受控制的模块中重复设计注册逻辑。

### 10.2 共享 DLL

共享插件导出两个 C 接口：

```cpp
extern "C" AK_DLLEXPORT int SetAudioBusHackerCallbacks(...);
extern "C" AK_DLLEXPORT int SetAudioBusHackerVisualizationCallback(...);
```

需要只声明 API、避免静态注册时，可在包含 Factory 头前定义 `AUDIO_BUS_HACKER_NO_STATIC_LINK`。应用必须保证函数指针的调用约定、结构体布局和 Wwise SDK 版本与插件一致。

## 11. 验证清单

编译后至少完成以下检查：

- `build_wwise_2025_1_4.bat verify` 通过。
- Wwise Authoring 能创建 AudioBusHacker Effect 并生成 SoundBank。
- 将 Effect 插入正在输出声音的 Bus 后，外部回调约 25 Hz 收到数据。
- `uBusID` 与目标 Bus 的 Short ID 一致。
- 无声时电平和频谱为 `-120 dBFS` 附近。
- 正弦波输入时，对应频点明显高于相邻频点。
- 双声道同相信号相关度接近 `1`，反相信号接近 `-1`。
- 注册和注销回调时没有音频线程阻塞、悬空对象或模块卸载崩溃。
- Release SoundEngine 的外部回调可用；不要依赖 Release 下的 Authoring Monitor 数据。

## 12. 已知限制

- 每个插件实例只能看到自己的插入点，无法读取任意 Bus 或拆分已经混合的 Voice。
- 最多分析 16 个声道；`uNumChannels` 仍会报告实际声道数。
- 当前刷新率固定为 25 Hz，频谱窗口固定为 1024 帧。
- 频谱是 64 个 Goertzel 频点，不等同于连续 FFT 频谱。
- 当前 Authoring GUI 只接收 Monitor 快照，没有实现可见的电平、波形或频谱绘制。
- `Placeholder` 参数不参与实际处理。
- Audio Object 路径未作为已验证能力。
- Android 的 Wwise 2025.1.4 构建尚未验证。

更精简的数据协议说明见 [VISUALIZATION.md](VISUALIZATION.md)，现有迁移记录见仓库根目录的 [BUILD.md](../BUILD.md)。
