#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiComponent.h"
#include "Engine.h"

void MyCallback(double DeltaTime, std::vector<unsigned char> *Message, void *UserData)
{
	UAkMidiComponent* MidiComponent = static_cast<UAkMidiComponent*>(UserData);

	// 回调运行在 RtMidi 线程：仅做入队，不读取/修改任何 UObject 状态，
	// 状态过滤（GetIsInputFromUnreal 等）统一放到游戏线程的 ProcessIncomingMidiQueue 中处理。
	if (!MidiComponent || Message == nullptr || Message->empty())
		return;

	FRawMidiPacket Packet;
	Packet.Data.SetNumUninitialized((int32)Message->size());
	FMemory::Memcpy(Packet.Data.GetData(), Message->data(), Message->size());
	Packet.DeltaTime = DeltaTime;
	MidiComponent->IncomingMidiQueue.Enqueue(MoveTemp(Packet));
}

void HandleRtMidiCallback(UAkMidiMessage* AkMessage, UAkMidiComponent* MidiComponent, std::vector<unsigned char> RawMessage, double DeltaTime)
{
	if (!AkMessage || !MidiComponent)
		return;

	//External Midi Message Send To Wwise
	if (MidiComponent->GetIsOutputToWwise() && !MidiComponent->GetIsInputFromUnreal())
	{
		size_t nBytes = RawMessage.size();
		for (size_t i = 0; i < nBytes;)
		{
			uint8 ID = RawMessage.at(i++);
			uint8 Type = ID >> 4;
			uint8 ChannelOrSubType = ID & 0x0F;

			if (Type >= 0x8 && Type <= 0xE)
			{
				const bool bHasData02 = (Type != 0xC && Type != 0xD);
				const size_t RequiredBytes = bHasData02 ? 2 : 1;
				// i 已指向数据字节，校验剩余长度是否足够，避免越界读取
				if (i + RequiredBytes > nBytes)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MIDI] Truncated message, expected %llu data byte(s)"), (uint64)RequiredBytes);
					break;
				}

				AkMessage->NoteType = (EAkMessageType)(Type & 0x0F);
				AkMessage->Channel = ChannelOrSubType;
				AkMessage->Data01 = RawMessage[i++] & 0xFF;

				if (bHasData02)
				{
					AkMessage->Data02 = RawMessage[i++] & 0xFF;
				}
			}
			//Wwise Not Support SysEx & Midi Clock Event Now
			/*else if (Type == 0xF)
			{
				//SysEx Message Start
				if (ChannelOrSubType == 0)
				{
					MidiComponent->StartSysEx();
					continue;
				}
				//SysEx Message End
				else if (ChannelOrSubType == 7)
				{
					MidiComponent->StopSysEx();
					continue;
				}
			}*/
			else
			{
				// 未识别的状态字节（如 0xF SysEx/Clock），跳出避免死循环
				break;
			}
		}
	}
	//External Midi Message Send To Other Midi Receiver
	else if (!MidiComponent->GetIsOutputToWwise() && !MidiComponent->GetIsInputFromUnreal())
	{
		MidiComponent->SendRawMidiMessage(RawMessage);
	}


	if (MidiComponent->bMidiFxOnOff)
	{
		MidiComponent->InsertMidiFx(AkMessage);
	}

	MidiComponent->MakePost(AkMessage);



	if (MidiComponent->OnMessageReceived.IsBound())
	{
		MidiComponent->OnMessageReceived.Broadcast(AkMessage, 0);

	}

	return;
}



void UAkMidiComponent::OnRegister()
{

	if (!AkAudioDevice)
		AkAudioDevice = FAkAudioDevice::Get();

	Super::OnRegister();
	
	AkAudioDevice->RegisterComponent(this);

	return;
}



void UAkMidiComponent::HandleWwiseCallback(AkAudioSettings* in_AudioSettings)
{
	if (!GetIsOutputToWwise())
		return;

	if (AudioSettings == nullptr)
		AudioSettings = in_AudioSettings;

	ProcessIncomingMidiQueue();
	PostMidiEvent();

	//UE_LOG(LogTemp, Warning, TEXT("HandleWwiseCallback"));

}


