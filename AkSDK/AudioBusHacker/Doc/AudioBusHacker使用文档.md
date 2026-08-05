# AudioBusHacker 使用文档

## 1. 用途

AudioBusHacker 用于读取 Wwise Bus 插入点的混合音频，并向游戏、工具或可视化模块提供：

- 每声道 Peak 和 RMS 电平；
- 128 段波形包络；
- 64 个对数分布频谱点；
- 双声道相关度；
- Bus ID、采样率、声道配置和下游增益。

插件本身不会改变声音。当前推荐用法是插入 Bus 并通过外部可视化回调读取汇总数据。

## 2. 版本与限制

当前发布目标是：

- Wwise `2025.1.4.9062`
- Windows `x64`
- Visual Studio 2022 / `vc170`

请不要把 `LegacyPackages` 中的 Wwise 2023.1.6.8555 包安装到 Wwise 2025.1。

当前版本还有以下使用边界：

- Wwise Authoring 中没有内置的可视化绘图面板；可视化由外部程序或游戏 UI 完成。
- 属性面板中的 `Placeholder` 是模板遗留参数，不影响声音和分析结果。
- 只把 Bus 插入作为已验证路径，不建议用于 Audio Object。
- Wwise 2025.1.4 Android 版本尚未验证。

## 3. 安装

### 3.1 本机开发构建

如果本机有源码和 Wwise SDK，在插件根目录运行：

```bat
set "WWISEROOT=G:\Wwise2025.1.4.9062"
set "PYTHON_EXE=C:\Path\To\Python3\python.exe"
build_wwise_2025_1_4.bat build
build_wwise_2025_1_4.bat verify
```

构建工具会把 Authoring 插件和 SoundEngine 产物部署到 `WWISEROOT` 对应目录。构建前关闭 Wwise Authoring，避免插件 DLL 被占用。

### 3.2 使用发布包

发布目录包含：

```text
bundle.json
AudioBusHacker_v2025.1.4_Build9062_Authoring.Windows_x64.Release.tar.xz
AudioBusHacker_v2025.1.4_Build9062_Authoring.Windows_x64.Debug.tar.xz
AudioBusHacker_v2025.1.4_Build9062_SDK.Windows_vc170.tar.xz
```

通过 Wwise Launcher 的插件安装入口添加包含 `bundle.json` 和三个包的目录，并为 Wwise 2025.1 安装 Authoring Release 与 Windows vc170 SDK 包。不同 Launcher 版本的入口文字可能不同；安装时以目标 Wwise 版本显示为 `2025.1`、插件名显示为 `AudioBusHacker` 为准。

Debug Authoring 包只用于调试 Authoring 插件，普通使用安装 Release 即可。

### 3.3 安装检查

确认下列文件存在：

```text
%WWISEROOT%\Authoring\x64\Release\bin\Plugins\AudioBusHacker.dll
%WWISEROOT%\Authoring\x64\Release\bin\Plugins\AudioBusHacker.xml
%WWISEROOT%\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h
%WWISEROOT%\SDK\x64_vc170\Profile\lib\AudioBusHackerFX.lib
```

重新启动 Wwise Authoring。若 Wwise 在安装时保持打开，插件列表可能不会刷新。

## 4. 在 Wwise 中配置

1. 打开目标 Wwise 工程。
2. 在 Master-Mixer Hierarchy 中选择需要观测的 Audio Bus 或 Auxiliary Bus。
3. 打开该 Bus 的 Effects 页面。
4. 在一个空的 Insert Effect 插槽中选择 `AudioBusHacker`。
5. 确认 Effect 未被 Bypass，并生成目标平台的 SoundBank。
6. 运行游戏或在 Wwise 中播放会路由到该 Bus 的声音。

插件只分析流经插入点的混合结果。若 Bus 没有实际音频经过，回调不会产生有意义的电平或频谱。

多 Bus 监控时，需要在每个目标 Bus 上分别插入一个 AudioBusHacker。每个实例通过 `uBusID` 标识数据来源。

## 5. 在应用中接收可视化数据

### 5.1 链接插件

将与应用配置匹配的 `AudioBusHackerFX.lib` 加入链接依赖，并确保运行时部署了对应插件。典型选择是：

