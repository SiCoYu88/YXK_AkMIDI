#include "NiagaraDataInterfaceAkWwiseSpectrum.h"

#include "AkAudioSampler.h"
#include "AkAudioSamplerModule.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraRenderer.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "VectorVM.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataInterfaceAkWwiseSpectrum)

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceAkWwiseSpectrum"

namespace NiagaraAkWwiseSpectrumPrivate
{
constexpr int32 MinResolution = 16;
constexpr int32 MaxResolution = 1024;

struct FInstanceData_GameThread
{
	TArray<float> Spectrum;
	FString LastBusName;
	int64 LastSequence = INDEX_NONE;
	uint32 LastSettingsHash = 0;
	float SecondsSinceLastSnapshot = 0.0f;
	bool bSettingsInitialized = false;
	bool bHasValidData = false;
	bool bRenderDataDirty = true;
	bool bOwnsVisualizationRegistration = false;
};

struct alignas(16) FInstanceData_GameToRender
{
	TArray<float> Spectrum;
	int32 Resolution = 0;
	int32 NumChannels = 0;
	uint32 bHasValidData = 0;
	uint32 bUpdateSpectrum = 0;
};

static_assert(
	sizeof(FInstanceData_GameToRender) % 16 == 0,
	"Niagara game-to-render instance data must be 16-byte aligned.");

struct FInstanceData_RenderThread
{
	FReadBuffer SpectrumBuffer;
	int32 Resolution = 0;
	int32 NumChannels = 0;
	bool bHasValidData = false;
};

struct FNiagaraDataInterfaceProxyAkWwiseSpectrum : public FNiagaraDataInterfaceProxy
{
	virtual ~FNiagaraDataInterfaceProxyAkWwiseSpectrum() override
	{
		check(IsInRenderingThread());
		for (TPair<FNiagaraSystemInstanceID, FInstanceData_RenderThread>& Pair : PerInstanceData)
		{
			Pair.Value.SpectrumBuffer.Release();
		}
	}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FInstanceData_GameToRender);
	}

	virtual void ConsumePerInstanceDataFromGameThread(
		void* InPerInstanceData,
		const FNiagaraSystemInstanceID& InstanceID) override
	{
		FInstanceData_GameToRender* GameToRender =
			static_cast<FInstanceData_GameToRender*>(InPerInstanceData);
		FInstanceData_RenderThread& RenderData = PerInstanceData.FindOrAdd(InstanceID);

		RenderData.Resolution = GameToRender->Resolution;
		RenderData.NumChannels = GameToRender->NumChannels;
		RenderData.bHasValidData = GameToRender->bHasValidData != 0;

		if (GameToRender->bUpdateSpectrum != 0)
		{
			FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
			const int32 ElementCount = FMath::Max(GameToRender->Spectrum.Num(), 1);
			const int32 NumBytes = ElementCount * sizeof(float);

			if (RenderData.SpectrumBuffer.NumBytes != NumBytes)
			{
				RenderData.SpectrumBuffer.Release();
				RenderData.SpectrumBuffer.Initialize(
					RHICmdList,
					TEXT("AkWwiseSpectrum"),
					sizeof(float),
					ElementCount,
					EPixelFormat::PF_R32_FLOAT,
					BUF_Static);
			}

			float* BufferData = static_cast<float*>(RHICmdList.LockBuffer(
				RenderData.SpectrumBuffer.Buffer,
				0,
				NumBytes,
				EResourceLockMode::RLM_WriteOnly));
			if (GameToRender->Spectrum.IsEmpty())
			{
				BufferData[0] = 0.0f;
			}
			else
			{
				FMemory::Memcpy(
					BufferData,
					GameToRender->Spectrum.GetData(),
					GameToRender->Spectrum.Num() * sizeof(float));
			}
			RHICmdList.UnlockBuffer(RenderData.SpectrumBuffer.Buffer);
		}

		GameToRender->~FInstanceData_GameToRender();
	}

	void RemoveInstance(const FNiagaraSystemInstanceID& InstanceID)
	{
		if (FInstanceData_RenderThread* RenderData = PerInstanceData.Find(InstanceID))
		{
			RenderData->SpectrumBuffer.Release();
			PerInstanceData.Remove(InstanceID);
		}
	}

	TMap<FNiagaraSystemInstanceID, FInstanceData_RenderThread> PerInstanceData;
};

template <typename TSpectrumInterface>
int32 GetResolution(const TSpectrumInterface& DataInterface)
{
	return FMath::Clamp(DataInterface.Resolution, MinResolution, MaxResolution);
}