UAkMidiComponent::UAkMidiComponent(const class FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer),
OutputTarget(EMidiOutputTarget::Wwise), InputSource(EMidiInputSource::Unreal), bMidiFxOnOff(false), MessagePoolCount(0), PostPoolCount(0)
{
	MidiDevice = NewObject<UAkMidiDevice>();
	MidiDevice->AddToRoot();

	AkAudioDevice = FAkAudioDevice::Get();

	for (int i = 0; i < MessagePoolMax; i++)
	{ 
		UAkMidiMessage* Message = NewObject<UAkMidiMessage>();
		Message->AddToRoot();
		MessagePool.Add(Message);

		AkMIDIPost* Post = new AkMIDIPost();
		PostPool.Add(Post);
	}
}

UAkMidiComponent::~UAkMidiComponent()
{
	CloseMidiDevice(EIOType::IO_Both);

	for (auto Message : MessagePool)
	{
		if (!Message)
			continue;
		if (!Message->IsValidLowLevel())
			continue;

		Message->ConditionalBeginDestroy();
		Message = nullptr;
	}
	MessagePool.Empty();

	for (auto Post : PostPool)
	{
		if (Post != nullptr)
		{
			delete Post;
			Post = nullptr;
		}
	}
	PostPool.Empty();

	Posts.Empty();
}


bool UAkMidiComponent::PostMidiEvent()
{
	if (AkAudioEvent == nullptr || Posts.Num() <= 0)
		return false;
	
	AkGameObjectID GameObjectID = GetAkGameObjectID();

	AkPlayingID PlayingID = AkAudioDevice->PostMidiEvent(AkAudioEvent, GameObjectID, Posts.GetData(), Posts.Num());

	Posts.Empty();

	if (PlayingID > 0)
		return true;
	else
		return false;
}


bool UAkMidiComponent::PostMidiEvent(TArray<UAkMidiMessage*> AkMidiMessages, UAkAudioEvent *AkEvent)
{
	if ((InputSource != EMidiInputSource::Unreal))
		return false;

	if (AkMidiMessages.Num() <= 0)
		return false;

	//Ak Midi Message Send To Wwise
	if (GetIsOutputToWwise())
	{
		for (auto MidiMessage : AkMidiMessages)
		{
			if (!bMidiFxOnOff)
			{
				MakePost(MidiMessage);
			}
			else if (bMidiFxOnOff && (InputSource == EMidiInputSource::Unreal))
			{
				//UAkMidiMessage* AkMessageFxProcessed = NewObject<UAkMidiMessage>(this, TEXT("AkMessageFxProcessed"), EObjectFlags::RF_NoFlags, MidiMessage);
				MidiMessage->BackupMidiMessage();
				InsertMidiFx(MidiMessage);
				MakePost(MidiMessage);
				MidiMessage->RecoverMidiMessage();
			}
			else if (bMidiFxOnOff && (InputSource != EMidiInputSource::Unreal))
			{
				InsertMidiFx(MidiMessage);
				MakePost(MidiMessage);
			}
		}
	}
	//Ak Midi Message Send To Other Midi Receiver
	else
	{
		for (auto MidiMessage : AkMidiMessages)
		{
			if (bMidiFxOnOff)
			{
				MidiMessage->BackupMidiMessage();
				InsertMidiFx(MidiMessage);
			}

			uint8 Status = ((uint8)MidiMessage->NoteType << 4) | MidiMessage->Channel;
			uint8 RawMessage[3] = { Status,(uint8)MidiMessage->Data01, (uint8)MidiMessage->Data02 };

			RtMidiOut* MidiOut = MidiDevice ? MidiDevice->GetRtMidiOut() : nullptr;
			if (MidiOut)
			{
				if (MidiMessage->NoteType != EAkMessageType::AMT_Program_Change && MidiMessage->NoteType != EAkMessageType::AMT_Channel_AfterTouch)
				{
					MidiOut->sendMessage(&RawMessage[0], 3);
				}
				else
				{
					MidiOut->sendMessage(&RawMessage[0], 2);
				}
			}

			MidiMessage->RecoverMidiMessage();
		}
		return true;
	}

	AkGameObjectID GameObjectID = GetAkGameObjectID();
	AkPlayingID PlayingID = 0;
	
	if(AkEvent)
		PlayingID = AkAudioDevice->PostMidiEvent(AkEvent, GameObjectID, Posts.GetData(), Posts.Num());
	else
		PlayingID = AkAudioDevice->PostMidiEvent(AkAudioEvent, GameObjectID, Posts.GetData(), Posts.Num());

	for (auto& Post : Posts) 
	{
		UE_LOG(LogTemp,Log,TEXT("[MIDI] byType = %d, noteNum = %d"), Post.midiEvent.byType, Post.midiEvent.NoteOnOff.byNote);
	}
	Posts.Empty();
	if (PlayingID > 0)
		return true;
	else
		return false;
}

