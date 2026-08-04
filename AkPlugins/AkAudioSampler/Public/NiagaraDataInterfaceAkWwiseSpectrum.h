#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceAudioSpectrum.h"

#include "NiagaraDataInterfaceAkWwiseSpectrum.generated.h"

/** Samples the latest AudioBusHacker spectrum for a named Wwise Bus. */
UCLASS(
	EditInlineNew,
	Category = "Wwise Audio",
	CollapseCategories,
	meta = (DisplayName = "Ak Wwise Spectrum"))
class AKAUDIOSAMPLER_API UNiagaraDataInterfaceAkWwiseSpectrum : public UNiagaraDataInterface
{
	GENERATED_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER(int32, NumChannels)
		SHADER_PARAMETER(int32, Resolution)
		SHADER_PARAMETER(int32, HasValidData)
		SHADER_PARAMETER_SRV(Buffer<float>, SpectrumBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	UNiagaraDataInterfaceAkWwiseSpectrum(const FObjectInitializer& ObjectInitializer);

	/** Wwise Bus containing the AudioBusHacker effect. */
	UPROPERTY(EditAnywhere, Category = "Wwise")
	FString BusName;

	/** Number of samples exposed to Niagara. Values above 64 interpolate the source spectrum. */
	UPROPERTY(EditAnywhere, Category = "Spectrum", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 Resolution;

	/** Frequency represented by normalized spectrum position 0. */
	UPROPERTY(EditAnywhere, Category = "Spectrum", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float MinimumFrequency;

	/** Frequency represented by normalized spectrum position 1. */
	UPROPERTY(EditAnywhere, Category = "Spectrum", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float MaximumFrequency;

	/** Decibel level mapped to zero amplitude. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Spectrum", meta = (ClampMin = "-120.0", ClampMax = "0.0"))
	float NoiseFloorDb;

	/** Automatically keeps the shared AudioBusHacker visualization callback registered. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Wwise")
	bool bAutoRegisterVisualizationCallback;

	/** Clears the spectrum if no new Wwise snapshot arrives within this time. Zero disables the timeout. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Spectrum", meta = (ClampMin = "0.0"))
	float StaleDataTimeoutSeconds;

	static const FName GetSpectrumFunctionName;
	static const FName GetNumChannelsFunctionName;
	static const FName IsSpectrumValidFunctionName;

	void GetSpectrumValue(FVectorVMExternalFunctionContext& Context);
	void GetNumChannels(FVectorVMExternalFunctionContext& Context);
	void IsSpectrumValid(FVectorVMExternalFunctionContext& Context);

	virtual void PostInitProperties() override;

	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(
		void* PerInstanceData,
		FNiagaraSystemInstance* SystemInstance,
		float DeltaSeconds) override;
	virtual void ProvidePerInstanceDataForRenderThread(
		void* DataForRenderThread,
		void* PerInstanceData,
		const FNiagaraSystemInstanceID& SystemInstance) override;

	virtual void GetVMExternalFunction(
		const FVMExternalFunctionBindingInfo& BindingInfo,
		void* InstanceData,
		FVMExternalFunction& OutFunc) override;

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual bool GetFunctionHLSL(
		const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
		const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo,
		int FunctionInstanceIndex,
		FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(
		const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
		FString& OutHLSL) override;
#endif

	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

protected:
#if WITH_EDITORONLY_DATA
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif

	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** CPU adapter that lets stock AudioSpectrum modules consume Wwise spectrum data. */
UCLASS(
	EditInlineNew,
	Category = "Wwise Audio",
	CollapseCategories,
	meta = (DisplayName = "Ak Wwise Audio Spectrum (CPU)"))
class AKAUDIOSAMPLER_API UNiagaraDataInterfaceAkWwiseAudioSpectrum
	: public UNiagaraDataInterfaceAudioSpectrum
{
	GENERATED_BODY()

public:
	UNiagaraDataInterfaceAkWwiseAudioSpectrum(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category = "Wwise")
	FString BusName;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Wwise")
	bool bAutoRegisterVisualizationCallback;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Spectrum", meta = (ClampMin = "0.0"))
	float StaleDataTimeoutSeconds;

	void GetSpectrumValue(FVectorVMExternalFunctionContext& Context);
	void GetNumChannels(FVectorVMExternalFunctionContext& Context);

	virtual void PostInitProperties() override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(
		void* PerInstanceData,
		FNiagaraSystemInstance* SystemInstance,
		float DeltaSeconds) override;
	virtual void GetVMExternalFunction(
		const FVMExternalFunctionBindingInfo& BindingInfo,
		void* InstanceData,
		FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

protected:
#if WITH_EDITORONLY_DATA
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};
