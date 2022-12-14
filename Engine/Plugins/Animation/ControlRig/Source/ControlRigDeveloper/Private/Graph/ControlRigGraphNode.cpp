// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/WatchedPin.h"
#include "KismetCompiler.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "ControlRig.h"
#include "Textures/SlateIcon.h"
#include "Units/RigUnit.h"
#include "ControlRigBlueprint.h"
#include "PropertyPathHelpers.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "ScopedTransaction.h"
#include "StructReference.h"
#include "UObject/PropertyPortFlags.h"
#include "ControlRigBlueprintUtils.h"
#include "Curves/CurveFloat.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "ControlRigDeveloper.h"
#include "ControlRigObjectVersion.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigGraphNode"

UControlRigGraphNode::UControlRigGraphNode()
: Dimensions(0.0f, 0.0f)
, NodeTitle(FText::GetEmpty())
, FullNodeTitle(FText::GetEmpty())
, NodeTopologyVersion(INDEX_NONE)
, CachedTitleColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
, CachedNodeColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
#if WITH_EDITOR
, bEnableProfiling(false)
#endif
{
	bHasCompilerMessage = false;
	ErrorType = (int32)EMessageSeverity::Info + 1;
}

FText UControlRigGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if(NodeTitle.IsEmpty())
	{
		FString SubTitle;
		if(URigVMNode* ModelNode = GetModelNode())
		{
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(ModelNode))
			{
				if (UnitNode->GetScriptStruct()->IsChildOf(FRigUnit::StaticStruct()))
				{
					if (TSharedPtr<FStructOnScope> StructOnScope = UnitNode->ConstructStructInstance())
					{
						FRigUnit* RigUnit = (FRigUnit*)StructOnScope->GetStructMemory();
						NodeTitle = FText::FromString(RigUnit->GetUnitLabel());
					}
				}
			}

			else if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(ModelNode))
			{
				if(URigVMLibraryNode* ReferencedNode = FunctionReferenceNode->GetReferencedNode())
				{
					UPackage* ReferencedPackage = ReferencedNode->GetOutermost();
					if(ReferencedPackage != ModelNode->GetOutermost())
					{
						SubTitle = FString::Printf(TEXT("From %s"), *ReferencedPackage->GetName());
					}
					else
					{
						static const FString LocalFunctionString = TEXT("Local Function");
						SubTitle = LocalFunctionString;
					}
				}
			}

			else if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(ModelNode))
			{
				static const FString CollapseNodeString = TEXT("Collapsed Graph");
				SubTitle = CollapseNodeString;
			}

			else if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(ModelNode))
			{
				if(VariableNode->IsLocalVariable())
				{
					static const FString LocalVariableString = TEXT("Local Variable");
					const FString DefaultValue = VariableNode->GetVariableDescription().DefaultValue;
					if(DefaultValue.IsEmpty())
					{
						SubTitle = LocalVariableString;
					}
					else
					{
						SubTitle = FString::Printf(TEXT("%s\nDefault %s"), *LocalVariableString, *DefaultValue);
					}
				}
				else if (VariableNode->IsInputArgument())
				{
					SubTitle = TEXT("Input parameter");
				}
				else
				{
					if(UBlueprint* Blueprint = GetBlueprint())
					{
						const FName VariableName = VariableNode->GetVariableName();
						for(const FBPVariableDescription& NewVariable : Blueprint->NewVariables)
						{
							if(NewVariable.VarName == VariableName)
							{
								FString DefaultValue = NewVariable.DefaultValue;
								if(DefaultValue.IsEmpty())
								{
									static const FString VariableString = TEXT("Variable");
									SubTitle = VariableString;
								}
								else
								{
									// Change the order of values in rotators so that they match the pin order
									if (!NewVariable.VarType.IsContainer() && NewVariable.VarType.PinSubCategoryObject == TBaseStructure<FRotator>::Get())
									{
										TArray<FString> Values;
										DefaultValue.ParseIntoArray(Values, TEXT(","));
										Values.Swap(0, 1);
										Values.Swap(0, 2);
										DefaultValue = FString::Join(Values, TEXT(","));										
									}
									SubTitle = FString::Printf(TEXT("Default %s"), *DefaultValue);
								}
								break;
							}
						}
					}
				}

				if(SubTitle.Len() > 40)
				{
					SubTitle = SubTitle.Left(36) + TEXT(" ...");
				}
			}

			if (NodeTitle.IsEmpty())
			{
				NodeTitle = FText::FromString(ModelNode->GetNodeTitle());
			}
		}

		if(IsDeprecated())
		{
			NodeTitle = FText::FromString(FString::Printf(TEXT("%s (Deprecated)"), *NodeTitle.ToString()));
		}

		FullNodeTitle = NodeTitle;

		if(!SubTitle.IsEmpty())
		{
			FullNodeTitle = FText::FromString(FString::Printf(TEXT("%s\n%s"), *NodeTitle.ToString(), *SubTitle));
		}
	}

	if(TitleType == ENodeTitleType::FullTitle)
	{
		return FullNodeTitle;
	}
	return NodeTitle;
}

