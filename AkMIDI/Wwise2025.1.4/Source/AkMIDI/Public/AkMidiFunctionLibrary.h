#pragma region H3D
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AkMidiComponent.h"
#include "AkMidiFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class AKMIDI_API UAkMidiFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|Function")
	static UAkMidiMessage* CreateAkMidiMessage(
		EAkMessageType NoteType,
		uint8 Channel,
		int32 NoteOffset,
		uint8 Data01,
		uint8 Data02);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|Function")
	static bool PostMidiEvent(
		UAkMidiComponent* MidiComponent, 
		TArray<UAkMidiMessage*> AkMidiMessages, 
		UAkAudioEvent *AkEvent = nullptr);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|Function")
	static bool StopMidiEvent(
		UAkMidiComponent* MidiComponent,
		UAkAudioEvent *AkEvent = nullptr);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|Function")
	static bool OpenMidiDevice(
		UAkMidiComponent* MidiComponent,
		EIOType PortType,
		uint8 Port);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|Function")
	static bool CloseMidiDevice(UAkMidiComponent* MidiComponent, EIOType ClosePort);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|Function")
	static void ModifyAkMidiMessage(UAkMidiMessage* MidiMessage, EMessageParamType NoteValueType, uint8 Value);



};

#pragma endregion
