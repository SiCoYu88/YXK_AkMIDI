# WwiseMedia 客户端场景接入记录

日期：2026-10-20。使用 VibeUE Python 服务和 Unreal 原生 MCP 完成资产、材质、蓝图连线与 PIE 检查。

## 入口

资源目录：`/Game/Developers/yuxikuo/WwiseMedia`。

打开 `Map_WwiseMedia`，把视口放在屏幕前，选择播放下拉菜单中的 **从当前摄像机位置播放 / Current Camera Location**。本次测试摄像机坐标为 `(0, 0, 230)`，旋转为零。点击游戏视口，使用 SpectatorPawn 的 WASD、鼠标移动和转向；Esc 结束。

必须使用带起点位置的播放方式：项目 `UClientGameInstance::OnStart` 在 `HasPlayWorldPlacement()` 为真时跳过业务初始化。普通 PlayerStart PIE 会进入 TypeScript 业务启动流程并切换到默认地图，干扰单屏测试。没有修改项目的 GameInstance 或默认启动地图配置。

## 资源关系

以下资产均位于上述资源目录。

| 资产 | 用途 |
| --- | --- |
| `Map_WwiseMedia` | 测试地图，屏幕位于 `(800, 0, 230)` |
| `BP_WwiseMediaScreen` | 独立屏幕 Actor，包含 ScreenRoot、Screen、WwiseMediaAudio |
| `GM_WwiseMedia` | 此地图的 GameMode，DefaultPawnClass 为 SpectatorPawn |
| `MP_WwiseMedia` | 屏幕专用 MediaPlayer；NativeAudioOut=false、PlayOnOpen=false、Looping=true |
| `FMS_WwiseMedia` | 本地测试视频源，当前路径 `./Movies/WwiseMedia_TestTone.mp4` |
| `MT_WwiseMedia` | 绑定专用 MediaPlayer 的 MediaTexture |
| `M_WwiseMediaScreen` | 双面 Unlit 材质，MediaTexture 的 RGB 连接 Emissive |

原有的 `BP_WwiseMediaPlayer` 是 Character 蓝图，保留原状；实际屏幕与控制逻辑放在新增的 `BP_WwiseMediaScreen`。

当前视频文件：`Content/Movies/WwiseMedia_TestTone.mp4`，48 秒，复制 VideoFile01 的 H.264 640×360 画面，另加 AAC 48 kHz 双声道合成提示音。提示音为 660 Hz，每 2 秒发声约 1.5 秒，供空间效果测试。原文件 `Content/Movies/VideoFile01.mp4` 保留，其音轨实际为全零静音。插件下混后向 Wwise 提供 48 kHz Mono，最终输出声道由 Wwise 空间渲染决定。

插件安装位置：`Plugins/WwisePlugins/WwiseMedia`，模块 `WwiseMediaRuntime`；已在 `M2_Client.uproject` 中启用，并完成当前引擎的运行时模块编译。

## 屏幕蓝图

`WwiseMediaAudio` 组件附着到 Screen，位置为屏幕中心。

- MediaPlayer：`MP_WwiseMedia`。
- AkAudioEvent：`/Game/WwiseAudio/Events/AudioInput/AudioInput/Audio_Input_ScreenVideo/Play_Audio_Input_ScreenVideo`。
- AutoPlay=false，由蓝图明确调用 OpenMediaSource 控制启动。
- OcclusionRefreshInterval=0.2 秒。
- 保留默认缓冲设置：20 ms 预缓冲、8 个 PCM 块、16 个媒体包。

EventGraph：BeginPlay → 组件 OpenMediaSource(FMS_WwiseMedia, PlayWhenOpened=true)；EndPlay → StopMedia(ClosePlayer=true)。组件 OnPlaybackError 的 Message 连接 PrintString，显示并记录错误。没有手动发布 Audio Input Event，也没有添加原生 MediaSoundComponent。

对屏幕 Actor 调用以下公开蓝图函数，即可接入业务 UI 或关卡逻辑。本场景没有额外绑定播放控制快捷键。

| 函数 | 行为 |
| --- | --- |
| `OpenVideo() → Success` | 打开指定 FMS，打开完成后自动播放；停止关闭后重新启动也用它 |
| `PlayVideo() → Success` | 恢复已打开媒体 |
| `PauseVideo() → Success` | 暂停媒体与有效 PCM 消费 |
| `SeekVideo(Time: Timespan) → Success` | 跳转，保留跳转前的暂停/播放意图；完成异步跳转后恢复对应状态 |
| `StopVideo()` | 停止 Wwise 输入并关闭 MediaPlayer |