template <typename TSpectrumInterface>
uint32 GetSettingsHash(const TSpectrumInterface& DataInterface)
{
	uint32 Hash = GetTypeHash(DataInterface.BusName);
	Hash = HashCombine(Hash, GetTypeHash(GetResolution(DataInterface)));
	Hash = HashCombine(Hash, GetTypeHash(DataInterface.MinimumFrequency));
	Hash = HashCombine(Hash, GetTypeHash(DataInterface.MaximumFrequency));
	Hash = HashCombine(Hash, GetTypeHash(DataInterface.NoiseFloorDb));
	return Hash;
}

bool SetZeroSpectrum(FInstanceData_GameThread& InstanceData, int32 Resolution)
{
	const bool bChanged = InstanceData.bHasValidData || InstanceData.Spectrum.Num() != Resolution;
	if (InstanceData.Spectrum.Num() != Resolution)
	{
		InstanceData.Spectrum.SetNumZeroed(Resolution);
	}
	else if (InstanceData.bHasValidData && Resolution > 0)
	{
		FMemory::Memzero(InstanceData.Spectrum.GetData(), Resolution * sizeof(float));
	}
	InstanceData.bHasValidData = false;
	return bChanged;
}

template <typename TSpectrumInterface>
bool ResampleSpectrum(
	const TSpectrumInterface& DataInterface,
	const FAkAudioBusHackerVisualizationData& Source,
	FInstanceData_GameThread& InstanceData)
{
	const int32 OutputResolution = GetResolution(DataInterface);
	const int32 SourceResolution = Source.SpectrumDb.Num();
	const float SourceMinHz = Source.SpectrumMinHz;
	const float SourceMaxHz = Source.SpectrumMaxHz;
	const float TargetMinHz = FMath::Clamp(DataInterface.MinimumFrequency, 20.0f, 20000.0f);
	const float TargetMaxHz = FMath::Clamp(DataInterface.MaximumFrequency, 20.0f, 20000.0f);

	if (SourceResolution < 2
		|| SourceMinHz <= 0.0f
		|| SourceMaxHz <= SourceMinHz
		|| TargetMaxHz <= TargetMinHz)
	{
		SetZeroSpectrum(InstanceData, OutputResolution);
		return false;
	}

	const float SourceLogRange = FMath::Loge(SourceMaxHz / SourceMinHz);
	const float TargetRatio = TargetMaxHz / TargetMinHz;
	if (!FMath::IsFinite(SourceLogRange)
		|| SourceLogRange <= 0.0f
		|| !FMath::IsFinite(TargetRatio)
		|| TargetRatio <= 1.0f)
	{
		SetZeroSpectrum(InstanceData, OutputResolution);
		return false;
	}

	InstanceData.Spectrum.SetNumUninitialized(OutputResolution);
	const float NoiseFloorDb = FMath::Clamp(DataInterface.NoiseFloorDb, -120.0f, 0.0f);
	const float DbScale = 1.0f / FMath::Max(1.0f, -NoiseFloorDb);

	for (int32 OutputIndex = 0; OutputIndex < OutputResolution; ++OutputIndex)
	{
		const float Alpha = OutputResolution > 1
			? static_cast<float>(OutputIndex) / static_cast<float>(OutputResolution - 1)
			: 0.0f;
		const float TargetFrequency = TargetMinHz * FMath::Pow(TargetRatio, Alpha);

		if (TargetFrequency < SourceMinHz || TargetFrequency > SourceMaxHz)
		{
			InstanceData.Spectrum[OutputIndex] = 0.0f;
			continue;
		}

		const float SourcePosition =
			FMath::Loge(TargetFrequency / SourceMinHz)
			/ SourceLogRange
			* static_cast<float>(SourceResolution - 1);
		const int32 LowerIndex = FMath::Clamp(FMath::FloorToInt(SourcePosition), 0, SourceResolution - 1);
		const int32 UpperIndex = FMath::Min(LowerIndex + 1, SourceResolution - 1);
		const float SourceAlpha = FMath::Clamp(SourcePosition - LowerIndex, 0.0f, 1.0f);
		const float SpectrumDb = FMath::Lerp(
			Source.SpectrumDb[LowerIndex],
			Source.SpectrumDb[UpperIndex],
			SourceAlpha);
		const float ClampedDb = FMath::IsFinite(SpectrumDb)
			? FMath::Max(SpectrumDb, NoiseFloorDb)
			: NoiseFloorDb;
		InstanceData.Spectrum[OutputIndex] = (ClampedDb - NoiseFloorDb) * DbScale;
	}

	InstanceData.bHasValidData = true;
	return true;
}

