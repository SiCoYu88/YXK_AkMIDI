# AkWwise Niagara 粒子改造方案库

## 1. 文档目的

本文记录 UE 5.7 中三类 Niagara 音频可视化系统接入 AkAudioSampler/Wwise
频谱数据的完整解决思路。后续遇到相似问题时，必须先按资产名、模块名、症状和
Data Interface 类型检索本文，再决定复用已有方案或新增方案。

本文覆盖以下已验证资产：

1. `/Game/WwiseAssets/AkWwiseSpectrum/NS_WwiseMusicSpectrum_AkDrivenV3`
2. `/Game/WwiseAssets/AkWwiseSpectrum/NS_PileTheRings`
3. `/Game/WwiseAssets/AkWwiseSpectrum/NS_MeshLines`

适用基础环境：

- Unreal Engine 5.7；
- Wwise Unreal Integration；
- AkAudioSampler；
- 目标 Wwise Bus 已插入 AudioBusHacker；
- 示例 Bus 名称为 `AkBus`。

## 2. 先检索、再修改

### 2.1 推荐检索词

优先使用以下关键词搜索本文：

```text
无粒子 音频不变化 AudioAmplitude ScaleSpriteSize
DynamicMaterialParameters Max_Float PileTheRings
AudioSpectrumUpdate MeshLines ParticleRead RibbonWidth
NiagaraDataInterfaceAkWwiseSpectrum AkWwiseAudioSpectrum
模块顺序 NCS_UpToDate AkBus
```

命令行示例：

```powershell
rg -n "关键词" AkPlugins/AkAudioSampler/Docs
```

如果 `rg` 不可用：

```powershell
Get-ChildItem AkPlugins/AkAudioSampler/Docs -File |
    Select-String -Pattern "关键词"
```

### 2.2 类型选择表

| 资产结构或症状 | 应选方案 | 核心原则 |
| --- | --- | --- |
| 可以从模板重建；效果主要由 Sprite Size/Ribbon Width 表达 | 类型一 | 明确消费 `AudioAmplitude`，并修复模块顺序 |
| 已有 Wwise 模块，但材质参数或动态输入仍未使用其输出 | 类型二 | 保留系统结构，重绑幅值输入，禁用冲突的旧音频模块 |
| 原 `AudioSpectrumUpdate` 同时负责位置、宽度、插值，且其他发射器通过 Particle Reader 读取结果 | 类型三 | 不替换算法，只替换 `Audio Spectrum` 数据源 |

不能只根据渲染器是 Sprite、Ribbon 或 Mesh 判断类型。必须检查 Particle Update
调用链、输出参数的消费者以及跨发射器依赖。

## 3. 通用数据链

```text
Wwise Bus
  -> AudioBusHacker
  -> FAkAudioBusHackerVisualizationData
  -> UAkAudioSampler 最新快照缓存
  -> Niagara AkWwise Data Interface
  -> Niagara 模块输出/原 AudioSpectrumUpdate
  -> Particle 属性或材质参数
  -> Renderer
```

两种 Data Interface 的用途不同：

| 类型 | 用途 | Simulation Target |
| --- | --- | --- |
| `UNiagaraDataInterfaceAkWwiseSpectrum` | 新建 AkWwise Niagara 模块，直接调用 `AudioSpectrum`、`GetNumChannels`、`IsSpectrumValid` | CPU/GPU |
| `UNiagaraDataInterfaceAkWwiseAudioSpectrum` | 兼容要求 UE `Audio Spectrum` 类型的既有模块 | CPU |

`UNiagaraDataInterfaceAkWwiseAudioSpectrum` 是兼容适配器。它用于保留 UE 原有
`AudioSpectrumUpdate` 算法，不应替代常规的新建 AkWwise Niagara 模块。

## 4. 类型一：直接幅值驱动的 Sprite/Ribbon

### 4.1 参考资产

```text
NS_WwiseMusicSpectrum_AkDrivenV3
```

### 4.2 适用特征

- 可以从已有 Emitter 模板重建系统；
- 每个粒子对应一个频谱采样位置；
- AkWwise 模块能够输出 `Output.WwiseAudioSpectrumV2.AudioAmplitude`；
- 最终外观主要由 Sprite Size 或 Ribbon Width 表达。

### 4.3 根因记录

已经拿到可视化数据但粒子不随音频变化，通常不是回调问题，而是以下原因：

