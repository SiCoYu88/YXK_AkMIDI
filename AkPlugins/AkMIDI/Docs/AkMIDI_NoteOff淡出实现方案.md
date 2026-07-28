# AkMIDI Note Off 淡出实现方案

## 1. 目标

本文给出 AkMIDI 长音符在 Note Off 时实现自然淡出或指定时长淡出的可行方案。

目标分为两类：

1. **自然释音**：收到 Note Off 后，当前音符按照乐器的 Release 包络衰减。
2. **强制淡出**：在指定时间内把声音从当前音量渐变到静音，再结束播放。

这两种行为并不等价。自然释音应优先由 Wwise 乐器完成；强制淡出通常作用于整个 MIDI 通道、Game Object、Event 或 Playing ID，不一定能只影响一个音符。

## 2. 当前实现边界

当前 AkMIDI 的 Note Off 转换只设置：

```text
Message Type = Note Off
Channel      = MIDI 通道
Data01       = Note
Data02       = Release Velocity
NoteOffset   = 相对当前音频帧的采样偏移
```

调用链为：

```text
UAkMidiMessage
  -> UAkMidiComponent::MakePost
  -> AkMIDIPost
  -> FAkAudioDevice::PostMidiEvent
  -> Wwise PostMIDIOnEvent
```

关键限制：

- `Data02` 是 Release Velocity，不是淡出毫秒数。
- `NoteOffset` 只决定 Note Off 的发送时刻，不表示淡出长度。
- `AkMIDIPost` 没有每条 Note Off 的 FadeTime 或 FadeCurve 字段。
- `StopMidiEvent` 调用 `StopMIDIOnEvent(EventID, GameObjectID)`，没有过渡时长参数。
- MIDI 文件播放器在 Stop、Pause、Seek 时会给活跃音符发送 Velocity 为 `0` 的 Note Off，也没有淡出参数。

因此，当前插件能够正确结束长音符，但不会自行生成淡出曲线。

## 3. 方案对比

| 方案 | 修改 AkMIDI | 作用范围 | 精度/稳定性 | 推荐场景 |
| --- | ---: | --- | --- | --- |
| Wwise 乐器 Release 包络 | 否 | 单音符 | 最高 | 固定或由 Wwise 管理的自然释音 |
| Wwise RTPC 控制 Release | 否 | 通常为 Game Object/乐器 | 高 | 运行时改变释音长度 |
| MIDI CC 72 控制 Release | 否 | MIDI 通道 | 取决于目标乐器 | 目标明确支持 CC 72 |
| RTPC 音量插值后 Note Off | 否 | Game Object/Event | 高 | 必须保证指定时长内静音 |
| CC 11/CC 7 阶梯渐变 | 否 | MIDI 通道 | 受 GameThread 定时影响 | 外部设备或无 RTPC 条件 |
| Playing ID 停止淡出 | 是 | 整个 Playing ID | 高 | 整个 Event 实例淡出，不要求单音符独立 |

## 4. 推荐方案一：Wwise Release 包络

### 4.1 适用条件

- 每个音符收到 Note Off 后都需要自然衰减。
- 淡出时间固定，或只需要在 Wwise 内按乐器、Switch、State 等配置。
- 多个音符可能同时复音播放，并且需要各自独立释放。

这是最推荐的方案，因为 Release 是乐器语义的一部分，Wwise 可以在音频线程内稳定地管理每个 Voice。

### 4.2 Wwise 配置

1. 找到接收 MIDI 的 Synth、Sampler 或 MIDI Target。
2. 打开该对象的音量包络或 ADSR 设置。
3. 设置 Release，例如：

   ```text
   短音效：50-150 ms
   钢琴/拨弦：150-800 ms
   Pad/环境音：1000-5000 ms
   ```

4. 确认 Note Off 后对象不会被 Event 的 Stop Action 立即终止。
5. 生成 SoundBank，并在 Wwise Profiler 中确认 Note Off 后 Voice 进入 Release 阶段。

具体属性名称取决于 Wwise 乐器类型。若目标对象没有可配置的 Release，需要使用后续 RTPC 或音量渐变方案。

### 4.3 Unreal 调用流程

```text
发送 Note On(Channel, Note, Velocity)
  -> 保持任意时长
  -> 发送 Note Off(相同 Channel, 相同 Note, ReleaseVelocity)
  -> Wwise 执行 Release 包络
```

Note Off 必须和 Note On 使用相同的 `Channel` 与 `Data01`。长音符和短音符的区别只是两条消息之间的时间差，Release 行为不需要 AkMIDI 额外处理。

### 4.4 优缺点

优点：

- 真正按音符独立释放。
- 不受 Unreal 帧率影响。
- 支持复音、快速重触发和 Sustain 等乐器行为。
- 不需要修改 AkMIDI。