float SampleSpectrum(const FInstanceData_GameThread* InstanceData, float NormalizedPosition, int32 ChannelIndex)
{
	if (!InstanceData
		|| !InstanceData->bHasValidData
		|| ChannelIndex != 0
		|| InstanceData->Spectrum.IsEmpty())
	{
		return 0.0f;
	}

	const int32 MaxIndex = InstanceData->Spectrum.Num() - 1;
	const float Position = FMath::Clamp(NormalizedPosition, 0.0f, 1.0f) * MaxIndex;
	const int32 LowerIndex = FMath::Clamp(FMath::FloorToInt(Position), 0, MaxIndex);
	const int32 UpperIndex = FMath::Min(LowerIndex + 1, MaxIndex);
	return FMath::Lerp(
		InstanceData->Spectrum[LowerIndex],
		InstanceData->Spectrum[UpperIndex],
		FMath::Clamp(Position - LowerIndex, 0.0f, 1.0f));
}
} // namespace NiagaraAkWwiseSpectrumPrivate

UNiagaraDataInterfaceAkWwiseSpectrum::UNiagaraDataInterfaceAkWwiseSpectrum(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, BusName(TEXT("Master Audio Bus"))
	, Resolution(64)
	, MinimumFrequency(20.0f)
	, MaximumFrequency(20000.0f)
	, NoiseFloorDb(-60.0f)
	, bAutoRegisterVisualizationCallback(true)
	, StaleDataTimeoutSeconds(0.25f)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Proxy = MakeUnique<
			NiagaraAkWwiseSpectrumPrivate::FNiagaraDataInterfaceProxyAkWwiseSpectrum>();
	}
}

void UNiagaraDataInterfaceAkWwiseSpectrum::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		const ENiagaraTypeRegistryFlags Flags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable
			| ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceAkWwiseSpectrum::InitPerInstanceData(
	void* PerInstanceData,
	FNiagaraSystemInstance* SystemInstance)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;
	(void)SystemInstance;

	FInstanceData_GameThread* InstanceData = new (PerInstanceData) FInstanceData_GameThread();
	InstanceData->Spectrum.SetNumZeroed(GetResolution(*this));

	if (bAutoRegisterVisualizationCallback)
	{
		InstanceData->bOwnsVisualizationRegistration =
			FAkAudioSamplerModule::AcquireVisualizationCallback(
				&UAkAudioSampler::VisualizationCallback);
	}

	return true;
}

void UNiagaraDataInterfaceAkWwiseSpectrum::DestroyPerInstanceData(
	void* PerInstanceData,
	FNiagaraSystemInstance* SystemInstance)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	FInstanceData_GameThread* InstanceData =
		static_cast<FInstanceData_GameThread*>(PerInstanceData);
	if (InstanceData->bOwnsVisualizationRegistration)
	{
		FAkAudioSamplerModule::ReleaseVisualizationCallback();
		InstanceData->bOwnsVisualizationRegistration = false;
	}

	if (IsUsedWithGPUScript())
	{
		const FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();
		FNiagaraDataInterfaceProxyAkWwiseSpectrum* SpectrumProxy =
			GetProxyAs<FNiagaraDataInterfaceProxyAkWwiseSpectrum>();
		ENQUEUE_RENDER_COMMAND(RemoveAkWwiseSpectrumInstance)(
			[SpectrumProxy, InstanceID](FRHICommandListImmediate& RHICmdList)
			{
				(void)RHICmdList;
				SpectrumProxy->RemoveInstance(InstanceID);
			});
	}

	InstanceData->~FInstanceData_GameThread();
}

int32 UNiagaraDataInterfaceAkWwiseSpectrum::PerInstanceDataSize() const
{
	return sizeof(NiagaraAkWwiseSpectrumPrivate::FInstanceData_GameThread);
}

