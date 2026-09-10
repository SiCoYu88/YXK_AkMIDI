# AkAudio 与 AudiokineticTools H3DWwise 版本升级迁移指南

## 1. 文档目的

本文记录将 AkMIDI 对 `AkAudio` 和 `AudiokineticTools` 的定制功能从一个 Wwise Unreal Integration 版本迁移到新版本时的处理方法、冲突决策和验证步骤。

本次实际迁移基线：

| 项目 | 路径 |
| --- | --- |
| 源版本 | `AkPlugins/AkMIDI/Wwise2025.1.9/Source/AkAudio` |
| 目标版本 | `AkPlugins/AkMIDI/Wwise2025.1.9.4397/Source/AkAudio` |
| 定制代码标记 | `#pragma region H3DWwise` |
| 迁移日期 | 2026-09-08 |

本文只适用于 `AkAudio` 和 `AudiokineticTools` 内由 `H3DWwise` 标记的 AkMIDI/Wwise 定制及其直接依赖。不要用旧版本文件整体覆盖新版本文件，也不要把本指南直接套用到 Niagara 系统。

## 2. 核心原则

1. 以目标版本官方代码为主体，只迁移定制逻辑。
2. 先按 region 找功能，再按函数和数据流确认依赖，不能只复制 region 内的文本。
3. 目标版本已有不同实现、函数签名改变或静态断言冲突时，停止自动合并，由维护者决定。
4. 声明、实现、头文件依赖、回调注册和清理逻辑必须成组迁移。
5. 所有 AkMIDI 定制统一使用 `#pragma region H3DWwise`；旧的 `H3D` 标记迁移时一并改名。
6. 不迁移没有闭合的 region，除非维护者明确确认内容范围并决定补齐 `#pragma endregion`。
7. 完成后必须检查 region 配对、声明与实现、Wwise API 签名、冲突标记，并在完整 Unreal 项目中编译。

## 3. 本次迁移范围

本次目标版本的 `AkAudio` 包含 18 个 `H3DWwise` region；`AudiokineticToolsModule.cpp` 另包含 2 个，分布如下。

| 文件 | Region 数 | 迁移内容 |
| --- | ---: | --- |
| `Public/AkAudioDevice.h` | 4 | MIDI 全局回调委托、回调实例、`PostMidiEvent`/`StopMidiEvent` 声明、MIDI EndOfEvent 回调声明 |
| `Private/AkAudioDevice.cpp` | 3 | MIDI 全局回调实现、初始化时注册回调、MIDI 发布/停止及 EndOfEvent 实现 |
| `Classes/AkAudioEvent.h` | 1 | `PostMIDIOn*` 和播放位置查询声明 |
| `Private/AkAudioEvent.cpp` | 2 | 附加头文件、`PostMIDIOn*` 和播放位置查询实现 |
| `Classes/AkGameplayTypes.h` | 2 | `EnableGetMusicPlayPosition` 枚举和静态校验 |
| `Classes/AkGameplayStatics.h` | 2 | 蓝图函数声明、`AKTools::EnumToString` |
| `Private/AkGameplayStatics.cpp` | 4 | 附加头文件、PostEvent 日志、查询/Seek/Flush/回调发布功能、SetState 日志 |
| `AudiokineticTools/Private/AudiokineticToolsModule.cpp` | 2 | 引入 `AssetTypeActions_AkMidiMessage.h`、注册 `FAssetTypeActions_AkMidiMessage` |

`AudiokineticTools.Build.cs` 还使用一组 `#region H3DWwise` / `#endregion` 标记 `AkMIDI` 私有模块依赖。它不是 `#pragma region`，因此不计入上表的 20 个 pragma region，但必须随功能一并核验。

以下冲突代码按本次决策不迁移：

| 文件 | 源 Region | 决策 |
| --- | --- | --- |
| `Private/AkComponentCallbackManager.cpp` | `BlueprintAkCallbackInfo` 空指针保护和错误日志 | 跳过，保留目标版本实现 |
| `Private/AkGameplayTypes.cpp` | `UAkEventCallbackInfo::Create` 对回调对象池的保护和错误日志 | 跳过，保留目标版本已有的新版空指针检查 |