限制：

- 默认 Release 时长由 Wwise 资源决定。
- 若需要每次 Note Off 都传入不同秒数，需要结合 RTPC 或分配独立 Voice/Game Object。

## 5. 推荐方案二：RTPC 动态控制 Release

### 5.1 适用条件

- Release 时长需要由游戏状态动态决定。
- 同一个 AkMIDI Game Object 上的音符可以共享当前 Release 参数。
- Wwise 目标乐器的 Release 属性支持 Game Parameter/RTPC 控制。

### 5.2 Wwise 配置

1. 创建 Game Parameter，例如 `MIDI_Release_Time`。
2. 建议范围直接使用毫秒：

   ```text
   Min     = 0
   Default = 300
   Max     = 5000
   ```

3. 将该参数绑定到目标乐器的 Release 属性。
4. 配置 RTPC 曲线。可以采用 1:1 毫秒映射，也可以让低值区间更精细。
5. 生成 SoundBank。

并非所有 Wwise 对象的 Release 属性都支持 RTPC。如果目标属性无法绑定，应改用“RTPC 音量插值”方案。

### 5.3 Blueprint 流程

```text
Ak MIDI Component
  -> Set RTPC Value
       RTPC Value          = MIDI_Release_Time
       Value               = FadeTimeMs
       InterpolationTimeMs = 0
  -> Create Ak Midi Message(Note Off)
  -> Post Midi Event
```

应在 `Ak MIDI Component` 自身调用 `Set RTPC Value`，确保 RTPC 和 MIDI 消息使用同一个 Wwise Game Object。不要在另一个 Actor 或全局作用域设置后假定它会影响当前组件。

`InterpolationTimeMs` 在这里建议为 `0`。它表示 RTPC 参数变化的插值时间，不是 Note Off 的淡出时间；真正的淡出时间来自 RTPC 映射后的 Release 值。

### 5.4 C++ 示例

```cpp
#include "AkMidiComponent.h"
#include "AkMidiFunctionLibrary.h"
#include "AkRtpc.h"

bool SendNoteOffWithRelease(
    UAkMidiComponent* MidiComponent,
    UAkAudioEvent* MidiEvent,
    UAkRtpc* ReleaseRtpc,
    float ReleaseTimeMs,
    uint8 Channel,
    uint8 Note,
    uint8 ReleaseVelocity)
{
    if (!MidiComponent || !MidiEvent || !ReleaseRtpc)
    {
        return false;
    }

    const float ClampedReleaseMs = FMath::Clamp(ReleaseTimeMs, 0.0f, 5000.0f);
    MidiComponent->SetRTPCValue(ReleaseRtpc, ClampedReleaseMs, 0, TEXT(""));

    UAkMidiMessage* NoteOff = UAkMidiFunctionLibrary::CreateAkMidiMessage(
        EAkMessageType::AMT_Note_Off,
        FMath::Clamp<uint8>(Channel, 0, 15),
        0,
        FMath::Clamp<uint8>(Note, 0, 127),
        FMath::Clamp<uint8>(ReleaseVelocity, 0, 127));

    TArray<UAkMidiMessage*> Messages;
    Messages.Add(NoteOff);
    return MidiComponent->PostMidiEvent(Messages, MidiEvent);
}
```

### 5.5 作用域限制

Game Object 级 RTPC 会影响该组件上的所有相关 Voice。若多个音符同时发声：

- 在发送某个 Note Off 前改变 Release，可能也影响之后释放的其他音符。
- 是否影响已经进入 Release 的 Voice，取决于 Wwise 属性的求值方式。
- 如果每个音符必须拥有不同 Release，应为不同 Voice 分配独立 Game Object，或在 Wwise 内实现按音符控制。

## 6. 方案三：发送 MIDI CC 72

### 6.1 原理

标准 MIDI CC 72 表示 Sound Release Time。AkMIDI 已支持 CC 消息：

```text
NoteType = AMT_CC
Channel  = 目标 MIDI 通道
Data01   = 72
Data02   = ReleaseValue，范围 0-127
```

随后发送相同通道和音高的 Note Off。

### 6.2 消息顺序

建议把 CC 72 和 Note Off 放在同一个 `PostMidiEvent` 数组中，并使用不同的采样偏移确保顺序：

```text
Message[0]: CC 72,   NoteOffset=0
Message[1]: Note Off, NoteOffset=1
```

示例：

```cpp
UAkMidiMessage* ReleaseCc = UAkMidiFunctionLibrary::CreateAkMidiMessage(
    EAkMessageType::AMT_CC,
    Channel,
    0,
    72,
    ReleaseValue);

UAkMidiMessage* NoteOff = UAkMidiFunctionLibrary::CreateAkMidiMessage(
    EAkMessageType::AMT_Note_Off,
    Channel,
    1,
    Note,
    0);

TArray<UAkMidiMessage*> Messages{ReleaseCc, NoteOff};
MidiComponent->PostMidiEvent(Messages, MidiEvent);
```