| 应用配置 | 推荐插件产物 |
| --- | --- |
| Debug | `SDK/x64_vc170/Debug` |
| 日常开发/Profile | `SDK/x64_vc170/Profile` |
| Shipping/Release | `SDK/x64_vc170/Release` |

不要混用 Wwise 版本、架构、MSVC 工具集或 CRT 类型。

### 5.2 注册回调

在 Wwise SoundEngine 初始化完成、目标 Bus 开始播放前注册：

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

    // 必须是预分配、无锁、固定耗时的队列。
    visualizationQueue.try_push(*in_pData);
}

void StartVisualization()
{
    SetAudioBusHackerVisualizationCallback(&OnAudioBusVisualization);
}
```

回调约 25 Hz 触发。传入指针只在本次回调期间有效，所以必须复制结构体，不能保存指针。

### 5.3 在 UI 线程消费

游戏或 UI 线程从队列读取最新快照：

```cpp
AkAudioBusHackerVisualizationData data{};
while (visualizationQueue.try_pop(data))
{
    // 若积压多帧，可只保留最后一帧。
}

DrawMeters(data.fChannelPeakDb, data.uAnalyzedChannels);
DrawWaveform(data.fWaveformMin, data.fWaveformMax, data.uWaveformBins);
DrawSpectrum(data.fSpectrumDb, data.uSpectrumBins,
             data.fSpectrumMinHz, data.fSpectrumMaxHz);
DrawCorrelation(data.fStereoCorrelation);
```

推荐显示规则：

| 数据 | 建议显示 |
| --- | --- |
| `fChannelPeakDb` | 瞬时峰值表，范围可设为 `-60..0 dBFS` |
| `fChannelRmsDb` | 平均响度趋势，范围可设为 `-60..0 dBFS` |
| `fWaveformMin/Max` | 每个时间段绘制从 Min 到 Max 的竖线或填充区 |
| `fSpectrumDb` | 横轴按对数频率，纵轴按 dBFS |
| `fStereoCorrelation` | `-1..1` 相关度表；0 以下提示相位风险 |

频谱启动后需要先积累 1024 个样本。在 48 kHz 下约为 21 ms，首帧暂时显示 `-120 dBFS` 属于正常现象。

### 5.4 区分多个 Bus

回调是进程级入口，多个插件实例都会进入同一个回调。用 `uBusID` 分流：

```cpp
const AkUniqueID musicBusId =
    AK::SoundEngine::GetIDFromString("Music");

if (in_pData->uBusID == musicBusId)
{
    musicVisualizationQueue.try_push(*in_pData);
}
```

Bus 名称与实际 Wwise 工程保持一致。也可以在初始化时缓存多个 Bus Short ID，避免在音频回调里做字符串计算。

### 5.5 停止与退出

在销毁队列、UI 对象、游戏模块或 Wwise SoundEngine 之前先注销回调：

```cpp
SetAudioBusHackerVisualizationCallback(nullptr);
```

注销后再等待或完成应用自身的音频线程同步，然后销毁回调依赖的存储。不要让回调访问已经卸载的 DLL 或模块代码。

## 6. 原始 PCM 回调

兼容接口 `SetAudioBusHackerCallbacks()` 会把 `AkAudioBuffer*` 直接传给应用：

```cpp
void OnAudioBlock(AkAudioBuffer* io_pBuffer)
{
    // 指针仅在回调期间有效。
}