1. `AudioAmplitude` 只被计算，没有被下游模块消费；
2. `ScaleSpriteSize` 位于 `WwiseAudioSpectrumV2` 之前，读到的是未写入值；
3. 初始 Sprite Size 为零或过小，后续缩放仍不可见；
4. 静音时幅值为零，粒子完全消失，难以区分“无数据”和“低电平”；
5. 模板中的旧 `AudioSpectrumUpdate` 与新模块同时写同一属性。

### 4.4 最终方案

使用 `Tools/rebuild_ak_wwise_spectrum_v3.py` 重建系统：

1. 从 `NE_BasicAudioVisualizer` 模板创建 Emitter；
2. 移除模板中的旧 Wwise 模块、旧 `AudioSpectrumUpdate`、`ScaleSpriteSize` 和
   `ScaleRibbonWidth`；
3. 添加 `WwiseAudioSpectrumV2`；
4. 将 `ID` 绑定到 `Engine.ExecIndex`，`ParticlesNum=256`，`Intensity=1.0`；
5. 将 AkWwise DI 配置为 `BusName=AkBus`；
6. 让 Sprite Y 尺寸与 Ribbon Width 消费
   `Output.WwiseAudioSpectrumV2.AudioAmplitude`；
7. 使用 `Amplitude * 20.0 + 0.10` 保留静音基线；
8. Sprite 使用 Non-Uniform Scale，X 为 `0.35`，Y 为处理后的幅值；
9. 设置 `ScaleSpriteSize.Initial Sprite Size=(50, 50)`；
10. 强制 Particle Update 顺序为：

```text
WwiseAudioSpectrumV2
  -> ScaleSpriteSize
  -> ScaleRibbonWidth
```

### 4.5 使用的编辑器辅助接口

- `ReorderParticleUpdateModules`
- `SetParticleUpdateModuleVector2Input`

### 4.6 验证重点

- `ScaleSpriteSize` 与 `ScaleRibbonWidth` 必须位于 AkWwise 输出之后；
- `AudioAmplitude` 必须出现在动态输入链接中；
- 静音时仍应看到最小尺寸粒子；
- Particle Update Script 为 `NCS_UpToDate`；
- PIE 中播放经过 `AkBus` 的音频后，尺寸随频谱变化。

## 5. 类型二：已有外观逻辑，只需重绑幅值

### 5.1 参考资产

```text
NS_PileTheRings
```

### 5.2 适用特征

- 系统已有 `WwiseAudioSpectrumV2`；
- Renderer、粒子生成和材质效果已经正确；
- 动态材质参数或动态输入仍连接旧值；
- 不需要重建整个 Niagara System。

### 5.3 根因记录

`WwiseAudioSpectrumV2` 即使正确输出 `AudioAmplitude`，如果
`DynamicMaterialParameters` 内的 `Max_Float.A` 没有绑定该输出，材质仍会使用
固定值或旧链路。与此同时，启用的 `AudioSpectrumUpdate` 可能继续消费 UE 音频 DI，
形成两套音频驱动逻辑并互相覆盖。

### 5.4 最终方案

使用 `Tools/convert_ns_pile_the_rings_to_ak_wwise.py` 原位修改：

1. 调整 Particle Update 顺序：

```text
ParticleState
  -> WwiseAudioSpectrumV2
  -> DynamicMaterialParameters
```

2. 将 `Max_Float.A` 链接到：

```text
Output.WwiseAudioSpectrumV2.AudioAmplitude
```

3. 禁用旧 `AudioSpectrumUpdate`，避免继续读取 UE Audio Spectrum；
4. 保存现有系统，不改 Renderer 和其余外观逻辑。

### 5.5 使用的编辑器辅助接口

- `ReorderParticleUpdateModules`
- `SetParticleUpdateFunctionFloatInputLinkedParameter`
- `SetParticleUpdateModuleEnabled`

### 5.6 验证重点

- `Max_Float.A` 的 linked parameter 必须是 AkWwise 输出；
- `AudioSpectrumUpdate` 必须为 Disabled；
- `DynamicMaterialParameters` 必须位于 AkWwise 模块之后；
- 编译状态为 `NCS_UpToDate`；
- 原 Sprite/Ribbon Renderer 和材质参数数量不变。

## 6. 类型三：保留复杂 AudioSpectrumUpdate 算法

### 6.1 参考资产

```text
NS_MeshLines
```

### 6.2 适用特征

- 系统包含两个或更多 Emitter；
- 主 Emitter 的 `AudioSpectrumUpdate` 写入 `Particles.Position`、
  `Particles.RibbonWidth` 和时间插值状态；