## 4. 功能依赖关系

### 4.1 MIDI 发布链路

迁移时必须保持以下调用链完整：

```text
UAkAudioEvent::PostMIDIOnActor
  -> UAkAudioEvent::PostMIDIOnComponent
  -> UAkAudioEvent::PostMIDIOnGameObject
  -> UAkAudioEvent::PostMIDIOnGameObjectID
  -> FAkAudioDevice::PostMidiEvent
  -> IWwiseSoundEngineAPI::PostMIDIOnEvent
```

需要同时迁移：

- `UAkAudioEvent` 中四个 `PostMIDIOn*` 方法的声明和实现。
- `FAkAudioDevice::PostMidiEvent` 的声明和实现。
- `FAkAudioDevice::StopMidiEvent` 的声明和实现。
- `MidiEndOfEventCallback` 的声明和实现。
- `FOnAkMIDIGlobalCallback`、`OnMessageWaitToSend`、`MidiCallback`。
- `FAkAudioDevice::Init` 中位于 `AkGlobalCallbackLocation_PreProcessMessageQueueForRender` 的注册代码。

只迁移 `H3DWwise` 而漏掉旧版中标记为 `H3D` 的 `FAkAudioDevice` 依赖，会导致 `PostMidiEvent` 未声明或链接失败。本次已将这些依赖一起迁移，并把标记统一改为 `H3DWwise`。

### 4.2 PlayingID 生命周期

首次发布 MIDI 时传入 `AK_INVALID_PLAYING_ID`，Wwise 创建实例并返回新的 PlayingID。后续批次（包括 Note-Off）应复用返回的 PlayingID。

新实例发布成功后调用 `AddPlayingID`。实例收到 `AK_EndOfEvent` 时，`MidiEndOfEventCallback` 调用 `RemovePlayingID`。`StopMidiEvent` 先调用 `StopMIDIOnEvent` 释放音符；指定了有效 PlayingID 时，再调用 `StopPlayingID` 终止被跟踪的实例。

升级时必须复核目标版本中以下接口是否仍存在且语义不变：

```cpp
IWwiseSoundEngineAPI::PostMIDIOnEvent(...)
IWwiseSoundEngineAPI::StopMIDIOnEvent(...)
FAkAudioDevice::AddPlayingID(...)
FAkAudioDevice::RemovePlayingID(...)
FAkAudioDevice::StopPlayingID(...)
```

### 4.3 播放位置查询

`UAkAudioEvent` 新增：

- `GetSourcePlayPosition`
- `GetPlayingSegmentInfo`
- `GetSourceActiveDuration`

`UAkGameplayStatics` 将这些能力暴露给蓝图，并通过 `bUseAkMusicHierarchy` 区分普通声音和 Interactive Music Hierarchy。

使用 `GetPlayingSegmentInfo` 前，发布事件的回调掩码必须包含 `AK_EnableGetMusicPlayPosition`。因此需要同步 `EAkCallbackType::EnableGetMusicPlayPosition = 21`。

### 4.4 MIDI Message 编辑器资产注册

`AudiokineticToolsModule.cpp` 中的两个 `H3DWwise` region 必须成组迁移：

```cpp
#pragma region H3DWwise
#include "AssetTypeActions_AkMidiMessage.h"
#pragma endregion
```

以及 `FAudiokineticToolsModule::StartupModule()` 中的资产类型注册：

```cpp
#pragma region H3DWwise
MakeShared<FAssetTypeActions_AkMidiMessage>(AudiokineticAssetCategoryBit),
#pragma endregion
```

这两处代码的完整依赖链是：

```text
AudiokineticTools.Build.cs 中的 AkMIDI 私有依赖
  -> AssetTypeActions_AkMidiMessage.h/.cpp
  -> AkMidiMessageFactory.h/.cpp
  -> UAkMidiMessage
  -> AudiokineticToolsModule.cpp 引入并注册资产类型
```