void UControlRigGraphNode::ReconstructNode()
{
	ReconstructNode_Internal();
}

void UControlRigGraphNode::ReconstructNode_Internal(bool bForce)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetOuter()); 
	if (RigGraph && !bForce)
	{
		if (RigGraph->bIsTemporaryGraphForCopyPaste)
		{
			return;
		}

		// if this node has been saved prior to our custom version,
		// don't reset the node
		int32 LinkerVersion = RigGraph->GetLinkerCustomVersion(FControlRigObjectVersion::GUID);
		if (LinkerVersion < FControlRigObjectVersion::SwitchedToRigVM)
		{
			return;
		}
	}

#if WITH_EDITOR
	bEnableProfiling = false;
	if(RigGraph)
	{
		if(UControlRigBlueprint* RigBlueprint = RigGraph->GetBlueprint())
		{
			bEnableProfiling = RigBlueprint->VMRuntimeSettings.bEnableProfiling;
		}
	}
#endif

	// Clear previously set messages
	ErrorMsg.Reset();

	// Move the existing pins to a saved array
	TArray<UEdGraphPin*> OldPins(Pins);
	Pins.Reset();

	// Recreate the new pins
	CachedPins.Reset();
	CachedModelPins.Reset();
	ReallocatePinsDuringReconstruction(OldPins);

	// Maintain watches up to date
	if (URigVMNode* Node = GetModelNode())
	{
		UBlueprint* Blueprint = GetBlueprint();
		for (UEdGraphPin* NewPin : Pins)
		{
			const FString PinName = NewPin->GetName();
			FString Left, Right = PinName;
			URigVMPin::SplitPinPathAtStart(PinName, Left, Right);
			if (URigVMPin* ModelPin = Node->FindPin(Right))
			{
				if (ModelPin->RequiresWatch())
				{
					FKismetDebugUtilities::AddPinWatch(Blueprint, FBlueprintWatchedPin(NewPin));
				}
			}
		}
	}
	
	RewireOldPinsToNewPins(OldPins, Pins);

	// Let subclasses do any additional work
	PostReconstructNode();

	if (RigGraph)
	{
		RigGraph->NotifyGraphChanged();
	}
}

bool UControlRigGraphNode::IsDeprecated() const
{
	if(URigVMNode* ModelNode = GetModelNode())
	{
		if(URigVMUnitNode* StructModelNode = Cast<URigVMUnitNode>(ModelNode))
		{
			return StructModelNode->IsDeprecated();
		}
	}
	return Super::IsDeprecated();
}

FEdGraphNodeDeprecationResponse UControlRigGraphNode::GetDeprecationResponse(EEdGraphNodeDeprecationType DeprecationType) const
{
	FEdGraphNodeDeprecationResponse Response = Super::GetDeprecationResponse(DeprecationType);

	if(URigVMNode* ModelNode = GetModelNode())
	{
		if(URigVMUnitNode* StructModelNode = Cast<URigVMUnitNode>(ModelNode))
		{
			FString DeprecatedMetadata = StructModelNode->GetDeprecatedMetadata();
			if (!DeprecatedMetadata.IsEmpty())
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("DeprecatedMetadata"), FText::FromString(DeprecatedMetadata));
				Response.MessageText = FText::Format(LOCTEXT("ControlRigGraphNodeDeprecationMessage", "Warning: This node is deprecated from: {DeprecatedMetadata}"), Args);
			}
		}
	}

	return Response;
}

