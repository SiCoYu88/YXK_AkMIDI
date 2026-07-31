# Wwise Music Segment Custom Cue Tool

本工具通过 Wwise Authoring API（WAAPI），按指定 beat 间隔为 MusicSegment 批量添加带自增索引的 Custom Cue。

## 免安装使用

解压完整压缩包后，直接运行 `WwiseCustomCueTool.exe`。不需要安装 Python、PySide6、waapi-client，也不需要管理员权限。

目录中必须保留以下文件：

```text
WwiseCustomCueTool.exe
custom_cue_event_types.json
README.md
```

不要只复制 EXE；事件类型下拉框依赖同目录下的 `custom_cue_event_types.json`。

## 使用条件

- Windows x64 系统。
- 已安装并启动 Wwise。
- Wwise 已打开目标工程。
- Wwise User Preferences 中已启用 WAAPI。
- 默认连接地址为 `ws://127.0.0.1:8080/waapi`。

如果同时运行多个 Wwise 实例，必须确保工具连接的端口对应正确的工程。

## 快速操作

1. 启动 Wwise，打开目标工程并启用 WAAPI。
2. 运行 `WwiseCustomCueTool.exe`。
3. 点击“刷新”，确认工具已经找到工程中的 MusicSegment。
4. 在 Wwise 中选择 MusicSegment、MusicTrack 或音频源，工具会自动同步所属 MusicSegment；也可以通过搜索或下拉框手动选择。
5. 选择事件类型并设置生成参数。
6. 点击“预览”，检查 Cue 名称、Beat、时间以及 End Cursor/Exit Cue；勾选循环时显示执行后的目标位置。
7. 点击“添加 Custom Cue”，核对确认窗口后执行。

## 界面参数

### WAAPI

Wwise Authoring API 的连接地址。

- 默认值：`ws://127.0.0.1:8080/waapi`
- Wwise 使用默认 WAAPI 端口时不需要修改。
- 仅在 Wwise 改用了其他端口或连接其他电脑上的 Wwise 时修改。

### 刷新

重新连接 Wwise，并同时重新载入：

- 当前工程中的全部 MusicSegment。
- EXE 同目录下的 `custom_cue_event_types.json`。
- Wwise 当前选中的对象。

修改事件类型配置文件后，需要点击“刷新”才能更新下拉框。

### 使用 Wwise 当前选择

使用 Wwise 当前选中的对象定位 MusicSegment。

可以选中：

- MusicSegment 本身。
- MusicSegment 下的 MusicTrack。
- MusicTrack 下的音频对象。

工具会向上查找最近的 MusicSegment，并直接读取该 Segment 的 Tempo 与 EndCursor。这样可以避免同名 MusicSegment 之间 BPM 读取错误。

### 跟随 Wwise 选择

默认选中。工具通过 WAAPI 实时监听 Wwise 当前选择：

- 选择 MusicSegment 时直接同步。
- 选择 MusicTrack 或其下音频对象时，向上定位所属 MusicSegment。
- 多选涉及多个 MusicSegment 时保留工具当前选择。
- Wwise 断开后自动等待重连，不会阻止工具关闭。
- 自动同步的目标被搜索条件隐藏时，会清除搜索条件并显示目标。

取消选中后，Wwise 的选择变化不再修改工具当前选择；仍可点击“使用 Wwise 当前选择”执行一次手动同步。工具中的手动选择不会反向修改 Wwise。

### 搜索

根据 MusicSegment 名称和完整路径过滤下拉列表，不会重新请求 Wwise：

- `模糊`：默认模式，不区分大小写；输入多个空格分隔的关键词时，名称或路径必须同时包含所有关键词。
- `正则`：使用不区分大小写的正则表达式匹配名称或完整路径。
- 正则表达式无效时输入框会提示错误，并保留当前列表和选择。

### MusicSegment

选择要添加 Custom Cue 的目标 MusicSegment。下拉项会显示：

- Wwise 对象完整路径。
- MusicSegment Tempo（BPM）。
- MusicSegment EndCursor（毫秒）。

### 事件类型

事件类型来自 `custom_cue_event_types.json`。每种事件类型对应一个 Custom Cue 名称前缀。

例如 `custom_cue_name` 为 `Note_` 时，起始索引为 0 将生成：

```text
Note_0
Note_1
Note_2
...
```

事件类型只影响按 beat 批量生成的事件 Cue，不影响独立的 `Loop` Cue。

