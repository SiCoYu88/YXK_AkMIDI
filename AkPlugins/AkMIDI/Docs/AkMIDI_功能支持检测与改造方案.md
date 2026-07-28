# AkMIDI 功能支持检测与改造方案

## 1. 检测范围

- 工程：`WwiseDemoGame`
- Wwise 插件版本：`Plugins/Wwise/Wwise.uplugin` 显示 `2025.1.4.9062.4131`
- 核心检测目录：
  - `Plugins/Wwise/Source/AkMIDI`
  - `Plugins/Wwise/Source/AkAudio`
  - `Plugins/Wwise/Source/AudiokineticTools`
  - `Plugins/WwiseSoundEngine/ThirdParty/include/AK/SoundEngine/Common`

## 2. 总体结论

| 功能 | 当前支持状态 | 结论 |
|---|---:|---|
| 1. 播放第三方 `.mid` 文件 | 不支持 / 需 Wwise Authoring 离线导入 | 插件没有 `.mid` 文件读取、解析、调度接口。若 `.mid` 先导入 Wwise 并生成 SoundBank，可按普通 Wwise Event 播放。 |
| 2. 运行时播放外部 `.mid` 文件 | 不支持 | 缺少 Standard MIDI File parser、tempo/tick 调度、长时序投递和 PlayingID 管理。 |
| 3. UE 内部输入，序列化并输出 `.mid` 文件 | 不支持 | 只能创建单条 `UAkMidiMessage`，没有 MIDI 录制缓冲、SMF Track 写入、VLQ、Tempo、Header/Track 导出。 |
| 4. 外部 MIDI 键盘 / 乐器触发 Wwise 播放 | 部分支持 | 已有 RtMidi 输入、设备枚举、回调转 `UAkMidiMessage`、投递到 Wwise `PostMIDIOnEvent` 的链路，但需要修复模块启用、线程/队列、消息边界等问题。 |
| 5. Unreal 发送 MIDI 给外部乐器 / DAW / VST | 部分支持 | 已有 RtMidiOut 打开和发送链路，但现有实现存在发送数据 bug，且缺少虚拟端口/设备状态/错误处理。 |

## 3. 当前插件能力检测

### 3.1 模块启用状态

#### 结论

`AkMIDI` 源码和 `AkMIDI.Build.cs` 存在，但没有在 `Plugins/Wwise/Wwise.uplugin` 的 `Modules` 列表中声明。

#### 证据

- `Plugins/Wwise/Wwise.uplugin`：`Modules` 列表包含 `Wwise`、`AkAudio`、`AudiokineticTools` 等，但未包含 `AkMIDI`。
- `WwiseDemoGame.uproject`：项目只启用 `Wwise`、`WwiseNiagara`，`AdditionalDependencies` 只有 `Engine`、`Wwise`、`WwiseNiagara`。
- `Plugins/Wwise/Source/AkMIDI/AkMIDI.Build.cs` 存在，且配置了平台 RtMidi 后端：
  - Win64：`__WINDOWS_MM__=1` + `winmm.lib`
  - Mac/iOS：`CoreMIDI` / `CoreAudio` / `CoreFoundation`
  - Linux：`asound` / `pthread`
- `Plugins/Wwise/Source/AudiokineticTools/AudiokineticTools.Build.cs` 中依赖了 `AkMIDI`。

#### 风险

当前属于“源码存在但插件描述未声明”的不一致状态。编辑器环境可能因历史中间产物能编译，但正式构建、CI、打包或新机器拉取工程后可能找不到 `AkMIDI` 模块。

#### 基础改造

在 `Plugins/Wwise/Wwise.uplugin` 的 `Modules` 中增加：

```json
{
  "Name": "AkMIDI",
  "Type": "Runtime",
  "LoadingPhase": "Default"
}
```

