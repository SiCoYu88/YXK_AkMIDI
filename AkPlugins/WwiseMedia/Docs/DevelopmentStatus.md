# WwiseMedia 0.1.0 开发与验证记录

日期：2026-10-20。

插件已由 AkMedia 统一更名为 WwiseMedia，运行时模块为 `WwiseMediaRuntime`，组件为 `UWwiseMediaAudioComponent`。`Docs/Validation` 中原始日志保留更名前名称，属于历史验证记录。更名后已重新通过 UHT、C++ 编译与 DLL 链接、10 万块并发缓冲测试及两项 `WwiseMedia.Bridge` 自动化测试；新报告为 `Docs/Validation/WwiseMediaAutomationResults.json`，构建及自动化脚本退出码为 0。

当前交付为可编译、通过核心自动化测试的首版源码。实际媒体后端到 Wwise 设备的完整播放、空间听感及音画同步尚待场景验收，因此暂不复制到客户端 `Plugins/WwiseMedia`。

## 已实现

| 范围 | 实现 |
| --- | --- |
| 插件边界 | 独立 WwiseMedia 插件、WwiseMediaRuntime 运行时模块；使用现有 AkAudio/WwiseSoundEngine 接口 |
| 场景组件 | UWwiseMediaAudioComponent 继承 UAkAudioInputComponent，利用其声源位置、Listener、衰减及空间音频接入 |
| 输入 | 绑定 Media Player 主音频 Sink，检查 Native Audio Out、重复消费者和活动 Media Sound |
| PCM | 工作线程使用 UE 重采样器下混为 48 kHz Mono；有界队列和预分配 SPSC 输出缓冲 |
| 音频回调 | 共享桥接状态，不访问 UObject；缺数/暂停输出静音；支持不同回调帧数 |
| 控制 | 蓝图打开、播放、暂停、Seek、停止、更换播放器，以及状态/错误通知 |
| 同步 | 消费端 PTS 回传主音频 Sink 与 Facade；可设置设备输出延迟补偿 |
| 跳转 | Epoch 与媒体 Flush Revision 隔离旧 PCM 和旧时钟 |
| 生命周期 | 停止自己的 Playing ID、结束转换线程、安全释放媒体 Sink 与委托状态 |
| 诊断 | 缓冲、欠载帧、输入格式、拒绝样本、消费 PTS、Playing ID 和错误查询 |

## 验证结果

环境为 Windows Win64 Development Editor、UE 5.8.3、Wwise Unreal Integration 2025.1.10.9233.4458。引擎目录虽然名为 UE_5.7_T3，版本文件实际为 5.8.3。

| 检查 | 结果 | 证据范围 |
| --- | --- | --- |
| UHT、C++ 编译与 DLL 链接 | 通过 | 独立宿主构建 WwiseMediaRuntime，2026-10-20 |
| 原生 SPSC 队列测试 | 通过 | 容量、FIFO、100000 块并发传输、完整载荷与代次 |
| WwiseMedia.Bridge.ResampleAndPause | 通过 | 合成 100 ms/44.1 kHz Stereo 输入；48 kHz Mono 幅度及帧数、暂停不消费、有效时钟、尾段排空、Shutdown 后静音 |
| WwiseMedia.Bridge.FlushRejectsOldAudio | 通过 | Flush 后没有旧波形、旧时钟；新 PTS 从跳转目标输出 |
| 构建脚本语法 | 通过 | PowerShell 解析；脚本内构建参数与已成功执行的隔离宿主流程一致 |
| 真实视频、Wwise 事件及物理输出 | 待验证 | 合成 PCM 测试未启动真实视频和 Wwise 播放事件 |
| Shipping 打包与非 Windows 平台 | 未验证 | 不能由 Editor 编译推断 |

自动化运行使用 `-nullrhi -nosound`；共执行 2 项 UE 测试，均为 Success，进程退出码为 0。原始记录随 `Docs/Validation` 交付。