bool UNiagaraDataInterfaceAkWwiseSpectrum::PerInstanceTick(
	void* PerInstanceData,
	FNiagaraSystemInstance* SystemInstance,
	float DeltaSeconds)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;
	(void)SystemInstance;

	FInstanceData_GameThread* InstanceData =
		static_cast<FInstanceData_GameThread*>(PerInstanceData);
	if (!InstanceData)
	{
		return false;
	}

	if (bAutoRegisterVisualizationCallback && !InstanceData->bOwnsVisualizationRegistration)
	{
		InstanceData->bOwnsVisualizationRegistration =
			FAkAudioSamplerModule::AcquireVisualizationCallback(
				&UAkAudioSampler::VisualizationCallback);
	}
	else if (!bAutoRegisterVisualizationCallback && InstanceData->bOwnsVisualizationRegistration)
	{
		FAkAudioSamplerModule::ReleaseVisualizationCallback();
		InstanceData->bOwnsVisualizationRegistration = false;
	}

	const float SafeDeltaSeconds = FMath::Max(DeltaSeconds, 0.0f);
	const uint32 SettingsHash = GetSettingsHash(*this);
	const bool bBusChanged = InstanceData->LastBusName != BusName;
	const bool bSettingsChanged =
		!InstanceData->bSettingsInitialized
		|| InstanceData->LastSettingsHash != SettingsHash;

	FAkAudioBusHackerVisualizationData Snapshot;
	const bool bHasSnapshot = UAkAudioSampler::GetLatestVisualizationData(BusName, Snapshot);
	if (bHasSnapshot)
	{
		const bool bNewSnapshot = bBusChanged || Snapshot.Sequence != InstanceData->LastSequence;
		if (bNewSnapshot)
		{
			InstanceData->SecondsSinceLastSnapshot = 0.0f;
		}
		else
		{
			InstanceData->SecondsSinceLastSnapshot += SafeDeltaSeconds;
		}

		if (bNewSnapshot || bSettingsChanged)
		{
			ResampleSpectrum(*this, Snapshot, *InstanceData);
			InstanceData->bRenderDataDirty = true;
		}

		InstanceData->LastSequence = Snapshot.Sequence;
	}
	else
	{
		InstanceData->SecondsSinceLastSnapshot += SafeDeltaSeconds;
		if (bBusChanged || bSettingsChanged)
		{
			InstanceData->bRenderDataDirty |=
				SetZeroSpectrum(*InstanceData, GetResolution(*this));
			InstanceData->LastSequence = INDEX_NONE;
		}
	}

	if (StaleDataTimeoutSeconds > 0.0f
		&& InstanceData->SecondsSinceLastSnapshot >= StaleDataTimeoutSeconds)
	{
		InstanceData->bRenderDataDirty |=
			SetZeroSpectrum(*InstanceData, GetResolution(*this));
	}

	InstanceData->LastBusName = BusName;
	InstanceData->LastSettingsHash = SettingsHash;
	InstanceData->bSettingsInitialized = true;
	return false;
}

void UNiagaraDataInterfaceAkWwiseSpectrum::ProvidePerInstanceDataForRenderThread(
	void* DataForRenderThread,
	void* PerInstanceData,
	const FNiagaraSystemInstanceID& SystemInstance)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;
	(void)SystemInstance;

	FInstanceData_GameThread* GameThreadData =
		static_cast<FInstanceData_GameThread*>(PerInstanceData);
	FInstanceData_GameToRender* GameToRender =
		new (DataForRenderThread) FInstanceData_GameToRender();

	GameToRender->Resolution = GameThreadData->Spectrum.Num();
	GameToRender->NumChannels = GameThreadData->bHasValidData ? 1 : 0;
	GameToRender->bHasValidData = GameThreadData->bHasValidData ? 1u : 0u;
	GameToRender->bUpdateSpectrum = GameThreadData->bRenderDataDirty ? 1u : 0u;
	if (GameThreadData->bRenderDataDirty)
	{
		GameToRender->Spectrum = GameThreadData->Spectrum;
		GameThreadData->bRenderDataDirty = false;
	}
}

void UNiagaraDataInterfaceAkWwiseSpectrum::GetSpectrumValue(
	FVectorVMExternalFunctionContext& Context)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	VectorVM::FExternalFuncInputHandler<float> InNormalizedPosition(Context);
	VectorVM::FExternalFuncInputHandler<int32> InChannelIndex(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAmplitude(Context);

	for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
	{
		*OutAmplitude.GetDestAndAdvance() = SampleSpectrum(
			InstanceData.Get(),
			InNormalizedPosition.GetAndAdvance(),
			InChannelIndex.GetAndAdvance());
	}
}

void UNiagaraDataInterfaceAkWwiseSpectrum::GetNumChannels(
	FVectorVMExternalFunctionContext& Context)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutNumChannels(Context);
	const int32 NumChannels = InstanceData.Get() && InstanceData->bHasValidData ? 1 : 0;
	for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
	{
		*OutNumChannels.GetDestAndAdvance() = NumChannels;
	}
}