Success 表示请求接受情况；最终异步状态通过组件 OnStateChanged、OnPlaybackError、GetDebugInfo 查询。更换视频时修改 FMS，或在业务蓝图中向组件 OpenMediaSource 传入新的 MediaSource。

每个独立屏幕必须创建独立 MediaPlayer 以及相应 MediaTexture/材质引用。本示例 BP 默认引用同一个播放器，不能直接复制多个实例同时播放；先为新实例分配独立资源。

## Wwise 资源核对

复用客户端现有事件与 SoundBank，没有改动 Wwise 工程或重新生成公共 Bank。

- `H:/H3D_M2_Stream/client_wwise/Containers/System.wwu` 中对应声音采用 Wwise Audio Input，已配置 `3DSpatialization=1`。
- 声音引用衰减 `AMB_50m`。生成的事件元数据和运行时组件读回最大衰减半径均为 **2000 UE 单位**；按默认 1 cm 单位为 20 米，不能按资源名称理解为 50 米。
- 输出总线已有 `Hall_Large` 固定混响发送，音量为 -15 dB。动态 Rooms/Portals、混响区及几何遮挡需沿用项目 Wwise 场景接入；本关卡未新建这些环境。

## 本次验证结果

环境：UE 5.8.3（本机引擎目录名为 UE_5.7_T3）、Wwise Integration 2025.1.10、Windows Editor PIE。

| 检查 | 观测 |
| --- | --- |
| 插件加载与编译 | WwiseMediaRuntime 编译、链接并在编辑器加载成功 |
| 蓝图编译 | BP_WwiseMediaScreen、GM_WwiseMedia 均零错误、零警告；返回节点生成的重复执行引脚通过刷新节点修复 |
| 视频画面 | 游戏视口截图确认屏幕显示真实视频帧 |
| 音频输入 | Playing，PlayingID=15；输入 48000 Hz / 2 声道，PTS 持续前进 |
| 暂停 | 两次查询间隔约 13.5 秒；媒体时间均为 3.8666666 秒，消费 PTS 均为 3.8933333 秒 |
| 暂停中 Seek | 跳到 10.0 秒，完成后仍为 Paused；旧消费时间戳清空，代次从 2 更新到 4 |
| 恢复 | 同一 PlayingID=15 继续；约 36.7 秒后消费 PTS=46.672 秒 |
| 远距离 | 听者移到 `(-4200,0,230)`，距屏幕 5000 单位；约 32.8 秒后仍 Playing，经过视频循环仍消费 PCM，无回调超时 |
| 停止 | Closed，PlayingID=0，MediaPlayer 关闭，缓冲清空 |
| 再打开 | 自动回到 Playing，新 PlayingID=16，消费 PTS 正常前进 |
| 播放中 Seek | 跳到 10 秒后回到 Playing；后续 PTS=10.704、11.8666666 秒 |
| 左右位置 | 听者位于 `(800,-800,230)` 和 `(800,800,230)` 时状态正常，PTS 前进 |
| 缓冲 | 采样点约 74.67 ms；记录中的 UnderrunFrames、RejectedSamples 均为 0，LastError 为空 |
| 单一音频路径 | 运行时关卡中原生 MediaSoundComponent 数量为 0 |
| 结束 PIE | MediaPlayer 读回 IsPlaying=false、IsClosed=true；后台节流恢复，目标资产与地图保存完成 |

原始记录：`Saved/Codex/WwiseMedia/runtime_evidence.jsonl`。
画面截图：`Saved/VibeUE/Captures/capture-game-20261020-201433-602.png`。
编辑器日志：`Saved/Logs/WwiseMediaIntegration.log`。

这些检查证明真实视频音轨进入 Wwise、控制流程和位置变化期间持续工作；没有记录声卡输出、测量声道电平或进行人工听感评审，因此不把位置变化等同于已完成方向、衰减、混响听感验收。媒体时钟与消费 PTS 的差值也不等同于目标设备音画同步实测。

## 后续验收

使用耳机及 Wwise Profiler 复核方向、衰减曲线、固定混响和遮挡；再检查动态房间混响叠加、长片、多屏独立播放器及目标设备同步标记。当前资源位于 Developers 目录，打包测试前需确认 Cook 收录策略或迁移到正式内容目录，再验证打包版。

