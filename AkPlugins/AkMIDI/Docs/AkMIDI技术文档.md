# AkMIDI 技术文档

## 1. 文档范围

本文面向负责 AkMIDI 集成、维护、移植和问题定位的研发人员，说明插件在 Unreal Engine 与 Wwise 之间传递 MIDI 的实现方式。

本文以仓库中的 `Wwise2025.1.9` 实现为基准。该目录包含 `AkAudio`、`AkMIDI` 和 `AudiokineticTools` 三个受影响模块的源码快照，不是完整的 Wwise Unreal Integration。旧版目录的接口与行为可能不同，移植时必须以目标版本源码为准。

| 目录 | 实时 MIDI | `.mid` 文件解析/播放 | 交付形态 |
| --- | --- | --- | --- |
| `AkMIDI_Wwise2018.1.9` | 支持 | 不支持 | 增量源码 |
| `AkMIDI_Wwise2019.1.4` | 支持 | 不支持 | 增量源码 |
| `AkMIDI_Wwise2019.1.6` | 支持 | 不支持 | 增量源码 |
| `AkMIDI_Wwise2025.1.4` | 支持 | 不支持 | 增量源码 |
| `Wwise2025.1.4` | 支持 | 支持 | 三个模块的完整源码快照 |
| `Wwise2025.1.9` | 支持 | 支持 | 三个模块的完整源码快照 |

本文不涉及 Niagara。仓库工作说明中指定的 `AkPlugins/AkAudioSampler/Docs/AkWwiseNiagara粒子改造方案库.md` 当前不存在，因此没有复用其中的 Niagara 方案。

## 2. 目标与能力边界

AkMIDI 为官方 Wwise Unreal Integration 增加以下能力：

- 在蓝图或 C++ 中创建 MIDI 通道消息，并投递给 Wwise Event。
- 枚举、打开和关闭操作系统 MIDI 输入/输出端口。
- 将外部 MIDI 输入转换为 Wwise MIDI Post。
- 将 Unreal 生成的 MIDI 消息发送到外部 MIDI 输出设备。
- 通过蓝图事件插入 MIDI FX。
- 在 `Wwise2025.1.4` 和 `Wwise2025.1.9` 中解析并播放标准 MIDI 文件。
- 在编辑器中创建和编辑 `UAkMidiMessage` 资产。

插件当前不处理 SysEx、MIDI Clock 和其他需要独立时序语义的系统实时消息。外部设备到外部设备的直通路径在 `Wwise2025.1.9` 中不可用，文件播放也不是采样级精度。

## 3. 模块组成

### 3.1 `AkMIDI`

运行时模块，公开组件、消息、设备、函数库、FX 和文件播放器。

`AkMIDI.Build.cs` 的主要依赖为：

- Public：`Core`、`CoreUObject`、`Engine`、`AkAudio`。
- Private：`Slate`、`SlateCore`。
- Windows：定义 `__WINDOWS_MM__` 并链接 `winmm.lib`。
- macOS/iOS：定义 `__MACOSX_CORE__` 并链接 CoreMIDI、CoreAudio、CoreFoundation。
- Linux：定义 `__LINUX_ALSA__` 并链接 ALSA、pthread。

`FAkMidiModule` 只维护模块单例指针，不创建运行时服务。设备和路由生命周期由组件管理。

### 3.2 `AkAudio` 改造

AkMIDI 不是独立旁路插件，它直接扩展 `FAkAudioDevice`：

- 新增单播委托 `OnMessageWaitToSend`。
- 在 `AkGlobalCallbackLocation_PreProcessMessageQueueForRender` 注册全局回调。
- 回调读取 `AkAudioSettings`，再执行 `OnMessageWaitToSend`。
- 新增 `PostMidiEvent`，内部调用 `IWwiseSoundEngineAPI::PostMIDIOnEvent`。
- 新增 `StopMidiEvent`，内部调用 `IWwiseSoundEngineAPI::StopMIDIOnEvent`。

这也是 AkMIDI 与具体 Wwise Unreal Integration 版本强耦合的主要原因。升级 Wwise 时不能直接沿用旧版 `AkAudioDevice` 补丁。

### 3.3 `AudiokineticTools` 改造

编辑器模块增加：

- `UAkMidiMessageFactory`，用于新建 `UAkMidiMessage` 资产。
- `FAssetTypeActions_AkMidiMessage`，使用简单资产编辑器打开消息资产。
- 对 `AkMIDI` 模块的依赖和资产类型注册。

这部分只影响编辑器资产工作流，不参与运行时投递。

