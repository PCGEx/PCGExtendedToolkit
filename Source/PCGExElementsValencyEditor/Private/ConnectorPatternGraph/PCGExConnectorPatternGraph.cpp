// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternConstraintNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternHeaderNode.h"
#include "Core/PCGExConnectorPatternAsset.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Volumes/ValencyContextVolume.h"
#include "EdGraph/EdGraphPin.h"
#include "PCGComponent.h"
#include "Subsystems/PCGSubsystem.h"
#include "EngineUtils.h"
#include "Editor.h"

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

	Asset->Patterns.Empty();

	// Collect all header nodes — each header defines one pattern
	TArray<UPCGExConnectorPatternHeaderNode*> Headers;
	for (UEdGraphNode* Node : Nodes)
	{
		if (UPCGExConnectorPatternHeaderNode* Header = Cast<UPCGExConnectorPatternHeaderNode>(Node))
		{
			Headers.Add(Header);
		}
	}

	if (Headers.IsEmpty())
	{
		Asset->Compile();
		return;
	}

	// Track all entry nodes claimed by a pattern (for orphan detection)
	TSet<UPCGExConnectorPatternGraphNode*> ClaimedEntries;

	for (const UPCGExConnectorPatternHeaderNode* Header : Headers)
	{
		// Skip disabled patterns — but still claim their entries so they don't trigger orphan warnings
		if (!Header->bEnabled)
		{
			const UEdGraphPin* DisabledRootOut = Header->FindPin(TEXT("RootOut"), EGPD_Output);
			if (DisabledRootOut)
			{
				for (const UEdGraphPin* LinkedPin : DisabledRootOut->LinkedTo)
				{
					if (UPCGExConnectorPatternGraphNode* Entry = Cast<UPCGExConnectorPatternGraphNode>(LinkedPin->GetOwningNode()))
					{
						// Flood-fill to claim entire disabled subgraph
						TArray<UPCGExConnectorPatternGraphNode*> DisabledStack;
						DisabledStack.Add(Entry);
						while (DisabledStack.Num() > 0)
						{
							UPCGExConnectorPatternGraphNode* Current = DisabledStack.Pop();
							if (ClaimedEntries.Contains(Current)) { continue; }
							ClaimedEntries.Add(Current);
							for (const UEdGraphPin* Pin : Current->Pins)
							{
								if (Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory) { continue; }
								for (const UEdGraphPin* Linked : Pin->LinkedTo)
								{
									if (UPCGExConnectorPatternGraphNode* Neighbor = Cast<UPCGExConnectorPatternGraphNode>(Linked->GetOwningNode()))
									{
										if (!ClaimedEntries.Contains(Neighbor)) { DisabledStack.Add(Neighbor); }
									}
								}
							}
						}
					}
				}
			}
			continue;
		}

		// Follow Root output pin to find the connected entry node (match center)
		const UEdGraphPin* RootOutPin = Header->FindPin(TEXT("RootOut"), EGPD_Output);
		if (!RootOutPin || RootOutPin->LinkedTo.Num() == 0) { continue; } // Unwired header — silently skip (WIP state)

		UPCGExConnectorPatternGraphNode* RootEntry = nullptr;
		for (const UEdGraphPin* LinkedPin : RootOutPin->LinkedTo)
		{
			if (!LinkedPin || !IsValid(LinkedPin->GetOwningNode())) { continue; }
			if (Cast<UPCGExConnectorPatternConstraintNode>(LinkedPin->GetOwningNode())) { continue; }
			RootEntry = Cast<UPCGExConnectorPatternGraphNode>(LinkedPin->GetOwningNode());
			if (RootEntry) { break; }
		}

		if (!RootEntry) { continue; }

		// Flood-fill from root entry through connector wires (skip headers, skip constraint nodes)
		TArray<UPCGExConnectorPatternGraphNode*> Component;
		TSet<UPCGExConnectorPatternGraphNode*> Visited;
		TArray<UPCGExConnectorPatternGraphNode*> Stack;
		Stack.Add(RootEntry);

		while (Stack.Num() > 0)
		{
			UPCGExConnectorPatternGraphNode* Current = Stack.Pop();
			if (Visited.Contains(Current)) { continue; }
			Visited.Add(Current);
			Component.Add(Current);

			for (const UEdGraphPin* Pin : Current->Pins)
			{
				// Skip Root pins during flood-fill (only follow connector/Any wires)
				if (Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory) { continue; }

				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !IsValid(LinkedPin->GetOwningNode())) { continue; }
					if (Cast<UPCGExConnectorPatternConstraintNode>(LinkedPin->GetOwningNode())) { continue; }
					if (Cast<UPCGExConnectorPatternHeaderNode>(LinkedPin->GetOwningNode())) { continue; }
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

		// Root entry is already at index 0 (first added to Component)
		// Mark all entries as claimed
		for (UPCGExConnectorPatternGraphNode* Entry : Component)
		{
			ClaimedEntries.Add(Entry);
		}

		// Build entry index map
		TMap<UPCGExConnectorPatternGraphNode*, int32> NodeToEntryIndex;
		for (int32 i = 0; i < Component.Num(); i++)
		{
			NodeToEntryIndex.Add(Component[i], i);
		}

		FPCGExConnectorPatternAuthored& Pattern = Asset->Patterns.Emplace_GetRef();

		// Copy pattern-level settings from header
		Pattern.PatternName = Header->PatternName;
		Pattern.Weight = Header->Weight;
		Pattern.bExclusive = Header->bExclusive;
		Pattern.MinMatches = Header->MinMatches;
		Pattern.MaxMatches = Header->MaxMatches;
		Pattern.OutputStrategy = Header->OutputStrategy;
		Pattern.TransformMode = Header->TransformMode;

		// Build entries
		for (int32 EntryIdx = 0; EntryIdx < Component.Num(); EntryIdx++)
		{
			const UPCGExConnectorPatternGraphNode* EntryNode = Component[EntryIdx];
			FPCGExConnectorPatternEntryAuthored& Entry = Pattern.Entries.Emplace_GetRef();

			Entry.ModuleNames = EntryNode->ModuleNames;
			Entry.bIsActive = EntryNode->bIsActive;

			// Build adjacencies from output pin connections
			TMap<int32, FPCGExConnectorPatternAdjacencyAuthored*> AdjacencyMap;

			for (const UEdGraphPin* Pin : EntryNode->Pins)
			{
				if (Pin->Direction != EGPD_Output) { continue; }
				// Skip Root pins
				if (Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory) { continue; }

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

					// Skip header nodes
					if (Cast<UPCGExConnectorPatternHeaderNode>(LinkedPin->GetOwningNode())) { continue; }

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
				// Skip Root pins
				if (Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory) { continue; }

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

	// Warn about orphan entry nodes (not reachable from any header)
	for (UEdGraphNode* Node : Nodes)
	{
		if (Cast<UPCGExConnectorPatternConstraintNode>(Node)) { continue; }
		if (Cast<UPCGExConnectorPatternHeaderNode>(Node)) { continue; }
		if (UPCGExConnectorPatternGraphNode* EntryNode = Cast<UPCGExConnectorPatternGraphNode>(Node))
		{
			if (!ClaimedEntries.Contains(EntryNode))
			{
				UE_LOG(LogTemp, Warning, TEXT("ConnectorPattern Compile: Orphan entry node '%s' not reachable from any pattern header — skipped"),
					*EntryNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
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

void UPCGExConnectorPatternGraph::RefreshPCGComponents()
{
	if (!GEditor) { return; }

	// Collect unique PCG actors from all ValencyContextVolumes in editor worlds
	TSet<AActor*> PCGActors;

	for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!World || World->WorldType != EWorldType::Editor) { continue; }

		for (TActorIterator<AValencyContextVolume> It(World); It; ++It)
		{
			for (const TObjectPtr<AActor>& ActorPtr : It->PCGActorsToRegenerate)
			{
				if (AActor* Actor = ActorPtr.Get())
				{
					PCGActors.Add(Actor);
				}
			}
		}
	}

	if (PCGActors.IsEmpty()) { return; }

	// Flush PCG cache so stale compiled data is discarded
	if (UPCGSubsystem* Subsystem = UPCGSubsystem::GetActiveEditorInstance())
	{
		Subsystem->FlushCache();
	}

	// Regenerate only the referenced PCG components
	for (AActor* Actor : PCGActors)
	{
		TArray<UPCGComponent*> PCGComponents;
		Actor->GetComponents<UPCGComponent>(PCGComponents);

		for (UPCGComponent* PCGComponent : PCGComponents)
		{
			if (!PCGComponent) { continue; }
			PCGComponent->Cleanup(true);
			PCGComponent->Generate(true);
		}
	}
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

		// Create header node with pattern-level settings, positioned above entries
		FGraphNodeCreator<UPCGExConnectorPatternHeaderNode> HeaderCreator(*this);
		UPCGExConnectorPatternHeaderNode* HeaderNode = HeaderCreator.CreateNode(true);
		HeaderNode->NodePosX = static_cast<int32>(XOffset);
		HeaderNode->NodePosY = -150;
		HeaderNode->PatternName = Pattern.PatternName;
		HeaderNode->Weight = Pattern.Weight;
		HeaderNode->bExclusive = Pattern.bExclusive;
		HeaderNode->MinMatches = Pattern.MinMatches;
		HeaderNode->MaxMatches = Pattern.MaxMatches;
		HeaderNode->OutputStrategy = Pattern.OutputStrategy;
		HeaderNode->TransformMode = Pattern.TransformMode;
		HeaderCreator.Finalize();

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

		// Wire header Root output → entry[0] Root input
		if (CreatedNodes.Num() > 0)
		{
			UEdGraphPin* HeaderRootOut = HeaderNode->FindPin(TEXT("RootOut"), EGPD_Output);
			UEdGraphPin* EntryRootIn = CreatedNodes[0]->FindPin(TEXT("RootIn"), EGPD_Input);
			if (HeaderRootOut && EntryRootIn)
			{
				HeaderRootOut->MakeLinkTo(EntryRootIn);
			}
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