### 6.3 限制

- CC 72 的 `0-127` 没有统一的毫秒换算，实际曲线由接收乐器决定。
- 仅有 `AkMidiTypes.h` 中的 CC 常量并不代表目标 Wwise 乐器一定响应 CC 72。
- CC 是通道级参数，会影响该通道上的其他音符。
- 必须在 Wwise Profiler 中验证目标对象确实收到并使用了 CC 72。

若 Wwise 目标不响应 CC 72，应使用 RTPC，而不是继续修改 CC 数值。

## 7. 方案四：RTPC 音量插值后发送 Note Off

### 7.1 适用条件

- 需要明确保证在指定毫秒内降到静音。
- 可以接受淡出作用于整个 AkMIDI Game Object，而不是单个音符。
- Wwise 乐器没有可动态控制的 Release 属性。

### 7.2 Wwise 配置

1. 创建 Game Parameter，例如 `MIDI_Fade_Gain`，范围 `0-1`。
2. 将其映射到目标对象或 Bus 的音量：

   ```text
   0 -> -96 dB
   1 -> 0 dB
   ```

3. 默认值设为 `1`。

### 7.3 调用流程

```text
SetRTPCValue(MIDI_Fade_Gain, 0, FadeTimeMs)
  -> 等待 FadeTimeMs
  -> 发送 Note Off
  -> 等待 Release 完成或确认无活动音符
  -> SetRTPCValue(MIDI_Fade_Gain, 1, 0)
```

这里 `InterpolationTimeMs` 才直接承担音量淡出的时间。Wwise 在音频线程内完成插值，通常比 Unreal Tick 中逐帧发送 CC 更平滑。

不能在发送 Note Off 后立即把 RTPC 恢复到 `1`，否则尾音可能重新变响。应在 Voice 已结束后恢复，或为下一次播放使用新的 Game Object。

### 7.4 作用域

- 绑定到 MIDI 组件 Game Object：影响该组件上的全部音符。
- 绑定到 Bus：影响该 Bus 下所有声音，通常范围过大。
- 要实现单音符独立淡出，需要一音符一 Game Object，或至少把需要独立控制的音符分配到不同组件。

## 8. 方案五：CC 11/CC 7 阶梯淡出

当输出目标是外部 MIDI 设备，或不能使用 Wwise RTPC 时，可以在 Unreal 中生成控制器渐变。

优先使用 CC 11 Expression，保留 CC 7 作为通道主音量：

```text
FadeDurationMs = 500
StepIntervalMs = 20
StepCount       = 25

每 20 ms：
  CC 11 Value = round(127 * (1 - CurrentStep / StepCount))

最后：
  发送 Note Off
  安全延迟后恢复 CC 11 = 127
```

限制：

- CC 11/CC 7 作用于整个 MIDI 通道。
- Unreal Timer/Tick 存在帧率和调度抖动，不适合严格的音乐采样精度。
- 同一通道上的其他音符会一起淡出。
- 恢复控制器过早可能让 Release 尾音重新变响。

如需单音符控制，可以为同时发声的 Voice 动态分配不同 MIDI 通道，但 MIDI 1.0 只有 16 个通道，需要处理 Voice Stealing 和通道回收。

## 9. 方案六：扩展 Playing ID 事件级淡出

### 9.1 适用条件

- 需求是停止并淡出整个 MIDI Event 实例。
- 不要求同一 Event 中的某个音符独立淡出。
- 可以修改 AkMIDI 源码。

Wwise 已提供带过渡时间和曲线的 Playing ID Stop：

```cpp
FAkAudioDevice::StopPlayingID(
    PlayingID,
    TransitionDurationMs,
    FadeCurve);
```

当前 AkMIDI 的问题是：

- `FAkAudioDevice::PostMidiEvent` 已返回 `AkPlayingID`。
- `UAkMidiComponent::PostMidiEvent` 将其转换为 `bool`，没有暴露给调用方。
- 后续 MIDI Post 也没有暴露目标 Playing ID 参数。

### 9.2 建议新增接口

保持现有 API 不变，新增接口避免破坏已有蓝图：

```cpp
UFUNCTION(BlueprintCallable, Category = "AkMIDI|Playback")
int32 PostMidiEventWithPlayingId(
    const TArray<UAkMidiMessage*>& Messages,
    UAkAudioEvent* AkEvent,
    int32 TargetPlayingId = 0);

UFUNCTION(BlueprintCallable, Category = "AkMIDI|Playback")
bool StopPlayingIdWithFade(
    int32 PlayingId,
    int32 FadeTimeMs,
    EAkCurveInterpolation FadeCurve);
```