## 4. 核心类型

### 4.1 `UAkMidiComponent`

核心路由组件，继承 `UAkComponent`，因此同时是 Wwise Game Object。默认状态为：

```text
InputSource = Unreal
OutputTarget = Wwise
MIDI FX = Bypass
```

主要职责：

- 管理输入源和输出目标。
- 持有 `UAkMidiDevice`。
- 把 `UAkMidiMessage` 转换为 `AkMIDIPost`。
- 调用 `FAkAudioDevice` 向 Wwise 投递或停止 MIDI。
- 接收 RtMidi 回调并通过无锁 MPSC 队列转移数据。
- 在 Wwise 音频帧回调中消费外部输入队列。
- 广播 `OnMessageReceived`。

### 4.2 `UAkMidiMessage`

表示一条 MIDI 通道消息：

| 字段 | 类型 | 语义 |
| --- | --- | --- |
| `NoteType` | `EAkMessageType` | 状态高半字节，对应 `0x8-0xE` |
| `Channel` | `uint8` | 零基通道，标准有效范围 `0-15` |
| `NoteOffset` | `int32` | 相对当前 Wwise 音频帧的采样偏移 |
| `Data01` | `uint8` | 第一个数据字节 |
| `Data02` | `uint8` | 第二个数据字节 |

默认值为 Note On、Channel `0`、Offset `0`、Note `60`、Velocity `72`。

消息对象还保存一份备份，用于 MIDI FX 临时修改后恢复原值。运行时创建的新对象不一定已经建立备份对象，因此调用方不应依赖所有临时对象都能自动恢复。

### 4.3 `UAkMidiDevice`

对 `RtMidiIn` 和 `RtMidiOut` 的轻量封装，负责：

- 枚举输入/输出端口。
- 打开和关闭端口。
- 暴露 RtMidi 对象供组件设置回调或发送原始字节。

`GetMidiDevice` 只在首次调用时创建 RtMidi 对象。物理输入和输出端口分别编号。

### 4.4 蓝图函数库

`UAkMidiFunctionLibrary` 提供消息创建、修改、组件打开/关闭和投递的包装。需要注意：

- `StopMidiEvent` 包装只要组件有效就返回 `true`，不会返回底层停止结果。
- `OpenMidiDevice(IO_Both, Port)` 使用同一个端口号打开输入和输出；两侧端口不一致时应分别调用组件接口。
- `ModifyAkMidiMessage` 的值参数为 `uint8`，通过它设置 `NoteOffset` 最多只能表达 `255`。

`UAkMidiFxLibrary` 提供：

- `Octave`：仅修改 Note On，范围参数被限制到 `-12..12` 个八度，但结果音高没有限制到 `0..127`。
- `VelocityCompression`：把 Note On/Off 力度限制到指定闭区间。

### 4.5 MIDI 文件类型

`FAkMidiFileData` 保存格式、轨道数、PPQ、总时长和排序后的事件。`FAkMidiTimedEvent` 同时保存 Tick、换算后的秒时间、通道消息字段和来源轨道。

`UAkMidiFilePlayerComponent` 是基于 Game Thread Tick 的播放器，提供加载、播放、暂停、恢复、停止、跳转、倍速和状态查询。

## 5. 实时数据流

### 5.1 Unreal 到 Wwise

```text
Blueprint/C++
  -> UAkMidiMessage[]
  -> UAkMidiComponent::PostMidiEvent
  -> MIDI FX（可选）
  -> UAkMidiComponent::MakePost
  -> AkMIDIPost[]
  -> FAkAudioDevice::PostMidiEvent
  -> IWwiseSoundEngineAPI::PostMIDIOnEvent
```

前置条件：

- `InputSource` 必须为 `Unreal`。
- 消息数组不能为空。
- 输出到 Wwise 时，参数 Event 或组件的 `AkAudioEvent` 必须有效。
- Event 的 Short ID 必须有效，SoundEngine 必须已初始化。

`PostMidiEvent` 每次调用都会直接调用一次 `PostMIDIOnEvent`。返回的 `AkPlayingID` 只被转换成 `bool`，没有向调用方暴露，也没有用于后续批次关联。

### 5.2 外部设备到 Wwise

```text
OS MIDI driver
  -> RtMidi callback thread
  -> MyCallback（只复制原始字节）
  -> TQueue<FRawMidiPacket, Mpsc>
  -> Wwise PreProcessMessageQueueForRender callback
  -> UAkMidiComponent::ProcessIncomingMidiQueue
  -> 通道消息解析 / MIDI FX / OnMessageReceived
  -> AkMIDIPost[]
  -> PostMIDIOnEvent
```

