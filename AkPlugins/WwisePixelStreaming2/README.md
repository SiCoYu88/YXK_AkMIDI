# WwisePixelStreaming2

将 Wwise 最终输出混音通过 Pixel Streaming 2 的 Audio Producer 发送到浏览器。实现针对 UE 5.7 与 Wwise 2025.1，浏览器前端、信令服务器和 SFU 无需修改。

## 安装

1. 将整个 `WwisePixelStreaming2` 目录复制到 UE 项目的 `Plugins` 目录。
2. 确保项目已安装 Wwise 2025.1 和 `AsyncInputSystem`，并启用 `Wwise`、`PixelStreaming2`、`AsyncInputSystem` 和 `WwisePixelStreaming2` 插件。
3. 重新生成项目文件并编译。

插件描述已设置为默认启用。仍建议确认项目的 `.uproject` 中没有将它显式禁用；成功加载后，启动日志的插件挂载列表中应包含 `WwisePixelStreaming2`。C++ 插件必须随项目重新编译和打包，仅复制到已经生成的运行包中不会生效。

插件默认启用桥接，并自动绑定 Pixel Streaming 2 的默认 Streamer。Wwise 主输出仍会正常播放。

## 配置

在项目的 `Config/DefaultGame.ini` 中覆盖配置：

```ini
[WwisePixelStreaming2]
Enabled=true
ForwardRemoteInputToAsyncInput=true
StreamerId=
OutputDeviceId=0
QueueSlots=8
MaxFrames=2048
MaxChannels=16
Gain=1.0
StatusLogIntervalSeconds=5.0
CaptureStallTimeoutSeconds=2.0
```

- `StreamerId` 为空时使用 Pixel Streaming 2 默认 Streamer。
- `ForwardRemoteInputToAsyncInput=true` 时，远程键鼠事件会旁路复制给 `AsyncInputSystem`；Pixel Streaming 2 原输入处理保持不变。
- `OutputDeviceId=0` 表示 Wwise 主输出；其他值使用 `AddOutput` 或 `GetOutputID` 返回的设备 ID。
- 队列满时丢弃最新缓冲区，绝不阻塞 Wwise 实时音频线程。
- `MaxFrames` 或 `MaxChannels` 小于实际 Wwise 输出时，对应缓冲区会被拒绝。
- `Gain` 在发送线程应用，范围为 0 到 8。
- `StatusLogIntervalSeconds` 控制状态日志间隔，设为 0 可关闭周期日志。
- `CaptureStallTimeoutSeconds` 控制捕获停滞后的自动重绑阈值，设为 0 可关闭自动重绑。

## 运行验证

启动时日志应依次出现：

```text
Attached Wwise audio producer to Pixel Streaming 2 streamer 'DefaultStreamer'.
Registered Wwise capture callback for OutputDeviceId=0 at 48000 Hz.
```

仅播放 Wwise Event 时，浏览器应能听到对应音频。若项目已通过 Wwise AudioLink 将同一输出送入 UE Audio Mixer，请关闭其中一条路径，否则会出现重复音频或梳状滤波。

关闭时日志会输出 Captured、Pushed、Dropped、Rejected 计数，可用于判断队列容量和输入格式是否合适。

运行时可执行 `WwisePixelStreaming2.Status` 立即输出状态。判断静音位置：

- `CaptureRegistered=false`：Wwise 没有初始化或 Capture Callback 注册失败。
- `Captured=0`：Wwise 回调没有触发，通常是输出设备 ID 不正确。
- `Captured>0` 且 `NonSilent=0`：所捕获的 Wwise 输出本身是静音，应检查 Bus 路由或 Secondary Output。
- `NonSilent>0`、`Pushed>0` 但浏览器仍静音：检查 `StreamerAttached`、`PS2DisableTransmitAudio` 和 `PS2AudioGain`。
- `Rejected>0`：实际帧数或声道数超过 `MaxFrames`/`MaxChannels`。

插件只在目标 Streamer 就绪后注册 Wwise Capture Callback，避免绑定到启动阶段的临时主输出。如果 Wwise 替换或重建输出导致回调停滞，插件会自动重绑；`Rebinds` 表示成功重绑次数，`LastCaptureAge` 表示距离最后一次回调的秒数。也可执行 `WwisePixelStreaming2.RebindCapture` 强制立即重绑。

编辑器或游戏退出时，插件会在 `OnEnginePreExit` 阶段提前注销 Pixel Streaming 2 委托、恢复输入处理器并停止 Wwise 捕获。正常退出日志应包含 `Stopped during engine pre-exit`，且不应出现 `Failed to unregister Wwise capture callback` 或 `DetachRemoteInputBridge` 访问冲突。

可将 `PixelStreaming2.DumpDebugAudio` 设为 `1`，播放一段声音后设回 `0`，让 Pixel Streaming 2 写出混音 WAV，以区分服务端混音与浏览器播放问题。