void UNiagaraDataInterfaceAkWwiseSpectrum::IsSpectrumValid(
	FVectorVMExternalFunctionContext& Context)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool> OutIsValid(Context);
	const bool bIsValid = InstanceData.Get() && InstanceData->bHasValidData;
	for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
	{
		OutIsValid.SetAndAdvance(bIsValid);
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceAkWwiseSpectrum::GetFunctionsInternal(
	TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	FNiagaraFunctionSignature DefaultSignature;
	DefaultSignature.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Spectrum"));
	DefaultSignature.bMemberFunction = true;
	DefaultSignature.bRequiresContext = false;
	DefaultSignature.bSupportsCPU = true;
	DefaultSignature.bSupportsGPU = true;

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = GetSpectrumFunctionName;
		Signature.Inputs.Emplace(
			FNiagaraTypeDefinition::GetFloatDef(),
			TEXT("NormalizedPositionInSpectrum"));
		Signature.Inputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("ChannelIndex"));
		Signature.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Amplitude"));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = GetNumChannelsFunctionName;
		Signature.Outputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumChannels"));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = IsSpectrumValidFunctionName;
		Signature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
	}
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseSpectrum, GetSpectrumValue);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseSpectrum, GetNumChannels);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseSpectrum, IsSpectrumValid);

void UNiagaraDataInterfaceAkWwiseSpectrum::GetVMExternalFunction(
	const FVMExternalFunctionBindingInfo& BindingInfo,
	void* InstanceData,
	FVMExternalFunction& OutFunc)
{
	(void)InstanceData;
	if (BindingInfo.Name == GetSpectrumFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseSpectrum, GetSpectrumValue)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetNumChannelsFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseSpectrum, GetNumChannels)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == IsSpectrumValidFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseSpectrum, IsSpectrumValid)::Bind(this, OutFunc);
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown Ak Wwise Spectrum function: %s"), *BindingInfo.Name.ToString());
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceAkWwiseSpectrum::AppendCompileHash(
	FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSuccess;
}

bool UNiagaraDataInterfaceAkWwiseSpectrum::GetFunctionHLSL(
	const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
	const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo,
	int FunctionInstanceIndex,
	FString& OutHLSL)
{
	(void)FunctionInstanceIndex;
	if (Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL))
	{
		return true;
	}

	if (FunctionInfo.DefinitionName == GetSpectrumFunctionName)
	{
		OutHLSL.Appendf(
			TEXT(R"(
void %s(float In_NormalizedPosition, int In_ChannelIndex, out float Out_Amplitude)
{
	if (%s_HasValidData == 0 || In_ChannelIndex != 0 || %s_Resolution <= 0)
	{
		Out_Amplitude = 0.0f;
		return;
	}

	float Position = saturate(In_NormalizedPosition) * (%s_Resolution - 1);
	int LowerIndex = clamp((int)floor(Position), 0, %s_Resolution - 1);
	int UpperIndex = min(LowerIndex + 1, %s_Resolution - 1);
	float Alpha = saturate(Position - LowerIndex);
	float LowerValue = %s_SpectrumBuffer.Load(LowerIndex);
	float UpperValue = %s_SpectrumBuffer.Load(UpperIndex);
	Out_Amplitude = lerp(LowerValue, UpperValue, Alpha);
}
)"),
			*FunctionInfo.InstanceName,
			*ParamInfo.DataInterfaceHLSLSymbol,
			*ParamInfo.DataInterfaceHLSLSymbol,
			*ParamInfo.DataInterfaceHLSLSymbol,
			*ParamInfo.DataInterfaceHLSLSymbol,
			*ParamInfo.DataInterfaceHLSLSymbol,
			*ParamInfo.DataInterfaceHLSLSymbol,
			*ParamInfo.DataInterfaceHLSLSymbol);
		return true;
	}

	if (FunctionInfo.DefinitionName == GetNumChannelsFunctionName)
	{
		OutHLSL.Appendf(
			TEXT("void %s(out int Out_NumChannels)\n{\n\tOut_NumChannels = %s_NumChannels;\n}\n"),
			*FunctionInfo.InstanceName,
			*ParamInfo.DataInterfaceHLSLSymbol);
		return true;
	}

	if (FunctionInfo.DefinitionName == IsSpectrumValidFunctionName)
	{
		OutHLSL.Appendf(
			TEXT("void %s(out bool Out_IsValid)\n{\n\tOut_IsValid = %s_HasValidData != 0;\n}\n"),
			*FunctionInfo.InstanceName,
			*ParamInfo.DataInterfaceHLSLSymbol);
		return true;
	}

	return false;
}