RtMidi 回调不直接操作 Wwise，而是复制数据到 `IncomingMidiQueue`。队列在 Wwise 全局回调触发时消费，以便把 MIDI Post 对齐到 Wwise 的音频处理周期。

`GetMidiDevice` 同时完成三件事：创建 RtMidi 对象、枚举端口、绑定 Wwise 单播委托和 RtMidi 输入回调。因此外部输入流程必须先调用它。

当前实现把 `OnMessageReceived` 的 `DeltaTime` 固定广播为 `0`；`FRawMidiPacket` 虽保存了 RtMidi DeltaTime，但同步消费路径没有向蓝图透传。

### 5.3 Unreal 到外部设备

```text
UAkMidiMessage[]
  -> MIDI FX（可选）
  -> status = (type << 4) | channel
  -> 2 或 3 字节 MIDI 消息
  -> RtMidiOut::sendMessage
```

Program Change 和 Channel Aftertouch 发送 2 字节，其余支持的通道消息发送 3 字节。该路径不使用 `NoteOffset`，也不需要 Wwise Event。

调用方必须先通过 `GetMidiDevice` 创建输出对象并打开物理输出端口。当前实现没有对 `RtMidiOut` 空指针、端口打开状态或异常做完整保护。

### 5.4 外部设备到外部设备

代码中保留了原始消息直通分支，但 `HandleWwiseCallback` 在输出目标不是 Wwise 时会提前返回，导致 `IncomingMidiQueue` 无消费者。因此 `Wwise2025.1.9` 不能把该路径视为可用能力。

## 6. MIDI 消息转换

| `EAkMessageType` | 状态 | Wwise 类型 | `Data01` | `Data02` |
| --- | ---: | --- | --- | --- |
| `AMT_Note_Off` | `0x8n` | Note Off | Note | Release Velocity |
| `AMT_Note_On` | `0x9n` | Note On | Note | Velocity |
| `AMT_AfterTouch` | `0xAn` | Note Aftertouch | Note | Pressure |
| `AMT_CC` | `0xBn` | Controller | CC Number | Value |
| `AMT_Program_Change` | `0xCn` | Program Change | Program | 未使用 |
| `AMT_Channel_AfterTouch` | `0xDn` | Channel Aftertouch | Pressure | 未使用 |
| `AMT_Pitch_Bend` | `0xEn` | Pitch Bend | LSB | MSB |

Pitch Bend 14 位值为 `(Data02 << 7) | Data01`，中心值 `8192` 对应 `Data01=0`、`Data02=64`。

接口层没有统一校验标准 MIDI 范围。调用方必须保证 Channel 为 `0-15`，数据字节为 `0-127`，并避免负的 `NoteOffset`。

## 7. 设备模型与保留端口

组件在物理设备列表前插入两个虚拟端点：

| 列表 | 名称 | Port | 含义 |
| --- | --- | ---: | --- |
| Input | `Unreal` | `127` | 消息由 Unreal 创建 |
| Output | `Wwise` | `127` | 消息投递给 Wwise |

物理端口通常从 `0` 开始，所以数组下标不等于 `FMidiDevice.Port`。例如数组第 `1` 项可能代表物理端口 `0`。UI 必须保存并传递结构体中的 `Port` 字段。

输入和输出使用相同的保留值 `127`，但其含义由调用的 Open 接口决定。由于端口类型为 `uint8`，实现也隐含限制了可寻址设备数量。

## 8. 回调、线程与对象生命周期

### 8.1 回调所有权

`FAkAudioDevice::OnMessageWaitToSend` 是单播委托。每次 `UAkMidiComponent::GetMidiDevice` 都会重新绑定，因此最后调用该函数的组件获得外部输入处理权。多个组件不能可靠地同时消费外部 MIDI 输入。

### 8.2 队列与处理时机

RtMidi 回调线程只复制字节并入队。解析、UObject 修改、FX 调用和蓝图广播发生在 Wwise 全局回调触发的消费路径中。

Wwise 全局回调不保证运行在 Unreal Game Thread。当前实现在该上下文中修改 `UAkMidiMessage`、调用蓝图原生事件并广播动态委托，存在 UObject/蓝图线程安全风险。后续改造应把需要访问 UObject 或蓝图的工作再次转发到 Game Thread，音频回调中只保留有界、无阻塞的数据转换与投递。

### 8.3 池化