void UControlRigGraphNode::ReallocatePinsDuringReconstruction(const TArray<UEdGraphPin*>& OldPins)
{
	AllocateDefaultPins();
}

void UControlRigGraphNode::RewireOldPinsToNewPins(TArray<UEdGraphPin*>& InOldPins, TArray<UEdGraphPin*>& InNewPins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for(UEdGraphPin* OldPin : InOldPins)
	{
		for(UEdGraphPin* NewPin : InNewPins)
		{
			if(OldPin->PinName == NewPin->PinName && OldPin->PinType == NewPin->PinType && OldPin->Direction == NewPin->Direction)
			{
				// make sure to remove invalid entries from the linked to list
				OldPin->LinkedTo.Remove(nullptr);
				
				NewPin->MovePersistentDataFromOldPin(*OldPin);
				break;
			}
		}
	}

	DestroyPinList(InOldPins);
}

void UControlRigGraphNode::DestroyPinList(TArray<UEdGraphPin*>& InPins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UBlueprint* Blueprint = GetBlueprint();
	bool bNotify = false;
	if (Blueprint != nullptr)
	{
		bNotify = !Blueprint->bIsRegeneratingOnLoad;
	}

	// Throw away the original pins
	for (UEdGraphPin* Pin : InPins)
	{
		Pin->BreakAllPinLinks(bNotify);

		UEdGraphNode::DestroyPin(Pin);
	}
}

void UControlRigGraphNode::PostReconstructNode()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (UEdGraphPin* Pin : Pins)
	{
		SetupPinDefaultsFromModel(Pin);
	}

	bCanRenameNode = false;

	if(URigVMNode* ModelNode = GetModelNode())
	{
		SetColorFromModel(ModelNode->GetNodeColor());
	}
}

void UControlRigGraphNode::SetColorFromModel(const FLinearColor& InColor)
{
	static const FLinearColor TitleToNodeColor(0.35f, 0.35f, 0.35f, 1.f);
	CachedNodeColor = InColor * TitleToNodeColor;
	CachedTitleColor = InColor;
}

void UControlRigGraphNode::HandleClearArray(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(URigVMController* Controller = GetController())
	{
		Controller->ClearArrayPin(InPinPath);
	}
}

void UControlRigGraphNode::HandleAddArrayElement(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (URigVMController* Controller = GetController())
	{
		Controller->OpenUndoBracket(TEXT("Add Array Pin"));
		FString PinPath = Controller->AddArrayPin(InPinPath, FString(), true, true);
		Controller->SetPinExpansion(InPinPath, true);
		Controller->SetPinExpansion(PinPath, true);
		Controller->CloseUndoBracket();
	}
}

void UControlRigGraphNode::HandleRemoveArrayElement(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (URigVMController* Controller = GetController())
	{
		Controller->RemoveArrayPin(InPinPath, true, true);
	}
}

void UControlRigGraphNode::HandleInsertArrayElement(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (URigVMController* Controller = GetController())
	{
		if (URigVMPin* ArrayElementPin = GetModelPinFromPinPath(InPinPath))
			{
			if (URigVMPin* ArrayPin = ArrayElementPin->GetParentPin())
				{
				Controller->OpenUndoBracket(TEXT("Add Array Pin"));
				FString PinPath = Controller->InsertArrayPin(InPinPath, ArrayElementPin->GetPinIndex() + 1, FString(), true, true);
				Controller->SetPinExpansion(InPinPath, true);
				Controller->SetPinExpansion(PinPath, true);
				Controller->CloseUndoBracket();
			}
		}
	}
}

int32 UControlRigGraphNode::GetInstructionIndex(bool bAsInput) const
{
	if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetGraph()))
	{
		return RigGraph->GetInstructionIndex(this, bAsInput);
	}
	return INDEX_NONE;
}

