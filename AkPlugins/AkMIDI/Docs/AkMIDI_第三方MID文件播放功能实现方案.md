# AkMIDI 第三方 `.mid` 文件播放功能实现方案

## 1. 目标

在 UE5 工程中支持运行时加载并播放第三方 Standard MIDI File，即 `.mid` 文件，并将解析出的 MIDI 事件发送到现有 `AkMIDI` / Wwise MIDI Event 链路中。

目标能力：

1. 从磁盘读取外部 `.mid` 文件。
2. 支持 Standard MIDI File 基础格式解析。
3. 将 MIDI tick / tempo 转换为播放时间。
4. 按时间调度 `Note On`、`Note Off`、`CC`、`Program Change`、`Pitch Bend` 等事件。
5. 通过现有 `UAkMidiComponent::PostMidiEvent` 播放到 Wwise。
6. 支持 `Play`、`Stop`、`Pause`、`Resume`、`Seek` 基础控制。
7. Stop 时自动发送未释放音符的 `Note Off`，避免 Wwise 残留发声。

非目标：

- 不在第一阶段实现 `.mid` 编辑器导入资产。
- 不在第一阶段实现 `.mid` 导出。
- 不在第一阶段支持 SysEx 直通。
- 不在第一阶段实现 sample-accurate 的 Wwise absolute offset 投递。

---

## 2. 当前插件现状

当前 `AkMIDI` 插件已有以下能力：

- `UAkMidiMessage`：可表达单条 MIDI 消息。
- `UAkMidiFunctionLibrary::CreateAkMidiMessage`：可创建单条 MIDI 消息。
- `UAkMidiComponent::PostMidiEvent`：可将 `UAkMidiMessage` 数组投递给 Wwise。
- `UAkMidiComponent::MakePost`：内部将 `UAkMidiMessage` 转成 Wwise `AkMIDIPost`。
- `FAkAudioDevice::PostMidiEvent`：调用 Wwise `PostMIDIOnEvent`。

当前缺失：

- `.mid` 文件读取。
- Standard MIDI File 解析。
- tick / tempo map 处理。
- MIDI 播放时间线。
- 长时间播放调度。
- Pause / Resume / Seek。
- 播放结束和 Stop 时的 active notes 清理。

因此，本功能应新增独立的 MIDI 文件解析与播放层，不建议直接把 `.mid` 解析逻辑塞进 `UAkMidiComponent`。

---

## 3. 总体架构

推荐新增三层：

```text
.mid 文件
  -> FAkMidiFileParser
      解析 SMF，输出标准化 timed events
  -> UAkMidiFilePlayerComponent
      维护播放状态、时间、调度、active notes
  -> UAkMidiComponent
      发送 UAkMidiMessage 到 Wwise
  -> Wwise PostMIDIOnEvent
```

模块职责：

| 模块 | 职责 |
|---|---|
| `FAkMidiFileParser` | 读取 `.mid` 二进制，解析 Header / Track / Event / Tempo。 |
| `FAkMidiFileData` | 保存解析后的 MIDI 文件信息和标准化事件数组。 |
| `FAkMidiTimedEvent` | 表达单条已带时间戳的 MIDI 事件。 |
| `UAkMidiFilePlayerComponent` | 播放器组件，负责加载、播放、暂停、停止、seek、调度。 |
| `UAkMidiComponent` | 复用现有 Wwise MIDI 投递能力。 |

---

## 4. 新增文件建议

建议新增在 `AkMIDI` Runtime 模块中：

```text
Plugins/Wwise/Source/AkMIDI/Public/
  AkMidiFileTypes.h
  AkMidiFileParser.h
  AkMidiFilePlayerComponent.h

Plugins/Wwise/Source/AkMIDI/Private/
  AkMidiFileParser.cpp
  AkMidiFilePlayerComponent.cpp
```

后续如果需要编辑器资产导入，再新增：

```text
Plugins/Wwise/Source/AudiokineticTools/Private/
  AkMidiFileFactory.h
  AkMidiFileFactory.cpp
  AssetTypeActions_AkMidiFile.h
  AssetTypeActions_AkMidiFile.cpp
```

---

## 5. 数据结构设计

### 5.1 MIDI 文件数据

```cpp
USTRUCT(BlueprintType)
struct FAkMidiFileData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 Format = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 TrackCount = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 TicksPerQuarterNote = 480;

    UPROPERTY(BlueprintReadOnly)
    double DurationSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly)
    TArray<FAkMidiTimedEvent> Events;
};
```

### 5.2 标准化 MIDI 事件

