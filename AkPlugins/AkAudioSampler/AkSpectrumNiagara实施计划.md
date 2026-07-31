# 实施计划：基于 AkAudioSampler 的 Niagara Audio Spectrum 节点

## 目标

新建一个自定义 Niagara Data Interface `UNiagaraDataInterfaceAkSpectrum`，从 Wwise（经 `AkAudioSampler` 的总线 hook）取得实时频谱数据，向 Niagara 脚本暴露与引擎版 `AudioSpectrum` 语义一致的节点，用于音乐律动粒子可视化。

**关键决策（已与用户确认）：**
- **仅 CPU 路径**（`CanExecuteOnTarget` 返回 `CPUSim`）—— 不实现 HLSL/SRV/GPU，与现有 `WwiseNiagara` DI 一致，最稳。
- **DI 自动注册 hook** —— DI 初始化时确保 `AkAudioSampler::RegisterCatchBuffer()` 已调用（带引用计数/幂等）。
- **对数分桶重采样** —— 把 Ak 的线性 FFT bin 按 `Min/MaxFrequency` 对数映射到固定 `Resolution`，观感对齐引擎版 CQT。

## 宿主插件

复用现有 `client/Plugins/WwiseNiagara`（已依赖 `Niagara`、`NiagaraCore`、`AkAudio`、`WwiseSoundEngine`）。新增文件与现有 `NiagaraDataInterfaceWwiseEvent` 并列，遵循其 UE 5.7 写法（`GetFunctions` 而非 `GetFunctionsInternal`；`PostInitProperties` 内 `FNiagaraTypeRegistry::Register` 自注册；`DEFINE_NDI_DIRECT_FUNC_BINDER` / `NDI_FUNC_BINDER`）。

## 前置修改：AkAudioSampler 线程安全（必做）

**问题**：`AkAudioSampler.cpp:20` 全局裸指针 `pdata`，`bufferlist` 在 Wwise 音频线程（`HackerCallback`→`SaveBufferAndChannels`）写、游戏线程（`UpdateSampleSpecturmCallback`）读，无锁竞态。DI 直接调会加剧问题。

**改法**（`client/Plugins/Wwise/Source/AkAudioSampler/`）：
1. `Public/AkAudioSampler.h`：`Samplerdata` 增加 `FCriticalSection`；新增静态接口
   ```cpp
   // 加锁拷贝当前频谱快照；返回 tick（无新数据时可返回 false）
   static bool GetSpectrumSnapshot(TArray<float>& OutSpectrum, int32& OutTick, int32& OutSampleRate);
   // 幂等注册（引用计数），供 DI 调用
   static int32 EnsureCaptureRegistered();
   ```
2. `Private/AkAudioSampler.cpp`：
   - `SaveBufferAndChannels` 写 `bufferlist` 处、`UpdateSampleSpecturmCallback`/新快照接口读处，统一用 `FScopeLock` 保护同一把锁。
   - `EnsureCaptureRegistered`：内部维护计数，首次调用走现有 `RegisterCatchBuffer()` 逻辑（Win: `SetCallBackFunc`；Android: `SetAudioBusHackerCallbacks`），重复调用直接返回。
   - `GetSpectrumSnapshot`：锁内 `OutSpectrum = pdata->bufferlist; OutTick = pdata->count;`，返回 `count>0`。
   - 记录 sampleRate（当前 `Samplerdata::sampleRate` 未被赋值，需在回调中从 `AkAudioBuffer`/输出配置填入，用于对数分桶的频率→bin 映射；若拿不到则回退固定 48000）。

> 注意：`SaveBufferAndChannels` 现有声道处理（硬取 channel 0/1、按 `2*i` 交错步长、两声道 FFT 取幅度平均）保持不变——本方案不改音频算法，只加锁与快照。

## 新增文件

### 1. `Public/Wwise/Niagara/NiagaraDataInterfaceAkSpectrum.h`

```cpp
UCLASS(EditInlineNew, Category = "WwiseAudio", meta = (DisplayName = "Niagara Ak Spectrum"))
class WWISENIAGARA_API UNiagaraDataInterfaceAkSpectrum : public UNiagaraDataInterface
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Spectrum", meta=(ClampMin="16", ClampMax="1024"))
    int32 Resolution = 512;
    UPROPERTY(EditAnywhere, Category="Spectrum", meta=(ClampMin="20.0", ClampMax="20000.0"))
    float MinimumFrequency = 55.f;
    UPROPERTY(EditAnywhere, Category="Spectrum", meta=(ClampMin="20.0", ClampMax="20000.0"))
    float MaximumFrequency = 10000.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="Spectrum", meta=(ClampMin="-120.0", ClampMax="0.0"))
    float NoiseFloorDb = -60.f;

    // UObject
    virtual void PostInitProperties() override;   // 注册类型 + EnsureCaptureRegistered
    // UNiagaraDataInterface
    virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo&, void*, FVMExternalFunction&) override;
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::CPUSim; }
protected:
    virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
    // VM 函数
    void GetSpectrumValue(FVectorVMExternalFunctionContext& Context); // (float NormalizedPos, int Channel) -> float Amplitude
    void GetNumBands(FVectorVMExternalFunctionContext& Context);      // () -> int Resolution
private:
    static const FName GetSpectrumFunctionName; // "AudioSpectrum"（与引擎版同名，便于复用美术模板）
    static const FName GetNumBandsFunctionName; // "GetNumBands"
    // 每帧缓存的重采样后频谱（对数分桶 + 归一化），CPU VM 直接读
    TArray<float> CachedSpectrum;
    int32 CachedTick = -1;
    void RefreshSpectrum(); // 取快照 -> 对数分桶到 Resolution -> noise floor 归一化到 0..1
};
```