bool UAkMidiComponent::StopMidiEvent(UAkAudioEvent *AkEvent)
{
	AkGameObjectID GameObjectID = GetAkGameObjectID();

	AKRESULT Res = AK_Fail;

	if (AkEvent)
	{
		Res = AkAudioDevice->StopMidiEvent(AkEvent, GameObjectID);

	}
	else
	{
		Res = AkAudioDevice->StopMidiEvent(AkAudioEvent, GameObjectID);
	}


	if (Res == AK_Success)
		return true;
	else
		return false;
}

UAkMidiMessage* UAkMidiComponent::InsertMidiFx_Implementation(UAkMidiMessage* MidiMessage)
{
	return MidiMessage;
}

void UAkMidiComponent::MidiFxBypass(bool bIsMidiFxBypass)
{
	bMidiFxOnOff = !bIsMidiFxBypass;
}


void UAkMidiComponent::MakePost(UAkMidiMessage *MIDINote)
{
	if (MIDINote == nullptr)
		return;

	// 用取模保证索引恒在 [0, PostPool.Num()) 范围内，避免 uint8 溢出与不必要的容量浪费
	if (PostPool.Num() == 0)
		return;
	PostPoolCount = PostPoolCount % PostPool.Num();

	AkMIDIPost *Post = PostPool[PostPoolCount++];

	Post->midiEvent.byChan = MIDINote->Channel;
	Post->uOffset = MIDINote->NoteOffset;

	switch (MIDINote->NoteType)
	{
	case EAkMessageType::AMT_Note_On:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_NOTE_ON;
		Post->midiEvent.NoteOnOff.byNote = MIDINote->Data01;
		Post->midiEvent.NoteOnOff.byVelocity = MIDINote->Data02;
		break;
	case EAkMessageType::AMT_Note_Off:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_NOTE_OFF;
		Post->midiEvent.NoteOnOff.byNote = MIDINote->Data01;
		Post->midiEvent.NoteOnOff.byVelocity = MIDINote->Data02;
		/*Post->uOffset = MIDINote->NoteOffset + AudioSettings->uNumSamplesPerFrame / 2;*/
		break;
	case EAkMessageType::AMT_AfterTouch:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_NOTE_AFTERTOUCH;
		Post->midiEvent.NoteAftertouch.byNote = MIDINote->Data01;
		Post->midiEvent.NoteAftertouch.byValue = MIDINote->Data02;
		break;
	case EAkMessageType::AMT_CC:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_CONTROLLER;
		Post->midiEvent.Cc.byCc = MIDINote->Data01;
		Post->midiEvent.Cc.byValue = MIDINote->Data02;
		break;
	case EAkMessageType::AMT_Program_Change:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_PROGRAM_CHANGE;
		Post->midiEvent.ProgramChange.byProgramNum = MIDINote->Data01;
		break;
	case EAkMessageType::AMT_Channel_AfterTouch:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_CHANNEL_AFTERTOUCH;
		Post->midiEvent.ChanAftertouch.byValue = MIDINote->Data01;
		break;
	case EAkMessageType::AMT_Pitch_Bend:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_PITCH_BEND;
		Post->midiEvent.PitchBend.byValueLsb = MIDINote->Data01;
		Post->midiEvent.PitchBend.byValueMsb = MIDINote->Data02;
		break;
	default:
		Post->midiEvent.byType = AK_MIDI_EVENT_TYPE_INVALID;
		break;
	}

	Posts.Add(*Post);

	return;
}

