# AkMIDI 使用文档

AkMIDI 为 Unreal Engine 的 Wwise 集成增加实时 MIDI 能力。它可以在 Unreal 中创建 MIDI 消息并发送给 Wwise，也可以读取外部 MIDI 设备、转发到 Wwise 或其他 MIDI 输出设备。

## 功能概览

- 在蓝图或 C++ 中创建并发送 Note On、Note Off、Aftertouch、CC、Program Change、Channel Aftertouch 和 Pitch Bend。
- 枚举、打开和关闭外部 MIDI 输入/输出设备。
- 支持以下路由：
  - Unreal -> Wwise
  - Unreal -> 外部 MIDI 输出
  - 外部 MIDI 输入 -> Wwise
- 通过 `OnMessageReceived` 接收 MIDI 消息通知。
- 通过 `InsertMidiFx` 插入自定义 MIDI 效果，并提供移调和力度压缩函数。

## 目录与版本选择

必须选择与项目中的 Wwise Unreal Integration **完全一致** 的版本，不要混用不同版本的 `AkAudio` 或 `AudiokineticTools` 源码。

| 目录 | 内容 | 建议用途 |
| --- | --- | --- |
| `AkMIDI_Wwise2018.1.9` | 23 个新增或修改文件 | 合并到 Wwise 2018.1.9 |
| `AkMIDI_Wwise2019.1.4` | 23 个新增或修改文件 | 合并到 Wwise 2019.1.4 |
| `AkMIDI_Wwise2019.1.6` | 23 个新增或修改文件 | 合并到 Wwise 2019.1.6 |
| `AkMIDI_Wwise2025.1.4` | 23 个新增或修改文件 | 优先用于已有的 Wwise 2025.1.4 集成 |
| `Wwise2025.1.4` | 三个受影响模块的完整源码快照 | 用于精确重建或对照 Wwise 2025.1.4 |
| `Wwise2025.1.9` | 三个受影响模块的完整源码快照 | 用于 Wwise 2025.1.9 |

`Wwise2025.1.4` 和 `Wwise2025.1.9` 不是完整的 Wwise 插件，仅包含 `AkAudio`、`AkMIDI` 和 `AudiokineticTools` 三个模块。安装时仍应合并到官方 Wwise 插件，不能用它们替换整个官方插件。

## 安装

### 前置条件

- 已在 Unreal 项目或引擎中安装官方 Wwise Unreal Integration。
- Wwise 版本与所选 AkMIDI 目录一致。
- 使用源码方式编译 Unreal 项目；仅安装预编译 Wwise 二进制而不重新编译，无法加载新增模块。
- 安装前建议提交或备份现有 Wwise 插件源码，因为部分文件会被覆盖。

### 合并源码

1. 关闭 Unreal Editor 和 IDE。
2. 找到当前项目实际使用的 Wwise 插件目录，常见位置为：

   ```text
   <Project>/Plugins/Wwise/
   <Engine>/Plugins/Wwise/
   ```

3. 选择匹配的版本目录，将其中 `Source` 的内容合并到 Wwise 插件的 `Source`：

   ```text
   AkMIDI/<匹配版本>/Source/
                        -> <Project>/Plugins/Wwise/Source/
   ```

4. 允许覆盖同名的 `AkAudio` 和 `AudiokineticTools` 文件，但不要删除 Wwise `Source` 中的其他模块。
5. 检查 `Wwise.uplugin` 的 `Modules` 数组。若没有 `AkMIDI`，加入以下运行时模块声明；已有时不要重复添加：

   ```json
   {
     "Name": "AkMIDI",
     "Type": "Runtime",
     "LoadingPhase": "Default"
   }
   ```

6. C++ 项目若直接使用 AkMIDI 类型，在游戏模块的 `.Build.cs` 中添加依赖：

   ```csharp
   PrivateDependencyModuleNames.AddRange(new string[]
   {
       "AkAudio",
       "AkMIDI"
   });
   ```

7. 重新生成项目文件并编译 Editor Target，然后启动 Unreal Editor。

安装成功后，应能在蓝图中搜索到 `Ak MIDI Component`、`Create Ak Midi Message`、`Post Midi Event` 等节点。

## Wwise 侧准备

1. 在 Wwise 工程中准备能够接收 MIDI 的对象和 Event。
2. 确保 Event 已生成到当前平台的 SoundBank。
3. 在 Unreal 中同步或重新生成 Wwise 资源，得到对应的 `Ak Audio Event` 资产。
4. 将该 Event 指定给 `Ak MIDI Component` 的事件属性，或在调用 `Post Midi Event` 时传入。

