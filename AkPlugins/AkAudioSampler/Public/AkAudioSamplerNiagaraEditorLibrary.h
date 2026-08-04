#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AkAudioSamplerNiagaraEditorLibrary.generated.h"

class UNiagaraSystem;

/** Editor-only helpers used to repair generated Niagara system stacks. */
UCLASS()
class AKAUDIOSAMPLER_API UAkAudioSamplerNiagaraEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Makes the named Particle Update modules contiguous in the supplied order.
	 * The first name is the anchor. Later modules are moved immediately after it.
	 */
	UFUNCTION(BlueprintCallable, Category = "AkAudioSampler|Niagara")
	static bool ReorderParticleUpdateModules(
		UNiagaraSystem* System,
		const TArray<FName>& OrderedModuleScriptNames,
		bool bMoveToEnd,
		FString& OutMessage);

	/** Sets a Vector2 input on a named Particle Update module through Niagara's stack input binder. */
	UFUNCTION(BlueprintCallable, Category = "AkAudioSampler|Niagara")
	static bool SetParticleUpdateModuleVector2Input(
		UNiagaraSystem* System,
		FName ModuleScriptName,
		FName InputName,
		FVector2D Value,
		FString& OutMessage);

	/** Enables or disables every matching module in a Particle Update stack. */
	UFUNCTION(BlueprintCallable, Category = "AkAudioSampler|Niagara")
	static bool SetParticleUpdateModuleEnabled(
		UNiagaraSystem* System,
		FName ModuleScriptName,
		bool bEnabled,
		FString& OutMessage);

	/** Replaces a linked float input on a Particle Update function or dynamic input. */
	UFUNCTION(BlueprintCallable, Category = "AkAudioSampler|Niagara")
	static bool SetParticleUpdateFunctionFloatInputLinkedParameter(
		UNiagaraSystem* System,
		FName FunctionScriptName,
		FName InputName,
		FName LinkedParameterName,
		FString& OutMessage);

	/** Replaces a stock Audio Spectrum DI used by Particle Update with the Ak Wwise-compatible DI. */
	UFUNCTION(BlueprintCallable, Category = "AkAudioSampler|Niagara")
	static bool ReplaceParticleUpdateAudioSpectrumWithAkWwise(
		UNiagaraSystem* System,
		FString BusName,
		int32 Resolution,
		float MinimumFrequency,
		float MaximumFrequency,
		float NoiseFloorDb,
		FString& OutMessage);
};
