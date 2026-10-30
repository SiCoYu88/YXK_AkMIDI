# AkWwise Niagara 已验证方案记录

## Android 链接失败：缺少 AudioBusHacker 可视化回调

- 日期：2026-10-30。
- 检索标识：AkAudioSampler、AudioBusHacker、NiagaraDataInterfaceAkWwiseSpectrum、
  `SetAudioBusHackerVisualizationCallback`、Android arm64、undefined symbol。
- 范围：平台库构建与回调接入。本次没有指定或修改 Niagara 资产；发射器数量、Particle Update
  数据流、Simulation Target、Renderer Binding、跨发射器依赖均不适用，不能据此套用粒子改造。
- 现象：Android 最终链接 `M2_Client-arm64.so` 时无法解析
  `SetAudioBusHackerVisualizationCallback`；构建前已提示预期 `libAudioBusHackerFX.a` 不存在。
- 原因：AkAudioSampler 已调用 Android 静态接口，但匹配版本的库构建与部署未闭合。
  Windows DLL 成功加载只说明编辑器主机平台可用。
- 实施：AudioBusHacker 新增 CMake 构建、符号/架构/16 KB 对齐检查、Android 包与 Unreal 部署；
  AkAudioSampler 显式链接目标架构和配置的静态库，缺库时提前终止，Android 启动时绑定静态接口。
  空 SoundEngine 的 Server/Program 不绑定该接口。
- 已验证：Wwise 2025.1.10.9233 / NDK r27c；arm64-v8a、x86_64 的 Debug/Profile/Release
  静态库、共享库和消费者测试程序编译、链接通过，两个 C 回调符号、ELF 架构和 16 KB 对齐通过。
  AkAudioSampler 构建规则通过 UE 5.8 编译检查；4 项打包/部署工具测试通过。
- 未验证：Android 设备上的测试程序、Unreal 完整 APK、具体 Niagara 资产的运行效果。
- 操作入口：`AkSDK/AudioBusHacker/Doc/ANDROID.md`。

新增资产方案时，须先记录并核对发射器结构、更新数据流、模拟目标、渲染绑定和跨发射器依赖，
并将资产实测结果与平台编译结果分别记录。