如果游戏模块 C++ 直接使用 `UAkMidiComponent`、`UAkMidiMessage`、`UAkMidiFunctionLibrary`，还需在 `Source/WwiseDemoGame/WwiseDemoGame.Build.cs` 增加依赖：

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "AkMIDI" });
```

---

### 3.2 MIDI 消息对象支持

#### 已支持

`UAkMidiMessage` 支持表达单条通道 MIDI 消息：

- `NoteType`
- `Channel`
- `NoteOffset`
- `Data01`
- `Data02`

支持的消息类型：

- `Note Off`
- `Note On`
- `Note Aftertouch`
- `CC`
- `Program Change`
- `Channel Aftertouch`
- `Pitch Bend`

相关文件：

- `Plugins/Wwise/Source/AkMIDI/Public/AkMidiMessage.h`
- `Plugins/Wwise/Source/AkMIDI/Private/AkMidiMessage.cpp`

#### 限制

- 只表示单条消息，不表示完整 MIDI 文件。
- `NoteOffset` 定义为 `uint8`，而 Wwise 原生 `AkMIDIPost::uOffset` 是 `AkUInt64`，不适合长时间线和精确 sample 调度。
- 运行时 `NewObject<UAkMidiMessage>()` 后 `MidiMessageBackup` 为空，`BackupMidiMessage` / `RecoverMidiMessage` 对运行时对象基本无效。

---

### 3.3 RtMidi 输入 / 输出支持

#### 已支持

`UAkMidiDevice` 封装了 RtMidi：

- `GetMidiDevice`：枚举输入 / 输出设备。
- `OpenInput` / `CloseInput`：打开 / 关闭 MIDI 输入。
- `OpenOutput` / `CloseOutput`：打开 / 关闭 MIDI 输出。
- `GetRtMidiIn` / `GetRtMidiOut`：获取底层 RtMidi 对象。

相关文件：

- `Plugins/Wwise/Source/AkMIDI/Public/AkMidiDevice.h`
- `Plugins/Wwise/Source/AkMIDI/Private/AkMidiDevice.cpp`
- `Plugins/Wwise/Source/AkMIDI/Classes/RtMidi.h`
- `Plugins/Wwise/Source/AkMIDI/Classes/RtMidi.cpp`

#### 当前约定

`UAkMidiComponent` 内置两个虚拟设备：

- 输入 `Unreal`：`Port = 127`
- 输出 `Wwise`：`Port = 127`

含义：

- `OpenMidiInputDevice(127)`：输入来自 Unreal 内部。
- `OpenMidiOutputDevice(127)`：输出到 Wwise。
- 非 `127`：真实外部 MIDI 端口。

#### 限制

- 没有虚拟 MIDI 端口创建能力，只能枚举系统已有端口。
- 没有统一错误码 / 错误日志。
- 没有设备热插拔处理。
- 没有设备名称持久化映射，端口序号重启后可能变化。

---

### 3.4 Wwise `PostMIDIOnEvent` 支持

#### 已支持

当前已有完整调用链：

```text
UAkMidiMessage
  -> UAkMidiComponent::MakePost
  -> AkMIDIPost
  -> FAkAudioDevice::PostMidiEvent
  -> IWwiseSoundEngineAPI::PostMIDIOnEvent
