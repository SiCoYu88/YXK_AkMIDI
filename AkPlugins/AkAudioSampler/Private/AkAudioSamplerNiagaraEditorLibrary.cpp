#include "AkAudioSamplerNiagaraEditorLibrary.h"

#include "NiagaraSystem.h"

#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraStackFunctionInputBinder.h"
#include "NiagaraTypes.h"
#include "UObject/UObjectIterator.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace AkAudioSamplerNiagaraEditor
{
struct FStackNodeGroup
{
	TArray<UNiagaraNode*> StartNodes;
	UNiagaraNode* EndNode = nullptr;
};

UEdGraphPin* GetParameterMapPin(const TArray<UEdGraphPin*>& Pins)
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin == nullptr)
		{
			continue;
		}

		const UEdGraphSchema_Niagara* NiagaraSchema = Cast<UEdGraphSchema_Niagara>(Pin->GetSchema());
		if (NiagaraSchema != nullptr &&
			NiagaraSchema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			return Pin;
		}
	}

	return nullptr;
}

UEdGraphPin* GetParameterMapInputPin(UNiagaraNode& Node)
{
	TArray<UEdGraphPin*> InputPins;
	Node.GetInputPins(InputPins);
	return GetParameterMapPin(InputPins);
}

UEdGraphPin* GetParameterMapOutputPin(UNiagaraNode& Node)
{
	TArray<UEdGraphPin*> OutputPins;
	Node.GetOutputPins(OutputPins);
	return GetParameterMapPin(OutputPins);
}

void GetOrderedModuleNodes(
	UNiagaraNodeOutput& OutputNode,
	TArray<UNiagaraNodeFunctionCall*>& OutModuleNodes)
{
	UNiagaraNode* PreviousNode = &OutputNode;
	while (PreviousNode != nullptr)
	{
		UEdGraphPin* PreviousInputPin = GetParameterMapInputPin(*PreviousNode);
		if (PreviousInputPin == nullptr || PreviousInputPin->LinkedTo.Num() != 1)
		{
			break;
		}

		UNiagaraNode* CurrentNode = Cast<UNiagaraNode>(
			PreviousInputPin->LinkedTo[0]->GetOwningNode());
		if (UNiagaraNodeFunctionCall* ModuleNode = Cast<UNiagaraNodeFunctionCall>(CurrentNode))
		{
			OutModuleNodes.Insert(ModuleNode, 0);
		}
		PreviousNode = CurrentNode;
	}
}

UNiagaraNodeInput* GetStackInputNode(UNiagaraNodeOutput& OutputNode)
{
	UNiagaraNode* PreviousNode = &OutputNode;
	while (PreviousNode != nullptr)
	{
		UEdGraphPin* PreviousInputPin = GetParameterMapInputPin(*PreviousNode);
		if (PreviousInputPin == nullptr || PreviousInputPin->LinkedTo.Num() != 1)
		{
			return nullptr;
		}

		UNiagaraNode* CurrentNode = Cast<UNiagaraNode>(
			PreviousInputPin->LinkedTo[0]->GetOwningNode());
		if (UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(CurrentNode))
		{
			return InputNode;
		}
		PreviousNode = CurrentNode;
	}

	return nullptr;
}

