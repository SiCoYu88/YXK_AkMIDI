# Ak Wwise Spectrum Niagara 使用示例

工程中已经添加了一些从NiagaraAudioVisualizer复刻的示例，后续如果使用到新的Niagara特效时，可以参考已有示例

```text
/Game/WwiseAssets/NiagaraWwiseVisualizer
```

该 System 使用 `UNiagaraDataInterfaceAkWwiseSpectrum` 读取 Wwise `Music` Bus 的实时频谱，并显示为 Niagara 音频可视化效果。

## 1. 使用前提

### 1.1 Wwise Bus

1. 在 Wwise 的 `Music` Bus 上插入 `AudioBusHacker` Effect。
2. 确认需要可视化的 Wwise Event 实际路由到 `Music` Bus。
3. 重新生成 SoundBank。
4. 确认工程加载的是包含 Visualization Callback 接口的 `AudioBusHacker.dll`。

编辑器启动日志应包含：

```text
LogAkAudioSampler: Loaded AudioBusHacker visualization API from .../AudioBusHacker.dll
```

如果实际 Bus 不叫 `Music`，需要修改第 4 节中的 `Bus Name`。

### 1.2 Unreal 插件

工程必须启用：

- `AkAudioSampler`
- `Niagara`
- Wwise Integration

新增或更新 `UNiagaraDataInterfaceAkWwiseSpectrum` 后，需要完整编译并重启编辑器，不能只依赖 Live Coding。

## 2. 打开示例关卡

1. 在 Content Browser 中打开：

   ```text
   /Game/WwiseAssets/DemoRoom/Maps/WwiseVisualizer_Demo
   ```
   
2. 使用"Play From Here"运行 PIE。

3. 查看已有的Wwise音频可视化的Niagara示例

Wwise的NiagaraDataInterfaceAkWwiseSpectrum中已经注册和监听了音频可视化接口

不需要在蓝图中调用 `Register Audio Bus Visualization`。System 内的 Data Interface 默认启用了 `Auto Register Visualization Callback`，Niagara 实例创建和销毁时会自动持有和释放共享回调。

## 3. 蓝图动态生成

不希望预先放入关卡时，可以在蓝图中动态生成：

```text
Event BeginPlay
    -> Post Wwise Event
    -> Spawn System at Location
       System = NS_StepLine
```

也可以使用 `Spawn System Attached` 将频谱跟随 Actor：

```text
Event BeginPlay
    -> Post Wwise Event
    -> Spawn System Attached
       System = NS_StepLine
       Attach Component = Root Component
       Location Type = Keep Relative Offset
```

结束时停止 Wwise Event，并对保存的 Niagara Component 调用 `Deactivate`。Data Interface 会自动注销自己的回调引用。

建议先播放 Wwise Event，再创建 Niagara System。顺序相反也不会报错，但首个 Wwise 频谱快照到达前可视化数据暂时无效。

## 4. 修改频谱参数

打开 `NS_StepLine`，选择 `NE_BasicWwiseVisualizer` Emitter，在 `Particle Update` 中找到 `WwiseAudioSpectrum` 模块，展开 `Ak Wwise Spectrum` 输入。

当前预设参数：

| 参数 | 当前值 | 说明 |
| --- | ---: | --- |
| Bus Name | `Music` | 安装 `AudioBusHacker` 的 Wwise Bus 名称 |
| Resolution | `64` | Niagara 频谱缓存采样数 |
| Minimum Frequency | `40 Hz` | 归一化位置 0 对应的频率 |
| Maximum Frequency | `16000 Hz` | 归一化位置 1 对应的频率 |
| Noise Floor Db | `-60 dBFS` | 映射为零幅度的噪声底 |
| Auto Register Visualization Callback | `true` | 自动注册共享回调 |
| Stale Data Timeout Seconds | `0.25` | 超过该时间未收到数据时清零 |

修改参数后点击 `Compile` 和 `Save`。

`Bus Name` 必须与 Wwise Bus 名称完全一致，并且 `AudioBusHacker` 必须安装在同一个 Bus 上。仅修改 Unreal 中的名称不会改变 Wwise 路由。

## 5. System 内部结构

该资产已经完成以下配置，通常不需要修改：

```text
NS_StepLine
└─ NE_BasicWwiseVisualizer
   └─ Particle Update
      ├─ WwiseAudioSpectrum
      ├─ WwiseAudioSpectrumUpdate
      ├─ ScaleSpriteSize
      └─ ScaleRibbonWidth
```

`ScaleSpriteSize` 和 `ScaleRibbonWidth` 直接绑定 `Output.WwiseAudioSpectrum.AudioAmplitude`：幅度经过 `0.10 + AudioAmplitude * 20` 放大后，分别驱动 Sprite 高度和 Ribbon 宽度。该输出绑定不能删除，否则即使 Ak Data Interface 已经取得数据，Niagara 也不会产生可见响应。

System 只包含 Wwise 频谱 Emitter，不包含 UE Submix 对照 Emitter，也没有引用其他 System 的私有内嵌 Emitter。

该 System 会依赖工程中现有的 `NE_BasicWwiseVisualizer`、`WwiseAudioSpectrumUpdate` 和 `WwiseAudioSpectrum`。迁移到其他工程时，应通过 Unreal 的 `Migrate` 一并复制依赖，不要只复制单个 `.uasset` 文件。

## 6. 运行时预期

1. System 创建后但尚未收到快照时，频谱无效，效果保持初始状态。
2. 音频经过 `Music` Bus 后，收到 AudioBusHacker 快照并开始响应。
3. 音频停止或超过 `0.25` 秒没有新快照时，频谱清零。
4. 多个 System 实例可同时使用；销毁其中一个不会中断其他实例。

## 7. 没有效果时的检查顺序

1. 查看日志是否成功加载 `AudioBusHacker.dll` 的 Visualization API。
2. 确认 Wwise Event 正在播放，并且实际路由到 `Music` Bus。
3. 确认 `AudioBusHacker` Effect 安装在 `Music` Bus。
4. 确认已重新生成并加载最新 SoundBank。
5. 确认 `WwiseAudioSpectrumV2 -> Ak Wwise Spectrum -> Bus Name` 与 Wwise 完全一致。
6. 在 Niagara 中重新执行 `Compile`，确认没有 Data Interface 或 HLSL 编译错误。
7. 如果使用 GPU Simulation，确认 Fixed Bounds 足够大，避免效果被裁剪。

如果 `Is Audio Bus Visualization Available` 仍为 `false`，问题发生在 DLL 接口加载阶段，而不是 Niagara System 节点连接阶段，应优先检查 DLL 路径、构建配置和导出接口。

## 8. 资产位置

生成的资产文件：

```text
WwiseDemoGame/Content/WwiseAssets/NiagaraWwiseVisualizer/Examples
```

目录结构：仿照NiagaraAudioVisualizer

1. Blueprint：存放一些蓝图文件，目前存放的播放Wwise的BP_AkVisualize_Niagara.uasset
2. Emitter：存放一些Niagara的Emitter发射器，目前存放基类发射器NE_BasicWwiseVisualizer.uasset
3. Examples：复刻NiagaraAudioVisualizer的NiagaraSystem
4. Module：复刻NiagaraAudioVisualizer的ModuleScripts