```

关键文件：

- `Plugins/Wwise/Source/AkMIDI/Private/AkMidiComponent.cpp`
- `Plugins/Wwise/Source/AkAudio/Private/AkAudioDevice.cpp`
- `Plugins/WwiseSoundEngine/ThirdParty/include/AK/SoundEngine/Common/AkSoundEngine.h`

Wwise SDK 原生接口说明中，`PostMIDIOnEvent` 支持：

- 将一组 `AkMIDIPost` 投递到指定 Event。
- `AkMIDIPost::uOffset` 可表示相对当前 audio frame 的 sample offset。
- `in_bAbsoluteOffsets = true` 时可用绝对 sample offset。
- 可传入 `PlayingID` 以投递到已存在播放实例。

#### 当前封装限制

当前 `FAkAudioDevice::PostMidiEvent` 只封装了：

```cpp
SoundEngine->PostMIDIOnEvent(EventID, in_gameObjectID, in_pPosts, in_uNumPosts);
```

没有暴露：

- `in_bAbsoluteOffsets`
- callback flags
- callback function
- cookie
- target `PlayingID`

因此无法优雅支持完整 `.mid` 文件长时序、精确调度、seek、pause/resume、stop 指定播放实例等能力。

---

### 3.5 MIDI 文件读写支持

#### 结论

未发现 Standard MIDI File 读写实现。

未发现以下能力：

- `.mid` / `.midi` 文件解析。
- `MThd` / `MTrk` 解析。
- VLQ delta time 解析。
- tempo meta event 解析。
- running status 解析。
- 多 Track 合并。
- MIDI 文件导出。

`AudiokineticTools` 中存在 `UAkMidiMessageFactory`，但只实现了 `FactoryCreateNew`，没有 `FactoryCreateFile`、`FactoryCanImport`、导入格式注册，也没有 MIDI 文件解析逻辑。

相关文件：

- `Plugins/Wwise/Source/AudiokineticTools/Private/AkMidiMessageFactory.h`
- `Plugins/Wwise/Source/AudiokineticTools/Private/AkMidiMessageFactory.cpp`

---

## 4. 现有明显问题与建议修复

### 4.1 `AkMIDI` 模块未在插件描述中声明

#### 问题

`AkMIDI` 源码存在，但 `Wwise.uplugin` 未声明模块。

#### 影响

- 新机器 / CI / 打包可能无法稳定加载模块。
- 游戏模块直接依赖 `AkMIDI` 时可能出现构建依赖不完整。

#### 修复

在 `Wwise.uplugin` 添加 Runtime 模块声明。

---

### 4.2 输出到外部 MIDI 设备时第三字节错误

#### 问题

`UAkMidiComponent::PostMidiEvent` 输出 raw MIDI 时：

```cpp
uint8 RawMessage[3] = { Status,(uint8)MidiMessage->Data01, (uint8)MidiMessage->Data01 };
```

第三字节应为 `Data02`。

#### 影响

- NoteOn velocity 错误。
- CC value 错误。
- Pitch Bend MSB 错误。

#### 修复

改为：

```cpp
uint8 RawMessage[3] = { Status, (uint8)MidiMessage->Data01, (uint8)MidiMessage->Data02 };
```

---

### 4.3 `CloseMidiDevice` 状态切换疑似反向

#### 问题

`CloseMidiDevice` 中关闭虚拟输入 / 输出时将 `bIsInputFromUnreal` 或 `bIsOutputToWwise` 设置为 `false`，语义上应表示关闭后不再来自 Unreal / 输出到 Wwise，但部分分支会影响下一次关闭逻辑，建议重构状态机。

#### 修复

用明确枚举替代两个 bool：

```cpp
enum class EMidiInputSource { Unreal, ExternalDevice, None };
enum class EMidiOutputTarget { Wwise, ExternalDevice, None };
```

---

### 4.4 析构清理顺序错误

#### 问题

`UAkMidiComponent::~UAkMidiComponent` 先 `MessagePool.Empty()` / `PostPool.Empty()`，再遍历释放，导致遍历不会执行，存在泄漏风险。

#### 修复

先遍历释放，再 `Empty()`。

---

### 4.5 外部 MIDI 回调线程安全不足

#### 问题

RtMidi callback 可能不在 GameThread；当前直接复用 `MessagePool`、调用 `MakePost`、Broadcast Blueprint delegate、操作 `Posts`，线程安全风险较高。

#### 修复

引入线程安全队列：

```text
RtMidi callback thread
  -> enqueue raw MIDI packet
GameThread / Audio-safe scheduler
  -> dequeue
  -> convert to UAkMidiMessage / AkMIDIPost
  -> dispatch delegate / Wwise post
```

---

### 4.6 SysEx / MIDI Clock / Meta Event 不支持

#### 问题

源码注释中明确外部 MIDI 转换部分未支持 SysEx 和 MIDI Clock。`.mid` 文件中的 Meta Event 也没有解析。

#### 建议

- 对 Wwise 播放：忽略 SysEx、Clock，解析 Tempo、Time Signature、Track Name 等必要 meta。
- 对外部设备转发：可保留 SysEx 原始字节并直接发给 RtMidiOut。

---

## 5. 功能逐项改造方案

## 5.1 播放第三方 `.mid` 文件

### 当前支持状态

不支持插件级直接播放。

### 可选路径 A：Wwise Authoring 离线导入

适合固定 MIDI 资源。

流程：

```text
第三方 .mid
  -> Wwise Authoring 导入 / 制作为 Music Segment 或 MIDI Target
  -> 创建 Play Event
  -> Generate SoundBank
  -> UE 使用普通 Ak Event 播放
```

优点：

- 不需要改插件。
- 节拍、循环、音乐结构交给 Wwise。
- 稳定性最高。

缺点：

- 不支持玩家运行时选择外部 `.mid`。
- 每次新增 MIDI 需要重新生成 SoundBank。

### 可选路径 B：插件内支持 `.mid` 资源导入

适合项目内管理 MIDI 文件资源。

新增模块建议：

```text
AkMIDI
  Public/AkMidiFile.h
  Public/AkMidiFilePlayerComponent.h
  Private/AkMidiFileParser.cpp
  Private/AkMidiFileWriter.cpp