bool BuildStackGroups(
	UNiagaraNodeOutput& OutputNode,
	TArray<FStackNodeGroup>& OutGroups,
	FString& OutError)
{
	UNiagaraNodeInput* InputNode = GetStackInputNode(OutputNode);
	if (InputNode == nullptr)
	{
		OutError = TEXT("Particle Update stack input node was not found.");
		return false;
	}

	FStackNodeGroup InputGroup;
	InputGroup.StartNodes.Add(InputNode);
	InputGroup.EndNode = InputNode;
	OutGroups.Add(MoveTemp(InputGroup));

	TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
	GetOrderedModuleNodes(OutputNode, ModuleNodes);
	for (UNiagaraNodeFunctionCall* ModuleNode : ModuleNodes)
	{
		UEdGraphPin* PreviousOutputPin = GetParameterMapOutputPin(*OutGroups.Last().EndNode);
		if (PreviousOutputPin == nullptr)
		{
			OutError = TEXT("A Niagara stack group has no parameter map output pin.");
			return false;
		}

		FStackNodeGroup ModuleGroup;
		for (UEdGraphPin* LinkedPin : PreviousOutputPin->LinkedTo)
		{
			if (UNiagaraNode* StartNode = Cast<UNiagaraNode>(LinkedPin->GetOwningNode()))
			{
				ModuleGroup.StartNodes.Add(StartNode);
			}
		}
		ModuleGroup.EndNode = ModuleNode;
		if (ModuleGroup.StartNodes.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Module '%s' has no stack group start node."),
				*ModuleNode->GetFunctionName());
			return false;
		}
		OutGroups.Add(MoveTemp(ModuleGroup));
	}

	UEdGraphPin* PreviousOutputPin = GetParameterMapOutputPin(*OutGroups.Last().EndNode);
	if (PreviousOutputPin == nullptr)
	{
		OutError = TEXT("The last Niagara module has no parameter map output pin.");
		return false;
	}

	FStackNodeGroup OutputGroup;
	for (UEdGraphPin* LinkedPin : PreviousOutputPin->LinkedTo)
	{
		if (UNiagaraNode* StartNode = Cast<UNiagaraNode>(LinkedPin->GetOwningNode()))
		{
			OutputGroup.StartNodes.Add(StartNode);
		}
	}
	OutputGroup.EndNode = &OutputNode;
	if (OutputGroup.StartNodes.IsEmpty())
	{
		OutError = TEXT("Particle Update output group has no start node.");
		return false;
	}
	OutGroups.Add(MoveTemp(OutputGroup));
	return true;
}

FName GetModuleScriptName(const FStackNodeGroup& Group)
{
	const UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Group.EndNode);
	if (FunctionCall == nullptr)
	{
		return NAME_None;
	}
	if (FunctionCall->FunctionScript != nullptr)
	{
		return FunctionCall->FunctionScript->GetFName();
	}
	return FName(*FunctionCall->GetFunctionName());
}

void LinkPins(UEdGraphPin& OutputPin, UEdGraphPin& InputPin)
{
	OutputPin.MakeLinkTo(&InputPin);
	OutputPin.GetOwningNode()->PinConnectionListChanged(&OutputPin);
	InputPin.GetOwningNode()->PinConnectionListChanged(&InputPin);
}

bool RewireGroups(const TArray<FStackNodeGroup>& Groups, FString& OutError)
{
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num() - 1; ++GroupIndex)
	{
		UEdGraphPin* OutputPin = GetParameterMapOutputPin(*Groups[GroupIndex].EndNode);
		if (OutputPin == nullptr)
		{
			OutError = TEXT("Could not find a parameter map output while rewiring the stack.");
			return false;
		}
		OutputPin->BreakAllPinLinks(true);
	}

	for (int32 GroupIndex = 0; GroupIndex < Groups.Num() - 1; ++GroupIndex)
	{
		UEdGraphPin* OutputPin = GetParameterMapOutputPin(*Groups[GroupIndex].EndNode);
		for (UNiagaraNode* StartNode : Groups[GroupIndex + 1].StartNodes)
		{
			UEdGraphPin* InputPin = GetParameterMapInputPin(*StartNode);
			if (InputPin == nullptr)
			{
				OutError = TEXT("Could not find a parameter map input while rewiring the stack.");
				return false;
			}
			LinkPins(*OutputPin, *InputPin);
		}
	}

	return true;
}
} // namespace AkAudioSamplerNiagaraEditor
#endif

