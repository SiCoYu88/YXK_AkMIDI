# Music Segment Custom Cue Generator

通过 WAAPI 按节拍间隔为 Wwise Music Segment 批量添加带自增索引的 Custom Cue。

## 免安装版本

将 `dist\WwiseCustomCueTool_Windows_x64.zip` 解压后，直接运行 `WwiseCustomCueTool.exe`。目标电脑无需安装 Python、PySide6 或 waapi-client，但仍需启动 Wwise、打开工程并启用 WAAPI。`custom_cue_event_types.json` 必须与 EXE 放在同一目录。

开发环境重新构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WwiseWAAPI\build_exe.ps1
```

## 环境

1. 启动 Wwise 并打开目标工程。
2. 在 Wwise User Preferences 中启用 WAAPI。
3. 安装 Python 依赖：

```powershell
python -m pip install -r .\Tools\WwiseWAAPI\requirements.txt
```

## 图形界面

双击运行：

```text
Tools\WwiseWAAPI\launch_custom_cue_gui.bat
```

也可以从 PowerShell 启动：

```powershell
python .\Tools\WwiseWAAPI\music_segment_custom_cue_gui.py
```

界面启动后会自动连接当前 Wwise 工程并列出全部 MusicSegment：

1. 默认跟随 Wwise 当前选择；也可以使用名称/完整路径模糊搜索或正则搜索，再从下拉列表选择 MusicSegment。
2. 从“事件类型”下拉框选择事件类型，再以分数配置 beat 间隔（例如 `1/4`、`1/2`、`1`）、起始索引及其他可选参数。界面固定从 0 beat 开始。
3. 点击“预览”检查 Cue 名称、时间以及 End Cursor/Exit Cue；勾选循环时显示执行后的目标位置。
4. 点击“添加 Custom Cue”，核对确认窗口后执行。

“跟随 Wwise 选择”、“循环”、“替换相同前缀的已有 Custom Cue”和“执行后保存 Wwise 工程”默认选中。整次执行位于一个 Wwise Undo Group 中；取消自动保存后，可以在 Wwise 中检查并一次撤销。

Wwise 中选择 MusicSegment、MusicTrack 或其下音频对象时，工具会自动定位所属 MusicSegment，并直接读取该 Segment 的 `Tempo` 配置。取消“跟随 Wwise 选择”后仍可点击“使用 Wwise 当前选择”进行一次手动同步。

搜索默认使用不区分大小写的模糊匹配，支持用空格组合多个关键词；切换到“正则”后按正则表达式匹配 MusicSegment 名称或完整路径。无效正则不会清空当前列表。

事件类型配置位于 `Tools\WwiseWAAPI\custom_cue_event_types.json`：

```json
{
  "event_types": [
    {
      "event_type": "示例事件",
      "custom_cue_name": "Example_"
    }
  ]
}
```

`event_type` 是下拉框显示的事件类型，`custom_cue_name` 是生成 Cue 的名称前缀。以上示例会生成 `Example_0、Example_1、Example_2…`。两项在配置文件中都必须唯一，点击界面“刷新”会重新载入配置。

勾选“循环”后，工具会在 MusicSegment 最晚 MusicClip/MIDI Clip 的内容结束位置添加名为 `Loop` 的 Custom Cue，并将 End Cursor 和 Exit Cue 都设置为 `Loop + 100 ms`。工具会重新读取并验证两者；验证失败则回滚。`Loop` 独立于事件类型，已有同名 User Cue 会自动替换。

## 命令行使用

先预览，不修改 Wwise：

```powershell
python .\Tools\WwiseWAAPI\add_music_segment_custom_cues.py `
  --segment '\Containers\Music\Default Work Unit\XG_Left_Right_Base_Loop_150' `
  --interval-beats 1/2 `
  --dry-run
```

确认后创建 Cue：

```powershell
python .\Tools\WwiseWAAPI\add_music_segment_custom_cues.py `
  --segment '\Containers\Music\Default Work Unit\XG_Left_Right_Base_Loop_150' `
  --interval-beats 1/2 `
  --start-index 0
```

默认生成 `CC_0`、`CC_1` 等名称，从 0 beat 开始且不进行索引补零，按指定间隔排列，并且不会在 Exit Cue 位置创建重叠 Cue。默认不保存工程，可先在 Wwise 中检查并通过一次 Undo 撤销全部操作；确认后手动保存，或在命令中加入 `--save`。

再次生成相同前缀的 Cue 时，使用 `--replace-existing` 删除并替换该 Music Segment 中同前缀的 User Cue。该选项不会删除 Entry Cue、Exit Cue 或其他前缀的 Custom Cue。

查看全部参数：

```powershell
python .\Tools\WwiseWAAPI\add_music_segment_custom_cues.py --help
```

常用参数：

- `--start-beat 0.5`：命令行模式下可让第一个 Cue 从半拍位置开始；图形界面固定为 0。
- `--tempo 150`：覆盖 Wwise 中读取到的 BPM。
- `--end-ms 12800`：覆盖 Music Segment 的结束位置。
- `--include-end`：当时间恰好落在结尾时也创建 Cue。
- `--loop`：在 MusicSegment 内容末尾添加或更新 `Loop` Cue，并保证 End Cursor/Exit Cue 晚 100 ms。
- `--replace-existing`：替换同前缀的已有 Custom Cue。
- `--waapi-url`：连接非默认 WAAPI 端口。
