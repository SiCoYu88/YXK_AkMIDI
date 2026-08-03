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
};