组件构造时创建 `30` 个 `UAkMidiMessage` 和 `30` 个 `AkMIDIPost`。计数在达到 `MessagePoolMax - 2` 时回绕，实际循环使用前 `28` 个槽位。`Posts` 在每次向 Wwise 投递后清空。

池对象通过 Root 集合保持存活；任何生命周期改造都应同时审查 AddToRoot/RemoveFromRoot、组件析构和热重载行为。

## 9. 标准 MIDI 文件解析

`FAkMidiFileParser` 直接读取二进制 Standard MIDI File，不依赖第三方解析库。

### 9.1 支持范围

- 最大文件大小：`16 MiB`。
- SMF Format 0 和 Format 1。
- PPQ 时间分辨率。
- 多轨事件合并。
- Running Status。
- 可变长数量 VLQ，最多 4 字节。
- Tempo Meta Event `FF 51`。
- Note On/Off、Aftertouch、CC、Program Change、Channel Aftertouch、Pitch Bend。
- Note On 且 Velocity 为 `0` 时规范化为 Note Off。

### 9.2 不支持或忽略

- SMF Format 2。
- SMPTE Time Division。
- SysEx 内容：解析时跳过，不进入输出事件。
- Meta Event 中除 Tempo 外的内容：跳过。
- 系统消息只按已知长度跳过，不进入输出事件。

### 9.3 时间换算

解析器先收集全部轨道的 Tempo Event，按 Tick 和发现顺序排序，再把每条通道事件从 Tick 换算为秒：

```text
seconds += deltaTicks * microsecondsPerQuarter / 1,000,000 / PPQ
```

默认 Tempo 为 `500000 us/quarter`，即 120 BPM。总时长取最后一条保留通道事件的时间，不包含 End-of-Track 后的静默尾部。

## 10. MIDI 文件播放器

### 10.1 状态机

```text
Unloaded -> Loaded -> Playing -> Paused
                    -> Stopped
                    -> Finished
```

- `LoadMidiFile` 会先停止当前播放并发送活跃音符的 Note Off。
- `Play` 从 Loaded/Paused 位置开始；Stopped、Finished 或到达末尾时从头开始。
- `Pause` 发送所有活跃音符的 Note Off。
- `Resume` 扫描目标时间之前的事件，尝试恢复当时仍应发声的音符。
- `SeekSeconds` 先关闭当前活跃音符，再跳转并尝试恢复目标位置的活跃音符。
- `Stop` 默认关闭所有活跃音符并把时间归零。
- `EndPlay` 调用 `Stop(true)`，降低 Actor 销毁时残留长音的风险。

### 10.2 调度

播放器在 `TickComponent` 中执行：

```text
PlaybackTime += DeltaTime * PlaybackRate
Dispatch all events with Event.TimeSeconds <= PlaybackTime
Limit dispatch count to MaxEventsPerTick
```

默认 `PlaybackRate=1.0`，编辑器元数据范围为 `0.1-4.0`；默认 `MaxEventsPerTick=256`，元数据范围为 `1-1024`。底层仍会对 PlaybackRate 使用不小于 `0` 的值。

文件事件转换为 `UAkMidiMessage` 时 `NoteOffset` 固定为 `0`。因此调度精度受 Game Thread 帧率和单帧事件上限影响，不是 sample-accurate。事件密集时，多余事件会延后到后续 Tick。

### 10.3 自动配置

`bAutoConfigureMidiComponent=true` 时，`Play` 会把输入设为 Unreal、输出设为 Wwise，均使用保留端口 `127`。关闭该选项后，调用方必须预先配置组件路由。

### 10.4 当前缺失能力

- 无循环播放和循环区间。
- 无播放完成委托。
- 无轨道 Mute/Solo、Channel Remap、Transpose 或 Velocity Scale。
- 无 `.mid` 资产导入，只接收文件路径。
- 无 Look-ahead 或基于 Wwise Sample Offset 的精确调度。

## 11. 安装与移植要求

1. 目标工程必须已安装与目录版本完全一致的官方 Wwise Unreal Integration。
2. 把所选目录中的 `Source` 合并到实际使用的 Wwise 插件 `Source`，不能用三模块快照替换整个插件。
3. 在 `Wwise.uplugin` 中声明 Runtime 模块 `AkMIDI`。
4. 使用 AkMIDI C++ 类型的游戏模块应依赖 `AkAudio` 和 `AkMIDI`。
5. 重新生成项目文件并编译 Editor 和所有目标平台。
6. 升级 Wwise 时重新移植 `FAkAudioDevice` 和 `AudiokineticTools` 改动，并复核 Wwise API 签名。

