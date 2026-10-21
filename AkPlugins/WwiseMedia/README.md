# WwiseMedia

WwiseMedia 是 UE 侧独立运行时插件，将 Media Player 解码的音轨作为 Wwise 三维声源播放。定位、距离衰减、混响和遮挡由 Wwise 配置；插件负责采样、48 kHz Mono 转换、缓冲、播放控制及媒体时钟反馈。

开发源目录约定为 `G:/M2_Wwise/WwisePlugins/WwiseMedia`。完成场景验收后再将插件接入客户端 `Plugins/WwiseMedia`。

## 兼容环境

- 开发基线为 UE 5.8.3 和 Wwise Unreal Integration 2025.1.10。
- 插件描述文件依赖 `Wwise` 和 `WwiseSoundEngine`；不会修改它们的源码。
- 首版为 Windows、正常速度播放及每个屏幕一个独立 Media Player。其他平台需要重新编译和验证媒体后端。
- 支持 UE 重采样器可处理的 1 至 8 声道及 Float、Double、Int8、Int16、Int32 PCM。采样必须具有有效时间戳和正的 Duration。
- 不提供视频解码器、不替代 Wwise SoundBank、不自动把视频中的多个物体分离为独立声源。

## 蓝图接入

1. 在屏幕 Actor 上添加 **Wwise Media Audio Component**，把它放在屏幕或扬声器发声中心。
2. 创建或分配专用 Media Player，材质使用该播放器对应的 Media Texture。
3. 在组件继承的 `Ak Audio Event` 中选择一个以 Wwise Audio Input 为音源的播放事件。
4. 设置组件的 `Media Player`。运行时更换播放器使用 `Set Media Player`。
5. 调用组件的 `Open Media Source`，将 `Play When Opened` 设为 true。
6. 后续使用组件的 `Play Media`、`Pause Media`、`Seek Media` 和 `Stop Media`。
7. 通过 `On State Changed`、`On Playback Error` 和 `Get Debug Info` 观察状态。

不需要手动调用底层 `Post Associated Audio Input Event`。组件会在事件加载和音频预缓冲完成后调用它，并防止重复发布同一实例。

如果由现有业务负责打开媒体，可以在 `On Media Opened` 后调用组件的 `Play Media`。`Auto Play` 仅用于组件自动接管已就绪或后续打开的媒体，不是媒体文件路径设置。

## 必须满足的前提

同一 Media Player 只能有一个活动的 WwiseMedia 消费者。绑定在同一播放器上的原生 Media Sound 应停止并解绑；插件发现活动的 Media Sound 时会拒绝启动，以避免抢占主音频 Sink 和重复播放。`Native Audio Out` 必须关闭，最好在打开视频前设置；插件自身的 `Open Media Source` 会预先关闭它。

每个独立屏幕使用独立 Media Player 和组件。声音事件可以共用。多个屏幕共用一个解码器并进行 PCM 分发不在本版范围，插件会拒绝这种容易抢读的用法。

Wwise 中要配置有效的 3D Positioning、Attenuation 和 Listener。首轮验证时使用低 Spread，并给视频声音保留足够声部，核对低于阈值时的 Continue to play 行为。若声音因虚拟化或 Voice Limit 长时间不再请求 PCM，组件会进入 Error 并暂停自己接管的媒体，避免静默堵死媒体时钟。

混响、遮挡滤波和 Rooms/Portals 使用现有 Wwise 场景接入。UE 原生混响设置不会自动作用于直接送入 Wwise 的声音。

## 当前客户端资源

客户端已有 `Audio_Input_ScreenVideo` 音源、`Play_Audio_Input_ScreenVideo` 事件及相应总线。可以在接入时配置组件使用该事件；插件没有硬编码它的资产路径。

现有衰减 `AMB_50m` 的实际最大距离值为 2000，应核对项目单位及缩放。现有视频总线带固定的 `Hall_Large` 混响发送；引入动态房间混响时要检查是否叠加。

## 缓冲与同步

默认输出为 48 kHz Mono，Wwise 根据空间位置渲染到设备的立体声或环绕声。解码与下混重采样在专用工作线程执行，Wwise 回调只读取预分配的 SPSC PCM 数据块。

`Prebuffer Milliseconds` 默认 20 ms；`Pcm Buffer Blocks` 默认 8，每块最多 512 帧，容量有界。Media Sample 队列另由 `Maximum Media Packets` 限制。短尾段允许在生产端暂时空闲后排出，避免小片段一直等待完整预缓冲。

Seek 及媒体后端 Flush 会更新代次，旧 PCM 不再输出。暂停时输出静音且保留未消费的音频；恢复继续消费。正常结束会排出已解码的有效尾段后停止输入实例。

组件将真实消费的媒体 PTS 回传给主音频 Sink 和 Media Player Facade。解码完成或补零帧不会冒充已播放媒体位置。`Output Latency Milliseconds` 是下游延迟的校准项，默认 0；应使用目标设备的同步标记视频测量后设置，不代表已有实测音画同步保证。

播放控制和调试查询需在游戏线程调用。首版拒绝非 1.0 倍速；不支持倒放与直播同步。

## 编译与测试

在含有匹配 Wwise 插件的 UE 工程中启用 WwiseMedia，正常构建 Editor 目标即可。需要编译隔离宿主时，使用 `Tools/Build.ps1`，指定引擎和 Wwise 两个插件目录。脚本把 WwiseMedia 作为外部插件构建，不会把它安装到客户端 Plugins。

测试源包括：

- `Tests/PcmQueueTests.cpp`：无需 UE 的有界 SPSC 队列测试，检查 FIFO、容量、完整数据和 10 万块并发传输。
- `WwiseMedia.Bridge.ResampleAndPause`：引擎内 44.1 kHz Stereo 到 48 kHz Mono 转换、帧数、暂停、时钟与关闭后回调。
- `WwiseMedia.Bridge.FlushRejectsOldAudio`：Seek/Flush 后拒绝旧音频和旧时间戳。

引擎内测试使用合成 PCM，不需要播放真实视频或启动 Wwise 输出设备。它们不能替代真实媒体后端、Wwise 事件、空间听感和目标设备音画同步验收。具体执行结果及剩余用例见 `Docs/DevelopmentStatus.md`。

## 接入客户端

复制插件描述文件、Source、README、Docs 和需要的 Tools。Intermediate、Saved、测试生成的 exe/obj 不属于交付源码。若携带预编译 Binaries，其引擎版本、配置和 Build ID 必须匹配目标环境；推荐在客户端重新编译。

接入后先使用单屏幕本地测试片验证方向、距离、暂停和 Seek，再验证长片、多屏幕、遮挡、混响、关卡销毁和打包版。本版代码可以开始集成测试，正式使用以这些场景验收为准。