bool UAkAudioSamplerNiagaraEditorLibrary::ReorderParticleUpdateModules(
	UNiagaraSystem* System,
	const TArray<FName>& OrderedModuleScriptNames,
	bool bMoveToEnd,
	FString& OutMessage)
{
#if WITH_EDITOR
	using namespace AkAudioSamplerNiagaraEditor;

	if (System == nullptr)
	{
		OutMessage = TEXT("Niagara system is null.");
		return false;
	}
	if (OrderedModuleScriptNames.Num() < 2)
	{
		OutMessage = TEXT("At least an anchor and one module to move are required.");
		return false;
	}

	int32 ReorderedStackCount = 0;
	for (TObjectIterator<UNiagaraNodeOutput> It; It; ++It)
	{
		UNiagaraNodeOutput* OutputNode = *It;
		if (OutputNode->GetOutermost() != System->GetOutermost() ||
			OutputNode->GetUsage() != ENiagaraScriptUsage::ParticleUpdateScript)
		{
			continue;
		}

		TArray<FStackNodeGroup> Groups;
		FString StackError;
		if (!BuildStackGroups(*OutputNode, Groups, StackError))
		{
			continue;
		}

		TArray<int32> NamedGroupIndices;
		bool bContainsAllNamedModules = true;
		for (const FName ModuleName : OrderedModuleScriptNames)
		{
			int32 MatchIndex = INDEX_NONE;
			int32 MatchCount = 0;
			for (int32 GroupIndex = 1; GroupIndex < Groups.Num() - 1; ++GroupIndex)
			{
				if (GetModuleScriptName(Groups[GroupIndex]) == ModuleName)
				{
					MatchIndex = GroupIndex;
					++MatchCount;
				}
			}
			if (MatchCount != 1)
			{
				bContainsAllNamedModules = false;
				break;
			}
			NamedGroupIndices.Add(MatchIndex);
		}
		if (!bContainsAllNamedModules)
		{
			continue;
		}

		TArray<FStackNodeGroup> NamedGroups;
		for (const int32 GroupIndex : NamedGroupIndices)
		{
			NamedGroups.Add(Groups[GroupIndex]);
		}

		const FStackNodeGroup InputGroup = Groups[0];
		const FStackNodeGroup OutputGroup = Groups.Last();
		TArray<FStackNodeGroup> NewGroups;
		NewGroups.Add(InputGroup);
		bool bInsertedNamedGroups = false;
		for (int32 GroupIndex = 1; GroupIndex < Groups.Num() - 1; ++GroupIndex)
		{
			const FName CurrentName = GetModuleScriptName(Groups[GroupIndex]);
			if (OrderedModuleScriptNames.Contains(CurrentName))
			{
				if (!bMoveToEnd && CurrentName == OrderedModuleScriptNames[0])
				{
					NewGroups.Append(NamedGroups);
					bInsertedNamedGroups = true;
				}
				continue;
			}
			NewGroups.Add(Groups[GroupIndex]);
		}
		if (bMoveToEnd)
		{
			NewGroups.Append(NamedGroups);
			bInsertedNamedGroups = true;
		}
		if (!bInsertedNamedGroups)
		{
			OutMessage = TEXT("Failed to insert the requested module sequence.");
			return false;
		}
		NewGroups.Add(OutputGroup);

		UNiagaraGraph* Graph = OutputNode->GetNiagaraGraph();
		if (Graph == nullptr)
		{
			continue;
		}
		Graph->Modify();
		for (const FStackNodeGroup& Group : NewGroups)
		{
			if (Group.EndNode != nullptr)
			{
				Group.EndNode->Modify();
			}
			for (UNiagaraNode* StartNode : Group.StartNodes)
			{
				StartNode->Modify();
			}
		}

		if (!RewireGroups(NewGroups, OutMessage))
		{
			return false;
		}
		++ReorderedStackCount;
	}

	if (ReorderedStackCount == 0)
	{
		OutMessage = TEXT("No Particle Update stack contained each requested module exactly once.");
		return false;
	}

	System->Modify();
	System->RequestCompile(true);
	OutMessage = FString::Printf(
		TEXT("Reordered %d Particle Update stack(s)."),
		ReorderedStackCount);
	return true;
#else
	OutMessage = TEXT("This function is only available in editor builds.");
	return false;
#endif
}

