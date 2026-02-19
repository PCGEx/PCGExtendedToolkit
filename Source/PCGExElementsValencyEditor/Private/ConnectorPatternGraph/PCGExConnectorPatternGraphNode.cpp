// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternHeaderNode.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternConstraintNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"

#pragma region UPCGExConnectorPatternGraphNode

void UPCGExConnectorPatternGraphNode::PostLoad()
{
	Super::PostLoad();

	// Migration: entries serialized before bOutput/bInput fields default to both false.
	// Treat as both true (legacy behavior was always both directions).
	for (FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (!Entry.bOutput && !Entry.bInput)
		{
			Entry.bOutput = true;
			Entry.bInput = true;
		}
	}

	// Migration: add RootIn pin if absent (nodes serialized before header node refactor)
	if (!FindPin(TEXT("RootIn"), EGPD_Input))
	{
		FEdGraphPinType RootPinType;
		RootPinType.PinCategory = PatternRootPinCategory;
		CreatePin(EGPD_Input, RootPinType, TEXT("RootIn"))->PinFriendlyName = INVTEXT("Root");
	}
}

const FName UPCGExConnectorPatternGraphNode::AnyPinCategory = TEXT("AnyConnector");
const FName UPCGExConnectorPatternGraphNode::ConnectorPinCategory = TEXT("ConnectorType");
const FName UPCGExConnectorPatternGraphNode::PatternRootPinCategory = TEXT("PatternRoot");

FName UPCGExConnectorPatternGraphNode::MakeOutputPinName(int32 TypeId, FName TypeName)
{
	return FName(*FString::Printf(TEXT("CT_%d_%s"), TypeId, *TypeName.ToString()));
}

FName UPCGExConnectorPatternGraphNode::MakeInputPinName(int32 TypeId, FName TypeName)
{
	return FName(*FString::Printf(TEXT("CT_%d_%s_In"), TypeId, *TypeName.ToString()));
}

int32 UPCGExConnectorPatternGraphNode::GetTypeIdFromPinName(FName PinName)
{
	// Parse "CT_{TypeId}_{TypeName}" or "CT_{TypeId}_{TypeName}_In"
	const FString PinStr = PinName.ToString();
	if (!PinStr.StartsWith(TEXT("CT_"))) { return 0; }

	// Extract the substring after "CT_", find the next underscore to isolate the TypeId
	const FString AfterPrefix = PinStr.Mid(3);
	int32 UnderscoreIdx = INDEX_NONE;
	if (AfterPrefix.FindChar(TEXT('_'), UnderscoreIdx))
	{
		return FCString::Atoi(*AfterPrefix.Left(UnderscoreIdx));
	}

	return 0;
}

void UPCGExConnectorPatternGraphNode::AllocateDefaultPins()
{
	// Root input pin — receives wire from Pattern Header node
	FEdGraphPinType RootPinType;
	RootPinType.PinCategory = PatternRootPinCategory;
	CreatePin(EGPD_Input, RootPinType, TEXT("RootIn"))->PinFriendlyName = INVTEXT("Root");

	// "Any" output pin (wildcard)
	FEdGraphPinType AnyPinType;
	AnyPinType.PinCategory = AnyPinCategory;

	CreatePin(EGPD_Output, AnyPinType, TEXT("AnyOut"))->PinFriendlyName = INVTEXT("Any");
	CreatePin(EGPD_Input, AnyPinType, TEXT("AnyIn"))->PinFriendlyName = INVTEXT("Any");

	// Resolve ConnectorSet for pin color support
	UPCGExValencyConnectorSet* ConnSet = nullptr;
	if (const UPCGExConnectorPatternGraph* PatternGraph = GetPatternGraph())
	{
		ConnSet = PatternGraph->GetConnectorSet();
	}

	// Recreate user-added connector pins
	for (const FPCGExConnectorPinEntry& PinEntry : ConnectorPins)
	{
		FEdGraphPinType ConnPinType;
		ConnPinType.PinCategory = ConnectorPinCategory;
		ConnPinType.PinSubCategory = PinEntry.StoredTypeName;
		ConnPinType.PinSubCategoryObject = ConnSet;

		const FText DisplayName = FText::FromName(PinEntry.StoredTypeName);

		if (PinEntry.bOutput)
		{
			CreatePin(EGPD_Output, ConnPinType, MakeOutputPinName(PinEntry.StoredTypeId, PinEntry.StoredTypeName))->PinFriendlyName = DisplayName;
		}
		if (PinEntry.bInput)
		{
			CreatePin(EGPD_Input, ConnPinType, MakeInputPinName(PinEntry.StoredTypeId, PinEntry.StoredTypeName))->PinFriendlyName = DisplayName;
		}
	}
}

FText UPCGExConnectorPatternGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Show module names summary
	if (ModuleNames.Num() > 0)
	{
		FString Summary;
		for (int32 i = 0; i < FMath::Min(ModuleNames.Num(), 2); i++)
		{
			if (i > 0) { Summary += TEXT(", "); }
			Summary += ModuleNames[i].ToString();
		}
		if (ModuleNames.Num() > 2)
		{
			Summary += FString::Printf(TEXT(" (+%d)"), ModuleNames.Num() - 2);
		}
		return FText::FromString(Summary);
	}

	return INVTEXT("Entry (Any)");
}

FLinearColor UPCGExConnectorPatternGraphNode::GetNodeTitleColor() const
{
	if (!bIsActive)
	{
		return FLinearColor(0.4f, 0.4f, 0.4f); // Gray for inactive/constraint-only
	}

	if (ModuleNames.IsEmpty())
	{
		return FLinearColor(0.6f, 0.6f, 0.6f); // Light gray for wildcard
	}

	return FLinearColor(0.2f, 0.8f, 0.3f); // Green for active with modules
}

void UPCGExConnectorPatternGraphNode::AddConnectorPin(int32 TypeId, FName TypeName, EEdGraphPinDirection Direction)
{
	// Find existing entry for this TypeId
	FPCGExConnectorPinEntry* Existing = nullptr;
	for (FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (Entry.StoredTypeId == TypeId)
		{
			Existing = &Entry;
			break;
		}
	}

	const bool bWantOutput = (Direction == EGPD_Output);
	const bool bWantInput = (Direction == EGPD_Input);

	if (Existing)
	{
		// Already has this direction? Nothing to do
		if ((bWantOutput && Existing->bOutput) || (bWantInput && Existing->bInput)) { return; }

		// Enable the additional direction
		if (bWantOutput) { Existing->bOutput = true; }
		if (bWantInput) { Existing->bInput = true; }
	}
	else
	{
		FPCGExConnectorPinEntry& NewEntry = ConnectorPins.Emplace_GetRef();
		NewEntry.StoredTypeId = TypeId;
		NewEntry.StoredTypeName = TypeName;
		NewEntry.bOutput = bWantOutput;
		NewEntry.bInput = bWantInput;
		Existing = &NewEntry;
	}

	// Resolve ConnectorSet for pin color support
	UPCGExValencyConnectorSet* ConnSet = nullptr;
	if (const UPCGExConnectorPatternGraph* PatternGraph = GetPatternGraph())
	{
		ConnSet = PatternGraph->GetConnectorSet();
	}

	// Create actual pin
	FEdGraphPinType ConnPinType;
	ConnPinType.PinCategory = ConnectorPinCategory;
	ConnPinType.PinSubCategory = TypeName;
	ConnPinType.PinSubCategoryObject = ConnSet;

	const FText DisplayName = FText::FromName(TypeName);

	if (bWantOutput)
	{
		CreatePin(EGPD_Output, ConnPinType, MakeOutputPinName(TypeId, TypeName))->PinFriendlyName = DisplayName;
	}
	if (bWantInput)
	{
		CreatePin(EGPD_Input, ConnPinType, MakeInputPinName(TypeId, TypeName))->PinFriendlyName = DisplayName;
	}
}

void UPCGExConnectorPatternGraphNode::AddConnectorPinBoth(int32 TypeId, FName TypeName)
{
	// Find existing entry for this TypeId
	for (const FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (Entry.StoredTypeId == TypeId && Entry.bOutput && Entry.bInput) { return; }
	}

	AddConnectorPin(TypeId, TypeName, EGPD_Output);
	AddConnectorPin(TypeId, TypeName, EGPD_Input);
}

