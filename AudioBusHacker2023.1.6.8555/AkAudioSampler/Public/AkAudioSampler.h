#pragma once

#include "CoreMinimal.h"
#include "AkAudioType.h"
#include "AkAudioDevice.h"
#include "AkComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AkAudioSamplerModule.h"
#include "AkAudioSampler.generated.h"
/**
 * 
 */

UCLASS(Blueprintable)

class AKAUDIOSAMPLER_API UAkAudioSampler : public UAkAudioType
{
	GENERATED_BODY()

public:
	UAkAudioSampler(const class FObjectInitializer& ObjectInitializer);

	/**
	 * @brief Regist Get Audio Buffer Callback per Frame.
	 * @return SpecutrumArray.
	 */
	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise",meta = (DisplayName = "UpdateSampleSpecturmCallback"))
	static TArray<float>UpdateSampleSpecturmCallback(float deltaTime, int32& tick);

	/**
	 * @brief InitSoundEngine.For AudioSampler.
	 * @return InitCallBack.
	 */

	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "RegisterCatchBuffer"))
	static int32 RegisterCatchBuffer();



	UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "StopEventByID"))
	static void StopEventByID(int32 ID);

	/**
 * @brief InitSoundEngine.For AudioSampler.
 * @return InitCallBack.
 */
	//UFUNCTION(BlueprintCallable, Category = "H3D | Wwise", meta = (DisplayName = "UnRegisterCatchBuffer"))
	//static int32 UnRegisterCatchBuffer();

	static void SaveBufferAndChannels(AkAudioBuffer* buffer, unsigned Numchannels, unsigned ucount);

	struct Samplerdata {
		 unsigned count;
		 unsigned sampleRate;
		 void* cookie;
		 AkOutputDeviceID m_defaultOutputDeviceId;
		 AkChannelConfig channelConfig;
		 Ak3DAudioSinkCapabilities audioSinkCapabilities;
		 TArray<float>bufferlist;

	};

	static void HackerCallback(AkAudioBuffer* io_pBufferOut);
};