### 2. `Private/Wwise/Niagara/NiagaraDataInterfaceAkSpectrum.cpp`

- `PostInitProperties`：CDO 分支 `FNiagaraTypeRegistry::Register(...)`（照抄 WwiseEvent 175 行附近）；非 CDO 分支调 `UAkAudioSampler::EnsureCaptureRegistered()`。
- `GetFunctions`：两个签名——
  - `AudioSpectrum`：Inputs `[self, float NormalizedPositionInSpectrum, int ChannelIndex]`，Output `[float Amplitude]`。
  - `GetNumBands`：Inputs `[self]`，Output `[int NumBands]`。
- `GetSpectrumValue`：先 `RefreshSpectrum()`；然后按引擎版 `.cpp:569-587` 的插值逻辑读 `CachedSpectrum`（`NormalizedPos * Num` → floor/ceil → `Lerp`）。`ChannelIndex` 忽略（Ak 只有一路混合频谱），或 >0 时返回 0。
- `GetNumBands`：输出 `Resolution`。
- `RefreshSpectrum()`（核心新逻辑）：
  1. `TArray<float> Raw; int32 Tick, SR; if (!GetSpectrumSnapshot(Raw, Tick, SR)) { CachedSpectrum 清零; return; }`
  2. `if (Tick == CachedTick) return;`（无新数据跳过）
  3. **对数分桶**：`CachedSpectrum` resize 到 `Resolution`；对每个 band `i`，其中心频率 `f = Min * pow(Max/Min, i/(Resolution-1))`；换算到 bin `k = f / (SR / N)`（N=Raw.Num()，只用前 N/2 有效）；在相邻 bin 间线性插值取幅度。
  4. **归一化**：幅度 → dB → 减 `NoiseFloorDb` → 乘 `1/max(1,-NoiseFloorDb)`，clamp 到 0..1（对齐引擎版 `.cpp:294-308`）。
  5. `CachedTick = Tick;`
- `DEFINE_NDI_DIRECT_FUNC_BINDER` × 2 + `GetVMExternalFunction` 分发（照抄 WwiseEvent 479-517）。
- `Equals` / `CopyToInternal`：比较/拷贝 4 个 UPROPERTY。
- 文件底部定义 `const FName ... GetSpectrumFunctionName("AudioSpectrum");` 等。

### 3. `WwiseNiagara.Build.cs`（修改）

`PublicDependencyModuleNames` 增加 `"AkAudioSampler"`。（CPU-only，无需 RenderCore/RHI。）

## 使用流程（美术/脚本侧，实施后）

1. 在 Niagara System 的 User/Emitter 参数里添加 `Niagara Ak Spectrum` 类型的 DI，设置 `Resolution`/`Min`/`MaxFrequency`/`NoiseFloorDb`。
2. 在 Module 脚本中调用 `AudioSpectrum(NormalizedPosition, 0)` 拿幅度驱动粒子（大小/颜色/位移），`GetNumBands()` 拿分辨率。
3. hook 由 DI 自动注册；播放中的 Wwise 音乐即被分析。无需 Submix、无需引擎原生 AudioSpectrum。

## 涉及文件汇总

| 操作 | 文件 |
|------|------|
| 改（加锁+快照+幂等注册） | `client/Plugins/Wwise/Source/AkAudioSampler/Public/AkAudioSampler.h` |
| 改 | `client/Plugins/Wwise/Source/AkAudioSampler/Private/AkAudioSampler.cpp` |
| 新增 | `client/Plugins/WwiseNiagara/Source/WwiseNiagara/Public/Wwise/Niagara/NiagaraDataInterfaceAkSpectrum.h` |
| 新增 | `client/Plugins/WwiseNiagara/Source/WwiseNiagara/Private/Wwise/Niagara/NiagaraDataInterfaceAkSpectrum.cpp` |
| 改（加依赖 AkAudioSampler） | `client/Plugins/WwiseNiagara/Source/WwiseNiagara/WwiseNiagara.Build.cs` |

## 参考实现（照抄/对齐）

- 引擎版 CPU VM + 归一化：`E:\UE_5.7_T3\Engine\Plugins\FX\Niagara\Source\Niagara\Private\NiagaraDataInterfaceAudioSpectrum.cpp:48-79, 294-308, 569-647`
- UE 5.7 DI 写法（GetFunctions/自注册/Binder）：`client/Plugins/WwiseNiagara/.../NiagaraDataInterfaceWwiseEvent.cpp:175, 357, 479-517`
- 数据源：`client/Plugins/Wwise/Source/AkAudioSampler/Private/AkAudioSampler.cpp`

## 验证

- 编译：打开 `client/M2_Client.sln` 构建（P4：改动文件先 `p4 edit`，新增文件 `p4 add`）。
- 运行时：播一段 Wwise 音乐，用一个测试 Niagara System 读 `AudioSpectrum`，确认粒子随节奏律动、无音频线程崩溃/数据竞争。

## 风险与待确认

1. **AkAudioSampler 是否为活代码**：需确认 `AudioBusHacker.dll` 在 Win 可用；iOS 分支（`RegisterCatchBuffer`）源码未实现，iOS 上频谱恒 0 —— 若需 iOS 支持需补齐 hook。
2. **sampleRate 来源**：`Samplerdata::sampleRate` 当前未赋值，对数分桶需要它；实施时在回调里填入，拿不到则回退 48000（分桶频率会有偏差但不崩）。
3. **Ak 频谱数组长度每帧可变**（=buffer 帧数）：`RefreshSpectrum` 每帧按 `Raw.Num()` 动态映射，已在方案内处理。