AudiokineticTools
  Private/AkMidiFileFactory.cpp
  Private/AssetTypeActions_AkMidiFile.cpp
```

新增资产：

```cpp
UCLASS(BlueprintType)
class UAkMidiFile : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Format;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 TicksPerQuarterNote;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FAkMidiTimedEvent> Events;
};
```

新增事件结构：

```cpp
USTRUCT(BlueprintType)
struct FAkMidiTimedEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double TimeSeconds = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int64 Tick = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAkMessageType Type = EAkMessageType::AMT_Note_On;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 Channel = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 Data01 = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 Data02 = 0;
};
```

需要实现：

- SMF Header：`MThd`
- Track Chunk：`MTrk`
- VLQ delta time
- running status
- tempo meta：`0xFF 0x51`
- format 0 / format 1
- NoteOn velocity 0 转 NoteOff
- CC / ProgramChange / PitchBend / Aftertouch
- 多 Track 合并排序

## 5.2 运行时播放外部 `.mid` 文件

### 当前支持状态

不支持。

### 推荐目标

新增运行时组件：

```cpp
UCLASS(ClassGroup="AkMIDI", meta=(BlueprintSpawnableComponent))
class UAkMidiFilePlayerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable)
    bool LoadMidiFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable)
    bool PlayToWwise(UAkMidiComponent* MidiComponent, UAkAudioEvent* Event);

    UFUNCTION(BlueprintCallable)
    bool PlayToExternalDevice(UAkMidiComponent* MidiComponent, int32 OutputPort);

    UFUNCTION(BlueprintCallable)
    void Stop();

    UFUNCTION(BlueprintCallable)
    void Pause();

    UFUNCTION(BlueprintCallable)
    void SeekSeconds(double TimeSeconds);
};
```

### 播放调度方案

#### 简单方案：GameThread 分批投递

```text
TickComponent
  -> 根据当前播放时间找出 due events
  -> 转成 UAkMidiMessage 数组
  -> UAkMidiComponent::PostMidiEvent
```

优点：实现快。

缺点：精度受帧率影响，不适合严格音乐时序。

#### 精确方案：Wwise absolute sample offset

扩展 `FAkAudioDevice::PostMidiEvent`，暴露：

```cpp
PostMIDIOnEvent(
  EventID,
  GameObjectID,
  Posts,
  NumPosts,
  true,
  Flags,
  Callback,
  Cookie,
  PlayingID
)
```

播放时：

```text
解析 MIDI TimeSeconds
  -> 转 sample tick
  -> AkMIDIPost::uOffset = absoluteSampleTick
  -> 分块提前投递给 Wwise
```

优点：sample 级时序更准。

缺点：改造量较大，需要处理 PlayingID、Stop、Seek、Pause、Tempo。

### 外部文件安全建议

运行时读取外部 `.mid` 时需要：

- 限制路径白名单，例如 `Saved/MIDI`。
- 限制文件大小。
- 校验 chunk 长度，防止越界。
- 解析失败返回明确错误。

## 5.3 UE 内部输入，序列化并输出 `.mid` 文件

### 当前支持状态

不支持。

### 改造目标

新增录制器：

```cpp
UCLASS(BlueprintType)
class UAkMidiRecorder : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable)
    void StartRecording(int32 TicksPerQuarterNote = 480, int32 TempoBPM = 120);

    UFUNCTION(BlueprintCallable)
    void RecordMidiMessage(const UAkMidiMessage* Message, double TimeSeconds);

    UFUNCTION(BlueprintCallable)
    bool SaveToMidiFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable)
    void StopRecording();
};
```

### 输出 `.mid` 需要实现

- Header chunk：`MThd`
- Track chunk：`MTrk`
- Big-endian 写入。
- Delta time VLQ 编码。
- Tempo meta event：`FF 51 03 tt tt tt`
- Time Signature 可选：`FF 58`
- End of Track：`FF 2F 00`
- NoteOn / NoteOff / CC / ProgramChange / PitchBend 序列化。

### 数据来源

可支持三类输入：

```text
UE 代码主动创建 UAkMidiMessage
外部 MIDI 输入 OnMessageReceived
蓝图 / UI / Gameplay 事件映射成 MIDI
```

### 注意

导出 `.mid` 时建议以 tick 为主，不要只存 seconds。若录制期间支持 tempo 变化，需要保存 tempo map。

## 5.4 外部 MIDI 键盘 / 乐器触发 Wwise 播放

### 当前支持状态

部分支持。

### 当前可用链路

```text
外部 MIDI 设备
  -> RtMidiIn callback
  -> UAkMidiComponent::HandleRtMidiCallback
  -> UAkMidiMessage
  -> UAkMidiComponent::MakePost
  -> FAkAudioDevice::PostMidiEvent
  -> Wwise PostMIDIOnEvent