bool UAkAudioSamplerNiagaraEditorLibrary::SetParticleUpdateModuleVector2Input(
	UNiagaraSystem* System,
	FName ModuleScriptName,
	FName InputName,
	FVector2D Value,
	FString& OutMessage)
{
#if WITH_EDITOR
	if (System == nullptr)
	{
		OutMessage = TEXT("Niagara system is null.");
		return false;
	}

	int32 UpdatedModuleCount = 0;
	for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitter VersionedEmitter = EmitterHandle.GetInstance();
		FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
		if (EmitterData == nullptr || EmitterData->UpdateScriptProps.Script == nullptr)
		{
			continue;
		}

		const UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
		if (ScriptSource == nullptr || ScriptSource->NodeGraph == nullptr)
		{
			continue;
		}

		TArray<UNiagaraNodeFunctionCall*> FunctionNodes;
		ScriptSource->NodeGraph->GetNodesOfClass(FunctionNodes);
		for (UNiagaraNodeFunctionCall* FunctionNode : FunctionNodes)
		{
			if (FunctionNode == nullptr || FunctionNode->FunctionScript == nullptr ||
				FunctionNode->FunctionScript->GetFName() != ModuleScriptName)
			{
				continue;
			}

			FNiagaraStackFunctionInputBinder InputBinder;
			FText BindError;
			const bool bBound = InputBinder.TryBind(
				EmitterData->UpdateScriptProps.Script,
				{},
				FCompileConstantResolver(VersionedEmitter, ENiagaraScriptUsage::ParticleUpdateScript),
				EmitterHandle.GetUniqueInstanceName(),
				FunctionNode,
				InputName,
				FNiagaraTypeDefinition::GetVec2Def(),
				true,
				BindError);
			if (!bBound)
			{
				OutMessage = FString::Printf(
					TEXT("Failed to bind %s.%s: %s"),
					*ModuleScriptName.ToString(),
					*InputName.ToString(),
					*BindError.ToString());
				return false;
			}

			InputBinder.SetValue(FVector2f(Value));
			++UpdatedModuleCount;
		}
	}

	if (UpdatedModuleCount == 0)
	{
		OutMessage = FString::Printf(
			TEXT("Particle Update module '%s' was not found."),
			*ModuleScriptName.ToString());
		return false;
	}

	System->Modify();
	System->RequestCompile(true);
	OutMessage = FString::Printf(
		TEXT("Updated %d '%s' module input(s)."),
		UpdatedModuleCount,
		*ModuleScriptName.ToString());
	return true;
#else
	OutMessage = TEXT("This function is only available in editor builds.");
	return false;
#endif
}

bool UAkAudioSamplerNiagaraEditorLibrary::SetParticleUpdateModuleEnabled(
	UNiagaraSystem* System,
	FName ModuleScriptName,
	bool bEnabled,
	FString& OutMessage)
{
#if WITH_EDITOR
	using namespace AkAudioSamplerNiagaraEditor;

	if (System == nullptr)
	{
		OutMessage = TEXT("Niagara system is null.");
		return false;
	}

	int32 UpdatedModuleCount = 0;
	for (TObjectIterator<UNiagaraNodeOutput> It; It; ++It)
	{
		UNiagaraNodeOutput* OutputNode = *It;
		if (OutputNode->GetOutermost() != System->GetOutermost() ||
			OutputNode->GetUsage() != ENiagaraScriptUsage::ParticleUpdateScript)
		{
			continue;
		}

		TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
		GetOrderedModuleNodes(*OutputNode, ModuleNodes);
		for (UNiagaraNodeFunctionCall* ModuleNode : ModuleNodes)
		{
			if (ModuleNode != nullptr && ModuleNode->FunctionScript != nullptr &&
				ModuleNode->FunctionScript->GetFName() == ModuleScriptName)
			{
				FNiagaraStackGraphUtilities::SetModuleIsEnabled(*ModuleNode, bEnabled);
				++UpdatedModuleCount;
			}
		}
	}

	if (UpdatedModuleCount == 0)
	{
		OutMessage = FString::Printf(
			TEXT("Particle Update module '%s' was not found."),
			*ModuleScriptName.ToString());
		return false;
	}

	System->Modify();
	System->RequestCompile(true);
	OutMessage = FString::Printf(
		TEXT("Set %d '%s' module(s) enabled=%s."),
		UpdatedModuleCount,
		*ModuleScriptName.ToString(),
		bEnabled ? TEXT("true") : TEXT("false"));
	return true;
#else
	OutMessage = TEXT("This function is only available in editor builds.");
	return false;
#endif
}