void UNiagaraDataInterfaceAkWwiseSpectrum::GetParameterDefinitionHLSL(
	const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
	FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);
	OutHLSL.Appendf(TEXT("Buffer<float> %s_SpectrumBuffer;\n"), *ParamInfo.DataInterfaceHLSLSymbol);
	OutHLSL.Appendf(TEXT("int %s_NumChannels;\n"), *ParamInfo.DataInterfaceHLSLSymbol);
	OutHLSL.Appendf(TEXT("int %s_Resolution;\n"), *ParamInfo.DataInterfaceHLSLSymbol);
	OutHLSL.Appendf(TEXT("int %s_HasValidData;\n"), *ParamInfo.DataInterfaceHLSLSymbol);
}
#endif

void UNiagaraDataInterfaceAkWwiseSpectrum::BuildShaderParameters(
	FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceAkWwiseSpectrum::SetShaderParameters(
	const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	const FNiagaraDataInterfaceProxyAkWwiseSpectrum& SpectrumProxy =
		Context.GetProxy<FNiagaraDataInterfaceProxyAkWwiseSpectrum>();
	const FInstanceData_RenderThread* RenderData =
		SpectrumProxy.PerInstanceData.Find(Context.GetSystemInstanceID());

	FShaderParameters* ShaderParameters = Context.GetParameterNestedStruct<FShaderParameters>();
	ShaderParameters->NumChannels = RenderData ? RenderData->NumChannels : 0;
	ShaderParameters->Resolution = RenderData ? RenderData->Resolution : 0;
	ShaderParameters->HasValidData = RenderData && RenderData->bHasValidData ? 1 : 0;
	ShaderParameters->SpectrumBuffer = RenderData
		? FNiagaraRenderer::GetSrvOrDefaultFloat(RenderData->SpectrumBuffer.SRV)
		: FNiagaraRenderer::GetSrvOrDefaultFloat(static_cast<FRHIShaderResourceView*>(nullptr));
}

bool UNiagaraDataInterfaceAkWwiseSpectrum::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceAkWwiseSpectrum* OtherSpectrum =
		Cast<const UNiagaraDataInterfaceAkWwiseSpectrum>(Other);
	return OtherSpectrum
		&& OtherSpectrum->BusName == BusName
		&& OtherSpectrum->Resolution == Resolution
		&& OtherSpectrum->MinimumFrequency == MinimumFrequency
		&& OtherSpectrum->MaximumFrequency == MaximumFrequency
		&& OtherSpectrum->NoiseFloorDb == NoiseFloorDb
		&& OtherSpectrum->bAutoRegisterVisualizationCallback == bAutoRegisterVisualizationCallback
		&& OtherSpectrum->StaleDataTimeoutSeconds == StaleDataTimeoutSeconds;
}

bool UNiagaraDataInterfaceAkWwiseSpectrum::CopyToInternal(
	UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceAkWwiseSpectrum* DestinationSpectrum =
		Cast<UNiagaraDataInterfaceAkWwiseSpectrum>(Destination);
	if (!DestinationSpectrum)
	{
		return false;
	}

	DestinationSpectrum->BusName = BusName;
	DestinationSpectrum->Resolution = Resolution;
	DestinationSpectrum->MinimumFrequency = MinimumFrequency;
	DestinationSpectrum->MaximumFrequency = MaximumFrequency;
	DestinationSpectrum->NoiseFloorDb = NoiseFloorDb;
	DestinationSpectrum->bAutoRegisterVisualizationCallback = bAutoRegisterVisualizationCallback;
	DestinationSpectrum->StaleDataTimeoutSeconds = StaleDataTimeoutSeconds;
	return true;
}

UNiagaraDataInterfaceAkWwiseAudioSpectrum::UNiagaraDataInterfaceAkWwiseAudioSpectrum(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, BusName(TEXT("Master Audio Bus"))
	, bAutoRegisterVisualizationCallback(true)
	, StaleDataTimeoutSeconds(0.25f)
{
}

void UNiagaraDataInterfaceAkWwiseAudioSpectrum::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		const ENiagaraTypeRegistryFlags Flags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable
			| ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceAkWwiseAudioSpectrum::InitPerInstanceData(
	void* PerInstanceData,
	FNiagaraSystemInstance* SystemInstance)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;
	(void)SystemInstance;

	FInstanceData_GameThread* InstanceData = new (PerInstanceData) FInstanceData_GameThread();
	InstanceData->Spectrum.SetNumZeroed(GetResolution(*this));
	if (bAutoRegisterVisualizationCallback)
	{
		InstanceData->bOwnsVisualizationRegistration =
			FAkAudioSamplerModule::AcquireVisualizationCallback(
				&UAkAudioSampler::VisualizationCallback);
	}
	return true;
}

