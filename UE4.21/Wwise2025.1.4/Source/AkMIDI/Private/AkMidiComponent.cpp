#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiComponent.h"
#include "Engine.h"

void MyCallback(double DeltaTime, std::vector<unsigned char> *Message, void *UserData)
{
	UAkMidiComponent* MidiComponent = (UAkMidiComponent*)UserData;

	if (!MidiComponent || MidiComponent->GetIsInputFromUnreal())
		return;

	FRawMidiPacket Packet;
	Packet.Data.SetNumUninitialized(Message->size());
	FMemory::Memcpy(Packet.Data.GetData(), Message->data(), Message->size());
	Packet.DeltaTime = DeltaTime;
	MidiComponent->IncomingMidiQueue.Enqueue(MoveTemp(Packet));
}

void HandleRtMidiCallback(UAkMidiMessage* AkMessage, UAkMidiComponent* MidiComponent, std::vector<unsigned char> RawMessage, double DeltaTime)
{
	if (!AkMessage)
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
				AkMessage->NoteType = (EAkMessageType)(Type & 0x0F);
				AkMessage->Channel = ChannelOrSubType;
				AkMessage->Data01 = RawMessage.at(i++) & 0xFF;

				if (Type != 0xC && Type != 0xD)
				{
					AkMessage->Data02 = RawMessage.at(i++) & 0xFF;
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
		}
	}
	//External Midi Message Send To Other Midi Receiver
	else if (!MidiComponent->GetIsOutputToWwise() && !MidiComponent->GetIsInputFromUnreal())
	{
		if (MidiComponent)
		{
			MidiComponent->SendRawMidiMessage(RawMessage);
		}
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
			
			if (MidiMessage->NoteType != EAkMessageType::AMT_Program_Change && MidiMessage->NoteType != EAkMessageType::AMT_Channel_AfterTouch)
			{
				MidiDevice->GetRtMidiOut()->sendMessage(&RawMessage[0], 3);
			}
			else
			{
				MidiDevice->GetRtMidiOut()->sendMessage(&RawMessage[0], 2);
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

	if (PostPoolCount >= PostsPoolMax - 2)
		PostPoolCount = 0;
	
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
	if (!MidiDevice)
		return;

	MidiDevice->GetRtMidiOut()->sendMessage(RawMessage.data(), RawMessage.size());

	return;
}



void UAkMidiComponent::ProcessIncomingMidiQueue()
{
	FRawMidiPacket Packet;
	while (IncomingMidiQueue.Dequeue(Packet))
	{
		std::vector<unsigned char> RawMessage(Packet.Data.GetData(), Packet.Data.GetData() + Packet.Data.Num());
		if (MessagePoolCount >= MessagePoolMax - 2)
			MessagePoolCount = 0;
		UAkMidiMessage* AkMessage = MessagePool[MessagePoolCount++];
		HandleRtMidiCallback(AkMessage, this, RawMessage, Packet.DeltaTime);
	}
}

void MakePostsAsync::DoWork()
{
	if (!AkMessage)
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
				AkMessage->NoteType = (EAkMessageType)(Type & 0x0F);
				AkMessage->Channel = ChannelOrSubType;
				AkMessage->Data01 = RawMessage.at(i++) & 0xFF;
				
				if (Type != 0xC && Type != 0xD)
				{
					AkMessage->Data02 = RawMessage.at(i++) & 0xFF;
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
		}	
	}
	//External Midi Message Send To Other Midi Receiver
	else if(!MidiComponent->GetIsOutputToWwise() && !MidiComponent->GetIsInputFromUnreal())
	{
		if (MidiComponent)
		{
			MidiComponent->SendRawMidiMessage(RawMessage);
		}
	}

	
	if (MidiComponent->bMidiFxOnOff)
	{
		MidiComponent->InsertMidiFx(AkMessage);
	}

	MidiComponent->MakePost(AkMessage);

	

	if (MidiComponent->OnMessageReceived.IsBound())
	{
		MidiComponent->OnMessageReceived.Broadcast(AkMessage, DeltaTime);

	}

	return;
}



#pragma endregion
