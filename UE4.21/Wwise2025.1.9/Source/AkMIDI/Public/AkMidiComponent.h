#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AkMidiMessage.h"
#include "AkComponent.h"
#include "AkMidiDevice.h"
#include "AkAudioDevice.h"
#include "Async/AsyncWork.h"

#include <vector>
#include "Containers/Queue.h"

#include "AkMidiComponent.generated.h"


#define MessagePoolMax 30
#define PostsPoolMax MessagePoolMax

UENUM(BlueprintType)
enum class EMidiInputSource : uint8
{
	Unreal UMETA(DisplayName = "Unreal"),
	ExternalDevice UMETA(DisplayName = "External Device"),
	None UMETA(DisplayName = "None")
};

UENUM(BlueprintType)
enum class EMidiOutputTarget : uint8
{
	Wwise UMETA(DisplayName = "Wwise"),
	ExternalDevice UMETA(DisplayName = "External Device"),
	None UMETA(DisplayName = "None")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRtMidiCallback, class UAkMidiMessage*, MidiMessage, float, DeltaTime);

struct FRawMidiPacket
{
	TArray<uint8> Data;
	double DeltaTime = 0.0;
};

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = "AkMIDI", meta = (BlueprintSpawnableComponent))
class AKMIDI_API UAkMidiComponent : public UAkComponent
{
	GENERATED_BODY()

public:
	UAkMidiComponent(const class FObjectInitializer &ObjectInitializer);
	
	~UAkMidiComponent();

	UPROPERTY(BlueprintAssignable, Category = "AkMIDI|Function")
	FRtMidiCallback OnMessageReceived;

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	void GetMidiDevice(TArray<FMidiDevice>& InputDevices, TArray<FMidiDevice>& OutputDevices);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	void OpenMidiInputDevice(uint8 InputPort);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	void OpenMidiOutputDevice(uint8 OutputPort);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	void CloseMidiDevice(EIOType ClosePort);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	bool PostMidiEvent(TArray<UAkMidiMessage*> AkMidiMessages, UAkAudioEvent *AkEvent = nullptr);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	bool StopMidiEvent(UAkAudioEvent *AkEvent = nullptr);

	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	UAkMidiMessage* InsertMidiFx(UAkMidiMessage* MidiMessage);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|AkMidiComponent")
	void MidiFxBypass(bool bIsMidiFxBypass);

	UFUNCTION(BlueprintPure, Category = "AkMIDI|AkMidiComponent")
	EMidiInputSource GetInputSource() const { return InputSource; }

	UFUNCTION(BlueprintPure, Category = "AkMIDI|AkMidiComponent")
	EMidiOutputTarget GetOutputTarget() const { return OutputTarget; }

	//----------------------------------------------------
	virtual bool PostMidiEvent();
	bool GetIsOutputToWwise() const { return OutputTarget == EMidiOutputTarget::Wwise; }
	bool GetIsInputFromUnreal() const { return InputSource == EMidiInputSource::Unreal; }
	virtual void OnRegister() override;



private:
	TArray<UAkMidiMessage*> MessagePool;
	
	TArray<AkMIDIPost> Posts;

	TArray<AkMIDIPost*> PostPool;

	UAkMidiDevice *MidiDevice;

	FAkAudioDevice* AkAudioDevice;
	AkAudioSettings* AudioSettings;

	EMidiOutputTarget OutputTarget = EMidiOutputTarget::Wwise;
	EMidiInputSource InputSource = EMidiInputSource::Unreal;
	bool bMidiFxOnOff = false;

	FMidiDevice DefaultInputDevice{TEXT("Unreal"), 127 };
	FMidiDevice DefaultOutputDevice{TEXT("Wwise"), 127 };


	uint8 MessagePoolCount = 0;
	uint8 PostPoolCount = 0;

	TQueue<FRawMidiPacket, EQueueMode::Mpsc> IncomingMidiQueue;

	friend class MakePostsAsync;



private:
	virtual void MakePost(UAkMidiMessage *MIDINote);

	void HandleWwiseCallback(AkAudioSettings* in_AudioSettings);

	void ProcessIncomingMidiQueue();

	void SendRawMidiMessage(std::vector<unsigned char>& RawMessage);
	
	friend void MyCallback(double DeltaTime, std::vector<unsigned char> *Message, void *UserData);

	friend void HandleRtMidiCallback(UAkMidiMessage* AkMessage, UAkMidiComponent* MidiComponent, std::vector<unsigned char> RawMessage, double DeltaTime);
};



class MakePostsAsync : public FNonAbandonableTask
{
public:
	friend class FAutoDeleteAsyncTask<MakePostsAsync>;

	MakePostsAsync(UAkMidiComponent* MidiComponent, UAkMidiMessage* AkMessage, std::vector<unsigned char> Message, double DeltaTime) : MidiComponent(MidiComponent),AkMessage(AkMessage), RawMessage(Message), DeltaTime(DeltaTime){}

protected:
	UAkMidiComponent* MidiComponent;
	UAkMidiMessage* AkMessage;
	std::vector<unsigned char> RawMessage;
	double DeltaTime;

	void DoWork();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(MakePostsAsync, STATGROUP_ThreadPoolAsyncTasks);
	}
};
#pragma endregion