首次普通 PIE 触发业务流程加载了默认地图，VibeUE 检测到驻留地图；保存测试资产后重启专用测试编辑器以清理该状态。后续复测使用上述带起点位置的入口。

## 2026-10-20 黑屏修复

用户重开编辑器后报告没有视频画面。复现时组件为 Playing、音频 PTS 前进，但 MT_WwiseMedia 的播放器为空，尺寸为 2×2，屏幕黑色。

根因是初次创建时调用的 `MediaTexture.set_media_player()` 只更新运行时的 CurrentPlayer，不写入资产持久化的 MediaPlayer 属性。首次会话能看到视频，重开后绑定丢失。已把 `MT_WwiseMedia.media_player` 编辑器属性设置为 `MP_WwiseMedia` 并保存；创建脚本与检查脚本也同步修正，后续检查同时断言持久化属性和运行时绑定。此次修复不改变视频材质采样模式。

修复后关闭并重开编辑器（PID 35872），未重新设置绑定即读回正确的播放器；PIE 中纹理尺寸为 640×360，截图确认视频帧正常，Wwise 为 Playing、PlayingID=1，消费 PTS 正常前进，LastError 为空。此轮采样记录 UnderrunFrames=3、RejectedSamples=0；不沿用首次测试的零欠载结论。截图为 `Saved/VibeUE/Captures/capture-game-20261020-205634-393.png`，日志为 `Saved/Logs/WwiseMediaBindingFix.log`。复测后已结束 PIE。

## 2026-10-20 无声排查与测试源修正

用户反馈没有声音后，对 VideoFile01.mp4 整段 48 秒音轨进行了独立解码。左右声道各 2,304,000 个采样，最小值和最大值均为 0，Peak/RMS 均为负无穷。该文件有音轨但内容是静音；首次仅检查音轨元数据、PlayingID 和消费 PTS，没有检查实际电平，因此此前不能据此认定可听声音正常。

为提供可立即试听的演示，新增 `WwiseMedia_TestTone.mp4`：视频流直接复制，使用合成的间歇提示音替换静音音轨；没有覆盖原视频。FMS_WwiseMedia 已保存为使用这个文件。测试音不是原片音频的恢复结果。前 10 秒解码电平为平均 -25.3 dBFS、峰值 -20.8 dBFS。

复测时播放器为 Playing、PlayingID=5，输入 48 kHz / 2 声道，欠载和拒绝采样均为 0。声源 `(800,0,230)`，PlayerCameraManager 上存在 AkComponent 听者 `(0,0,230)`，GameAudioSubsystem 未挂起。

使用 Wwise `StartOutputCapture` 录制了实际混音输出：`Saved/Wwise/WwiseMediaOutputNear.wav`，48 kHz 立体声，约 59.39 秒；平均 -44.0 dBFS、峰值 -38.4 dBFS，确认实际输出非静音。该录制不经过系统扬声器回录，系统设备、系统音量及最终听感仍需用户实际试听。播放完成后已结束 PIE，并恢复测试前的编辑器后台节流设置。

后续更换视频时先独立检查解码音轨电平，再核对 Wwise 输出捕获；不能只凭“有音轨”或 PTS 前进判断有声。

## 2026-10-21 第二块屏幕白板修复

`WwiseMediaScreen2` 使用独立的 `MP_WwiseMedia2`，运行时通过 `Source Material=MI_WwiseMedia2` 创建动态材质，纹理参数 `RT` 指向 `MT_WwiseMedia2`。复现时播放器和 Wwise 输入均正常，但屏幕为白色。原因是第二份 MediaTexture 的 `New Style Output=true`，与共享父材质 `M_WwiseMediaScreen` 的 External 采样方式不匹配。

已将 `MT_WwiseMedia2.New Style Output` 关闭并保存，与第一份纹理保持一致。PIE 截图确认第二屏显示视频帧；运行约 26 秒时两个播放器及两个 Wwise 输入仍为 Playing，LastError 为空，采样点的 UnderrunFrames、RejectedSamples 均为 0。此次只修改第二份纹理，没有更换视频、修改蓝图或修改公共材质。修复前后截图分别为 `Saved/VibeUE/Captures/capture-game-20261021-105413-807.png`、`Saved/VibeUE/Captures/capture-game-20261021-105536-205.png`。

复制多屏资源时，除独立播放器、纹理、材质参数外，也要核对 MediaTexture 输出模式与材质采样方式是否匹配。