- 次级 Emitter 使用 `NiagaraDataInterfaceParticleRead` 读取主 Emitter；
- 禁用 `AudioSpectrumUpdate` 会直接破坏 Mesh Lines 形变和下游数据。

### 6.3 根因记录

该类型不能套用类型二。`AudioSpectrumUpdate` 不只是“取音频幅值”，还是完整的几何
变形算法。禁用它并添加一个只输出 `AudioAmplitude` 的模块，会导致以下逻辑丢失：

- Position 形变；
- Ribbon Width 计算；
- Threshold/Equalizer；
- 帧间插值；
- 次级 Emitter 的 Particle Reader 输入。

项目中的
`/Game/WwiseAssets/AudioVisualizeV3/Modules/WwiseAudioSpectrumUpdate`
也不能直接使用。检查确认它仍引用 UE 的
`NiagaraDataInterfaceAudioSpectrum` 和
`NiagaraDataInterfaceAudioOscilloscope`，并非真正的 AkWwise 数据源。

### 6.4 最终方案

保留整个 `AudioSpectrumUpdate`，只将其 `Emitter.Audio Spectrum` 输入对象替换为
`UNiagaraDataInterfaceAkWwiseAudioSpectrum`。

使用 `Tools/convert_ns_mesh_lines_to_ak_wwise.py`，参数为：

| 参数 | 值 |
| --- | ---: |
| `BusName` | `AkBus` |
| `Resolution` | 512 |
| `MinimumFrequency` | 55 Hz |
| `MaximumFrequency` | 3000 Hz |
| `NoiseFloorDb` | -27 dBFS |
| `AutoRegisterVisualizationCallback` | true |
| `StaleDataTimeoutSeconds` | 0.25 s |

其中 512 和 55 Hz 是 UE Audio Spectrum 默认值；3000 Hz 和 -27 dBFS 来自原资产，
用于保持视觉响应接近改造前。

### 6.5 CPU 兼容适配器

`UNiagaraDataInterfaceAkWwiseAudioSpectrum` 继承
`UNiagaraDataInterfaceAudioSpectrum`，因此可以放入原模块要求的 Audio Spectrum
输入，但 VM 的 `AudioSpectrum()` 实际从 AkAudioSampler 快照读取数据。

该适配器只允许 `CPUSim`。`NS_MeshLines` 两个 Emitter 的编译定义均确认包含
`CPUSim`。

不要让通用的 `UNiagaraDataInterfaceAkWwiseSpectrum` 直接继承 UE Audio Spectrum，
然后在派生构造器中替换父类 Proxy。实践中这会触发以下问题：

- 父 Spectrum Proxy 必须在渲染线程析构；
- AudioSubmix 的 `PostLoad`、`BeginDestroy` 和 `CopyToInternal` 会把 Ak Proxy
  错误转换为 UE Submix Proxy；
- 提前删除父 Proxy 会在 Niagara 渲染命令中产生访问冲突。

最终结构采用两个职责独立的 DI，避免上述生命周期冲突。

### 6.6 使用的编辑器辅助接口

- `ReplaceParticleUpdateAudioSpectrumWithAkWwise`

该接口执行以下检查：

1. 只处理 Particle Update Stack 中存在 `AudioSpectrumUpdate` 的 Emitter；
2. 查找名称以 `Audio Spectrum` 结尾的 Input Node；
3. 确认原对象属于 UE Audio Spectrum；
4. 替换为 AkWwise CPU 兼容适配器；
5. 设置 Bus、频率范围、分辨率和 Noise Floor；
6. 请求 Niagara 重新编译。

### 6.7 验证重点

主 Emitter 必须满足：

```text
DataInterfaces 包含 NiagaraDataInterfaceAkWwiseAudioSpectrum
CalledVMExternalFunctions 包含 AudioSpectrum
OwnerName 保持 Emitter.Audio Spectrum
StatScopes 包含 ParticleState_Emitter
StatScopes 包含 AudioSpectrumUpdate_Emitter
LastCompileStatus=NCS_UpToDate
```

次级 Emitter 必须满足：

```text
保留 NiagaraDataInterfaceParticleRead
StatScopes 包含 Attribute_Update_Emitter
LastCompileStatus=NCS_UpToDate
```

## 7. 通用故障排查

### 7.1 已拿到数据，但 Niagara 没有变化

按顺序检查：

