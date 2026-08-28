#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AkMidiMessage.h"
#include "AkComponent.h"
#include "AkMidiDevice.h"
#include "AkAudioDevice.h"
#include "HAL/ThreadSafeBool.h"

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
	bool GetIsOutputToWwise() const;
	bool GetIsInputFromUnreal() const;
	virtual void OnRegister() override;
	virtual void BeginDestroy() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;



private:
	// 环形复用池：OnMessageReceived 广播出去的 UAkMidiMessage* 会在后续消息到来时被覆盖。
	// 蓝图侧应在回调内同步读取所需数据，切勿异步/延迟持有该指针，否则会读到脏数据。
	TArray<UAkMidiMessage*> MessagePool;
	
	TArray<AkMIDIPost> Posts;

	TArray<AkMIDIPost*> PostPool;

	UAkMidiDevice *MidiDevice;

	FAkAudioDevice* AkAudioDevice;
	AkAudioSettings* AudioSettings;

	EMidiOutputTarget OutputTarget = EMidiOutputTarget::Wwise;
	EMidiInputSource InputSource = EMidiInputSource::Unreal;
	bool bMidiFxOnOff = false;

	// 保护 InputSource / OutputTarget / bMidiFxOnOff 的跨线程读写（游戏线程写，音频线程读）
	mutable FCriticalSection StateCS;

	FMidiDevice DefaultInputDevice{TEXT("Unreal"), 127 };
	FMidiDevice DefaultOutputDevice{TEXT("Wwise"), 127 };


	uint8 MessagePoolCount = 0;
	uint8 PostPoolCount = 0;

	TQueue<FRawMidiPacket, EQueueMode::Mpsc> IncomingMidiQueue;

	// 音频渲染线程仅置位；真正的消费（广播/蓝图/RtMidi 发送）放在游戏线程 TickComponent 中执行
	FThreadSafeBool bHasPendingMidi = false;

	// 标记 RtMidi 输入回调是否已注册，析构时据此决定是否需要 cancelCallback
	bool bInputCallbackRegistered = false;



private:
	virtual void MakePost(UAkMidiMessage *MIDINote);

	void HandleWwiseCallback(AkAudioSettings* in_AudioSettings);

	void ProcessIncomingMidiQueue();

	void SendRawMidiMessage(std::vector<unsigned char>& RawMessage);
	
	friend void MyCallback(double DeltaTime, std::vector<unsigned char> *Message, void *UserData);

	friend void HandleRtMidiCallback(UAkMidiMessage* AkMessage, UAkMidiComponent* MidiComponent, std::vector<unsigned char> RawMessage, double DeltaTime);
};



// 已移除：MakePostsAsync 为遗留死代码，其 DoWork 与 HandleRtMidiCallback 完全重复且从未被使用，
// 且会在工作线程中直接操作 UObject / 广播动态委托，存在线程安全隐患。
#pragma endregion
