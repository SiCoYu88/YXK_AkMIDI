#pragma once

#include "CoreMinimal.h"
#include "AkAudioType.h"
#include "AkAudioSampler.generated.h"

struct AkAudioBusHackerVisualizationData;

USTRUCT(BlueprintType)
struct AKAUDIOSAMPLER_API FAkAudioBusHackerVisualizationData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	int64 Sequence = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	FString BusName;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	int32 SampleRate = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	int64 ChannelConfig = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	int32 NumChannels = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	int32 AnalyzedChannels = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	int32 Frames = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	float SpectrumMinHz = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	float SpectrumMaxHz = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	float StereoCorrelation = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	float DownstreamGain = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	TArray<float> ChannelPeakDb;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	TArray<float> ChannelRmsDb;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	TArray<float> WaveformMin;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	TArray<float> WaveformMax;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	TArray<float> SpectrumDb;

	UPROPERTY(BlueprintReadOnly, Category = "H3D | Wwise")
	TArray<float> SpectrumFrequenciesHz;
};

UCLASS(Blueprintable)
class AKAUDIOSAMPLER_API UAkAudioSampler : public UAkAudioType
{
	GENERATED_BODY()

public:
	UAkAudioSampler(const FObjectInitializer& ObjectInitializer);

	/** Registers the fixed-size visualization callback exported by AudioBusHacker. */
	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "Register Audio Bus Visualization"))
	static int32 RegisterCatchBuffer();

	/** Stops AudioBusHacker from calling into this UE module. */
	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "Unregister Audio Bus Visualization"))
	static int32 UnregisterCatchBuffer();

	/** Returns whether the AudioBusHacker visualization export was loaded. */
	UFUNCTION(BlueprintPure, Category = "H3D | Wwise", meta = (DisplayName = "Is Audio Bus Visualization Available"))
	static bool IsVisualizationAvailable();

	/**
	 * Returns the newest snapshot for the named Wwise Bus. The Short ID is
	 * generated internally and is not exposed to Blueprint.
	 */
	UFUNCTION(BlueprintPure, Category = "H3D | Wwise", meta = (DisplayName = "Get Audio Bus Visualization"))
	static bool GetLatestVisualizationData(const FString& BusName, FAkAudioBusHackerVisualizationData& OutData);

	/**
	 * Legacy Blueprint node. The returned values now come from Wwise's 64-bin
	 * logarithmic spectrum and are expressed in dBFS.
	 */
	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "Update Sample Spectrum Callback"))
	static TArray<float> UpdateSampleSpecturmCallback(float DeltaTime, int32& Tick);

	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "Stop Event By ID"))
	static void StopEventByID(int32 ID);

	/** Called on the Wwise audio thread. It must remain allocation-free and non-blocking. */
	static void VisualizationCallback(const AkAudioBusHackerVisualizationData* InData);
};