底层需要同时扩展 `FAkAudioDevice::PostMidiEvent`，将 Wwise `PostMIDIOnEvent` 的 Target Playing ID 参数暴露出来。

### 9.3 建议实现

```cpp
bool UAkMidiComponent::StopPlayingIdWithFade(
    int32 PlayingId,
    int32 FadeTimeMs,
    EAkCurveInterpolation FadeCurve)
{
    if (PlayingId <= 0 || !AkAudioDevice)
    {
        return false;
    }

    AkAudioDevice->StopPlayingID(
        static_cast<AkPlayingID>(PlayingId),
        FMath::Max(0, FadeTimeMs),
        static_cast<AkCurveInterpolation>(FadeCurve));
    return true;
}
```

### 9.4 重要限制

- Playing ID Stop 是音频实例级淡出，不是标准 MIDI Note Off。
- 同一 Playing ID 下的所有音符和声音都会一起停止。
- 当前插件按批次调用 `PostMIDIOnEvent`；必须验证返回的 Playing ID 生命周期，并确保后续消息投递到同一实例。
- 如果要做到真正的“一音符一淡出”，必须给每个音符创建独立播放实例或独立 Game Object，复杂度和 Voice 数量都会显著增加。

因此，不建议仅为了普通乐器释音就修改 Playing ID 链路。

## 10. 推荐落地组合

### 10.1 普通乐器

```text
Wwise 固定 Release 包络
  + AkMIDI 正常 Note On/Note Off
```

无需修改插件，复音表现最好。

### 10.2 游戏状态控制释音长度

```text
Wwise Release 属性绑定 MIDI_Release_Time RTPC
  + AkMidiComponent.SetRTPCValue(..., 0 ms interpolation)
  + Note Off
```

适合冰冻、慢动作、距离、角色状态等影响释音长度的场景。

### 10.3 必须在指定时间内静音

```text
MIDI_Fade_Gain RTPC 插值到 0
  + 等待 FadeTimeMs
  + Note Off
  + Voice 结束后恢复 RTPC
```

适合暂停、切场景、强制终止长 Pad 等场景。

### 10.4 整个 Event 实例淡出

```text
扩展 AkMIDI 暴露 Playing ID
  + StopPlayingID(FadeTimeMs, FadeCurve)
```

只在确实需要 Event 级控制时采用。

## 11. 建议实施顺序

1. 在 Wwise 为目标乐器配置固定 Release，并验证普通 Note Off。
2. 如需动态 Release，增加 Game Parameter 并用 `AkMidiComponent.SetRTPCValue` 控制。
3. 如目标属性不支持 RTPC，改用 Game Object 音量 RTPC 插值。
4. 仅在外部 MIDI 或无 RTPC 条件时使用 CC 72/CC 11。
5. 只有 Event 级淡出需求明确时，才改造 Playing ID API。

## 12. 验收测试

### 12.1 基础测试

- 发送 100 ms、1 秒、10 秒三种长度的同一音符。
- 三种音符的 Note Off 后衰减时间应一致，并符合 Wwise Release 设置。
- Release 结束后 Wwise Profiler 中不应残留活动 Voice。

### 12.2 动态 Release 测试

- 分别设置 `50 ms`、`500 ms`、`3000 ms`。
- 每次设置后发送 Note Off，测量实际尾音时间。
- 检查 RTPC 作用域是否和 AkMIDI 组件的 Game Object 一致。

### 12.3 复音测试

- 同一通道同时保持多个音符，只释放其中一个。
- 确认 Release 包络只结束目标音符。
- 使用 CC 或 Game Object RTPC 时，记录其他音符是否受到影响。

### 12.4 边界测试

- Note On Velocity 为 `0` 的隐式 Note Off。
- 相同音高快速重复触发。
- Sustain CC 64 开启和关闭。
- MIDI 文件播放结束、Pause、Seek、Stop。
- FadeTime 为 `0`、负值、超长值。
- Actor 销毁或组件关闭时是否残留长音符。

## 13. 最终建议

对于“长音符松键后自然淡出”，应采用 **Wwise Release 包络**，不需要修改 AkMIDI。

对于“每次 Note Off 使用不同释音长度”，优先采用 **Wwise Release RTPC + 同一个 AkMidiComponent Game Object**。它比 Unreal 中逐帧发送音量 CC 更稳定。

对于“无论乐器包络如何，都必须在指定时间内静音”，采用 **Game Object 音量 RTPC 插值 + Note Off**。

Playing ID 淡出属于 Event 级停止机制，不是单音符 Note Off 的替代方案，应作为最后选择。
