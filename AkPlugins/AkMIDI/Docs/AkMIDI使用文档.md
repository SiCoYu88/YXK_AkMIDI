# AkMIDI 使用文档

## 1. AkMIDI 能做什么

AkMIDI 用来连接 Unreal、Wwise 和 MIDI 设备。安装完成后，你可以：

- 用 MIDI 键盘、打击垫等设备控制 Wwise 中的声音。
- 在蓝图中主动发送音符、控制器和弯音等 MIDI 消息。
- 把 Unreal 生成的 MIDI 消息发送到外部合成器或虚拟 MIDI 端口。
- 播放电脑上的 `.mid` 文件，并控制播放、暂停、继续、停止和跳转。
- 在消息发送前加入简单的力度限制或移调处理。

本文主要面向关卡、美术、音频和其他不负责插件开发的使用者。涉及源码合并和编译的工作应由项目技术人员完成。

## 2. 开始前检查

开始制作蓝图前，请向项目技术人员确认：

- AkMIDI 已与项目当前的 Wwise 版本匹配并编译成功。
- Unreal 中可以添加 `Ak MIDI Component`。
- 若要播放 `.mid` 文件，可以添加 `Ak MIDI File Player Component`。
- Wwise 中已经准备好接收 MIDI 的 Event，并已生成、同步当前平台的 SoundBank。
- 使用外部设备时，操作系统能识别该设备，且设备没有被其他软件独占。

如果蓝图里找不到上述组件或节点，通常是安装问题，不是蓝图操作问题。

## 3. 先认识两个组件

### `Ak MIDI Component`

负责发送和接收 MIDI。实时键盘、蓝图音符和外部输出都要通过它。

### `Ak MIDI File Player Component`

负责读取和播放 `.mid` 文件。它本身不发声，播放时仍需要一个 `Ak MIDI Component` 和一个 Wwise Event。

播放 `.mid` 文件的功能只包含在仓库的 `Wwise2025.1.4` 和 `Wwise2025.1.9` 完整源码版本中。旧版项目如果找不到该组件，说明当前版本不支持此功能。

## 4. 最快上手：MIDI 键盘控制 Wwise

### 4.1 在 Wwise 中准备声音

请让音频设计人员准备一个能接收 MIDI 的 Wwise 对象和 Event，并完成 SoundBank 生成。把同步到 Unreal 的 `Ak Audio Event` 资产交给蓝图使用。

如果 Event 本身不能接收 MIDI，即使 Unreal 正确收到了键盘消息也不会发声。

### 4.2 在 Unreal 中搭建

1. 新建或打开一个 Actor 蓝图。
2. 添加 `Ak MIDI Component`。
3. 在组件的 Event 属性中选择准备好的 Wwise Event。
4. 在 `BeginPlay` 中调用 `Get Midi Device`。
5. 从返回的 `Input Devices` 中找到你的 MIDI 键盘。
6. 读取该设备结构中的 `Port`，调用 `Open Midi Input Device`。
7. 调用 `Open Midi Output Device`，端口填 `127`，表示输出到 Wwise。
8. 运行关卡并弹奏键盘。
9. Actor 不再使用时，调用 `Close Midi Device`，选择 `Input & Output`。

推荐的节点顺序：

```text
BeginPlay
  -> Get Midi Device
  -> 从 Input Devices 选择设备并读取 Port
  -> Open Midi Input Device(设备的 Port)
  -> Open Midi Output Device(127)
```

### 4.3 不要把数组位置当成 Port

设备列表的第一项是 AkMIDI 自带的虚拟端点：

```text
Input Devices[0] = Unreal，Port 127
Input Devices[1] = 你的键盘，Port 0
```

选择键盘时应传入它的 `Port=0`，而不是数组位置 `1`。这是外部设备无法打开时最常见的原因。

### 4.4 接收消息通知

可以绑定组件的 `On Message Received`，用来点亮 UI、显示当前音符或触发游戏逻辑。

回调中的 `Delta Time` 在当前版本可能一直是 `0`，不要用它计算节拍或做精确同步。回调传出的消息对象还会被组件重复使用；需要稍后再用时，请立即保存其中的类型、通道和数据值，而不是长期保存对象本身。

## 5. 播放 `.mid` 文件

### 5.1 支持的文件

支持常见的 Standard MIDI File：

- Format 0 和 Format 1。
- 单轨和多轨。
- 文件中的速度变化。
- 最大文件大小 16 MiB。

不支持 Format 2、SMPTE 时间格式和 SysEx 播放。`.mid` 文件只包含演奏信息，不包含声音；最终音色由 Wwise Event 决定。

### 5.2 蓝图搭建

1. 在同一个 Actor 上添加 `Ak MIDI Component`。
2. 再添加 `Ak MIDI File Player Component`。
3. 准备 `.mid` 文件的完整路径。打包项目中应使用项目允许读取的位置，并让技术人员确认文件会随版本一起部署。
4. 调用 `Load Midi File(File Path)`。
5. 检查返回值。返回 `true` 表示加载成功。
6. 调用 `Play`，传入 `Ak MIDI Component` 和目标 Wwise Event。
7. 检查 `Play` 返回值。返回 `true` 后播放器进入播放状态。