迁移时必须确认上述文件存在，并确认 `AudiokineticTools.Build.cs` 的 `PrivateDependencyModuleNames` 包含 `AkMIDI`。仅复制 `AudiokineticToolsModule.cpp` 中的两个 region，会因缺少类型声明、实现或模块依赖而导致编译失败。

## 5. 本次冲突及处理结论

### 5.1 `CreateCallbackPackage` 新增 EventId 参数

源版本接口：

```cpp
CreateCallbackPackage(..., GameObjectID, false)
```

目标 `2025.1.9.4397` 接口新增必填的 `EventId`：

```cpp
CreateCallbackPackage(..., GameObjectID, false, EventId)
```

本次决策是传入当前事件的 Short ID：

```cpp
CallbackManager->CreateCallbackPackage(
    *Delegate,
    CallbackMask,
    GameObjectID,
    false,
    GetShortID());
```

没有蓝图 Delegate 时创建的函数回调包同样传入 `GetShortID()`。这个选择与目标版本现有 `PostOnGameObjectID` 的处理一致。以后若接口再次变化，应以目标版本现有事件发布路径为参考，不能盲目追加默认值或传 `0`。

### 5.2 `EAkCallbackType::Last` 静态断言冲突

目标版本包含：

```cpp
static_assert(
    AK_Callback_Last == (1 << (uint32)EAkCallbackType::Last),
    "An AkCallbackType value was added to the enum, please update EAkCallbackType accordingly.");
```

添加显式值为 `21` 的 `EnableGetMusicPlayPosition` 后，隐式枚举项 `Last` 变为 `22`，而 Wwise 2025.1 的 `AK_Callback_Last` 是回调通知位，不包含播放位置查询标记，因此该断言会失败。

本次处理：

```cpp
#pragma region H3DWwise
EnableGetMusicPlayPosition = 21 UMETA(...),
#pragma endregion

...

#pragma region H3DWwise
CHECK_CALLBACK_TYPE_VALUE(EnableGetMusicPlayPosition);
#pragma endregion

//#if WWISE_2024_1_OR_LATER
//static_assert(...);
//#endif
```

升级到后续 Wwise 版本时必须重新查看原生 `AkCallbackTypes.h`，确认 `AK_EnableGetMusicPlayPosition` 和 `AK_Callback_Last` 的实际数值后再决定是否继续注释断言。

### 5.3 回调对象池空指针处理

源版本通过两个分离的 region 包裹目标原有代码，增加外层 `if/else`。目标版本已经把 `FAkAudioDevice` 和 `GetAkCallbackInfoPool()` 保存为局部变量，并在空指针时提前返回。

这不是可以原样复制的新增函数，而是对目标现有控制流的修改。本次选择跳过以下两组改造：

- `AkComponentCallbackManager.cpp` 中 `BlueprintAkCallbackInfo` 的额外保护。
- `AkGameplayTypes.cpp` 中 `UAkEventCallbackInfo::Create` 的额外错误日志。

以后需要恢复这些日志或保护时，应以目标版本控制流重新实现并单独评审。

### 5.4 已存在的头文件

目标 `AkAudioEvent.cpp` 已经包含：

```cpp
#include "Wwise/API/WwiseSoundEngineAPI.h"
```

因此迁移源 include region 时不重复添加，只补充目标缺少的：

```cpp
#include "AkGameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
```

升级时应逐项确认 include 是否已经由目标版本提供，避免重复包含和不必要的排序变化。

### 5.5 损坏文本

源 `SeekOnEventWithMS` 日志中存在损坏字符：

```text
PlayingID = %d��OffsetMS = %d
```

本次明确修正为：

```text
PlayingID = %d, OffsetMS = %d
```

源代码中的损坏中文注释也使用可读文本恢复，不保留替换字符 `�`。

### 5.6 未闭合的 `AKTools` region

源 `AkGameplayStatics.h` 文件末尾的 `AKTools::EnumToString` 只有：

```cpp
#pragma region H3DWwise
```

没有对应的 `#pragma endregion`。本次确认整个 `AKTools` namespace 都属于该定制，并在目标文件末尾补齐：

