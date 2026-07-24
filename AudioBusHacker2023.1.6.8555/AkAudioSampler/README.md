# AkAudioSampler 可视化数据接入

AkAudioSampler 使用 `AudioBusHacker.dll` 导出的
`SetAudioBusHackerVisualizationCallback`，直接消费 Wwise 侧生成的可视化快照。
UE 不再接收原始 PCM 或自行执行 FFT。

## 前置条件

- Wwise 工程中的目标 Bus 已插入 AudioBusHacker。
- 使用包含提交 `6ac93bd7d1082d9d88e85575af2d6b81195eb5fb` 的插件源码重新构建。
- `AudioBusHacker.dll` 已安装到 Wwise Unreal 插件的
  `ThirdParty/x64_vc170/<Profile|Release|Debug>/bin`。
- `AudioBusHackerFXFactory.h` 已安装到 Wwise SDK 的 `include/AK/Plugin`。

## 蓝图使用

1. Wwise SoundEngine 初始化后，确认 `Is Audio Bus Visualization Available` 为 true，
   再调用 `Register Audio Bus Visualization`。
2. 只有一个 Bus 时，调用 `Get Audio Bus Visualization` 并将 `BusID` 设为 `-1`。
3. 多个 Bus 时，先调用 `Get Audio Bus Visualization IDs`，再用指定 ID 查询。
4. 模块卸载或不再需要数据时，调用 `Unregister Audio Bus Visualization`。

返回结构包含：

- 每声道 Peak/RMS，单位 dBFS；
- 128 段波形最小值和最大值，线性幅度；
- 64 段对数频谱及对应频率，频谱单位 dBFS、频率单位 Hz；
- 立体声相关度、下游增益、采样率、声道数和 Bus ID。

为兼容已有蓝图，`Update Sample Spectrum Callback` 节点仍然保留。它现在返回
Wwise 侧计算的 64 段 `SpectrumDb`，`Tick` 对应快照序号，不再返回 UE 侧 FFT 的线性幅度。

Wwise 回调运行在音频线程。插件只在该线程执行固定大小复制并写入预分配无锁队列；
蓝图数组和频率坐标在查询线程生成。