最小流程：

```text
BeginPlay
  -> Load Midi File("完整文件路径")
  -> Branch
       True  -> Play(Ak MIDI Component, Wwise Event)
       False -> 显示“文件加载失败”
```

默认情况下，播放器会自动把 MIDI 组件设置为“Unreal 输入、Wwise 输出”，不需要先枚举物理设备。

### 5.3 播放控制

| 节点 | 作用 | 使用结果 |
| --- | --- | --- |
| `Play` | 开始播放 | 停止或播完后再次调用会从头播放 |
| `Pause` | 暂停 | 当前发声的音符会先被关闭，避免持续音 |
| `Resume` | 继续 | 从暂停时间继续，并尝试恢复此时应保持的长音 |
| `Stop` | 停止 | 默认关闭活跃音符并回到开头 |
| `Seek Seconds` | 跳到指定秒数 | 超出范围的值会自动限制到开头或结尾 |
| `Get Playback Time Seconds` | 获取当前位置 | 可用于进度条 |
| `Get Duration Seconds` | 获取总时长 | 可用于进度条最大值 |
| `Get Player State` | 获取状态 | 可区分 Loaded、Playing、Paused、Stopped、Finished |

播放器没有自动循环选项，也没有“播放完成”事件。需要循环时，可以定期检查 `Get Player State`；当状态为 `Finished` 时再次调用 `Play`。

当前版本在跳转或继续时会尝试重建长音。若文件中同一通道、同一音高存在密集的重复按下和松开，可能误恢复已经结束的音符；`Seek Seconds` 也可能在尚未播放时立即触发目标位置的长音。此类文件需要实际试听验证，出现残留音时先调用 `Stop`，再从目标位置 `Play`。

### 5.4 播放速度和单帧事件数

`Ak MIDI File Player Component` 有三个常用属性：

| 属性 | 默认值 | 建议 |
| --- | ---: | --- |
| `Playback Rate` | `1.0` | `0.5` 为半速，`2.0` 为双速 |
| `Max Events Per Tick` | `256` | 普通文件保持默认；密集文件丢节奏时可适当提高 |
| `Auto Configure Midi Component` | 开启 | 播放到 Wwise 时保持开启 |

播放器跟随 Unreal 帧更新，不是录音棚级的采样精确播放器。游戏卡顿或单帧事件过多时，个别消息可能延后。

## 6. 在蓝图中主动演奏一个音符

该流程适合按钮音效、音乐玩法或程序生成旋律。

1. 在 Actor 上添加 `Ak MIDI Component`。
2. 使用 `Create Ak Midi Message` 创建 Note On：

   ```text
   Note Type  = Note On
   Channel    = 0
   Note Offset= 0
   Data01     = 60
   Data02     = 100
   ```

3. 把消息放进数组。
4. 调用 `Post Midi Event`，传入 MIDI 组件和 Wwise Event。
5. 需要结束声音时，再创建一条 Note Off。
6. Note Off 的 `Channel` 和 `Data01` 必须与 Note On 相同。

```text
按下按钮 -> Note On(Channel 0, Note 60, Velocity 100)
松开按钮 -> Note Off(Channel 0, Note 60, Velocity 0)
```

只发送 Note On 而不发送 Note Off，可能产生一直不停止的声音。

## 7. 把 Unreal 消息发送到外部设备

1. 调用 `Get Midi Device`。
2. 从 `Output Devices` 选择目标设备并读取它的 `Port`。
3. 调用 `Open Midi Input Device(127)`，表示消息来自 Unreal。
4. 调用 `Open Midi Output Device(目标设备的 Port)`。
5. 使用 `Create Ak Midi Message` 和 `Post Midi Event` 发送消息。
6. 此时 `Post Midi Event` 的 Wwise Event 可以留空。
7. 使用完成后关闭 Output 或 Input & Output。

当前版本不应使用“外部 MIDI 输入 -> 外部 MIDI 输出”的直通方式。需要设备直通时，请使用操作系统 MIDI 路由工具或由技术人员改造插件。

## 8. MIDI 参数速查

### 8.1 通用范围

- `Channel`：`0-15`，分别代表 MIDI 通道 1-16。
- `Data01`、`Data02`：通常使用 `0-127`。
- `Note Offset`：`0` 表示尽快发送；它主要用于 Wwise 输出，对外部设备输出不起作用。

插件不会在所有情况下自动修正超出范围的值，建议在蓝图输入处主动限制。

### 8.2 不同消息的数据含义

| 消息类型 | `Data01` | `Data02` |
| --- | --- | --- |
| Note On | 音高 | 按键力度 |
| Note Off | 音高 | 松键力度，通常可填 `0` |
| Note Aftertouch | 音高 | 压力值 |
| CC | 控制器编号 | 控制器值 |
| Program Change | 音色编号 | 填 `0` |
| Channel Aftertouch | 压力值 | 填 `0` |
| Pitch Bend | 低 7 位 | 高 7 位 |