```cpp
USTRUCT(BlueprintType)
struct FAkMidiTimedEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int64 Tick = 0;

    UPROPERTY(BlueprintReadOnly)
    double TimeSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly)
    EAkMessageType Type = EAkMessageType::AMT_Note_On;

    UPROPERTY(BlueprintReadOnly)
    uint8 Channel = 0;

    UPROPERTY(BlueprintReadOnly)
    uint8 Data01 = 0;

    UPROPERTY(BlueprintReadOnly)
    uint8 Data02 = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 TrackIndex = 0;
};
```

字段映射：

| MIDI 消息 | `Type` | `Data01` | `Data02` |
|---|---|---|---|
| Note Off `0x8n` | `AMT_Note_Off` | Note | Velocity |
| Note On `0x9n` | `AMT_Note_On` | Note | Velocity |
| Note On velocity 0 | `AMT_Note_Off` | Note | 0 |
| Aftertouch `0xAn` | `AMT_AfterTouch` | Note | Pressure |
| CC `0xBn` | `AMT_CC` | Controller | Value |
| Program Change `0xCn` | `AMT_Program_Change` | Program | 0 |
| Channel Aftertouch `0xDn` | `AMT_Channel_AfterTouch` | Pressure | 0 |
| Pitch Bend `0xEn` | `AMT_Pitch_Bend` | LSB | MSB |

---

## 6. `.mid` 解析范围

第一阶段建议支持 Standard MIDI File 常用子集。

### 6.1 必须支持

- Header Chunk：`MThd`
- Track Chunk：`MTrk`
- Format `0`
- Format `1`
- Division：PPQ / ticks per quarter note
- Variable Length Quantity，简称 VLQ
- Running Status
- MIDI Channel Voice Messages：
  - `Note Off`
  - `Note On`
  - `Poly Aftertouch`
  - `Control Change`
  - `Program Change`
  - `Channel Aftertouch`
  - `Pitch Bend`
- Meta Events：
  - `0x2F` End of Track
  - `0x51` Set Tempo
  - `0x58` Time Signature，可解析但第一阶段可不用于调度
  - `0x03` Track Name，可跳过
- 多 Track 合并排序。

### 6.2 第一阶段可忽略

- Format `2`。
- SMPTE time division。
- SysEx `0xF0` / `0xF7`。
- MIDI Clock / realtime messages。
- Lyrics / Marker / Cue Point。
- Key Signature 对播放调度无影响，可跳过。

### 6.3 解析失败处理

需要明确错误类型：

```cpp
enum class EAkMidiFileParseResult : uint8
{
    Success,
    FileNotFound,
    FileTooLarge,
    InvalidHeader,
    UnsupportedFormat,
    UnsupportedDivision,
    TruncatedChunk,
    InvalidVLQ,
    InvalidRunningStatus,
    UnknownError
};
```

---

## 7. Tempo 与时间换算

`.mid` 中 event 原始时间是 delta tick。播放时需要转换为秒。

默认 tempo：

```text
120 BPM = 500000 microseconds per quarter note
```

换算公式：

```text
secondsPerTick = tempoMicrosecondsPerQuarter / 1000000.0 / ticksPerQuarterNote
TimeSeconds += deltaTicks * secondsPerTick
```

遇到 Set Tempo meta event：

```text
FF 51 03 tt tt tt
```

更新 `tempoMicrosecondsPerQuarter`，后续 delta tick 使用新 tempo。

Format 1 多 Track 情况下，推荐策略：

1. 每个 Track 先解析为带绝对 tick 的事件。
2. 收集所有 tempo event，生成全局 tempo map。
3. 再把每个 MIDI channel event 的 absolute tick 转换为 absolute seconds。
4. 合并排序所有 playable events。

这样可避免 tempo track 和 note track 分离时换算错误。

---

## 8. 播放器组件设计

### 8.1 组件接口

```cpp
UCLASS(ClassGroup="AkMIDI", meta=(BlueprintSpawnableComponent))
class AKMIDI_API UAkMidiFilePlayerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    bool LoadMidiFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    bool Play(UAkMidiComponent* InMidiComponent, UAkAudioEvent* InAkEvent);

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    void Stop(bool bSendAllNotesOff = true);

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    void Pause();

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    void Resume();

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    bool SeekSeconds(double InSeconds);

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    double GetPlaybackTimeSeconds() const;

    UFUNCTION(BlueprintCallable, Category="AkMIDI|MidiFile")
    double GetDurationSeconds() const;
};
```

### 8.2 播放状态

```cpp
enum class EAkMidiFilePlayerState : uint8
{
    Unloaded,
    Loaded,
    Playing,
    Paused,
    Stopped,
    Finished
};
```

内部状态：

```text
CurrentEventIndex
PlaybackStartTimeSeconds
PausedTimeSeconds
CurrentPlaybackTimeSeconds
ActiveNotes[Channel][Note]
LoadedFileData
Weak MidiComponent
Weak AkEvent
```