### 间隔（beat）

相邻事件 Cue 之间的 beat 间隔，使用分数表示。

| 值 | 含义 |
|---|---|
| `1/8` | 每八分之一个 beat 添加一个 Cue |
| `1/4` | 每四分之一个 beat 添加一个 Cue |
| `1/2` | 每半个 beat 添加一个 Cue，默认值 |
| `1` | 每 1 个 beat 添加一个 Cue |
| `2` | 每 2 个 beat 添加一个 Cue |
| `4` | 每 4 个 beat 添加一个 Cue |

工具从 MusicSegment 的 0 beat 开始生成。时间计算使用 MusicSegment 的 Tempo，例如 150 BPM 时：

```text
1 beat = 60000 / 150 = 400 ms
1/2 beat = 200 ms
```

### 起始索引

第一个事件 Cue 使用的数字索引。

- 默认值：`0`
- 名称不补零。
- `Note_` 配合起始索引 0，生成 `Note_0、Note_1、Note_2…`。
- 起始索引 10，生成 `Note_10、Note_11、Note_12…`。

起始位置固定为 MusicSegment 的 0 beat，界面中不可修改。

### 覆盖 BPM（可选）

留空时，直接使用所选 MusicSegment 的 Tempo 配置。

只有在需要临时使用其他 BPM 计算普通事件 Cue 时才填写。覆盖值只影响本次 Cue 时间计算，不会修改 Wwise 中 MusicSegment 的 Tempo。

发现 BPM 不正确时，建议先在 Wwise 中选中目标 MusicSegment 或其下的 MusicTrack，再点击“使用 Wwise 当前选择”，不要直接使用覆盖值掩盖选择错误。

### 覆盖结束 ms（可选）

留空时，使用 MusicSegment 的 EndCursor 作为普通事件 Cue 的生成范围。

填写后，只改变普通事件 Cue 的生成结束范围。独立 `Loop` Cue 始终以 MusicSegment 内最晚 MusicClip/MIDI Clip 的内容结束位置为准；End Cursor 和 Exit Cue 随 Loop 规则更新。

### 在结尾恰好命中时也添加 Cue

默认不选中。

- 未选中：只生成时间严格小于结束位置的普通事件 Cue。
- 选中：当间隔恰好落在结束位置时，也在结束位置添加一个普通事件 Cue。

选中后可能与 Exit Cue 位于相同时间，请先通过预览确认。

### 替换相同前缀的已有 Custom Cue

默认选中。

执行时先删除目标 MusicSegment 中名称以当前 `custom_cue_name` 开头的已有 User Cue，再创建新的事件 Cue。

不会删除：

- Entry Cue。
- Exit Cue。
- 其他事件类型前缀的 Cue。
- 名为 `Loop` 的独立 Cue。

取消选中后，如果存在同名 Cue，工具会停止执行并提示名称冲突，不会生成部分结果。

### 循环

默认选中。

选中后，工具会在 MusicSegment 内最晚 MusicClip/MIDI Clip 的内容结束位置添加一个名为 `Loop` 的 Custom Cue，并把 End Cursor 设置到 Loop 后 100 ms。

```text
Loop 时间 = MusicSegment 内容结束时间
End Cursor = Loop 时间 + 100 ms
Exit Cue = Loop 时间 + 100 ms
```

- `Loop` 不属于事件类型下拉框。
- `Loop` 不带自增索引。
- 已有同名 User Cue 时，会自动删除并在正确时间重新创建。
- “覆盖结束 ms”不会改变 Loop、End Cursor 或 Exit Cue 的位置。
- 工具会同时设置 End Cursor 和 Exit Cue，并重新读取两者；任一时间不正确时，整次操作回滚。
- 重复执行时仍使用内容结束位置，因此不会不断把 End Cursor 向后延长。

### 执行后保存 Wwise 工程

默认选中。

- 选中：添加成功后立即保存 Wwise 工程和对应 Work Unit。
- 未选中：修改保留在 Wwise 内存中，需要手动保存。

整次添加操作位于一个 Wwise Undo Group 中。即使自动保存，也可以在 Wwise 中执行一次 Undo 撤销本次生成，然后再次保存。

## 事件类型配置文件

配置文件名固定为：

```text
custom_cue_event_types.json
```

该文件必须使用 UTF-8 编码，并与 `WwiseCustomCueTool.exe` 放在同一目录。

### 配置格式