FLinearColor UControlRigGraphNode::GetNodeProfilingColor() const
{
#if WITH_EDITOR
	if(bEnableProfiling)
	{
		if(UControlRigBlueprint* Blueprint = GetTypedOuter<UControlRigBlueprint>())
		{
			if(UControlRig* DebuggedControlRig = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
			{
				if(URigVMNode* ModelNode = GetModelNode())
				{
					const double MicroSeconds = ModelNode->GetInstructionMicroSeconds(DebuggedControlRig->GetVM(), FRigVMASTProxy());
					if(MicroSeconds >= 0.0)
					{
						if(Blueprint->RigGraphDisplaySettings.bAutoDetermineRange)
						{
							if(MicroSeconds < Blueprint->RigGraphDisplaySettings.MinMicroSeconds)
							{
								Blueprint->RigGraphDisplaySettings.MinMicroSeconds = MicroSeconds;
							}
							if(MicroSeconds > Blueprint->RigGraphDisplaySettings.MaxMicroSeconds)
							{
								Blueprint->RigGraphDisplaySettings.MaxMicroSeconds = MicroSeconds;
							}
						}
							
						const double MinMicroSeconds = Blueprint->RigGraphDisplaySettings.LastMinMicroSeconds;
						const double MaxMicroSeconds = Blueprint->RigGraphDisplaySettings.LastMaxMicroSeconds;
						if(MaxMicroSeconds <= MinMicroSeconds)
						{
							return FLinearColor::Black;
						}
			
						const FLinearColor& MinColor = Blueprint->RigGraphDisplaySettings.MinDurationColor;
						const FLinearColor& MaxColor = Blueprint->RigGraphDisplaySettings.MaxDurationColor;

						const double T = (MicroSeconds - MinMicroSeconds) / (MaxMicroSeconds - MinMicroSeconds);
						return FMath::Lerp<FLinearColor>(MinColor, MaxColor, (float)T);
					}
				}
			}
		}
	}
#endif
	return FLinearColor::Black;
}

void UControlRigGraphNode::AllocateDefaultPins()
{
	ExecutePins.Reset();
	InputPins.Reset();
	InputOutputPins.Reset();
	OutputPins.Reset();

	if (URigVMNode* ModelNode = GetModelNode())
	{
		for(int32 PinListIndex=0; PinListIndex<2; PinListIndex++)
		{
			const TArray<URigVMPin*>& ModelPins = PinListIndex == 0 ? ModelNode->GetPins() : ModelNode->GetOrphanedPins();
			for (URigVMPin* ModelPin : ModelPins)
			{
				if (ModelPin->ShowInDetailsPanelOnly())
				{
					continue;
				}
				if (ModelPin->GetDirection() == ERigVMPinDirection::IO)
				{
					if (ModelPin->IsStruct())
					{
						if (ModelPin->GetScriptStruct()->IsChildOf(FRigVMExecuteContext::StaticStruct()))
						{
							ExecutePins.Add(ModelPin);
							continue;
						}
					}
					InputOutputPins.Add(ModelPin);
				}
				else if (ModelPin->GetDirection() == ERigVMPinDirection::Input || 
                    ModelPin->GetDirection() == ERigVMPinDirection::Visible)
				{
					InputPins.Add(ModelPin);
				}
				else if (ModelPin->GetDirection() == ERigVMPinDirection::Output)
				{
					OutputPins.Add(ModelPin);
				}
			}
		}
	}

	CreateExecutionPins();
	CreateInputPins();
	CreateInputOutputPins();
	CreateOutputPins();

	// Fill the variable list
	ExternalVariables.Reset();
	if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(GetModelNode()))
	{
		if(FunctionReferenceNode->RequiresVariableRemapping())
		{
			TArray<FRigVMExternalVariable> CurrentExternalVariables = FunctionReferenceNode->GetContainedGraph()->GetExternalVariables();
			for(const FRigVMExternalVariable& CurrentExternalVariable : CurrentExternalVariables)
			{
				ExternalVariables.Add(MakeShared<FRigVMExternalVariable>(CurrentExternalVariable));
			}
		}
	}
}

void UControlRigGraphNode::CreateExecutionPins()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = ExecutePins;
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.InputPin == nullptr)
		{
			Pair.InputPin = CreatePin(EGPD_Input, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.InputPin != nullptr)
		{
				Pair.InputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
		}
	}
		if (Pair.OutputPin == nullptr)
	{
			Pair.OutputPin = CreatePin(EGPD_Output, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.OutputPin != nullptr)
		{
				Pair.OutputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
		}
	}
		// note: no recursion for execution pins
	}
}