```cpp
#pragma endregion
```

以后不应以“文件末尾”自动推断任意未闭合 region 的范围，必须先人工确认。

### 5.7 `AudiokineticToolsModule.cpp` 核验结论

项目实际使用的 `AudiokineticToolsModule.cpp` 与 `Wwise2025.1.10.9233` 快照中的目标文件逐行比较后完全一致，目标文件已经包含以下两处定制：

- `AssetTypeActions_AkMidiMessage.h` 头文件引入。
- `FAssetTypeActions_AkMidiMessage` 资产类型注册。

因此本次不重复插入代码，也不改动目标版本控制流。以后升级时仍需逐项核对这两处位置，不能仅以目标文件中出现 `H3DWwise` 字样判断依赖完整。

## 6. 推荐升级流程

### 第一步：准备基线

1. 保留一份已验证可用的旧 Wwise 集成作为源版本。
2. 准备未修改的新 Wwise 官方集成作为目标版本。
3. 提交或备份目标版本，确保所有后续改动可单独查看和回退。
4. 确认项目实际使用的 Wwise 版本、补丁号和 Unreal Engine 版本。

### 第二步：搜索定制代码

至少搜索以下内容：

```powershell
rg -n "#pragma\s+region\s+H3DWwise|#pragma\s+region\s+H3D" <源AkAudio目录>
rg -n "PostMidiEvent|StopMidiEvent|PostMIDIOn|EnableGetMusicPlayPosition" <源AkAudio目录>
rg -n "H3DWwise|AssetTypeActions_AkMidiMessage" <源AudiokineticTools目录>/Private/AudiokineticToolsModule.cpp
rg -n "AkMIDI|H3DWwise" <源AudiokineticTools目录>/AudiokineticTools.Build.cs
```

不要只搜索 region。部分必要适配可能位于 region 外，例如为解决枚举冲突而注释的静态断言。

### 第三步：建立迁移清单

按以下类别记录每处改造：

| 类型 | 处理方式 |
| --- | --- |
| 独立新增声明/函数 | 在目标对应类和实现文件中迁移 |
| 修改目标现有函数 | 对照新版控制流人工合并 |
| 修改枚举或宏 | 同时检查静态断言、序列化和蓝图位掩码 |
| 注册回调 | 同时检查声明、实例、注册位置和注销机制 |
| 调用 Wwise API | 对照目标版本 SDK 头文件核对完整签名 |
| 未闭合 region | 暂停，人工确认范围 |

### 第四步：先迁移底层，再迁移上层

建议顺序：

1. `FAkAudioDevice` 委托和底层 MIDI API。
2. `UAkAudioEvent` MIDI 发布和查询 API。
3. `EAkCallbackType` 扩展。
4. `UAkGameplayStatics` 蓝图接口和辅助工具。
5. `AudiokineticTools` 的模块依赖、MIDI Message Factory 和资产类型实现。
6. `AudiokineticToolsModule.cpp` 的头文件引入和资产类型注册。
7. 目标现有函数中的日志或保护性改造。

每完成一层，立即确认声明、实现和调用点数量，避免最后才发现缺少底层依赖。

### 第五步：处理冲突

遇到以下情况必须暂停并记录，不要自行选择：

- 目标函数签名增加、删除或改变参数语义。
- 目标版本已经实现了同类空指针保护或生命周期管理。
- 枚举值与原生 SDK 常量不一致。
- 静态断言因定制枚举发生变化。
- 同名 include、函数或 Blueprint 节点已经存在。
- region 跨越目标已有代码，无法作为独立块插入。
- 需要修改 region 外代码才能编译。

记录冲突时至少给出源实现、目标实现、可选方案、编译影响和运行时影响，由维护者确认后继续。

## 7. 验证清单

### 7.1 静态检查