测试宿主显式启用 Niagara，因为本机 Wwise 的 AkAudioSampler 模块导入 Niagara/NiagaraEditor DLL。早期测试启动还遇到共享引擎 Build ID 更新；重新构建生成匹配的模块清单后正常加载，没有手动改写 Build ID。

## 场景验收步骤

使用匹配 UE/Wwise 的临时测试工程启用 WwiseMedia。客户端已有的 `Play_Audio_Input_ScreenVideo` 可用作事件配置参考，但源码没有硬编码资源。先准备带闪光/脉冲同步标记、左右声道内容不同、44.1 kHz 和 48 kHz 的本地视频。

| 步骤 | 操作 | 验收观察 |
| --- | --- | --- |
| 1 | 为屏幕设置专用 Media Player/Texture 和 Wwise Media Audio Component，指定 Audio Input 播放事件 | 正常图像与单路声音；Wwise Profiler 中一个对应 Playing ID，无原生重复声音 |
| 2 | Listener 绕屏幕移动，靠近并远离到衰减边界 | 方向与坐标一致；距离曲线生效。首轮降低 Spread；不要按 AMB_50m 名称推断实际距离 |
| 3 | 接入项目已有遮挡、房间及混响系统 | 墙后音量/滤波符合配置；房间混响切换；检查固定 Hall_Large 发送是否叠加 |
| 4 | 暂停 10 秒再恢复，连续 Seek 前后跳转，暂停时 Seek | 无旧位置的短促音频，无累计排队；图像和声音恢复到同一时间 |
| 5 | 播放短片至结尾、长片至结尾，以及循环播放 | 尾音完整、无卡死；结束实例停止；循环不残留旧代次音频 |
| 6 | 用同步标记片在目标设备测量首帧、Seek 后及连续 30 分钟的偏差 | 记录测量设备、媒体后端、输出缓冲及偏差；必要时校准 Output Latency Milliseconds |
| 7 | 使用两个独立播放器/组件；停止其中一个；尝试重复绑定同一播放器 | 多路位置独立，停止不影响另一实例；重复消费者明确报错 |
| 8 | 切关卡、销毁 Actor、关闭播放器、重复进入/退出 PIE | 无悬空回调、残留声音或工作线程，Wwise 实例可回收 |
| 9 | 模拟媒体停顿、无音轨、无效事件、声音虚拟化/Voice Limit | 有明确状态/错误；缓冲有界。回调中断超时后暂停本组件媒体 |
| 10 | Development/Shipping 打包，目标机重复基本播放和空间测试 | 插件与媒体解码器、事件资源正确打包，行为与 Editor 一致 |

执行以上步骤后填写实际结果和缺陷记录，再决定客户端正式接入。首版不承诺直播、倒放、变速、共享解码器分发或跨平台兼容。

## 文件与构建

核心入口为 `Source/WwiseMediaRuntime/Public/WwiseMediaAudioComponent.h`。完整使用说明见 `README.md`，详细设计见 `Docs/TechnicalDesign.md`；后者含设计示意，实际 API 与实现状态以源码和本记录为准。

`Tools/Build.ps1` 支持为插件生成隔离宿主。PowerShell 示例（替换为本机匹配目录）：

```powershell
.\Tools\Build.ps1 -EngineRoot 'F:\UE5\UE_5.7_T3' `
  -WwisePluginDirectory 'H:\H3D_M2_Stream\game\client\Plugins\Wwise' `
  -WwiseSoundEnginePluginDirectory 'H:\H3D_M2_Stream\game\client\Plugins\WwiseSoundEngine' `
  -RunAutomationTests
```

宿主以目录联接引用依赖；构建会更新所引用 Wwise 插件的 Intermediate/构建输出，也可能写入引擎构建缓存，但不修改依赖源码。需要隔离这些产物时，先提供独立的 Wwise 副本。脚本保留宿主供查看日志，要求 HostDirectory 尚未包含这些插件路径。

发布采用源码形式，不带只适用于开发机 Build ID 的 Binaries，也不带 Intermediate、Saved 和测试 exe/obj。