void UControlRigGraphNode::CreateInputPins(URigVMPin* InParentPin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = InParentPin == nullptr ? InputPins : InParentPin->GetSubPins();
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.InputPin == nullptr)
		{
			Pair.InputPin = CreatePin(EGPD_Input, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.InputPin != nullptr)
			{
				Pair.InputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.InputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::Input;
				Pair.InputPin->bOrphanedPin = ModelPin->IsOrphanPin() ? 1 : 0; 

				SetupPinDefaultsFromModel(Pair.InputPin);

				if (InParentPin != nullptr)
	{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.InputPin->SubPins.Add(Pair.InputPin);
					Pair.InputPin->ParentPin = ParentPair.InputPin;
				}
		}
		}
		CreateInputPins(ModelPin);
	}
}

void UControlRigGraphNode::CreateInputOutputPins(URigVMPin* InParentPin, bool bHidden)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	bool bIsContainer = false;
	if(InParentPin)
	{
		bIsContainer = InParentPin->IsArray();
	}

	const TArray<URigVMPin*> ModelPins = InParentPin == nullptr ? InputOutputPins : InParentPin->GetSubPins();
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.InputPin == nullptr)
		{
			Pair.InputPin = CreatePin(EGPD_Input, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.InputPin != nullptr)
		{
				Pair.InputPin->bHidden = bHidden;
				Pair.InputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.InputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::IO;
				Pair.InputPin->bOrphanedPin = ModelPin->IsOrphanPin() ? 1 : 0; 

				SetupPinDefaultsFromModel(Pair.InputPin);

				if (InParentPin != nullptr)
		{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.InputPin->SubPins.Add(Pair.InputPin);
					Pair.InputPin->ParentPin = ParentPair.InputPin;
		}
	}
	}
		if (Pair.OutputPin == nullptr && !bIsContainer)
	{
			Pair.OutputPin = CreatePin(EGPD_Output, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.OutputPin != nullptr)
		{
				Pair.OutputPin->bHidden = bHidden;
				Pair.OutputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.OutputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::IO;
				Pair.OutputPin->bOrphanedPin = ModelPin->IsOrphanPin() ? 1 : 0; 

				if (InParentPin != nullptr)
		{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.OutputPin->SubPins.Add(Pair.OutputPin);
					Pair.OutputPin->ParentPin = ParentPair.OutputPin;
				}
		}
	}

		// don't recurse on knot / compact reroute nodes
		if(URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(GetModelNode()))
	{
			if (!RerouteNode->GetShowsAsFullNode())
		{
				bHidden = true;
		}
	}

		if(bIsContainer)
		{
			CreateInputPins(ModelPin);
		}
		else
		{
			CreateInputOutputPins(ModelPin, bHidden);
		}
	}
}

void UControlRigGraphNode::CreateOutputPins(URigVMPin* InParentPin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = InParentPin == nullptr ? OutputPins : InParentPin->GetSubPins();
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.OutputPin == nullptr)
		{
			Pair.OutputPin = CreatePin(EGPD_Output, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.OutputPin != nullptr)
			{
				Pair.OutputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.OutputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::Output;
				Pair.OutputPin->bOrphanedPin = ModelPin->IsOrphanPin() ? 1 : 0; 

				if (InParentPin != nullptr)
	{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.OutputPin->SubPins.Add(Pair.OutputPin);
					Pair.OutputPin->ParentPin = ParentPair.OutputPin;
				}
			}
		}
		CreateOutputPins(ModelPin);
	}
}

UClass* UControlRigGraphNode::GetControlRigGeneratedClass() const
{
	UControlRigBlueprint* Blueprint = GetTypedOuter<UControlRigBlueprint>();
	if(Blueprint)
	{
		if (Blueprint->GeneratedClass)
		{
			check(Blueprint->GeneratedClass->IsChildOf(UControlRig::StaticClass()));
			return Blueprint->GeneratedClass;
		}
	}

	return nullptr;
}

UClass* UControlRigGraphNode::GetControlRigSkeletonGeneratedClass() const
{
	UControlRigBlueprint* Blueprint = GetTypedOuter<UControlRigBlueprint>();
	if(Blueprint)
	{
		if (Blueprint->SkeletonGeneratedClass)
		{
			check(Blueprint->SkeletonGeneratedClass->IsChildOf(UControlRig::StaticClass()));
			return Blueprint->SkeletonGeneratedClass;
		}
	}
	return nullptr;
}

