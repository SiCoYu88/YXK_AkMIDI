# AkWwisePixelStreaming

将 Wwise 最终输出混音通过 Pixel Streaming 2 的 Audio Producer 发送到浏览器。实现针对 UE 5.7 与 Wwise 2025.1，浏览器前端、信令服务器和 SFU 无需修改。

## 安装

1. 将整个 `AkWwisePixelStreaming` 目录复制到 UE 项目的 `Plugins` 目录。
2. 确保项目已安装 Wwise 2025.1 集成，并启用 `Wwise`、`PixelStreaming2` 和 `AkWwisePixelStreaming` 插件。
3. 重新生成项目文件并编译。

插件默认启用桥接，并自动绑定 Pixel Streaming 2 的默认 Streamer。Wwise 主输出仍会正常播放。

## 配置

在项目的 `Config/DefaultGame.ini` 中覆盖配置：

```ini
[AkWwisePixelStreaming]
Enabled=true
StreamerId=
OutputDeviceId=0
QueueSlots=8
MaxFrames=2048
MaxChannels=16
Gain=1.0
```

- `StreamerId` 为空时使用 Pixel Streaming 2 默认 Streamer。
- `OutputDeviceId=0` 表示 Wwise 主输出；其他值使用 `AddOutput` 或 `GetOutputID` 返回的设备 ID。
- 队列满时丢弃最新缓冲区，绝不阻塞 Wwise 实时音频线程。
- `MaxFrames` 或 `MaxChannels` 小于实际 Wwise 输出时，对应缓冲区会被拒绝。
- `Gain` 在发送线程应用，范围为 0 到 8。

## 运行验证

启动时日志应依次出现：

```text
Capturing Wwise output at 48000 Hz.
Attached Wwise audio producer to Pixel Streaming 2 streamer 'DefaultStreamer'.
```

仅播放 Wwise Event 时，浏览器应能听到对应音频。若项目已通过 Wwise AudioLink 将同一输出送入 UE Audio Mixer，请关闭其中一条路径，否则会出现重复音频或梳状滤波。

关闭时日志会输出 Captured、Pushed、Dropped、Rejected 计数，可用于判断队列容量和输入格式是否合适。