常用音高：

| 音符 | MIDI 值 |
| --- | ---: |
| C3 | 48 |
| C4（中央 C） | 60 |
| A4 | 69 |
| C5 | 72 |

## 9. 使用 MIDI FX

`Ak MIDI Component` 提供 `Insert Midi Fx`，可以在消息发送前修改内容。

使用方式：

1. 创建 `Ak MIDI Component` 的蓝图子类。
2. 重写 `Insert Midi Fx`。
3. 在事件中调用所需处理节点。
4. 调用 `Midi Fx Bypass(false)` 启用处理。
5. 调用 `Midi Fx Bypass(true)` 暂时绕过处理。

内置处理：

- `Velocity Compression`：把力度限制在指定范围。
- `Octave`：改变 Note On 的音高。

内置 `Octave` 只修改 Note On，不会同步修改 Note Off。直接使用可能导致移调后的音符无法正确关闭，也不会自动把结果限制在 `0-127`。正式内容中应让技术人员提供同时处理 Note On/Note Off 的安全版本。

## 10. 长音与淡出

Note Off 中的力度不是“淡出时间”。想让长音松开后自然衰减，应在 Wwise 乐器中设置 Release 包络。

推荐流程：

```text
AkMIDI 发送 Note On
  -> 保持音符
  -> AkMIDI 发送相同 Channel 和 Note 的 Note Off
  -> Wwise 按乐器的 Release 设置完成尾音
```

如果不同场景需要不同的释音时间，请让音频设计人员使用 Wwise RTPC 控制 Release。不要在蓝图中连续发送大量音量消息来模拟淡出，容易受帧率影响。

## 11. 常见问题

### 蓝图中找不到 AkMIDI 组件或节点

请让技术人员检查：

- 项目使用的 Wwise 版本是否与 AkMIDI 目录一致。
- `AkMIDI` 模块是否已加入 Wwise 插件。
- 项目是否已重新生成并编译，而不是仍在加载旧二进制。

### 键盘出现在列表里，但没有输入

- 确认传入的是设备的 `Port`，不是数组位置。
- 确认先调用 `Get Midi Device`，再打开输入端口。
- 关闭可能独占 MIDI 设备的其他软件。
- 拔插设备后重新枚举，不要继续使用旧列表。
- 确认只有一个活动组件负责外部输入。

### Unreal 能收到键盘消息，但 Wwise 不发声

- 确认输出端口是 `127`，即 Wwise。
- 确认 MIDI 组件或 `Post Midi Event` 使用了正确 Event。
- 确认 Event 已生成到当前平台 SoundBank，且 SoundBank 已加载。
- 请音频设计人员确认 Wwise 对象确实接收对应通道和音符。

### `Post Midi Event` 返回 `false`

- 消息数组不能是空的。
- 蓝图主动发送时，输入源应为 Unreal。
- 输出到 Wwise 时必须有有效 Event。
- 输出到外部设备时，必须先枚举并成功打开输出端口。

### `.mid` 文件加载失败

- 使用实际存在的完整路径。
- 检查扩展名和文件权限。
- 文件必须不超过 16 MiB。
- 确认文件为 Format 0 或 Format 1，并使用普通 PPQ 时间格式。
- 查看 Unreal 日志中的 `Load MIDI file failed` 信息，交给技术人员定位。

### 暂停、停止或跳转后仍有持续音

播放器默认会发送 Note Off。仍有尾音时：

- 确认调用 `Stop` 时启用了 `Send All Notes Off`。
- 检查 Wwise 是否设置了很长的 Release。
- 检查同一个声音是否由另一个 MIDI 组件触发。
- 必要时调用 `Stop Midi Event` 停止该 Wwise Event 的 MIDI 播放。

### 升级 Wwise 后功能失效

AkMIDI 会修改 Wwise 集成源码。升级后必须由技术人员重新应用与新版匹配的改造，不能混用旧版本文件。

## 12. 当前限制

- 不支持 SysEx、MIDI Clock 和其他系统实时消息。
- 不支持可靠的外部输入到外部输出直通。
- 同一时间建议只使用一个组件处理外部 MIDI 输入。
- `On Message Received` 的 Delta Time 不能用于精确节拍。
- `.mid` 播放没有内置循环、完成事件、轨道静音/独奏和移调。
- `.mid` 播放跟随 Unreal 帧更新，卡顿时可能出现时序误差。
- Android 等未配置 MIDI 后端的平台不能直接使用外部 MIDI 设备。

## 13. 交付检查清单

把使用 AkMIDI 的关卡或蓝图交付给其他成员前，请确认：

- Wwise Event、SoundBank 和 Unreal 资产均已提交。
- 外部设备选择使用 `FMidiDevice.Port`。
- 每个 Note On 都有对应 Note Off。
- Actor 结束时会关闭 MIDI 设备。
- `.mid` 文件已包含在项目部署方案中，运行时路径有效。
- 文件播放器的失败返回值已有提示或备用处理。
- 目标平台已经实际测试，而不只是编辑器中可用。
