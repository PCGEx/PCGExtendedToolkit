// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternConstraintNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "Core/PCGExConnectorPatternAsset.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "EdGraph/EdGraphPin.h"

#pragma region UPCGExConnectorPatternGraph

namespace PCGExConnectorPatternGraphHelpers
{
	FName GetTypeNameFromPin(const UEdGraphPin* Pin)
	{
		if (Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::AnyPinCategory)
		{
			return NAME_None; // Wildcard
		}
		return Pin->PinType.PinSubCategory;
	}
}

UPCGExValencyConnectorSet* UPCGExConnectorPatternGraph::GetConnectorSet() const
{
	if (const UPCGExConnectorPatternAsset* Asset = OwningAsset.Get())
	{
		return Asset->ConnectorSet;
	}
	return nullptr;
}

void UPCGExConnectorPatternGraph::CompileGraphToAsset()
{
	UPCGExConnectorPatternAsset* Asset = OwningAsset.Get();
	if (!Asset) { return; }

	// Collect all pattern nodes (exclude constraint nodes which inherit from pattern node)
	TArray<UPCGExConnectorPatternGraphNode*> AllNodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Cast<UPCGExConnectorPatternConstraintNode>(Node)) { continue; }
		if (UPCGExConnectorPatternGraphNode* PatternNode = Cast<UPCGExConnectorPatternGraphNode>(Node))
		{
			AllNodes.Add(PatternNode);
		}
	}

	if (AllNodes.IsEmpty())
	{
		Asset->Patterns.Empty();
		Asset->Compile();
		return;
	}

	// Find connected components via flood-fill
	TSet<UPCGExConnectorPatternGraphNode*> Visited;
	TArray<TArray<UPCGExConnectorPatternGraphNode*>> Components;

	for (UPCGExConnectorPatternGraphNode* StartNode : AllNodes)
	{
		if (Visited.Contains(StartNode)) { continue; }

		TArray<UPCGExConnectorPatternGraphNode*>& Component = Components.Emplace_GetRef();
		TArray<UPCGExConnectorPatternGraphNode*> Stack;
		Stack.Add(StartNode);

		while (Stack.Num() > 0)
		{
			UPCGExConnectorPatternGraphNode* Current = Stack.Pop();
			if (Visited.Contains(Current)) { continue; }
			Visited.Add(Current);
			Component.Add(Current);

			// Follow all pin connections to find neighbors
			for (const UEdGraphPin* Pin : Current->Pins)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !IsValid(LinkedPin->GetOwningNode())) { continue; }
					if (Cast<UPCGExConnectorPatternConstraintNode>(LinkedPin->GetOwningNode())) { continue; }
					if (UPCGExConnectorPatternGraphNode* Neighbor = Cast<UPCGExConnectorPatternGraphNode>(LinkedPin->GetOwningNode()))
					{
						if (!Visited.Contains(Neighbor))
						{
							Stack.Add(Neighbor);
						}
					}
				}
			}
		}
	}

	// Build patterns from components
	Asset->Patterns.Empty();

	for (TArray<UPCGExConnectorPatternGraphNode*>& Component : Components)
	{
		// Find root node (bIsPatternRoot=true, or first node)
		int32 RootIdx = 0;
		for (int32 i = 0; i < Component.Num(); i++)
		{
			if (Component[i]->bIsPatternRoot)
			{
				RootIdx = i;
				break;
			}
		}

		// Put root first
		if (RootIdx != 0) { Component.Swap(0, RootIdx); }

		// Build entry index map
		TMap<UPCGExConnectorPatternGraphNode*, int32> NodeToEntryIndex;
		for (int32 i = 0; i < Component.Num(); i++)
		{
			NodeToEntryIndex.Add(Component[i], i);
		}

		FPCGExConnectorPatternAuthored& Pattern = Asset->Patterns.Emplace_GetRef();

		// Copy pattern-level settings from root
		const UPCGExConnectorPatternGraphNode* Root = Component[0];
		Pattern.PatternName = Root->PatternName;
		Pattern.Weight = Root->Weight;
		Pattern.bExclusive = Root->bExclusive;
		Pattern.MinMatches = Root->MinMatches;
		Pattern.MaxMatches = Root->MaxMatches;
		Pattern.OutputStrategy = Root->OutputStrategy;
		Pattern.TransformMode = Root->TransformMode;

		// Build entries
		for (int32 EntryIdx = 0; EntryIdx < Component.Num(); EntryIdx++)
		{
			const UPCGExConnectorPatternGraphNode* EntryNode = Component[EntryIdx];
			FPCGExConnectorPatternEntryAuthored& Entry = Pattern.Entries.Emplace_GetRef();

			Entry.ModuleNames = EntryNode->ModuleNames;
			Entry.bIsActive = EntryNode->bIsActive;
			// BoundaryConnectorTypes and WildcardConnectorTypes are derived from constraint nodes below

			// Build adjacencies from output pin connections
			// Group by target node, collect type pairs
			TMap<int32, FPCGExConnectorPatternAdjacencyAuthored*> AdjacencyMap;

			for (const UEdGraphPin* Pin : EntryNode->Pins)
			{
				if (Pin->Direction != EGPD_Output) { continue; }

				const FName SourceTypeName = PCGExConnectorPatternGraphHelpers::GetTypeNameFromPin(Pin);

				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !IsValid(LinkedPin->GetOwningNode())) { continue; }

					// Check if target is a constraint node
					if (const UPCGExConnectorPatternConstraintNode* ConstraintNode = Cast<UPCGExConnectorPatternConstraintNode>(LinkedPin->GetOwningNode()))
					{
						if (!SourceTypeName.IsNone() && ConstraintNode->bIsActive)
						{
							if (ConstraintNode->ConstraintType == EPCGExPatternConstraintType::Boundary)
							{
								Entry.BoundaryConnectorTypes.AddUnique(SourceTypeName);
							}
							else
							{
								Entry.WildcardConnectorTypes.AddUnique(SourceTypeName);
							}
						}
						continue;
					}

					UPCGExConnectorPatternGraphNode* TargetNode = Cast<UPCGExConnectorPatternGraphNode>(LinkedPin->GetOwningNode());
					if (!TargetNode) { continue; }

					const int32* TargetEntryIdx = NodeToEntryIndex.Find(TargetNode);
					if (!TargetEntryIdx) { continue; }

					// Get or create adjacency for this target
					FPCGExConnectorPatternAdjacencyAuthored*& Adj = AdjacencyMap.FindOrAdd(*TargetEntryIdx);
					if (!Adj)
					{
						Adj = &Entry.Adjacencies.Emplace_GetRef();
						Adj->TargetEntryIndex = *TargetEntryIdx;
					}

					const FName TargetTypeName = PCGExConnectorPatternGraphHelpers::GetTypeNameFromPin(LinkedPin);

					FPCGExConnectorTypePairAuthored& Pair = Adj->TypePairs.Emplace_GetRef();
					Pair.SourceType = SourceTypeName;
					Pair.TargetType = TargetTypeName;
				}
			}

			// Also check input pins for connections FROM constraint nodes
			for (const UEdGraphPin* Pin : EntryNode->Pins)
			{
				if (Pin->Direction != EGPD_Input) { continue; }

				const FName TypeName = PCGExConnectorPatternGraphHelpers::GetTypeNameFromPin(Pin);
				if (TypeName.IsNone()) { continue; }

				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !IsValid(LinkedPin->GetOwningNode())) { continue; }
					const UPCGExConnectorPatternConstraintNode* ConstraintNode = Cast<UPCGExConnectorPatternConstraintNode>(LinkedPin->GetOwningNode());
					if (!ConstraintNode || !ConstraintNode->bIsActive) { continue; }

					if (ConstraintNode->ConstraintType == EPCGExPatternConstraintType::Boundary)
					{
						Entry.BoundaryConnectorTypes.AddUnique(TypeName);
					}
					else
					{
						Entry.WildcardConnectorTypes.AddUnique(TypeName);
					}
				}
			}
		}
	}

	// Compile the asset
	TArray<FText> Errors;
	Asset->Compile(&Errors);

	// Log errors
	for (const FText& Error : Errors)
	{
		UE_LOG(LogTemp, Warning, TEXT("ConnectorPattern Compile: %s"), *Error.ToString());
	}
	
	Asset->MarkPackageDirty();
}