void UPCGExConnectorPatternGraphNode::RemoveConnectorPin(int32 TypeId, EEdGraphPinDirection Direction)
{
	int32 EntryIdx = INDEX_NONE;
	for (int32 i = 0; i < ConnectorPins.Num(); i++)
	{
		if (ConnectorPins[i].StoredTypeId == TypeId)
		{
			EntryIdx = i;
			break;
		}
	}

	if (EntryIdx == INDEX_NONE) { return; }

	FPCGExConnectorPinEntry& Entry = ConnectorPins[EntryIdx];

	// Find pin by TypeId (resilient to name changes) rather than by constructed name
	auto FindPinByTypeId = [this](int32 InTypeId, EEdGraphPinDirection InDir) -> UEdGraphPin*
	{
		for (UEdGraphPin* Pin : Pins)
		{
			if (Pin->Direction != InDir) { continue; }
			if (Pin->PinType.PinCategory != ConnectorPinCategory) { continue; }
			if (GetTypeIdFromPinName(Pin->PinName) == InTypeId) { return Pin; }
		}
		return nullptr;
	};

	if (Direction == EGPD_Output && Entry.bOutput)
	{
		if (UEdGraphPin* OutPin = FindPinByTypeId(TypeId, EGPD_Output))
		{
			OutPin->BreakAllPinLinks();
			RemovePin(OutPin);
		}
		Entry.bOutput = false;
	}
	else if (Direction == EGPD_Input && Entry.bInput)
	{
		if (UEdGraphPin* InPin = FindPinByTypeId(TypeId, EGPD_Input))
		{
			InPin->BreakAllPinLinks();
			RemovePin(InPin);
		}
		Entry.bInput = false;
	}

	// Remove entry entirely if no directions remain
	if (!Entry.bOutput && !Entry.bInput)
	{
		ConnectorPins.RemoveAt(EntryIdx);
	}
}

void UPCGExConnectorPatternGraphNode::ResolveConnectorPins(const UPCGExValencyConnectorSet* InConnectorSet)
{
	if (!InConnectorSet) { return; }

	for (FPCGExConnectorPinEntry& PinEntry : ConnectorPins)
	{
		const int32 TypeIdx = InConnectorSet->FindConnectorTypeIndexById(PinEntry.StoredTypeId);
		if (TypeIdx != INDEX_NONE)
		{
			PinEntry.StoredTypeName = InConnectorSet->ConnectorTypes[TypeIdx].ConnectorType;
		}

		// Update all connector pins (display name, color ref)
		const FText DisplayName = FText::FromName(PinEntry.StoredTypeName);

		for (UEdGraphPin* Pin : Pins)
		{
			if (Pin->PinType.PinCategory != ConnectorPinCategory) { continue; }

			const int32 PinTypeId = GetTypeIdFromPinName(Pin->PinName);
			if (PinTypeId == PinEntry.StoredTypeId)
			{
				Pin->PinType.PinSubCategory = PinEntry.StoredTypeName;
				Pin->PinType.PinSubCategoryObject = const_cast<UPCGExValencyConnectorSet*>(InConnectorSet);
				Pin->PinFriendlyName = DisplayName;
			}
		}
	}
}

bool UPCGExConnectorPatternGraphNode::RemoveStalePins(const UPCGExValencyConnectorSet* InConnectorSet)
{
	if (!InConnectorSet) { return false; }

	TArray<int32> StaleTypeIds;
	for (const FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (InConnectorSet->FindConnectorTypeIndexById(Entry.StoredTypeId) == INDEX_NONE)
		{
			StaleTypeIds.Add(Entry.StoredTypeId);
		}
	}

	if (StaleTypeIds.IsEmpty()) { return false; }

	for (const int32 TypeId : StaleTypeIds)
	{
		// Remove both directions
		RemoveConnectorPin(TypeId, EGPD_Output);
		RemoveConnectorPin(TypeId, EGPD_Input);
	}

	return true;
}

