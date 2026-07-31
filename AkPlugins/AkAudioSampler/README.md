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
- 承载 `AkAudioSampler` 模块的父级 `.uplugin` 已声明 Niagara 插件依赖：

  ```json
  "Plugins": [
    { "Name": "Niagara", "Enabled": true }
  ]
  ```

  如果父插件已有 `Plugins` 数组，只添加其中的 Niagara 项，不要建立第二个同名字段。

## 蓝图使用

1. Wwise SoundEngine 初始化后，确认 `Is Audio Bus Visualization Available` 为 true，
   再调用 `Register Audio Bus Visualization`。
2. 调用 `Get Audio Bus Visualization`，直接传入 Wwise Bus 名称，例如 `Music`。
3. C++ 内部会将名称转换为 Wwise Short ID，并查询对应 Bus 的最新快照。
4. 模块卸载或不再需要数据时，调用 `Unregister Audio Bus Visualization`。

返回结构包含：

- 每声道 Peak/RMS，单位 dBFS；
- 128 段波形最小值和最大值，线性幅度；
- 64 段对数频谱及对应频率，频谱单位 dBFS、频率单位 Hz；
- 立体声相关度、下游增益、采样率、声道数和 Bus 名称。

Short ID 只在 C++ 内部用于匹配 Wwise 回调数据，不会暴露给蓝图。返回结构中的
`BusName` 是查询时传入的名称；Wwise Short ID 是单向哈希，不能从回调 ID 反查名称。

为兼容已有蓝图，`Update Sample Spectrum Callback` 节点仍然保留。它现在返回
Wwise 侧计算的 64 段 `SpectrumDb`，`Tick` 对应快照序号，不再返回 UE 侧 FFT 的线性幅度。

Wwise 回调运行在音频线程。插件只在该线程执行固定大小复制并写入预分配无锁队列；
蓝图数组和频率坐标在查询线程生成。

## Niagara 使用

插件提供 `Ak Wwise Spectrum` Data Interface，可用于 Niagara CPU Simulation 和
GPU Compute Simulation。它直接读取 AudioBusHacker 已计算的频谱，不会在 UE 中重复执行 FFT。

1. 在 Niagara System 或 Emitter 中新建 Data Interface 参数，类型选择
   `Ak Wwise Spectrum`。
2. 将 `Bus Name` 设置为安装了 AudioBusHacker 效果器的 Wwise Bus 名称，例如
   `Music`。名称会在运行时转换为 Wwise Short ID，必须与 Wwise 工程一致。
3. 在 Niagara 脚本中调用 `AudioSpectrum`，传入 `[0, 1]` 的
   `Normalized Position In Spectrum` 和 `Channel Index = 0`。
4. 可先调用 `IsSpectrumValid` 判断是否已收到目标 Bus 的有效快照。

Data Interface 默认启用 `Auto Register Visualization Callback`，因此仅使用 Niagara 时
不需要再从蓝图调用 `Register Audio Bus Visualization`。如果关闭自动注册，必须由蓝图或
C++ 在音频播放前保持回调注册。多个 Niagara 实例和蓝图消费者共享同一回调，内部使用引用计数，
销毁其中一个实例不会中断其他实例。

### Niagara 函数

- `AudioSpectrum(NormalizedPositionInSpectrum, ChannelIndex) -> Amplitude`：
  在配置频率范围内采样，返回归一化幅度。无数据或 `ChannelIndex != 0` 时返回 0。
- `GetNumChannels() -> NumChannels`：有效时返回 1，无有效数据时返回 0。
  AudioBusHacker 频谱是所分析声道混合后的单路数据。
- `IsSpectrumValid() -> IsValid`：目标 Bus 已收到且未超时的快照时返回 true。

### Data Interface 参数

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| `Bus Name` | `Master Audio Bus` | Wwise Bus 名称，目标 Bus 必须安装 AudioBusHacker |
| `Resolution` | 64 | Niagara 输出采样数，范围 16-1024；超过 64 只会插值 |
| `Minimum Frequency` | 20 Hz | 归一化位置 0 对应的频率 |
| `Maximum Frequency` | 20000 Hz | 归一化位置 1 对应的频率 |
| `Noise Floor Db` | -60 dBFS | 映射为幅度 0 的电平；0 dBFS 映射为 1 |
| `Auto Register Visualization Callback` | true | 自动持有共享可视化回调注册 |
| `Stale Data Timeout Seconds` | 0.25 s | 超过该时间没有新快照时清零；0 表示不超时 |

AudioBusHacker 提供 64 个对数分布频点，更新率约为 25 Hz。Niagara 在两个快照之间保持
最近一次数据。提高 `Resolution` 或 Niagara Tick 频率不会增加源频谱的真实时间或频率精度。

### 编辑器预览与排查

- 编辑器预览必须已经初始化 Wwise SoundEngine，并有音频实际经过目标 Bus；仅打开 Niagara
  编辑器不会产生频谱。
- `Is Audio Bus Visualization Available` 为 false 时，检查日志中是否出现
  `Loaded AudioBusHacker visualization API`。
- Windows 下 DLL 必须位于
  `Plugins/WwiseSoundEngine/ThirdParty/x64_vc170/<Profile|Release|Debug>/bin/AudioBusHacker.dll`。
- `IsSpectrumValid` 为 false 时，依次检查 Bus 名称、AudioBusHacker 插入位置、音频路由和回调注册。
- 目前仅支持混合单路频谱；逐声道 Peak/RMS 仍通过 `Get Audio Bus Visualization` 获取。