void UPCGExConnectorPatternGraph::BuildGraphFromAsset()
{
	const UPCGExConnectorPatternAsset* Asset = OwningAsset.Get();
	if (!Asset) { return; }

	const UPCGExValencyConnectorSet* ConnSet = Asset->ConnectorSet;

	// Clear existing nodes
	TArray<UEdGraphNode*> NodesToRemove = Nodes;
	for (UEdGraphNode* Node : NodesToRemove)
	{
		RemoveNode(Node);
	}

	float XOffset = 0.0f;

	for (int32 PatternIdx = 0; PatternIdx < Asset->Patterns.Num(); PatternIdx++)
	{
		const FPCGExConnectorPatternAuthored& Pattern = Asset->Patterns[PatternIdx];

		TArray<UPCGExConnectorPatternGraphNode*> CreatedNodes;

		for (int32 EntryIdx = 0; EntryIdx < Pattern.Entries.Num(); EntryIdx++)
		{
			const FPCGExConnectorPatternEntryAuthored& Entry = Pattern.Entries[EntryIdx];

			FGraphNodeCreator<UPCGExConnectorPatternGraphNode> NodeCreator(*this);
			UPCGExConnectorPatternGraphNode* NewNode = NodeCreator.CreateNode(true);

			NewNode->NodePosX = static_cast<int32>(XOffset) + (EntryIdx % 3) * 250;
			NewNode->NodePosY = (EntryIdx / 3) * 200;

			// Copy entry properties
			NewNode->ModuleNames = Entry.ModuleNames;
			NewNode->bIsActive = Entry.bIsActive;
			NewNode->BoundaryConnectorTypes = Entry.BoundaryConnectorTypes;
			NewNode->WildcardConnectorTypes = Entry.WildcardConnectorTypes;
			NewNode->bIsPatternRoot = (EntryIdx == 0);

			// Copy pattern-level settings to root
			if (EntryIdx == 0)
			{
				NewNode->PatternName = Pattern.PatternName;
				NewNode->Weight = Pattern.Weight;
				NewNode->bExclusive = Pattern.bExclusive;
				NewNode->MinMatches = Pattern.MinMatches;
				NewNode->MaxMatches = Pattern.MaxMatches;
				NewNode->OutputStrategy = Pattern.OutputStrategy;
				NewNode->TransformMode = Pattern.TransformMode;
			}

			// Collect connector type names used by this entry
			TSet<FName> UsedTypes;
			for (const FPCGExConnectorPatternAdjacencyAuthored& Adj : Entry.Adjacencies)
			{
				for (const FPCGExConnectorTypePairAuthored& Pair : Adj.TypePairs)
				{
					if (!Pair.SourceType.IsNone()) { UsedTypes.Add(Pair.SourceType); }
				}
			}

			// Check incoming adjacencies targeting this entry
			for (int32 OtherIdx = 0; OtherIdx < Pattern.Entries.Num(); OtherIdx++)
			{
				if (OtherIdx == EntryIdx) { continue; }
				for (const FPCGExConnectorPatternAdjacencyAuthored& Adj : Pattern.Entries[OtherIdx].Adjacencies)
				{
					if (Adj.TargetEntryIndex != EntryIdx) { continue; }
					for (const FPCGExConnectorTypePairAuthored& Pair : Adj.TypePairs)
					{
						if (!Pair.TargetType.IsNone()) { UsedTypes.Add(Pair.TargetType); }
					}
				}
			}

			// Add connector pins for used types
			for (const FName& TypeName : UsedTypes)
			{
				int32 TypeId = 0;
				if (ConnSet)
				{
					const int32 TypeIdx = ConnSet->FindConnectorTypeIndex(TypeName);
					if (TypeIdx != INDEX_NONE)
					{
						TypeId = ConnSet->ConnectorTypes[TypeIdx].TypeId;
					}
				}
				if (TypeId == 0)
				{
					TypeId = GetTypeHash(TypeName);
				}
				NewNode->AddConnectorPinBoth(TypeId, TypeName);
			}

			NodeCreator.Finalize();
			CreatedNodes.Add(NewNode);
		}

		// Create wires from adjacency data
		for (int32 EntryIdx = 0; EntryIdx < Pattern.Entries.Num(); EntryIdx++)
		{
			const FPCGExConnectorPatternEntryAuthored& Entry = Pattern.Entries[EntryIdx];
			UPCGExConnectorPatternGraphNode* SourceNode = CreatedNodes[EntryIdx];

			for (const FPCGExConnectorPatternAdjacencyAuthored& Adj : Entry.Adjacencies)
			{
				if (!CreatedNodes.IsValidIndex(Adj.TargetEntryIndex)) { continue; }
				UPCGExConnectorPatternGraphNode* TargetNode = CreatedNodes[Adj.TargetEntryIndex];

				for (const FPCGExConnectorTypePairAuthored& Pair : Adj.TypePairs)
				{
					UEdGraphPin* OutPin = nullptr;
					UEdGraphPin* InPin = nullptr;

					if (Pair.SourceType.IsNone())
					{
						OutPin = SourceNode->FindPin(TEXT("AnyOut"), EGPD_Output);
					}
					else
					{
						for (UEdGraphPin* Pin : SourceNode->Pins)
						{
							if (Pin->Direction == EGPD_Output &&
								Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::ConnectorPinCategory &&
								Pin->PinType.PinSubCategory == Pair.SourceType)
							{
								OutPin = Pin;
								break;
							}
						}
					}

					if (Pair.TargetType.IsNone())
					{
						InPin = TargetNode->FindPin(TEXT("AnyIn"), EGPD_Input);
					}
					else
					{
						for (UEdGraphPin* Pin : TargetNode->Pins)
						{
							if (Pin->Direction == EGPD_Input &&
								Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::ConnectorPinCategory &&
								Pin->PinType.PinSubCategory == Pair.TargetType)
							{
								InPin = Pin;
								break;
							}
						}
					}

					if (OutPin && InPin)
					{
						OutPin->MakeLinkTo(InPin);
					}
				}
			}
		}

		XOffset += 800.0f;
	}
}

void UPCGExConnectorPatternGraph::RebuildAllNodePins()
{
	const UPCGExValencyConnectorSet* ConnSet = GetConnectorSet();

	for (UEdGraphNode* Node : Nodes)
	{
		if (UPCGExConnectorPatternGraphNode* PatternNode = Cast<UPCGExConnectorPatternGraphNode>(Node))
		{
			PatternNode->ResolveConnectorPins(ConnSet);
		}
	}
}

#pragma endregion