void UAkMidiComponent::GetMidiDevice(TArray<FMidiDevice>& InputDevices, TArray<FMidiDevice>& OutputDevices)
{
	if (!MidiDevice)
		return;

	AkAudioDevice->OnMessageWaitToSend.BindUObject(this, &UAkMidiComponent::HandleWwiseCallback);

	InputDevices.Empty();
	OutputDevices.Empty();

	InputDevices.Insert(DefaultInputDevice, 0);
	OutputDevices.Insert(DefaultOutputDevice, 0);

	MidiDevice->GetMidiDevice(InputDevices, OutputDevices);

	MidiDevice->GetRtMidiIn()->setCallback(MyCallback, this);


	return;
}

void UAkMidiComponent::OpenMidiInputDevice(uint8 InputPort)
{
	if (!MidiDevice)
		return;
	
	if (InputPort == 127)
	{
		InputSource = EMidiInputSource::Unreal;
	}
	else
	{
		InputSource = EMidiInputSource::ExternalDevice;
		MidiDevice->OpenInput(InputPort);

	}

	return;
}

void UAkMidiComponent::OpenMidiOutputDevice(uint8 OutputPort)
{
	if (!MidiDevice)
		return;

	if (OutputPort == 127)
	{
		OutputTarget = EMidiOutputTarget::Wwise;
	}
	else
	{
		OutputTarget = EMidiOutputTarget::ExternalDevice;
		MidiDevice->OpenOutput(OutputPort);
	}

	return;
}

void UAkMidiComponent::CloseMidiDevice(EIOType ClosePort)
{
	if (!MidiDevice)
		return;

	if (ClosePort == EIOType::IO_Both)
	{
		if (InputSource == EMidiInputSource::ExternalDevice)
		{
			MidiDevice->CloseInput();
		}
		if (OutputTarget == EMidiOutputTarget::ExternalDevice)
		{
			MidiDevice->CloseOutput();
		}
		InputSource = EMidiInputSource::None;
		OutputTarget = EMidiOutputTarget::None;
	}
	else if (ClosePort == EIOType::IO_Input)
	{
		if (InputSource == EMidiInputSource::ExternalDevice)
		{
			MidiDevice->CloseInput();
		}
		InputSource = EMidiInputSource::None;
	}
	else if (ClosePort == EIOType::IO_Output)
	{
		if (OutputTarget == EMidiOutputTarget::ExternalDevice)
		{
			MidiDevice->CloseOutput();
		}
		OutputTarget = EMidiOutputTarget::None;
	}

	return;

}

void UAkMidiComponent::SendRawMidiMessage(std::vector<unsigned char>& RawMessage)
{
	if (!MidiDevice || RawMessage.empty())
		return;

	RtMidiOut* MidiOut = MidiDevice->GetRtMidiOut();
	if (!MidiOut)
		return;

	MidiOut->sendMessage(RawMessage.data(), RawMessage.size());

	return;
}



void UAkMidiComponent::ProcessIncomingMidiQueue()
{
	FRawMidiPacket Packet;
	while (IncomingMidiQueue.Dequeue(Packet))
	{
		// 状态过滤统一在游戏线程处理（原先位于 RtMidi 回调线程的 MyCallback 中）
		if (GetIsInputFromUnreal())
			continue;

		std::vector<unsigned char> RawMessage(Packet.Data.GetData(), Packet.Data.GetData() + Packet.Data.Num());
		if (MessagePool.Num() == 0)
			continue;
		MessagePoolCount = MessagePoolCount % MessagePool.Num();
		UAkMidiMessage* AkMessage = MessagePool[MessagePoolCount++];
		HandleRtMidiCallback(AkMessage, this, RawMessage, Packet.DeltaTime);
	}
}

// 已移除：MakePostsAsync::DoWork 与 HandleRtMidiCallback 逻辑完全重复，且从未被实例化调用。
// 该类会在工作线程中直接操作 UObject / 广播动态委托，存在线程安全隐患。



#pragma endregion
