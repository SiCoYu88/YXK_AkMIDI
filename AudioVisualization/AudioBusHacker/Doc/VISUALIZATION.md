# AudioBusHacker 可视化数据 API

AudioBusHacker 在 Bus 插入点分析 `AkAudioBuffer`，以约 25 Hz 生成固定大小的 `AkAudioBusHackerVisualizationData`。分析不会修改音频样本，也不会在 `Execute()` 中动态分配内存或加锁。

公共定义位于：

```text
SoundEnginePlugin/AudioBusHackerFXFactory.h
```

Authoring 构建后，该头文件会安装到：

```text
G:\Wwise2025.1.4.9062\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h
```

## 数据内容

| 字段 | 说明 |
| --- | --- |
| `uVersion` / `uStructSize` | 数据协议版本和结构体大小 |
| `uSequence` | 当前插件实例递增的快照序号 |
| `uBusID` | `IAkEffectPluginContext::GetAudioNodeID()` 返回的 Bus ID |
| `uSampleRate` | 采样率 |
| `uChannelConfig` | 序列化的 Wwise 声道配置 |
| `uNumChannels` | Bus 实际声道数 |
| `uAnalyzedChannels` | 实际分析的声道数，最多 16 |
| `uFrames` | 当前快照累计的音频帧数 |
| `fChannelPeakDb` | 每声道 Peak，单位 dBFS |
| `fChannelRmsDb` | 每声道 RMS，单位 dBFS |
| `fWaveformMin/Max` | 128 段单声道混合波形包络，线性幅度 |
| `fSpectrumDb` | 64 段对数频谱，单位 dBFS |
| `fStereoCorrelation` | 前两个声道的相关度，范围 -1 到 1 |
| `fDownstreamGain` | 当前节点到输出设备的累计线性增益 |

无信号的 dB 值钳制为 `-120 dBFS`。波形是所分析声道的平均值，保留每个时间段内的最小值和最大值。

## 频谱

频谱使用最近 1024 个单声道混合样本、Hann 窗和 64 个 Goertzel 频点。频点在 `fSpectrumMinHz` 到 `fSpectrumMaxHz` 之间按对数分布：

```text
frequency(bin) = minHz * pow(maxHz / minHz, bin / 63.0)
```

`bin` 范围为 0–63。最大频率为采样率 Nyquist 和 20 kHz 中的较小值。

## 外部可视化回调

注册新的汇总数据回调：

```cpp
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/Plugin/AudioBusHackerFXFactory.h>

void OnAudioBusVisualization(
    const AkAudioBusHackerVisualizationData* in_pData)
{
    // Copy the complete POD value into an application-owned lock-free queue.
    visualizationQueue.try_push(*in_pData);
}

void StartVisualization()
{
    SetAudioBusHackerVisualizationCallback(&OnAudioBusVisualization);
}

void StopVisualization()
{
    SetAudioBusHackerVisualizationCallback(nullptr);
}
```

回调在 Wwise 音频线程执行，传入指针只在回调期间有效。回调中只能做固定大小复制或写入无锁队列，不能：

- 保存传入指针供以后使用；
- 更新 UI；
- 分配或释放内存；
- 获取互斥锁；
- 写文件或执行网络请求；
- 执行耗时日志。

UI/渲染线程应从自己的队列读取快照，然后绘制电平、波形、频谱和相关度。

旧的 `SetAudioBusHackerCallbacks` 原始 PCM 回调仍然保留，但它每个音频块调用一次，且暴露临时 `AkAudioBuffer*`。新代码应优先使用可视化汇总回调。

## Wwise Authoring 监控

在 Debug/Profile SoundEngine 中，插件会在 `CanPostMonitorData()` 返回 true 时调用 `PostMonitorData()`。Authoring 侧的 `AudioBusHackerPluginGUI` 实现了：

```cpp
AK::Wwise::Plugin::Notifications::Monitor
```

并在 `NotifyMonitorData()` 中检查版本和大小后保存最新快照。Wwise 的监控数据传输由 Profiling 通道异步复制，不会保留音频线程的栈指针。

`PostMonitorData()` 在优化后的 Release SoundEngine 中不可用；外部可视化回调仍会生成。

## 多 Bus

每个 Bus 上的插件实例独立分析，并通过 `uBusID` 标识来源。若要同时显示多个 Bus：

1. 在每个目标 Bus 上插入 AudioBusHacker。
2. 回调端按 `uBusID` 将快照写入不同队列或缓存。
3. 使用 `AK::SoundEngine::GetIDFromString()` 建立 Bus 名称与 ID 的映射。

一个实例只能看到其插入点的混合结果，无法读取任意其他 Bus，也无法把已混合信号拆回各个 Voice。

## 数据刷新与性能

- 目标刷新率：25 Hz。
- 频谱窗口：1024 帧。
- 波形包络：128 段。
- 频谱：64 段。
- 最大分析声道：16。
- 每个快照约 1.5 KB。

分析始终使用预分配成员数组。没有注册外部回调且 `CanPostMonitorData()` 为 false 时会自动旁路。若目标平台的音频预算仍然紧张，可进一步调低 `kVisualizationRateHz`。