bool UPCGExConnectorPatternGraphNode::HasConnectorPin(int32 TypeId, EEdGraphPinDirection Direction) const
{
	for (const FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (Entry.StoredTypeId != TypeId) { continue; }
		if (Direction == EGPD_Output && Entry.bOutput) { return true; }
		if (Direction == EGPD_Input && Entry.bInput) { return true; }
	}
	return false;
}

bool UPCGExConnectorPatternGraphNode::HasAnyConnectorPin(int32 TypeId) const
{
	for (const FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (Entry.StoredTypeId == TypeId) { return true; }
	}
	return false;
}

UPCGExConnectorPatternGraph* UPCGExConnectorPatternGraphNode::GetPatternGraph() const
{
	return Cast<UPCGExConnectorPatternGraph>(GetGraph());
}

void UPCGExConnectorPatternGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!FromPin) { return; }

	// Try to find a matching pin type on this node
	const EEdGraphPinDirection TargetDirection = (FromPin->Direction == EGPD_Output) ? EGPD_Input : EGPD_Output;

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction != TargetDirection) { continue; }

		// Match by category and subcategory
		if (Pin->PinType.PinCategory == FromPin->PinType.PinCategory &&
			Pin->PinType.PinSubCategory == FromPin->PinType.PinSubCategory)
		{
			if (GetSchema()->TryCreateConnection(FromPin, Pin))
			{
				return;
			}
		}
	}

	// Fall back to "Any" pin
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction != TargetDirection) { continue; }
		if (Pin->PinType.PinCategory == AnyPinCategory)
		{
			GetSchema()->TryCreateConnection(FromPin, Pin);
			return;
		}
	}
}

#if WITH_EDITOR
void UPCGExConnectorPatternGraphNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// NotifyGraphChanged triggers the editor's OnGraphChanged handler which calls CompileGraphToAsset.
	// It also forces the SGraphEditor to refresh node visuals (title, colors, etc.).
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}
#endif

#pragma endregion

#pragma region UPCGExConnectorPatternConstraintNode

FText UPCGExConnectorPatternConstraintNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	switch (ConstraintType)
	{
	case EPCGExPatternConstraintType::Boundary:
		return INVTEXT("[Boundary]");
	case EPCGExPatternConstraintType::Wildcard:
		return INVTEXT("[Wildcard]");
	default:
		return INVTEXT("[Constraint]");
	}
}

FLinearColor UPCGExConnectorPatternConstraintNode::GetNodeTitleColor() const
{
	switch (ConstraintType)
	{
	case EPCGExPatternConstraintType::Boundary:
		return FLinearColor(0.8f, 0.2f, 0.2f); // Red for boundary (no connections)
	case EPCGExPatternConstraintType::Wildcard:
		return FLinearColor(0.9f, 0.7f, 0.1f); // Yellow for wildcard (must connect)
	default:
		return FLinearColor::Gray;
	}
}

#if WITH_EDITOR
void UPCGExConnectorPatternConstraintNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}
#endif

#pragma endregion

#pragma region UPCGExConnectorPatternHeaderNode

const FName UPCGExConnectorPatternHeaderNode::PatternRootPinCategory = TEXT("PatternRoot");

void UPCGExConnectorPatternHeaderNode::AllocateDefaultPins()
{
	FEdGraphPinType RootPinType;
	RootPinType.PinCategory = PatternRootPinCategory;

	CreatePin(EGPD_Output, RootPinType, TEXT("RootOut"))->PinFriendlyName = INVTEXT("Root");
}

FText UPCGExConnectorPatternHeaderNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FString Prefix = bEnabled ? TEXT("") : TEXT("[OFF] ");

	if (!PatternName.IsNone())
	{
		return FText::FromString(FString::Printf(TEXT("%sPattern: %s"), *Prefix, *PatternName.ToString()));
	}
	return FText::FromString(FString::Printf(TEXT("%sPattern"), *Prefix));
}

FLinearColor UPCGExConnectorPatternHeaderNode::GetNodeTitleColor() const
{
	if (!bEnabled)
	{
		return FLinearColor(0.35f, 0.35f, 0.35f); // Gray when disabled
	}
	return FLinearColor(0.6f, 0.3f, 0.9f); // Purple accent
}

#if WITH_EDITOR
void UPCGExConnectorPatternHeaderNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}
#endif

#pragma endregion
