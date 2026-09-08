#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiMessage.h"

UAkMidiMessage::UAkMidiMessage(const class FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer),
NoteType(EAkMessageType::AMT_Note_On), Channel(0), NoteOffset(0), Data01(60), Data02(72), bIsDirty(false), MidiMessageBackup(nullptr)
{

}

void UAkMidiMessage::PostLoad()
{
	Super::PostLoad();
	MidiMessageBackup = NewObject<UAkMidiMessage>();
	BackupMidiMessage();

	return;
}



#if WITH_EDITOR
void UAkMidiMessage::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	BackupMidiMessage();

	SetDirtyState(true);
}
#endif

void UAkMidiMessage::SetDirtyState(bool bInIsDirty)
{
	bIsDirty = bInIsDirty;
}

bool UAkMidiMessage::GetDirtyState()
{
	return bIsDirty;
}

void UAkMidiMessage::BackupMidiMessage()
{
	if (MidiMessageBackup == nullptr)
		return;

	MidiMessageBackup->NoteType = (EAkMessageType)this->NoteType;
	MidiMessageBackup->Channel = this->Channel;
	MidiMessageBackup->NoteOffset = this->NoteOffset;
	MidiMessageBackup->Data01 = this->Data01;
	MidiMessageBackup->Data02 = this->Data02;

	return;
}

void UAkMidiMessage::RecoverMidiMessage()
{
	if (MidiMessageBackup == nullptr)
		return;

	this->NoteType = (EAkMessageType)MidiMessageBackup->NoteType;
	this->Channel = MidiMessageBackup->Channel;
	this->NoteOffset = MidiMessageBackup->NoteOffset;
	this->Data01 = MidiMessageBackup->Data01;
	this->Data02 = MidiMessageBackup->Data02;

	return;
}

bool UAkMidiMessage::ToAkMIDIPost(AkMIDIPost& OutPost) const
{
	OutPost = AkMIDIPost{};
	OutPost.midiEvent.byChan = Channel;
	OutPost.uOffset = NoteOffset;

	switch (NoteType)
	{
	case EAkMessageType::AMT_Note_On:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_NOTE_ON;
		OutPost.midiEvent.NoteOnOff.byNote = Data01;
		OutPost.midiEvent.NoteOnOff.byVelocity = Data02;
		break;
	case EAkMessageType::AMT_Note_Off:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_NOTE_OFF;
		OutPost.midiEvent.NoteOnOff.byNote = Data01;
		OutPost.midiEvent.NoteOnOff.byVelocity = Data02;
		break;
	case EAkMessageType::AMT_AfterTouch:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_NOTE_AFTERTOUCH;
		OutPost.midiEvent.NoteAftertouch.byNote = Data01;
		OutPost.midiEvent.NoteAftertouch.byValue = Data02;
		break;
	case EAkMessageType::AMT_CC:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_CONTROLLER;
		OutPost.midiEvent.Cc.byCc = Data01;
		OutPost.midiEvent.Cc.byValue = Data02;
		break;
	case EAkMessageType::AMT_Program_Change:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_PROGRAM_CHANGE;
		OutPost.midiEvent.ProgramChange.byProgramNum = Data01;
		break;
	case EAkMessageType::AMT_Channel_AfterTouch:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_CHANNEL_AFTERTOUCH;
		OutPost.midiEvent.ChanAftertouch.byValue = Data01;
		break;
	case EAkMessageType::AMT_Pitch_Bend:
		OutPost.midiEvent.byType = AK_MIDI_EVENT_TYPE_PITCH_BEND;
		OutPost.midiEvent.PitchBend.byValueLsb = Data01;
		OutPost.midiEvent.PitchBend.byValueMsb = Data02;
		break;
	default:
		return false;
	}

	return true;
}

#pragma endregion