1. AkWwise 模块输出是否被任何 Particle 属性、材质参数或 Renderer 输入消费；
2. 消费模块是否位于生产 `AudioAmplitude` 的模块之后；
3. 旧 `AudioSpectrumUpdate` 是否覆盖同一属性；
4. 初始 Sprite Size、Ribbon Width 或 Mesh Scale 是否为零；
5. 静音幅值是否导致粒子完全不可见；
6. Renderer Binding 是否绑定到实际写入的属性；
7. Fixed Bounds、剔除、材质透明度和 Actor 可见性是否正确。

### 7.2 编辑器中 `IsVisualizationAvailable()` 为 false

检查：

- Wwise SoundEngine 是否已经初始化；
- 当前构建配置对应目录中是否存在 `AudioBusHacker.dll`；
- 日志是否包含 `Loaded AudioBusHacker visualization API`；
- AudioBusHacker 效果器是否插入目标 Bus；
- 实际音频是否路由经过该 Bus。

### 7.3 `GetAudioBusVisualization` 返回 false

重点检查 Bus 名称、回调注册时机、SoundEngine 状态和是否已经收到至少一个快照。
编辑器仅打开 Niagara 资产不会自动产生频谱数据。

### 7.4 编译成功但运行时仍无效果

命令行的 `-nullrhi -nosound` 只能验证资产加载、VM 绑定和编译结构，不能验证真实
Wwise 音频数据或屏幕表现。最终必须在 Editor/PIE 中播放经过目标 Bus 的音频目视验证。

## 8. 安全修改流程

1. 确认 `UnrealEditor` 和 `UnrealEditor-Cmd` 均未运行；
2. 记录目标 `.uasset` 的长度、修改时间和 SHA-256；
3. 将原资产备份到 `Tools/Backups` 并核对哈希一致；
4. 检查 Emitter 数量、Simulation Target、Particle Update 模块顺序、DI 和 Renderer；
5. 按第 2.2 节选择最小改造方案；
6. 先编译 AkAudioSampler，再运行转换脚本；
7. 使用检查脚本重新加载资产；
8. 确认所有相关 Script 为 `NCS_UpToDate`；
9. 检查实际 DI、VM External Function、Stat Scope 和跨 Emitter 依赖；
10. 在 Editor/PIE 中做真实音频目视验证。

禁止在用户编辑器运行时通过命令行覆盖同一资产，也不要主动终止用户的编辑器进程。

## 9. 脚本与源码索引

### 转换脚本

| 用途 | 文件 |
| --- | --- |
| 重建直接幅值驱动系统 | `Tools/rebuild_ak_wwise_spectrum_v3.py` |
| 转换 PileTheRings | `Tools/convert_ns_pile_the_rings_to_ak_wwise.py` |
| 转换 MeshLines | `Tools/convert_ns_mesh_lines_to_ak_wwise.py` |

### 检查脚本

| 用途 | 文件 |
| --- | --- |
| 检查 V3 | `Tools/inspect_ak_wwise_spectrum_v3.py` |
| 检查 PileTheRings | `Tools/inspect_ns_pile_the_rings.py` |
| 检查 MeshLines | `Tools/inspect_ns_mesh_lines.py` |

### 插件源码

| 用途 | 文件 |
| --- | --- |
| AkWwise DI 与 CPU 兼容适配器 | `AkPlugins/AkAudioSampler/Public/NiagaraDataInterfaceAkWwiseSpectrum.h` |
| DI 数据采样和 VM 实现 | `AkPlugins/AkAudioSampler/Private/NiagaraDataInterfaceAkWwiseSpectrum.cpp` |
| 编辑器辅助接口声明 | `AkPlugins/AkAudioSampler/Public/AkAudioSamplerNiagaraEditorLibrary.h` |
| Niagara Stack 修改实现 | `AkPlugins/AkAudioSampler/Private/AkAudioSamplerNiagaraEditorLibrary.cpp` |

## 10. 新方案回写规则

后续问题如果不能匹配上述三种类型，在实现并验证新方案后，应在本文追加一节，至少记录：

1. 参考资产和 Niagara 结构；
2. 用户可见症状；
3. 根因和排除过程；
4. 最终数据链和模块顺序；
5. 使用的 DI、参数和编辑器辅助接口；
6. 失败方案及不能复用的原因；
7. 自动检查结果；
8. Editor/PIE 目视验证结果。

不要只记录“修改成功”。方案库的目标是让后续问题能够通过结构和症状快速归类。