- [ ] 所有声明都有实现，所有实现都有声明。
- [ ] `PostMIDIOnActor` 到 `PostMIDIOnGameObjectID` 的调用链完整。
- [ ] `PostMidiEvent`、`StopMidiEvent`、`MidiEndOfEventCallback` 均已声明和实现。
- [ ] `CreateCallbackPackage` 参数符合目标版本签名。
- [ ] `AK_EnableGetMusicPlayPosition` 数值与目标 SDK 一致。
- [ ] 所有 `H3DWwise` region 成对闭合。
- [ ] 目标 `AkAudio` 中没有遗留普通 `H3D` region。
- [ ] 没有 `<<<<<<<`、`=======`、`>>>>>>>` 合并冲突标记。
- [ ] 没有损坏替换字符 `�`。
- [ ] `AudiokineticToolsModule.cpp` 同时包含 MIDI Message 资产类型的 include 和注册代码。
- [ ] `AudiokineticTools.Build.cs` 包含 `AkMIDI` 私有模块依赖。
- [ ] `AssetTypeActions_AkMidiMessage` 和 `AkMidiMessageFactory` 的声明、实现文件均存在。
- [ ] Git 差异只包含确认过的文件和冲突处理。

可使用以下搜索命令辅助检查：

```powershell
rg -n "#pragma\s+region\s+H3DWwise" <目标AkAudio目录>
rg -n "#pragma\s+region\s+H3D(?:\s|$)" <目标AkAudio目录>
rg -n "^(<<<<<<< .+|=======|>>>>>>> .+)$" <目标AkAudio目录>
rg -n "�" <目标AkAudio目录>
```

本次迁移结果为：

```text
AkAudio H3DWwise regions: 18
AudiokineticToolsModule.cpp H3DWwise regions: 2
本指南覆盖的 H3DWwise regions: 20
AkAudio plain H3D regions: 0
AudiokineticToolsModule.cpp plain H3D regions: 0
所有 pragma region 已闭合
没有合并冲突标记
没有损坏替换字符
```

### 7.2 编译检查

目标快照目录只有模块源码，没有 `.uplugin` 或 `.uproject`，不能在该目录独立完成 Unreal 编译。合并到完整 Wwise 插件后，应执行：

1. 重新生成项目文件。
2. 编译项目的 Editor Target。
3. 确认 Unreal Header Tool 能处理新增 `UFUNCTION` 和 `EAkCallbackType`。
4. 确认 `AkAudio`、`AkMIDI`、`AudiokineticTools` 均成功链接。
5. 确认 `AudiokineticTools` 可以解析 `UAkMidiMessage`、资产类型 Action 和 Factory。

### 7.3 运行时检查

- [ ] 首次 MIDI 发布返回有效 PlayingID。
- [ ] 后续 MIDI 批次复用同一个 PlayingID。
- [ ] Note-Off 能正确停止对应音符。
- [ ] `StopMidiEvent` 能停止指定实例。
- [ ] Event 结束后 PlayingID 从跟踪表移除。
- [ ] Actor 或 Component 销毁时，回调包和播放实例按预期清理。
- [ ] 普通声音的 `GetSourcePlayPosition` 返回有效位置。
- [ ] Music Hierarchy 在启用 `EnableGetMusicPlayPosition` 后能返回 Segment 信息。
- [ ] `SeekOnEvent` 的百分比和毫秒接口均可用。
- [ ] `PostEvent_WithFlush` 和 `PostEventWithSeek` 行为符合预期。
- [ ] 蓝图中能找到新增节点，参数默认值和分类正确。
- [ ] 内容浏览器可以创建 `Audiokinetic Midi Message` 资产。
- [ ] MIDI Message 资产可以使用简单资产编辑器打开。

## 8. 后续版本迁移记录模板

每次升级复制以下表格到本节末尾填写：

```markdown
### Wwise <源版本> -> <目标版本>

- 日期：
- Unreal Engine 版本：
- 源目录：
- 目标目录：
- 迁移 region 数：
- `AudiokineticToolsModule.cpp` region 数：
- 目标 API 签名变化：
- 冲突文件：
- 决策：
- 编译结果：
- 运行时验证结果：
- 提交或变更集：
```

不要只记录“迁移成功”。至少记录所有目标 API 变化和人工冲突决策，以便下一次升级判断旧方案是否仍然适用。