void UNiagaraDataInterfaceAkWwiseAudioSpectrum::DestroyPerInstanceData(
	void* PerInstanceData,
	FNiagaraSystemInstance* SystemInstance)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;
	(void)SystemInstance;

	FInstanceData_GameThread* InstanceData =
		static_cast<FInstanceData_GameThread*>(PerInstanceData);
	if (InstanceData->bOwnsVisualizationRegistration)
	{
		FAkAudioSamplerModule::ReleaseVisualizationCallback();
		InstanceData->bOwnsVisualizationRegistration = false;
	}
	InstanceData->~FInstanceData_GameThread();
}

int32 UNiagaraDataInterfaceAkWwiseAudioSpectrum::PerInstanceDataSize() const
{
	return sizeof(NiagaraAkWwiseSpectrumPrivate::FInstanceData_GameThread);
}

bool UNiagaraDataInterfaceAkWwiseAudioSpectrum::PerInstanceTick(
	void* PerInstanceData,
	FNiagaraSystemInstance* SystemInstance,
	float DeltaSeconds)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;
	(void)SystemInstance;

	FInstanceData_GameThread* InstanceData =
		static_cast<FInstanceData_GameThread*>(PerInstanceData);
	if (!InstanceData)
	{
		return false;
	}

	if (bAutoRegisterVisualizationCallback && !InstanceData->bOwnsVisualizationRegistration)
	{
		InstanceData->bOwnsVisualizationRegistration =
			FAkAudioSamplerModule::AcquireVisualizationCallback(
				&UAkAudioSampler::VisualizationCallback);
	}
	else if (!bAutoRegisterVisualizationCallback && InstanceData->bOwnsVisualizationRegistration)
	{
		FAkAudioSamplerModule::ReleaseVisualizationCallback();
		InstanceData->bOwnsVisualizationRegistration = false;
	}

	const float SafeDeltaSeconds = FMath::Max(DeltaSeconds, 0.0f);
	const uint32 SettingsHash = GetSettingsHash(*this);
	const bool bBusChanged = InstanceData->LastBusName != BusName;
	const bool bSettingsChanged =
		!InstanceData->bSettingsInitialized
		|| InstanceData->LastSettingsHash != SettingsHash;

	FAkAudioBusHackerVisualizationData Snapshot;
	const bool bHasSnapshot = UAkAudioSampler::GetLatestVisualizationData(BusName, Snapshot);
	if (bHasSnapshot)
	{
		const bool bNewSnapshot = bBusChanged || Snapshot.Sequence != InstanceData->LastSequence;
		InstanceData->SecondsSinceLastSnapshot = bNewSnapshot
			? 0.0f
			: InstanceData->SecondsSinceLastSnapshot + SafeDeltaSeconds;
		if (bNewSnapshot || bSettingsChanged)
		{
			ResampleSpectrum(*this, Snapshot, *InstanceData);
		}
		InstanceData->LastSequence = Snapshot.Sequence;
	}
	else
	{
		InstanceData->SecondsSinceLastSnapshot += SafeDeltaSeconds;
		if (bBusChanged || bSettingsChanged)
		{
			SetZeroSpectrum(*InstanceData, GetResolution(*this));
			InstanceData->LastSequence = INDEX_NONE;
		}
	}

	if (StaleDataTimeoutSeconds > 0.0f
		&& InstanceData->SecondsSinceLastSnapshot >= StaleDataTimeoutSeconds)
	{
		SetZeroSpectrum(*InstanceData, GetResolution(*this));
	}

	InstanceData->LastBusName = BusName;
	InstanceData->LastSettingsHash = SettingsHash;
	InstanceData->bSettingsInitialized = true;
	return false;
}

void UNiagaraDataInterfaceAkWwiseAudioSpectrum::GetSpectrumValue(
	FVectorVMExternalFunctionContext& Context)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	VectorVM::FExternalFuncInputHandler<float> InNormalizedPosition(Context);
	VectorVM::FExternalFuncInputHandler<int32> InChannelIndex(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAmplitude(Context);
	for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
	{
		*OutAmplitude.GetDestAndAdvance() = SampleSpectrum(
			InstanceData.Get(),
			InNormalizedPosition.GetAndAdvance(),
			InChannelIndex.GetAndAdvance());
	}
}