向 Wwise 发送时，Event 不能为空且必须具有有效的 Short ID，否则发送会失败。

## 蓝图快速开始

### Unreal 生成 MIDI 并发送到 Wwise

1. 在 Actor 上添加 `Ak MIDI Component`。
2. 保持默认路由：输入为 `Unreal`，输出为 `Wwise`。
3. 调用 `Create Ak Midi Message` 创建消息，例如：

   ```text
   Note Type  = Note On
   Channel    = 0
   NoteOffset = 0
   Data01     = 60
   Data02     = 100
   ```

4. 将消息放入数组，调用组件的 `Post Midi Event`，并传入 Wwise Event。
5. 结束音符时再发送一条 `Note Off`；其 `Channel` 和 `Data01` 应与 Note On 一致。

常用值：

- `Data01 = 60`：中央 C。
- `Data02`：Note On/Off 的力度。
- `NoteOffset`：相对当前音频帧的采样偏移，`0` 表示立即发送。

### 使用外部 MIDI 输入

调用顺序很重要：

1. 调用 `Get Midi Device` 获取输入和输出设备。
2. 从返回的 `FMidiDevice` 中读取 `Port`，不要使用数组下标代替端口号。
3. 调用 `Open Midi Input Device`，传入所选输入设备的 `Port`。
4. 在组件上指定接收 MIDI 的 Wwise Event。
5. 调用 `Open Midi Output Device(127)` 或保留默认 Wwise 输出。
6. 按需绑定 `OnMessageReceived`。
7. Actor 结束使用时调用 `Close Midi Device`，关闭 Input、Output 或 Both。

`Get Midi Device` 会在设备列表开头加入两个虚拟端点：

| 名称 | Port | 含义 |
| --- | ---: | --- |
| `Unreal` | `127` | MIDI 消息由 Unreal 创建 |
| `Wwise` | `127` | MIDI 消息发送到 Wwise |

物理设备的 `Port` 通常从 `0` 开始。由于虚拟端点位于数组第一个位置，数组下标与物理端口号通常并不相同。

### 发送到外部 MIDI 设备

1. 先调用 `Get Midi Device` 初始化并枚举设备。
2. 调用 `Open Midi Output Device`，传入目标输出设备的 `Port`。
3. 使用 `Create Ak Midi Message` 和 `Post Midi Event` 发送消息。

当输出目标是外部设备时，传给 `Post Midi Event` 的 Wwise Event 可以为空。

## MIDI 消息字段

| 消息类型 | `Data01` | `Data02` |
| --- | --- | --- |
| Note On | 音高 | 力度 |
| Note Off | 音高 | 力度 |
| Note Aftertouch | 音高 | 压力值 |
| CC | 控制器编号 | 控制器值 |
| Program Change | Program 编号 | 未使用，填 `0` |
| Channel Aftertouch | 压力值 | 未使用，填 `0` |
| Pitch Bend | 低 7 位 | 高 7 位 |

使用以下范围可以避免产生无效 MIDI 数据：

- `Channel`：`0-15`，对应 MIDI 通道 1-16。
- `Data01`、`Data02`：`0-127`。
- `NoteOffset`：非负采样偏移。

这些字段在接口层使用整数类型，但多数路径不会自动限制到标准 MIDI 范围，调用方应自行校验。

## MIDI FX

`Ak MIDI Component` 提供 `InsertMidiFx` 蓝图原生事件。可以创建 `Ak MIDI Component` 的蓝图子类，重写该事件并修改传入的消息。

- 调用 `Midi Fx Bypass(false)`：启用 FX。
- 调用 `Midi Fx Bypass(true)`：旁路 FX。
- `Octave`：按八度移动 Note On 的音高。
- `Velocity Compression`：把 Note On/Off 的力度限制在指定区间。

`Octave` 不会把最终音高自动限制到 `0-127`。使用较大移调值前应自行检查，避免 `uint8` 溢出。

## C++ 示例

头文件：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MidiSenderActor.generated.h"

class UAkAudioEvent;
class UAkMidiComponent;

UCLASS()
class AMidiSenderActor : public AActor
{
    GENERATED_BODY()

public:
    AMidiSenderActor();

    UFUNCTION(BlueprintCallable)
    bool SendNoteOn(uint8 Note, uint8 Velocity);

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UAkMidiComponent> MidiComponent;

    UPROPERTY(EditAnywhere, Category = "MIDI")
    TObjectPtr<UAkAudioEvent> MidiEvent;
};
```

实现：

```cpp
#include "MidiSenderActor.h"
#include "AkMidiComponent.h"
#include "AkMidiFunctionLibrary.h"