```

### 使用方式

```text
1. 组件持有 UAkAudioEvent，例如 Play_MIDI_Piano
2. 调用 GetMidiDevice 枚举设备
3. OpenMidiInputDevice(外部键盘 Port)
4. OpenMidiOutputDevice(127) 输出到 Wwise
5. Wwise 侧 Event 的 Play Action 指向可响应 MIDI 的对象
```

### 必要修复

- 确认 `AkMIDI` Runtime 模块被插件声明。
- 外部 callback 不要直接广播蓝图 delegate，改为 GameThread 派发。
- MIDI raw packet 解析需要处理消息长度和 running status。
- `Posts` 访问需要加锁或改为单线程队列。
- Wwise `AkAudioEvent` 为空时应日志提示。

### Wwise 侧要求

需要制作普通 Wwise Event：

```text
Play_MIDI_Piano
  -> Play
    -> MIDI_Piano / Instrument / Container
```

目标对象需要配置 MIDI 响应，例如 Note Tracking、Root Note、Velocity、Release 等。

## 5.5 Unreal 发送 MIDI 给外部乐器 / DAW / VST

### 当前支持状态

部分支持。

### 当前可用链路

```text
UE 创建 UAkMidiMessage
  -> UAkMidiComponent::PostMidiEvent
  -> RtMidiOut::sendMessage
  -> 外部 MIDI Out / 虚拟端口 / DAW / VST
```

### 使用方式

```text
1. GetMidiDevice 枚举输出设备
2. OpenMidiInputDevice(127) 输入来自 Unreal
3. OpenMidiOutputDevice(外部设备 Port)
4. UE 创建 NoteOn / NoteOff / CC 等消息
5. PostMidiEvent(Messages, nullptr)
```

Windows 常见目标：

- 硬件 USB MIDI Out
- loopMIDI 虚拟端口
- DAW 监听 loopMIDI 输入
- VST Host 监听 loopMIDI 输入

### 必要修复

必须先修复 raw MIDI 第三字节：

```cpp
uint8 RawMessage[3] = { Status, (uint8)MidiMessage->Data01, (uint8)MidiMessage->Data02 };
```

### 建议增强

新增更底层的输出接口：

```cpp
UFUNCTION(BlueprintCallable)
bool SendRawMidiBytes(const TArray<uint8>& Bytes);

UFUNCTION(BlueprintCallable)
bool SendNoteOn(uint8 Channel, uint8 Note, uint8 Velocity);

UFUNCTION(BlueprintCallable)
bool SendNoteOff(uint8 Channel, uint8 Note, uint8 Velocity);

UFUNCTION(BlueprintCallable)
bool SendCC(uint8 Channel, uint8 Controller, uint8 Value);

UFUNCTION(BlueprintCallable)
bool SendProgramChange(uint8 Channel, uint8 Program);