FLinearColor UControlRigGraphNode::GetNodeOpacityColor() const
{
	if (URigVMNode* ModelNode = GetModelNode())
	{
		if (Cast<URigVMParameterNode>(ModelNode) || Cast<URigVMVariableNode>(ModelNode))
		{
			return FLinearColor::White;
		}

		if(GetInstructionIndex(true) == INDEX_NONE)
		{
			return FLinearColor(0.35f, 0.35f, 0.35f, 0.35f);
		}
	}
	return FLinearColor::White;
}

FLinearColor UControlRigGraphNode::GetNodeTitleColor() const
{
	// return a darkened version of the default node's color
	return CachedTitleColor * GetNodeOpacityColor();
}

FLinearColor UControlRigGraphNode::GetNodeBodyTintColor() const
{
#if WITH_EDITOR
	if(bEnableProfiling)
	{
		return GetNodeProfilingColor();
	}
#endif
	
	return CachedNodeColor * GetNodeOpacityColor();
}

bool UControlRigGraphNode::ShowPaletteIconOnNode() const
{
	if (URigVMNode* ModelNode = GetModelNode())
	{
		return ModelNode->IsEvent() ||
			ModelNode->IsA<URigVMFunctionEntryNode>() ||
			ModelNode->IsA<URigVMFunctionReturnNode>() ||
			ModelNode->IsA<URigVMFunctionReferenceNode>() ||
			ModelNode->IsA<URigVMCollapseNode>() ||
			ModelNode->IsLoopNode() ||
			Cast<URigVMUnitNode>(ModelNode) != nullptr;
	}
	return false;
}

FSlateIcon UControlRigGraphNode::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;

	static FSlateIcon FunctionIcon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	static FSlateIcon EventIcon("EditorStyle", "GraphEditor.Event_16x");
	static FSlateIcon EntryReturnIcon("EditorStyle", "GraphEditor.Default_16x");
	static FSlateIcon CollapsedNodeIcon("EditorStyle", "GraphEditor.SubGraph_16x");
	static FSlateIcon ArrayNodeIteratorIcon("EditorStyle", "GraphEditor.Macro.ForEach_16x");

	if (URigVMNode* ModelNode = GetModelNode())
	{
		if (ModelNode->IsEvent())
		{
			return EventIcon;
		}

		if (ModelNode->IsA<URigVMFunctionReferenceNode>())
		{ 
			return FunctionIcon;
		}
		
		if (ModelNode->IsA<URigVMCollapseNode>())
		{
			return CollapsedNodeIcon;
		}

		if (ModelNode->IsA<URigVMFunctionEntryNode>() || 
            ModelNode->IsA<URigVMFunctionReturnNode>())
		{
			return EntryReturnIcon;
		}

		if (URigVMArrayNode* ArrayNode = Cast<URigVMArrayNode>(ModelNode))
		{
			if(ArrayNode->IsLoopNode())
			{
				return ArrayNodeIteratorIcon;
			}
		}

		if (URigVMUnitNode *UnitNode = Cast<URigVMUnitNode>(ModelNode))
		{
			ensure(UnitNode->GetScriptStruct());
			
			FString IconPath;

			// icon path format: StyleSetName|StyleName|SmallStyleName|StatusOverlayStyleName
			// the last two names are optional, see FSlateIcon() for reference
			UnitNode->GetScriptStruct()->GetStringMetaDataHierarchical(FRigVMStruct::IconMetaName, &IconPath);

			const int32 NumOfIconPathNames = 4;
			
			FName IconPathNames[NumOfIconPathNames] = {
				NAME_None, // StyleSetName
				NAME_None, // StyleName
				NAME_None, // SmallStyleName
				NAME_None  // StatusOverlayStyleName
			};

			int32 NameIndex = 0;

			while (!IconPath.IsEmpty() && NameIndex < NumOfIconPathNames)
			{
				FString Left;
				FString Right;

				if (!IconPath.Split(TEXT("|"), &Left, &Right))
				{
					Left = IconPath;
				}

				IconPathNames[NameIndex] = FName(*Left);

				NameIndex++;
				IconPath = Right;
			}

			return FSlateIcon(IconPathNames[0], IconPathNames[1], IconPathNames[2], IconPathNames[3]);
			
		}
	}

	return FunctionIcon;
}

void UControlRigGraphNode::GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
#if WITH_EDITOR
	const UControlRigGraphSchema* Schema = Cast<UControlRigGraphSchema>(GetSchema());
	IControlRigEditorModule::Get().GetContextMenuActions(Schema, Menu, Context);