bool UAkAudioSamplerNiagaraEditorLibrary::SetParticleUpdateFunctionFloatInputLinkedParameter(
	UNiagaraSystem* System,
	FName FunctionScriptName,
	FName InputName,
	FName LinkedParameterName,
	FString& OutMessage)
{
#if WITH_EDITOR
	if (System == nullptr)
	{
		OutMessage = TEXT("Niagara system is null.");
		return false;
	}

	int32 UpdatedFunctionCount = 0;
	for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitter VersionedEmitter = EmitterHandle.GetInstance();
		FVersionedNiagaraEmitterData* EmitterData = VersionedEmitter.GetEmitterData();
		if (EmitterData == nullptr || EmitterData->UpdateScriptProps.Script == nullptr)
		{
			continue;
		}

		UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
		if (ScriptSource == nullptr || ScriptSource->NodeGraph == nullptr)
		{
			continue;
		}

		TArray<UNiagaraNodeFunctionCall*> FunctionNodes;
		ScriptSource->NodeGraph->GetNodesOfClass(FunctionNodes);
		for (UNiagaraNodeFunctionCall* FunctionNode : FunctionNodes)
		{
			if (FunctionNode == nullptr || FunctionNode->FunctionScript == nullptr ||
				FunctionNode->FunctionScript->GetFName() != FunctionScriptName)
			{
				continue;
			}

			UEdGraphPin* FunctionMapInput = AkAudioSamplerNiagaraEditor::GetParameterMapInputPin(*FunctionNode);
			if (FunctionMapInput == nullptr || FunctionMapInput->LinkedTo.Num() != 1)
			{
				continue;
			}

			UNiagaraNode* OverrideNode = Cast<UNiagaraNode>(
				FunctionMapInput->LinkedTo[0]->GetOwningNode());
			if (OverrideNode == nullptr ||
				OverrideNode->GetClass()->GetFName() != TEXT("NiagaraNodeParameterMapSet"))
			{
				continue;
			}

			const FName AliasedInputName(
				*(FunctionNode->GetFunctionName() + TEXT(".") + InputName.ToString()));
			TArray<UEdGraphPin*> OverrideInputPins;
			OverrideNode->GetInputPins(OverrideInputPins);
			UEdGraphPin** OverridePinPtr = OverrideInputPins.FindByPredicate(
				[AliasedInputName](const UEdGraphPin* Pin)
				{
					return Pin != nullptr && Pin->PinName == AliasedInputName;
				});
			if (OverridePinPtr == nullptr)
			{
				continue;
			}

			UEdGraphPin* OverridePin = *OverridePinPtr;
			if (OverridePin->LinkedTo.Num() != 1)
			{
				OutMessage = FString::Printf(
					TEXT("Input '%s' does not have exactly one linked value."),
					*AliasedInputName.ToString());
				return false;
			}

			UEdGraphNode* PreviousValueNode = OverridePin->LinkedTo[0]->GetOwningNode();
			if (PreviousValueNode == nullptr ||
				PreviousValueNode->GetClass()->GetFName() != TEXT("NiagaraNodeParameterMapGet"))
			{
				OutMessage = FString::Printf(
					TEXT("Input '%s' is not currently a linked parameter."),
					*AliasedInputName.ToString());
				return false;
			}

			UNiagaraGraph* Graph = FunctionNode->GetNiagaraGraph();
			if (Graph == nullptr)
			{
				continue;
			}

			Graph->Modify();
			OverrideNode->Modify();
			FunctionNode->Modify();
			Graph->RemoveNode(PreviousValueNode);
			if (OverridePin->LinkedTo.Num() != 0)
			{
				OutMessage = FString::Printf(
					TEXT("Failed to clear the previous value for input '%s'."),
					*AliasedInputName.ToString());
				return false;
			}

			const TSet<FNiagaraVariableBase> KnownParameters;
			const FNiagaraVariableBase LinkedParameter(
				FNiagaraTypeDefinition::GetFloatDef(),
				LinkedParameterName);
			FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
				*OverridePin,
				LinkedParameter,
				KnownParameters);
			++UpdatedFunctionCount;
		}
	}

	if (UpdatedFunctionCount == 0)
	{
		OutMessage = FString::Printf(
			TEXT("Function '%s' input '%s' was not found."),
			*FunctionScriptName.ToString(),
			*InputName.ToString());
		return false;
	}

	System->Modify();
	System->RequestCompile(true);
	OutMessage = FString::Printf(
		TEXT("Linked %d '%s.%s' input(s) to '%s'."),
		UpdatedFunctionCount,
		*FunctionScriptName.ToString(),
		*InputName.ToString(),
		*LinkedParameterName.ToString());
	return true;
#else
	OutMessage = TEXT("This function is only available in editor builds.");
	return false;
#endif
}