建议把 AkMIDI 与 Wwise Integration 版本作为一个不可拆分的交付单元管理。

## 12. 关键 API 契约

| API | 成功条件 | 重要副作用/限制 |
| --- | --- | --- |
| `GetMidiDevice` | 组件和 RtMidi 可创建 | 重绑全局单播委托和输入回调 |
| `OpenMidiInputDevice` | 端口有效 | `127` 只切换为 Unreal，不打开硬件 |
| `OpenMidiOutputDevice` | 端口有效 | `127` 只切换为 Wwise，不打开硬件 |
| `PostMidiEvent` | 输入为 Unreal、数组非空、目标有效 | Wwise 路径清空本批 `Posts` |
| `StopMidiEvent` | Event 和 SoundEngine 有效 | 以 Event ID + Game Object ID 停止 |
| `LoadMidiFile` | 路径存在、文件 <=16 MiB、格式可解析 | 先停止当前播放 |
| `Play` | 已加载至少一条事件且组件有效 | 默认强制路由到 Wwise |
| `SeekSeconds` | 已加载事件 | 发送 Note Off 并重建活跃音符 |

## 13. 已知风险

- **版本耦合**：直接修改官方 `AkAudio` 和 `AudiokineticTools`，Wwise 升级可能造成编译或运行时回归。
- **多组件冲突**：外部输入回调最终只由最后绑定的组件处理。
- **范围校验不足**：非法 Channel、Data 或移调结果可能产生无效 MIDI 或 `uint8` 回绕。
- **设备异常处理不足**：RtMidi 打开/发送异常未完整转换成 Unreal 可观察错误。
- **对象生命周期**：Root 池化对象和设备对象需要专门验证销毁、PIE 重启和热重载。
- **文件播放抖动**：基于 Tick 的调度会受帧率、卡顿和 `MaxEventsPerTick` 限制。
- **活跃音符恢复**：`RecoverActiveNotesAtTime` 的反向布尔扫描不能可靠区分已经遇到的 Note Off；同一通道、同一音高重复触发时，Pause/Resume 或 Seek 可能错误恢复已结束音符。`SeekSeconds` 即使不处于 Playing 状态也会立即尝试发送恢复用 Note On。
- **回调线程安全**：Wwise 全局回调中存在 UObject 修改和蓝图广播，应在目标平台用线程检测和压力测试验证，并优先改为 Game Thread 通知。
- **临时设置指针**：组件把全局回调传入的局部 `AkAudioSettings` 地址保存在成员指针中；虽然当前有效逻辑未读取它，但不能在后续代码中直接复用该指针。
- **返回值信息损失**：部分函数库包装不保留底层错误，`AkPlayingID` 也未暴露。
- **日志成本**：Wwise 投递路径会逐条输出 MIDI 日志，高密度播放时可能造成额外开销。

## 14. 测试建议

### 14.1 实时路由

- 对七类支持消息逐项验证 Wwise 字段映射。
- 验证 Note On Velocity `0`、16 个通道和边界数据值。
- 验证 Unreal -> Wwise、Unreal -> 外部设备、外部设备 -> Wwise。
- 验证设备插拔、端口被占用、PIE 多次启动和 Actor 销毁。
- 验证多个组件竞争外部输入时的实际所有权。

### 14.2 MIDI 文件

- Format 0/1、单轨/多轨、Running Status、Tempo Change。
- 非法 Header、截断 Chunk、非法 VLQ、Format 2、SMPTE Division 和大于 16 MiB 文件。
- Pause/Resume/Seek/Stop 后无残留音。
- 低帧率、单 Tick 超过 `MaxEventsPerTick` 和 PlaybackRate 边界。
- 文件末尾持续音、空轨道、只有 Meta Event 的文件。

### 14.3 平台

- Windows 验证 WinMM 输入/输出。
- macOS/iOS 验证 CoreMIDI 权限和设备生命周期。
- Linux 验证 ALSA 设备枚举和链接。
- Android 等未配置后端的平台应在构建阶段明确禁用或补充实现。

## 15. 推荐扩展方向

优先级建议如下：

1. 把外部输入消费从单播委托改造成可注册/注销的多实例机制。
2. 增加统一参数校验、RtMidi 异常转换和蓝图错误信息。
3. 暴露 `AkPlayingID` 与明确的停止结果。
4. 使用 Wwise Sample Offset 或 Look-ahead 队列提升文件播放精度。
5. 增加完成事件、循环、轨道控制和可 Cook 的 MIDI 资产。
6. 修正对象池 Root 生命周期并补充自动化测试。