#endif
}

bool UControlRigGraphNode::IsPinExpanded(const FString& InPinPath)
{
	if (URigVMPin* ModelPin = GetModelPinFromPinPath(InPinPath))
	{
		return ModelPin->IsExpanded();
	}
	return false;
}

void UControlRigGraphNode::DestroyNode()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Graph->GetOuter());
		if(ControlRigBlueprint)
	{
			BreakAllNodeLinks();
			if(PropertyName_DEPRECATED.IsValid())
		{
				FControlRigBlueprintUtils::RemoveMemberVariableIfNotUsed(ControlRigBlueprint, PropertyName_DEPRECATED, this);
			}
		}
		}

	UEdGraphNode::DestroyNode();
}

void UControlRigGraphNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	CopyPinDefaultsToModel(Pin, true, true);
}

void UControlRigGraphNode::CopyPinDefaultsToModel(UEdGraphPin* Pin, bool bUndo, bool bPrintPythonCommand)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Pin->Direction != EGPD_Input)
	{
		return;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (URigVMPin* ModelPin = GetModelPinFromPinPath(Pin->GetName()))
	{
		if (ModelPin->GetSubPins().Num() > 0)
		{
			return;
		}

		FString DefaultValue = Pin->DefaultValue;

		if(DefaultValue.IsEmpty() && (
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
			Pin->PinType.PinCategory == UEdGraphSchema_K2::AllObjectTypes
			))
		{
			if(Pin->DefaultObject)
			{
				DefaultValue = Pin->DefaultObject->GetPathName();
			}
		}
		
		if (DefaultValue == FName(NAME_None).ToString() && Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Name)
		{
			DefaultValue = FString();
		}

		if (ModelPin->GetDefaultValue() != DefaultValue)
		{
			if (URigVMController* Controller = GetController())
			{
				Controller->SetPinDefaultValue(ModelPin->GetPinPath(), DefaultValue, false, true, false, bPrintPythonCommand);
			}
		}
	}
}

UControlRigBlueprint* UControlRigGraphNode::GetBlueprint() const
{
	if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		return Graph->GetBlueprint();
		if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(Graph->GetOuter()))
		{
			return OuterGraph->GetBlueprint();
		}
		return Cast<UControlRigBlueprint>(Graph->GetOuter());
	}
	return nullptr;
}

URigVMGraph* UControlRigGraphNode::GetModel() const
{
	if (UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		return Graph->GetModel();
	}
	return nullptr;
}

URigVMController* UControlRigGraphNode::GetController() const
{
	if (UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		return Graph->GetController();
	}
	return nullptr;
}

URigVMNode* UControlRigGraphNode::GetModelNode() const
{
	UControlRigGraphNode* MutableThis = (UControlRigGraphNode*)this;
	if (CachedModelNode)
	{
		if (CachedModelNode->GetOuter() == GetTransientPackage())
		{
			MutableThis->CachedModelNode = nullptr;
		}
		else
		{
			return CachedModelNode;
		}
	}

	if (UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
#if WITH_EDITOR

		if (Graph->TemplateController != nullptr)
		{
			return MutableThis->CachedModelNode = Graph->TemplateController->GetGraph()->FindNode(ModelNodePath);
		}

#endif

		if (URigVMGraph* Model = GetModel())
		{
			return MutableThis->CachedModelNode = Model->FindNode(ModelNodePath);
		}
	}

	return nullptr;
}

FName UControlRigGraphNode::GetModelNodeName() const
{
	if (URigVMNode* ModelNode = GetModelNode())
	{
		return ModelNode->GetFName();
	}
	return NAME_None;
}

URigVMPin* UControlRigGraphNode::GetModelPinFromPinPath(const FString& InPinPath) const
{
	if (TObjectPtr<URigVMPin> const* CachedModelPinPtr = CachedModelPins.Find(InPinPath))
	{
		URigVMPin* CachedModelPin = *CachedModelPinPtr;
		if (!CachedModelPin->HasAnyFlags(RF_Transient) && CachedModelPin->GetNode())
		{
			return CachedModelPin;
		}
	}

	if (URigVMNode* ModelNode = GetModelNode())
	{
		FString PinPath = InPinPath.RightChop(ModelNode->GetNodePath().Len() + 1);
		URigVMPin* ModelPin = ModelNode->FindPin(PinPath);
		if (ModelPin)
		{
			UControlRigGraphNode* MutableThis = (UControlRigGraphNode*)this;
			MutableThis->CachedModelPins.FindOrAdd(InPinPath) = ModelPin;
		}
		return ModelPin;
	}
	
	return nullptr;
}