UFUNCTION(BlueprintCallable)
bool SendPitchBend(uint8 Channel, int32 Bend14Bit);
```

这样可避免蓝图层每次手工构造 `UAkMidiMessage`。

---

## 6. 推荐分阶段实施计划

### 阶段 0：模块与稳定性修复

优先级最高。

1. `Wwise.uplugin` 增加 `AkMIDI` Runtime 模块。
2. 游戏模块需要直接调用时，`WwiseDemoGame.Build.cs` 增加 `AkMIDI` 依赖。
3. 修复外部 MIDI 输出第三字节 bug。
4. 修复析构清理顺序。
5. 重构输入/输出状态，替代 `bIsInputFromUnreal` / `bIsOutputToWwise` 双 bool。
6. 为 RtMidi callback 增加线程安全队列。
7. 增加日志和错误码。

### 阶段 1：外部 MIDI 键盘触发 Wwise

目标：稳定支持实时演奏。

1. 完善设备枚举 UI / 蓝图接口。
2. 支持按设备名称打开，而不是只按 Port 序号。
3. 支持 GameThread 派发 `OnMessageReceived`。
4. 支持通道过滤、Note Range、Velocity Curve。
5. 验证 Wwise Event + MIDI Target 配置。

### 阶段 2：Unreal 输出 MIDI 到外部设备

目标：支持外部乐器、DAW、VST 联动。

1. 修复 `RtMidiOut::sendMessage` 数据。
2. 新增 `SendNoteOn` / `SendNoteOff` / `SendCC` / `SendRawMidiBytes`。
3. 支持虚拟端口文档化，例如 Windows loopMIDI。
4. 支持外部设备断开重连。

### 阶段 3：MIDI 文件解析与运行时播放

目标：支持运行时加载外部 `.mid`。

1. 实现 `FAkMidiFileParser`。
2. 支持 format 0 / 1。
3. 实现 tempo map 和 tick -> seconds/sample。
4. 实现 `UAkMidiFilePlayerComponent`。
5. 先实现 GameThread Tick 分批投递。
6. 再扩展 Wwise absolute sample offset / PlayingID 精确投递。

### 阶段 4：MIDI 文件导出

目标：UE 内部输入和外部输入均可录制成 `.mid`。

1. 实现 `UAkMidiRecorder`。
2. 实现 `FAkMidiFileWriter`。
3. 支持单 Track 导出。
4. 支持多 Track / 多 Channel 导出。
5. 支持 tempo / time signature meta。

---

## 7. 建议新增文件结构

```text
Plugins/Wwise/Source/AkMIDI/Public/
  AkMidiTypesEx.h
  AkMidiFile.h
  AkMidiFilePlayerComponent.h
  AkMidiRecorder.h

Plugins/Wwise/Source/AkMIDI/Private/
  AkMidiFileParser.cpp
  AkMidiFileWriter.cpp
  AkMidiFilePlayerComponent.cpp
  AkMidiRecorder.cpp

Plugins/Wwise/Source/AudiokineticTools/Private/
  AkMidiFileFactory.h
  AkMidiFileFactory.cpp
  AssetTypeActions_AkMidiFile.h
  AssetTypeActions_AkMidiFile.cpp
```

---

## 8. 验收用例

### 用例 1：外部键盘触发 Wwise

1. 连接 MIDI 键盘。
2. UE 枚举到输入设备。
3. 打开键盘输入 Port。
4. 输出选择 `Wwise` / `127`。
5. 按键后 Wwise Profiler 看到 `PostMIDIOnEvent`。
6. 听到 Wwise MIDI Target 发声。
7. 松键后 NoteOff 正常释放。

### 用例 2：UE 输出到 DAW

1. Windows 安装 loopMIDI。
2. UE 枚举到 loopMIDI 输出端口。
3. DAW 监听 loopMIDI 输入。
4. UE 发送 NoteOn / NoteOff。
5. DAW 或 VST 正常发声。
6. Velocity 与 CC 值正确。

### 用例 3：运行时播放 `.mid`

1. 将测试 `.mid` 放入 `Saved/MIDI`。
2. UE 调用 `LoadMidiFile`。
3. Parser 解析 format、TPQN、tracks、tempo。
4. 调用 `PlayToWwise`。
5. Wwise Profiler 可见 MIDI 投递。
6. 音符时序和 NoteOff 正常。
7. Stop 后所有未释放音符被关闭。

### 用例 4：录制并导出 `.mid`

1. UE 启动录制。
2. 外部键盘输入或 UE 内部生成 MIDI。
3. 停止录制并保存 `.mid`。
4. 用 DAW 打开导出的 `.mid`。
5. 音符、力度、时序、通道正确。

---

## 9. 最终建议

如果项目短期目标是“外部 MIDI 键盘实时触发 Wwise”或“UE 控制外部 DAW/VST”，当前插件可以作为基础，但必须先做阶段 0 的稳定性修复。

如果目标是“运行时播放任意第三方 `.mid` 文件”或“UE 导出 `.mid` 文件”，当前插件缺少核心 MIDI 文件读写能力，需要新增 parser、writer、player、recorder 四个模块，不建议在现有 `UAkMidiComponent` 中直接堆逻辑，应保持实时设备 IO 与 MIDI 文件时序播放解耦。