void UNiagaraDataInterfaceAkWwiseAudioSpectrum::GetNumChannels(
	FVectorVMExternalFunctionContext& Context)
{
	using namespace NiagaraAkWwiseSpectrumPrivate;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutNumChannels(Context);
	const int32 NumChannels = InstanceData.Get() && InstanceData->bHasValidData ? 1 : 0;
	for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
	{
		*OutNumChannels.GetDestAndAdvance() = NumChannels;
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceAkWwiseAudioSpectrum::GetFunctionsInternal(
	TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	FNiagaraFunctionSignature DefaultSignature;
	DefaultSignature.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Spectrum"));
	DefaultSignature.bMemberFunction = true;
	DefaultSignature.bRequiresContext = false;
	DefaultSignature.bSupportsCPU = true;
	DefaultSignature.bSupportsGPU = false;

	FNiagaraFunctionSignature& SpectrumSignature = OutFunctions.Add_GetRef(DefaultSignature);
	SpectrumSignature.Name = UNiagaraDataInterfaceAudioSpectrum::GetSpectrumFunctionName;
	SpectrumSignature.Inputs.Emplace(
		FNiagaraTypeDefinition::GetFloatDef(),
		TEXT("NormalizedPositionInSpectrum"));
	SpectrumSignature.Inputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("ChannelIndex"));
	SpectrumSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Amplitude"));

	FNiagaraFunctionSignature& ChannelsSignature = OutFunctions.Add_GetRef(DefaultSignature);
	ChannelsSignature.Name = UNiagaraDataInterfaceAudioSpectrum::GetNumChannelsFunctionName;
	ChannelsSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumChannels"));
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseAudioSpectrum, GetSpectrumValue);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseAudioSpectrum, GetNumChannels);

void UNiagaraDataInterfaceAkWwiseAudioSpectrum::GetVMExternalFunction(
	const FVMExternalFunctionBindingInfo& BindingInfo,
	void* InstanceData,
	FVMExternalFunction& OutFunc)
{
	(void)InstanceData;
	if (BindingInfo.Name == UNiagaraDataInterfaceAudioSpectrum::GetSpectrumFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseAudioSpectrum, GetSpectrumValue)::Bind(
			this,
			OutFunc);
	}
	else if (BindingInfo.Name == UNiagaraDataInterfaceAudioSpectrum::GetNumChannelsFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceAkWwiseAudioSpectrum, GetNumChannels)::Bind(
			this,
			OutFunc);
	}
}

bool UNiagaraDataInterfaceAkWwiseAudioSpectrum::CanExecuteOnTarget(
	ENiagaraSimTarget Target) const
{
	return Target == ENiagaraSimTarget::CPUSim;
}

bool UNiagaraDataInterfaceAkWwiseAudioSpectrum::Equals(
	const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceAkWwiseAudioSpectrum* OtherSpectrum =
		Cast<const UNiagaraDataInterfaceAkWwiseAudioSpectrum>(Other);
	return OtherSpectrum
		&& OtherSpectrum->BusName == BusName
		&& OtherSpectrum->bAutoRegisterVisualizationCallback == bAutoRegisterVisualizationCallback
		&& OtherSpectrum->StaleDataTimeoutSeconds == StaleDataTimeoutSeconds;
}

bool UNiagaraDataInterfaceAkWwiseAudioSpectrum::CopyToInternal(
	UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceAkWwiseAudioSpectrum* DestinationSpectrum =
		Cast<UNiagaraDataInterfaceAkWwiseAudioSpectrum>(Destination);
	if (!DestinationSpectrum)
	{
		return false;
	}
	DestinationSpectrum->BusName = BusName;
	DestinationSpectrum->bAutoRegisterVisualizationCallback = bAutoRegisterVisualizationCallback;
	DestinationSpectrum->StaleDataTimeoutSeconds = StaleDataTimeoutSeconds;
	return true;
}

const FName UNiagaraDataInterfaceAkWwiseSpectrum::GetSpectrumFunctionName(TEXT("AudioSpectrum"));
const FName UNiagaraDataInterfaceAkWwiseSpectrum::GetNumChannelsFunctionName(TEXT("GetNumChannels"));
const FName UNiagaraDataInterfaceAkWwiseSpectrum::IsSpectrumValidFunctionName(TEXT("IsSpectrumValid"));

#undef LOCTEXT_NAMESPACE