void UControlRigGraphNode::SetupPinDefaultsFromModel(UEdGraphPin* Pin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Pin->Direction != EGPD_Input)
	{
		return;
		}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (URigVMPin* ModelPin = GetModelPinFromPinPath(Pin->GetName()))
	{
		if (ModelPin->GetSubPins().Num() > 0)
		{
			return;
			}

		FString DefaultValueString = ModelPin->GetDefaultValue();
		if (DefaultValueString.IsEmpty() && ModelPin->GetCPPType() == TEXT("FName"))
	{
			DefaultValueString = FName(NAME_None).ToString();
	}
						K2Schema->GetPinDefaultValuesFromString(Pin->PinType, Pin->GetOwningNodeUnchecked(), DefaultValueString, Pin->DefaultValue, Pin->DefaultObject, Pin->DefaultTextValue);
					}
}

FText UControlRigGraphNode::GetTooltipText() const
{
	if(URigVMNode* ModelNode = GetModelNode())
	{
		return ModelNode->GetToolTipText();
	}
	return FText::FromString(ModelNodePath);
}

void UControlRigGraphNode::InvalidateNodeTitle() const
{
	NodeTitle = FText();
	FullNodeTitle = FText();
	NodeTitleDirtied.ExecuteIfBound();
}

bool UControlRigGraphNode::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* InSchema) const
{
	return InSchema->IsA<UControlRigGraphSchema>();
}

void UControlRigGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::AutowireNewNode(FromPin);

	const UControlRigGraphSchema* Schema = GetDefault<UControlRigGraphSchema>();

	for(UEdGraphPin* Pin : Pins)
	{
		if (Pin->ParentPin != nullptr)
		{
			continue;
		}

		FPinConnectionResponse ConnectResponse = Schema->CanCreateConnection(FromPin, Pin);
		if(ConnectResponse.Response != ECanCreateConnectionResponse::CONNECT_RESPONSE_DISALLOW)
		{
			if (Schema->TryCreateConnection(FromPin, Pin))
			{
				break;
					}
				}
		}
}

bool UControlRigGraphNode::IsSelectedInEditor() const
{
	URigVMNode* ModelNode = GetModelNode();
	if (ModelNode)
	{
		return ModelNode->IsSelected();
	}
	return false;
}

bool UControlRigGraphNode::ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const
{
	if (URigVMRerouteNode* Reroute = Cast<URigVMRerouteNode>(GetModelNode()))
	{
		if (!Reroute->GetShowsAsFullNode())
	{
			if (Pins.Num() >= 2)
			{
				OutInputPinIndex = 0;
				OutOutputPinIndex = 1;
				return true;
			}
	}
	}
	return false;
}

FEdGraphPinType UControlRigGraphNode::GetPinTypeForModelPin(URigVMPin* InModelPin)
{
	FEdGraphPinType PinType;
	PinType.ResetToDefaults();

	FName ModelPinCPPType = InModelPin->IsArray() ? *InModelPin->GetArrayElementCppType() : *InModelPin->GetCPPType();
	if (ModelPinCPPType == TEXT("bool"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (ModelPinCPPType == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (ModelPinCPPType == TEXT("float"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (ModelPinCPPType == TEXT("double"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (ModelPinCPPType == TEXT("FName"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (ModelPinCPPType == TEXT("FString"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (InModelPin->GetScriptStruct() != nullptr)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = InModelPin->GetScriptStruct();
	}
	else if (InModelPin->IsUObject())
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = InModelPin->GetCPPTypeObject();
	}
	else if (InModelPin->GetEnum() != nullptr)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		PinType.PinSubCategoryObject = InModelPin->GetEnum();
	}

	if (InModelPin->IsArray())
	{
		PinType.ContainerType = EPinContainerType::Array;
	}
	else
	{
		PinType.ContainerType = EPinContainerType::None;
	}

	PinType.bIsConst = InModelPin->IsDefinedAsConstant();

	return PinType;
}

#undef LOCTEXT_NAMESPACE