---

## 9. 调度方案

### 9.1 第一阶段推荐：GameThread Tick 分批投递

每帧计算当前播放时间，并取出当前窗口内的 MIDI 事件：

```text
CurrentTime = Now - PlaybackStartTime
LookAheadTime = CurrentTime + DispatchLookAheadSeconds

while Event[Index].TimeSeconds <= LookAheadTime:
    Convert to UAkMidiMessage
    Add to batch
    Index++

PostMidiEvent(batch, AkEvent)
```

建议参数：

```text
DispatchLookAheadSeconds = 0.02 ~ 0.05
MaxEventsPerTick = 256 或 512
```

优点：

- 改造量小。
- 可快速验证第三方 `.mid` 文件播放链路。
- 复用现有 `UAkMidiComponent::PostMidiEvent`。

缺点：

- 精度受 GameThread Tick 影响。
- 高密度 MIDI 文件可能抖动。
- `UAkMidiMessage::NoteOffset` 当前只有 `uint8`，不能表达长时序。

### 9.2 第二阶段优化：Wwise absolute sample offset

如果第一阶段可行，再扩展：

- `FAkAudioDevice::PostMidiEvent` 增加 `bAbsoluteOffsets`、`PlayingID` 参数。
- `UAkMidiComponent` 支持直接发送 `AkMIDIPost` 或扩展 `UAkMidiMessage` 的 `NoteOffset` 为 `uint64`。
- 使用 `AK::SoundEngine::GetSampleTick` 获得当前绝对 sample tick。
- 将 MIDI 事件绝对秒转换为 sample tick。
- 提前一段时间批量投递给 Wwise。

该方案可获得更接近 sample-accurate 的播放精度，但实现复杂度更高，不建议作为第一阶段入口。

---

## 10. Active Notes 管理

播放 `.mid` 时必须维护当前打开的音符，避免 Stop / Seek / Pause 后残留音符。

### 10.1 Note On

收到有效 `Note On`：

```text
ActiveNotes[Channel][Note] = true
```

### 10.2 Note Off

收到 `Note Off` 或 `Note On velocity 0`：

```text
ActiveNotes[Channel][Note] = false
```

### 10.3 Stop

Stop 时发送所有 active notes 的 `Note Off`：

```text
for Channel 0..15:
  for Note 0..127:
    if ActiveNotes[Channel][Note]:
       Send NoteOff(Channel, Note, 0)
```

可选再发送：

```text
CC 123 All Notes Off
CC 120 All Sound Off
```

但 Wwise 对 CC 的响应取决于目标对象配置，第一阶段仍以逐个 `Note Off` 最稳。

### 10.4 Seek

Seek 时建议：

1. 先 `Stop(bSendAllNotesOff=true)` 清理旧音符。
2. 根据目标时间重置 `CurrentEventIndex`。
3. 如需恢复跨越目标时间的长音，需要回溯计算 active notes；第一阶段可不恢复跨越长音，只从 seek 点之后继续播放。

---

## 11. 与 Wwise 侧的配合

UE 播放 `.mid` 文件只是发送 MIDI 消息，真正发声依赖 Wwise Event。

Wwise 侧需要：

```text
Event: Play_MIDI_xxx
  -> Play
    -> 可响应 MIDI 的 Instrument / Container / Synth / Fusion 类对象
```

需要检查：

- Event 已加入 SoundBank。
- UE 中 `AkAudioEvent` 指向该 Event。
- 目标对象支持 MIDI Note 响应。
- Root Note / Key Range / Velocity Range 配置正确。
- Voice Limit 不会过早抢占。
- Bus 上压缩器 / limiter 不会导致越播越小。

---

## 12. 文件路径与安全策略

运行时外部文件建议限制在：

```text
<Project>/Saved/MIDI/
```

或由调用方传入绝对路径，但需要：

- 检查扩展名 `.mid` / `.midi`。
- 限制文件大小，例如默认不超过 `16 MB`。
- 所有 chunk 长度都做越界检查。
- VLQ 最多读取 4 字节。
- 解析异常必须安全返回，不能崩溃。

---

## 13. 第一阶段实现步骤

### Step 1：模块启用修复

- `Wwise.uplugin` 增加 `AkMIDI` Runtime 模块声明。
- 游戏模块需要直接调用时，在 `WwiseDemoGame.Build.cs` 增加 `AkMIDI` 依赖。

### Step 2：新增 MIDI 文件类型定义

- 新增 `AkMidiFileTypes.h`。
- 定义 `FAkMidiFileData`、`FAkMidiTimedEvent`、解析结果枚举、播放器状态枚举。

### Step 3：实现 Parser