```json
{
  "event_types": [
    {
      "event_type": "唱片音符",
      "custom_cue_name": "Note_"
    },
    {
      "event_type": "节拍事件",
      "custom_cue_name": "Beat_"
    }
  ]
}
```

### 字段说明

| 字段 | 必填 | 说明 |
|---|---|---|
| `event_types` | 是 | 事件类型数组，至少包含一项 |
| `event_type` | 是 | 界面下拉框显示的事件类型名称 |
| `custom_cue_name` | 是 | Custom Cue 名称前缀，工具会在后面追加自增索引 |

### 配置规则

- `event_type` 不能为空并且不能重复。
- `custom_cue_name` 不能为空并且不能重复。
- 建议名称只使用字母、数字和下划线，例如 `Note_`。
- JSON 最后一项后面不能保留多余逗号。
- 修改并保存配置后，在工具中点击“刷新”。
- 配置错误时，工具不会执行生成操作，并会显示错误原因。

## 生成示例

MusicSegment 配置：

```text
Tempo: 150 BPM
内容结束: 12800 ms
执行前 EndCursor: 12800 ms
事件类型: 唱片音符 -> Note_
间隔: 1/2 beat
起始索引: 0
循环: 开启
```

生成结果：

```text
Note_0   0 ms
Note_1   200 ms
Note_2   400 ms
...
Note_63  12600 ms
Loop     12800 ms
EndCursor 12900 ms
Exit Cue  12900 ms
```

普通事件 Cue 默认只生成到 Loop 之前，不会生成在 12800 ms 的 Loop 位置。End Cursor 与 Exit Cue 比 Loop 晚 100 ms。

## 执行安全

- 第一次对正式工程使用前，建议确认工程已纳入版本控制或完成备份。
- 每次执行前先点击“预览”，检查 MusicSegment、BPM、Cue 数量和 Loop 时间。
- 执行按钮会显示最终确认窗口，取消确认不会修改 Wwise。
- 执行中任何 WAAPI 操作失败时，工具会回滚整个 Undo Group，避免只生成部分 Cue。
- 不要在工具执行过程中关闭 Wwise、切换工程或关闭工具。

## 常见问题

### 无法连接 Wwise

检查：

1. Wwise 是否已经启动并打开工程。
2. Wwise User Preferences 是否启用了 WAAPI。
3. WAAPI 地址和端口是否正确。
4. Wwise 是否停留在未关闭的模态对话框。
5. 防火墙是否阻止了自定义的远程 WAAPI 连接。

### MusicSegment 列表为空

- 确认当前 Wwise 工程中存在 MusicSegment。
- 点击“刷新”。
- 检查工具是否连接到了另一个 Wwise 实例。

### BPM 与 Wwise 不一致

1. 在 Wwise 中选中目标 MusicSegment、MusicTrack 或其下音频对象。
2. 点击“使用 Wwise 当前选择”。
3. 确认界面显示的完整 MusicSegment 路径。

工程中可能存在同名 MusicSegment，工具以完整路径和 GUID 区分对象。

### 事件类型配置读取失败

- 确认 `custom_cue_event_types.json` 与 EXE 在同一目录。
- 确认文件是有效 JSON。
- 确认 `event_type` 和 `custom_cue_name` 没有重复或空值。
- 修改完成后点击“刷新”。

### Windows 阻止运行 EXE

本工具未进行代码签名，首次运行可能显示“未知发布者”或 Windows SmartScreen 提示。请确认文件来自可信内部渠道后，再选择允许运行。

### 双击没有反应

- 先将 ZIP 完整解压，不要直接在压缩包内运行。
- 确认 EXE 与配置文件位于同一目录。
- 检查安全软件是否隔离了 EXE。
- 查看 Wwise 是否已经启动并启用 WAAPI。

### 先关闭 Wwise 后，工具提示 WAAPI 操作未结束

Wwise 在 WAAPI 请求进行中被关闭时，底层连接可能无法正常返回。再次关闭工具窗口，会出现“WAAPI 操作未结束”确认框：

- 选择“是”：立即强制退出工具。
- 选择“否”：返回工具窗口并继续等待。

如果 Wwise 仍然打开并正在写入，请先确认工程状态再强制退出；如果 Wwise 已经关闭，可以直接选择“是”。

## 版本分发

向其他使用者分发时，优先发送完整的 `WwiseCustomCueTool_Windows_x64.zip`。不要只发送 EXE，以免缺少事件类型配置文件。