SetAudioBusHackerCallbacks(&OnAudioBlock);
```

该接口每个音频块调用一次，而且缓冲区可写。除非确实需要原始样本，否则使用汇总回调：它的数据量更小，也更适合电平、波形和频谱 UI。

不再使用时注销：

```cpp
SetAudioBusHackerCallbacks(nullptr);
```

## 7. 音频线程注意事项

两种回调都运行在 Wwise 音频线程。回调中不得：

- 更新 UI；
- 创建容器或申请内存；
- 使用互斥锁、条件变量或阻塞队列；
- 写日志文件或频繁打印；
- 发起磁盘、网络或进程调用；
- 保存传入的结构体指针或音频缓冲区指针。

正确做法是在回调中复制到预分配的无锁队列，再由游戏线程或 UI 线程绘制。

## 8. 数据解释

### 8.1 电平

`fChannelPeakDb` 和 `fChannelRmsDb` 均为 dBFS。静音值为 `-120`。数值越接近 `0`，信号越强；大于 `0` 的值可能表示浮点音频链路中存在超出满刻度的信号。

### 8.2 波形

波形是最多 16 个已分析声道的平均值，不是某一个独立声道。每个 bin 同时包含最小值和最大值，用于保留短促峰值。

### 8.3 频谱

64 个频点在 `fSpectrumMinHz` 和 `fSpectrumMaxHz` 之间按对数分布。横坐标不要按线性 Hz 等距解释；可用以下公式恢复第 `bin` 个频率：

```text
frequency = minHz * pow(maxHz / minHz, bin / (binCount - 1))
```

### 8.4 立体声相关度

- 接近 `1`：左右声道高度同相。
- 接近 `0`：左右声道相关性较低。
- 小于 `0`：存在反相成分，折叠为单声道时可能抵消。

只有前两个声道参与相关度计算。

### 8.5 下游增益

`fDownstreamGain` 是当前节点到输出设备的线性累计增益，不是 dB。转换为 dB 时可用 `20 * log10(gain)`，并处理 0 值。

## 9. Wwise Authoring Monitor

Debug/Profile SoundEngine 可通过 Wwise Profiling 通道把相同的快照发送给 Authoring。当前 Authoring 类能够接收并保存最新快照，但没有实现可见的绘制面板。

Release SoundEngine 中 `PostMonitorData()` 不可用，这是 Wwise 优化构建的预期行为。外部 `SetAudioBusHackerVisualizationCallback()` 在 Release 中仍可使用。

## 10. 常见问题

### Wwise 中找不到 AudioBusHacker

- 确认 Authoring DLL 和 XML 安装在同一个目标 Wwise 版本下。
- 确认安装的是 Windows x64 Release Authoring 包。
- 关闭并重新启动 Wwise Authoring。
- 不要把 2023.1 插件包安装到 2025.1。

### 能添加 Effect，但没有回调

- 确认目标 Bus 上的 Effect 没有 Bypass。
- 确认有声音实际路由到该 Bus。
- 确认游戏使用的 SoundBank 已重新生成并部署。
- 确认应用链接或部署了正确配置的 SoundEngine 插件。
- 确认回调在 SoundEngine 初始化后注册，且未被其他代码设置为 `nullptr`。
- 回调是全局单槽位；后一次注册会覆盖前一次注册。

### 回调有数据，但 UI 不动

- 不要从音频线程直接操作 UI。
- 检查无锁队列是否被 UI 线程持续消费。
- 检查 `uSequence` 是否递增。
- 多 Bus 时检查 `uBusID` 过滤条件是否正确。

### 电平一直是 -120 dBFS

- 确认插入点确实有非静音音频。
- 检查 Bus 音量、Mute、Solo、路由和播放状态。
- 检查是否错误读取了 `uAnalyzedChannels` 之外的数组元素。

### 频谱首帧为空

频谱需要先积累 1024 个样本。这是正常的启动状态，不需要重启插件。

### 编译提示找不到 Factory 头

重新编译或安装 SDK 包，并确认存在：

```text
%WWISEROOT%\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h
```

不要从旧 Wwise 安装目录复制 2023.1 的头文件。

### Wwise DLL 无法覆盖

关闭 Wwise Authoring，再重新编译。Authoring 运行时会占用插件 DLL。

## 11. 最短使用清单

1. 安装与 Wwise 2025.1.4.9062 匹配的 Authoring 和 Windows vc170 SDK 包。
2. 在目标 Bus 的 Insert Effect 中添加 AudioBusHacker。
3. 重新生成并部署 SoundBank。
4. 应用链接正确配置的 `AudioBusHackerFX.lib`。
5. 注册 `SetAudioBusHackerVisualizationCallback()`。
6. 音频线程只复制数据到无锁队列。
7. UI 线程按 `uBusID` 消费并绘制最新快照。
8. 模块退出或 SoundEngine 终止前注销回调。

编译和代码设计细节见 [AudioBusHacker技术文档.md](AudioBusHacker技术文档.md)，字段协议速查见 [VISUALIZATION.md](VISUALIZATION.md)。