- 新增 `FAkMidiFileParser`。
- 实现 `ParseFile(FilePath, OutData, OutError)`。
- 支持 `MThd`、`MTrk`、VLQ、running status、tempo map、format 0/1。

### Step 4：实现 Player Component

- 新增 `UAkMidiFilePlayerComponent`。
- Tick 中按 `TimeSeconds` 调度事件。
- 转换 `FAkMidiTimedEvent` 到 `UAkMidiMessage`。
- 调用 `UAkMidiComponent::PostMidiEvent`。

### Step 5：Active Notes 与 Stop

- 维护 `ActiveNotes[16][128]`。
- Stop / EndPlay / Seek 前发送 NoteOff。

### Step 6：蓝图测试接口

提供蓝图调用流程：

```text
LoadMidiFile(Path)
OpenMidiInputDevice(127)
OpenMidiOutputDevice(127)
Play(MidiComponent, AkEvent)
Stop()
```

### Step 7：测试与验收

- 单轨 `.mid`。
- 多轨 `.mid`。
- 带 tempo change 的 `.mid`。
- 大量 Note On / Note Off。
- Stop 后无残留音。
- Pause / Resume 时间正确。

---

## 14. 第二阶段增强项

第一阶段验证可行后，可继续增强：

1. 支持 absolute sample offset 投递。
2. 支持 `PlayingID` 复用和精确 Stop。
3. 支持 `.mid` 资产导入。
4. 支持外部设备播放 `.mid`。
5. 支持 SysEx 直通到 RtMidiOut。
6. 支持 tempo map 可视化。
7. 支持 loop 区间。
8. 支持 track mute / solo。
9. 支持 channel remap。
10. 支持 velocity scale / transpose。

---

## 15. 风险点

| 风险 | 说明 | 规避 |
|---|---|---|
| 播放精度 | GameThread Tick 调度不是 sample-accurate。 | 第一阶段接受，第二阶段改 Wwise absolute offset。 |
| Note 残留 | Stop / Seek 时未发 NoteOff 会导致持续发声。 | 必须维护 Active Notes。 |
| Tempo 错误 | 多 Track MIDI 中 tempo track 与 note track 分离。 | 先生成全局 tempo map 再换算时间。 |
| Running Status | 很多 `.mid` 文件会使用 running status。 | Parser 必须支持。 |
| 文件异常 | 第三方 `.mid` 可能格式不规范。 | 所有 chunk / VLQ / length 做边界检查。 |
| Wwise 不发声 | UE 已发送 MIDI，但 Wwise Event 不响应。 | 提供最小 Wwise MIDI Event 配置说明。 |
| 高频事件堆积 | 大型 MIDI 文件单帧事件过多。 | `MaxEventsPerTick`、look-ahead、日志告警。 |

---

## 16. 验收标准

### 16.1 基础播放

- 能加载 `Saved/MIDI/test.mid`。
- 能解析 format、track count、duration。
- 能播放到 Wwise Event。
- `Note On`、`Note Off` 正常。

### 16.2 Tempo

- 120 BPM 默认文件播放速度正确。
- 包含 `FF 51` tempo change 的文件播放速度基本正确。

### 16.3 控制

- `Pause` 后无新音符触发。
- `Resume` 后从暂停处继续。
- `Stop` 后所有音符释放。
- `SeekSeconds` 后从目标时间附近继续。

### 16.4 稳定性

- 空文件不崩溃。
- 非 MIDI 文件返回解析失败。
- 超大文件被拒绝或安全失败。
- 播放结束后状态为 `Finished`。

---

## 17. 推荐最小测试蓝图流程

```text
Actor
  -> AkMidiComponent
  -> AkMidiFilePlayerComponent

BeginPlay:
  AkMidiComponent.GetMidiDevice
  AkMidiComponent.OpenMidiInputDevice(127)
  AkMidiComponent.OpenMidiOutputDevice(127)
  MidiFilePlayer.LoadMidiFile("Saved/MIDI/test.mid")
  MidiFilePlayer.Play(AkMidiComponent, Play_MIDI_Piano)
```

停止：

```text
Input Stop
  -> MidiFilePlayer.Stop(true)
```

暂停：

```text
Input Pause
  -> MidiFilePlayer.Pause()
```

继续：

```text
Input Resume
  -> MidiFilePlayer.Resume()
```

---

## 18. 结论

支持播放第三方 `.mid` 文件是可行的，但当前插件缺少 MIDI 文件解析和播放调度层。

推荐先实现第一阶段：

```text
SMF Parser + Timed Events + GameThread Tick Player + Active Notes 清理 + Wwise PostMidiEvent 输出
```

该阶段改造量可控，能最快验证第三方 `.mid` 到 Wwise 发声链路。验证通过后，再进入第二阶段，扩展 Wwise absolute sample offset、PlayingID、资产导入和更高精度调度。