AMidiSenderActor::AMidiSenderActor()
{
    MidiComponent = CreateDefaultSubobject<UAkMidiComponent>(TEXT("AkMidi"));
    RootComponent = MidiComponent;
}

bool AMidiSenderActor::SendNoteOn(uint8 Note, uint8 Velocity)
{
    UAkMidiMessage* Message = UAkMidiFunctionLibrary::CreateAkMidiMessage(
        EAkMessageType::AMT_Note_On,
        0,
        0,
        Note,
        Velocity);

    TArray<UAkMidiMessage*> Messages;
    Messages.Add(Message);
    return MidiComponent->PostMidiEvent(Messages, MidiEvent);
}
```

如果需要跨帧保存 `UAkMidiMessage`，应将它放在 `UPROPERTY` 引用中，避免被垃圾回收。

## 主要蓝图 API

| API | 用途 |
| --- | --- |
| `CreateAkMidiMessage` | 创建 MIDI 消息对象 |
| `ModifyAkMidiMessage` | 修改现有消息字段 |
| `GetMidiDevice` | 初始化 MIDI 后端并返回设备列表 |
| `OpenMidiInputDevice` | 选择 Unreal 或物理 MIDI 输入 |
| `OpenMidiOutputDevice` | 选择 Wwise 或物理 MIDI 输出 |
| `CloseMidiDevice` | 关闭输入、输出或全部设备 |
| `PostMidiEvent` | 向当前输出目标发送消息数组 |
| `StopMidiEvent` | 停止指定 Wwise Event 的 MIDI 播放 |
| `MidiFxBypass` | 启用或旁路组件 MIDI FX |
| `OnMessageReceived` | 收到外部 MIDI 消息时广播 |

## 平台说明

`Wwise2025.1.9` 的构建配置包含以下 RtMidi 后端：

- Windows 64 位：Windows Multimedia，链接 `winmm.lib`。
- macOS / iOS：CoreMIDI、CoreAudio、CoreFoundation。
- Linux：ALSA 和 pthread。

Android 等其他平台没有对应的后端配置。不同版本和目标平台应分别完成编译与真机测试。

## 已知限制

- 当前代码不处理 SysEx 和 MIDI Clock/实时消息。
- AkMIDI 会修改 `AkAudio`，并让 `AudiokineticTools` 依赖 `AkMIDI`；升级 Wwise 后需要重新移植匹配版本，不能直接保留旧补丁。
- `GetMidiDevice` 同时负责设置底层回调。未先调用它就直接打开设备，操作可能无效。
- Wwise 音频帧回调使用单播委托；不建议同时用多个 `Ak MIDI Component` 处理外部输入，后绑定的组件会替换先前绑定。
- `Wwise2025.1.9` 中 `OnMessageReceived` 的 `DeltaTime` 可能为 `0`，不要将它用于精确节拍同步。
- `Wwise2025.1.9` 的外部输入队列只在 Wwise 输出路径中消费；外部输入直通外部输出不能作为可靠路由使用。
- 设备端口使用 `uint8`，`127` 已保留给 Unreal/Wwise 虚拟端点。

## 常见问题

### 蓝图中找不到 AkMIDI 节点

检查 `Source/AkMIDI` 是否已合并、`Wwise.uplugin` 是否声明 `AkMIDI` 模块，并确认 Editor Target 已成功重新编译。若仍加载旧二进制，可关闭编辑器后清理项目的 `Binaries` 和 `Intermediate`，重新生成项目文件并编译。

### `PostMidiEvent` 返回 `false`

依次检查：

- `Ak MIDI Component` 是否有效。
- 消息数组是否非空。
- Unreal 主动发送时，输入源是否为 `Unreal`。
- 输出到 Wwise 时，Event 是否有效且 SoundBank 已生成和加载。
- Wwise SoundEngine 是否已完成初始化。

### 外部设备没有数据

- 确认先调用了 `GetMidiDevice`，再打开端口。
- 使用 `FMidiDevice.Port`，不要传设备在数组中的位置。
- 检查设备是否被其他程序独占，以及操作系统 MIDI 权限是否已授予。
- 退出或销毁 Actor 前关闭设备，避免下一次运行无法重新打开端口。

### 升级 Wwise 后编译失败

AkMIDI 直接调用并修改 Wwise 集成源码，版本耦合较强。恢复新版官方 Wwise 插件后，只应用与新版完全匹配的 AkMIDI 目录；没有匹配目录时，需要重新适配 `AkAudioDevice` 和 `AudiokineticTools`，不应混用旧版本文件。
