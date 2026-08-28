#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ObjectKey.h"
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

	// 活动 MIDI 播放实例记录（按 Event 记录；Game Object 即本组件自身，无需额外维度）。
	// 规则（参考 Doc/Wwise_MIDI_Event循环播放与NoteOff排查说明.md 第 5.3 节）：
	//   1. 首次 Post 传 AK_INVALID_PLAYING_ID 让 Wwise 创建播放实例；
	//   2. 保存返回值；
	//   3. 后续批次（含 Note-Off）一律复用保存的 PlayingID，保证路由到同一实例；
	//   4. StopMidiEvent 时用该 PlayingID 精确停止，随后清空，下次播放重新创建实例。
	// 所有读写均发生在游戏线程（Post/Stop/BeginDestroy），与 StateCS 保护的音频线程读不冲突。
	//
	// key 使用 TObjectKey（记录 object index + serial number）而非裸指针：Event 资产被卸载/替换
	// （如 UGC 切换音频工程）后，即便新对象复用了同一内存地址，serial number 不同也不会误命中，
	// 从而杜绝把消息路由到错误实例。TObjectKey 不参与 GC、不保活对象，且可用 ResolveObjectPtr()
	// 安全取回仍存活的 Event 指针（对象已销毁时返回 nullptr），供 BeginDestroy 精确停止实例。
	TMap<TObjectKey<UAkAudioEvent>, AkPlayingID> ActivePlayingIDs;

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

	// 判断当前待提交的 Posts 中是否至少包含一个 Note-On（速度可为 0 的 Note-On 视作 Note-Off，
	// 语义上等价于停止，不计入）。用于在无活动实例时拦截"纯 Note-Off 批次"，避免新建孤儿实例。
	bool PostsContainNoteOn() const;

	// 清理 ActivePlayingIDs 中因 Event 资产被 GC/卸载而失效的弱引用 key，防止 map 缓慢膨胀。
	void PurgeStalePlayingIDs();

	void HandleWwiseCallback(AkAudioSettings* in_AudioSettings);

	void ProcessIncomingMidiQueue();

	void SendRawMidiMessage(std::vector<unsigned char>& RawMessage);
	
	friend void MyCallback(double DeltaTime, std::vector<unsigned char> *Message, void *UserData);

	friend void HandleRtMidiCallback(UAkMidiMessage* AkMessage, UAkMidiComponent* MidiComponent, std::vector<unsigned char> RawMessage, double DeltaTime);
};



// 已移除：MakePostsAsync 为遗留死代码，其 DoWork 与 HandleRtMidiCallback 完全重复且从未被使用，
// 且会在工作线程中直接操作 UObject / 广播动态委托，存在线程安全隐患。
#pragma endregion
