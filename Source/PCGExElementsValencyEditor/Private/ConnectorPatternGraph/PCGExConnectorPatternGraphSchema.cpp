// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternGraphSchema.h"

#include "ConnectionDrawingPolicy.h"
#include "EdGraphNode_Comment.h"
#include "ScopedTransaction.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternConstraintNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternHeaderNode.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

namespace
{
	/** Wire color inherits from the output pin's type color. */
	class FPCGExPatternConnectionDrawingPolicy : public FConnectionDrawingPolicy
	{
	public:
		FPCGExPatternConnectionDrawingPolicy(
			int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
			const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements,
			const UEdGraphSchema* InSchema)
			: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
			, Schema(InSchema)
		{
		}

		virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params) override
		{
			Params.AssociatedPin1 = OutputPin;
			Params.AssociatedPin2 = InputPin;
			Params.WireThickness = 1.5f;

			if (OutputPin && Schema)
			{
				Params.WireColor = Schema->GetPinTypeColor(OutputPin->PinType);
			}

			if (HoveredPins.Num() > 0)
			{
				ApplyHoverDeemphasis(OutputPin, InputPin, Params.WireThickness, Params.WireColor);
			}
		}

	private:
		const UEdGraphSchema* Schema = nullptr;
	};

	/** Schema action for creating constraint nodes (Boundary/Wildcard). */
	struct FPCGExSchemaAction_AddConstraint : public FEdGraphSchemaAction
	{
		EPCGExPatternConstraintType ConstraintType = EPCGExPatternConstraintType::Boundary;

		FPCGExSchemaAction_AddConstraint(FText InCategory, FText InMenuDesc, FText InToolTip, int32 InGrouping, EPCGExPatternConstraintType InType)
			: FEdGraphSchemaAction(MoveTemp(InCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
			, ConstraintType(InType)
		{
		}

		virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
		{
			FGraphNodeCreator<UPCGExConnectorPatternConstraintNode> NodeCreator(*ParentGraph);
			UPCGExConnectorPatternConstraintNode* NewNode = NodeCreator.CreateNode(bSelectNewNode);
			NewNode->NodePosX = static_cast<int32>(Location.X);
			NewNode->NodePosY = static_cast<int32>(Location.Y);
			NewNode->ConstraintType = ConstraintType;
			NodeCreator.Finalize();

			if (FromPin)
			{
				NewNode->AutowireNewNode(FromPin);
			}

			if (UPCGExConnectorPatternGraph* PatternGraph = Cast<UPCGExConnectorPatternGraph>(ParentGraph))
			{
				PatternGraph->CompileGraphToAsset();
			}

			return NewNode;
		}
	};

	/** Schema action for creating comment boxes. */
	struct FPCGExSchemaAction_AddComment : public FEdGraphSchemaAction
	{
		FPCGExSchemaAction_AddComment()
			: FEdGraphSchemaAction(
				FText::FromString(TEXT("Utility")),
				INVTEXT("Add Comment..."),
				INVTEXT("Create a resizable comment box"),
				0)
		{
		}

		virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
		{
			UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(ParentGraph);
			Comment->NodePosX = static_cast<int32>(Location.X);
			Comment->NodePosY = static_cast<int32>(Location.Y);
			Comment->NodeWidth = 400;
			Comment->NodeHeight = 100;
			Comment->NodeComment = TEXT("Comment");
			ParentGraph->AddNode(Comment, true, bSelectNewNode);
			return Comment;
		}
	};
}

#pragma region FPCGExConnectorPatternGraphSchemaAction_NewEntry

UEdGraphNode* FPCGExConnectorPatternGraphSchemaAction_NewEntry::PerformAction(
	UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	FGraphNodeCreator<UPCGExConnectorPatternGraphNode> NodeCreator(*ParentGraph);
	UPCGExConnectorPatternGraphNode* NewNode = NodeCreator.CreateNode(bSelectNewNode);
	NewNode->NodePosX = static_cast<int32>(Location.X);
	NewNode->NodePosY = static_cast<int32>(Location.Y);
	NodeCreator.Finalize();

	// Auto-wire if dragged from a pin
	if (FromPin)
	{
		NewNode->AutowireNewNode(FromPin);
	}

	// Trigger recompile
	if (UPCGExConnectorPatternGraph* PatternGraph = Cast<UPCGExConnectorPatternGraph>(ParentGraph))
	{
		PatternGraph->CompileGraphToAsset();
	}

	return NewNode;
}

#pragma endregion

#pragma region FPCGExSchemaAction_AddPattern

UEdGraphNode* FPCGExSchemaAction_AddPattern::PerformAction(
	UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	// Create header node
	FGraphNodeCreator<UPCGExConnectorPatternHeaderNode> HeaderCreator(*ParentGraph);
	UPCGExConnectorPatternHeaderNode* HeaderNode = HeaderCreator.CreateNode(bSelectNewNode);
	HeaderNode->NodePosX = static_cast<int32>(Location.X);
	HeaderNode->NodePosY = static_cast<int32>(Location.Y);
	HeaderNode->PatternName = FName("NewPattern");
	HeaderCreator.Finalize();

	// Create entry node below header
	FGraphNodeCreator<UPCGExConnectorPatternGraphNode> EntryCreator(*ParentGraph);
	UPCGExConnectorPatternGraphNode* EntryNode = EntryCreator.CreateNode(false);
	EntryNode->NodePosX = static_cast<int32>(Location.X);
	EntryNode->NodePosY = static_cast<int32>(Location.Y) + 150;
	EntryCreator.Finalize();

	// Wire header Root output → entry Root input
	UEdGraphPin* HeaderRootOut = HeaderNode->FindPin(TEXT("RootOut"), EGPD_Output);
	UEdGraphPin* EntryRootIn = EntryNode->FindPin(TEXT("RootIn"), EGPD_Input);
	if (HeaderRootOut && EntryRootIn)
	{
		HeaderRootOut->MakeLinkTo(EntryRootIn);
	}

	// Trigger recompile
	if (UPCGExConnectorPatternGraph* PatternGraph = Cast<UPCGExConnectorPatternGraph>(ParentGraph))
	{
		PatternGraph->CompileGraphToAsset();
	}

	return HeaderNode;
}

#pragma endregion

#pragma region UPCGExConnectorPatternGraphSchema

void UPCGExConnectorPatternGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// "Add Pattern" action — creates header + entry pair, pre-wired
	{
		TSharedPtr<FPCGExSchemaAction_AddPattern> Action = MakeShared<FPCGExSchemaAction_AddPattern>(
			FText::FromString(TEXT("Pattern")),
			FText::FromString(TEXT("Add Pattern")),
			FText::FromString(TEXT("Create a new pattern (header + entry, pre-wired)")),
			0);
		ContextMenuBuilder.AddAction(Action);
	}

	// "Add Pattern Entry" action — standalone entry node
	{
		TSharedPtr<FPCGExConnectorPatternGraphSchemaAction_NewEntry> Action = MakeShared<FPCGExConnectorPatternGraphSchemaAction_NewEntry>(
			FText::FromString(TEXT("Pattern")),
			FText::FromString(TEXT("Add Pattern Entry")),
			FText::FromString(TEXT("Create a standalone pattern entry node")),
			0);
		ContextMenuBuilder.AddAction(Action);
	}

	// "Add Boundary Constraint" action
	ContextMenuBuilder.AddAction(MakeShared<FPCGExSchemaAction_AddConstraint>(
		FText::FromString(TEXT("Constraints")),
		INVTEXT("Add Boundary Constraint"),
		INVTEXT("Create a boundary node (connected types must have NO neighbors)"),
		0,
		EPCGExPatternConstraintType::Boundary));

	// "Add Wildcard Constraint" action
	ContextMenuBuilder.AddAction(MakeShared<FPCGExSchemaAction_AddConstraint>(
		FText::FromString(TEXT("Constraints")),
		INVTEXT("Add Wildcard Constraint"),
		INVTEXT("Create a wildcard node (connected types must have at least one neighbor)"),
		0,
		EPCGExPatternConstraintType::Wildcard));
}

void UPCGExConnectorPatternGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetContextMenuActions(Menu, Context);

	if (!Context || !Context->Node) { return; }

	UPCGExConnectorPatternGraphNode* PatternNode = const_cast<UPCGExConnectorPatternGraphNode*>(Cast<UPCGExConnectorPatternGraphNode>(Context->Node.Get()));
	if (!PatternNode) { return; }

	const UPCGExConnectorPatternGraph* PatternGraph = Cast<UPCGExConnectorPatternGraph>(Context->Graph.Get());
	const UPCGExValencyConnectorSet* ConnSet = PatternGraph ? PatternGraph->GetConnectorSet() : nullptr;

	if (ConnSet && ConnSet->ConnectorTypes.Num() > 0)
	{
		// --- Add Input ---
		FToolMenuSection& AddInputSection = Menu->AddSection("AddInputPins", INVTEXT("Add Input"));
		for (const FPCGExValencyConnectorEntry& ConnEntry : ConnSet->ConnectorTypes)
		{
			if (PatternNode->HasConnectorPin(ConnEntry.TypeId, EGPD_Input)) { continue; }

			const int32 TypeId = ConnEntry.TypeId;
			const FName TypeName = ConnEntry.ConnectorType;

			AddInputSection.AddMenuEntry(
				FName(*FString::Printf(TEXT("AddInPin_%d"), TypeId)),
				FText::FromString(FString::Printf(TEXT("+ %s"), *TypeName.ToString())),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[PatternNode, TypeId, TypeName, PatternGraph]()
					{
						const FScopedTransaction Transaction(INVTEXT("Add Input Pin"));
						PatternNode->Modify();
						PatternNode->AddConnectorPin(TypeId, TypeName, EGPD_Input);
						PatternNode->GetGraph()->NotifyGraphChanged();

						if (UPCGExConnectorPatternGraph* MutableGraph = const_cast<UPCGExConnectorPatternGraph*>(PatternGraph))
						{
							MutableGraph->CompileGraphToAsset();
						}
					}))
			);
		}

		// --- Add Output ---
		FToolMenuSection& AddOutputSection = Menu->AddSection("AddOutputPins", INVTEXT("Add Output"));
		for (const FPCGExValencyConnectorEntry& ConnEntry : ConnSet->ConnectorTypes)
		{
			if (PatternNode->HasConnectorPin(ConnEntry.TypeId, EGPD_Output)) { continue; }

			const int32 TypeId = ConnEntry.TypeId;
			const FName TypeName = ConnEntry.ConnectorType;

			AddOutputSection.AddMenuEntry(
				FName(*FString::Printf(TEXT("AddOutPin_%d"), TypeId)),
				FText::FromString(FString::Printf(TEXT("+ %s"), *TypeName.ToString())),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[PatternNode, TypeId, TypeName, PatternGraph]()
					{
						const FScopedTransaction Transaction(INVTEXT("Add Output Pin"));
						PatternNode->Modify();
						PatternNode->AddConnectorPin(TypeId, TypeName, EGPD_Output);
						PatternNode->GetGraph()->NotifyGraphChanged();

						if (UPCGExConnectorPatternGraph* MutableGraph = const_cast<UPCGExConnectorPatternGraph*>(PatternGraph))
						{
							MutableGraph->CompileGraphToAsset();
						}
					}))
			);
		}
	}

	// --- Remove Input ---
	{
		bool bHasInputPins = false;
		for (const FPCGExConnectorPinEntry& PinEntry : PatternNode->ConnectorPins)
		{
			if (PinEntry.bInput) { bHasInputPins = true; break; }
		}

		if (bHasInputPins)
		{
			FToolMenuSection& RemoveInputSection = Menu->AddSection("RemoveInputPins", INVTEXT("Remove Input"));
			for (const FPCGExConnectorPinEntry& PinEntry : PatternNode->ConnectorPins)
			{
				if (!PinEntry.bInput) { continue; }

				const int32 TypeId = PinEntry.StoredTypeId;
				const FName TypeName = PinEntry.StoredTypeName;

				RemoveInputSection.AddMenuEntry(
					FName(*FString::Printf(TEXT("RemoveInPin_%d"), TypeId)),
					FText::FromString(FString::Printf(TEXT("- %s"), *TypeName.ToString())),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda(
						[PatternNode, TypeId, PatternGraph]()
						{
							const FScopedTransaction Transaction(INVTEXT("Remove Input Pin"));
							PatternNode->Modify();
							PatternNode->RemoveConnectorPin(TypeId, EGPD_Input);
							PatternNode->GetGraph()->NotifyGraphChanged();

							if (UPCGExConnectorPatternGraph* MutableGraph = const_cast<UPCGExConnectorPatternGraph*>(PatternGraph))
							{
								MutableGraph->CompileGraphToAsset();
							}
						}))
				);
			}
		}
	}

	// --- Remove Output ---
	{
		bool bHasOutputPins = false;
		for (const FPCGExConnectorPinEntry& PinEntry : PatternNode->ConnectorPins)
		{
			if (PinEntry.bOutput) { bHasOutputPins = true; break; }
		}

		if (bHasOutputPins)
		{
			FToolMenuSection& RemoveOutputSection = Menu->AddSection("RemoveOutputPins", INVTEXT("Remove Output"));
			for (const FPCGExConnectorPinEntry& PinEntry : PatternNode->ConnectorPins)
			{
				if (!PinEntry.bOutput) { continue; }

				const int32 TypeId = PinEntry.StoredTypeId;
				const FName TypeName = PinEntry.StoredTypeName;

				RemoveOutputSection.AddMenuEntry(
					FName(*FString::Printf(TEXT("RemoveOutPin_%d"), TypeId)),
					FText::FromString(FString::Printf(TEXT("- %s"), *TypeName.ToString())),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda(
						[PatternNode, TypeId, PatternGraph]()
						{
							const FScopedTransaction Transaction(INVTEXT("Remove Output Pin"));
							PatternNode->Modify();
							PatternNode->RemoveConnectorPin(TypeId, EGPD_Output);
							PatternNode->GetGraph()->NotifyGraphChanged();

							if (UPCGExConnectorPatternGraph* MutableGraph = const_cast<UPCGExConnectorPatternGraph*>(PatternGraph))
							{
								MutableGraph->CompileGraphToAsset();
							}
						}))
				);
			}
		}
	}

	// --- Cleanup: Remove Stale Pins ---
	if (ConnSet && PatternNode->ConnectorPins.Num() > 0)
	{
		// Check if any pins reference types no longer in ConnectorSet
		bool bHasStalePins = false;
		for (const FPCGExConnectorPinEntry& PinEntry : PatternNode->ConnectorPins)
		{
			if (ConnSet->FindConnectorTypeIndexById(PinEntry.StoredTypeId) == INDEX_NONE)
			{
				bHasStalePins = true;
				break;
			}
		}

		if (bHasStalePins)
		{
			FToolMenuSection& CleanupSection = Menu->AddSection("PinCleanup", INVTEXT("Cleanup"));
			CleanupSection.AddMenuEntry(
				"RemoveStalePins",
				INVTEXT("Remove Stale Pins"),
				INVTEXT("Remove all connector pins whose type no longer exists in the ConnectorSet"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[PatternNode, ConnSet, PatternGraph]()
					{
						const FScopedTransaction Transaction(INVTEXT("Remove Stale Pins"));
						PatternNode->Modify();
						if (PatternNode->RemoveStalePins(ConnSet))
						{
							PatternNode->GetGraph()->NotifyGraphChanged();
							if (UPCGExConnectorPatternGraph* MutableGraph = const_cast<UPCGExConnectorPatternGraph*>(PatternGraph))
							{
								MutableGraph->CompileGraphToAsset();
							}
						}
					}))
			);
		}
	}
}

const FPinConnectionResponse UPCGExConnectorPatternGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	// No self-connect
	if (A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Cannot connect to self"));
	}

	// Must be Output→Input
	const UEdGraphPin* OutputPin = (A->Direction == EGPD_Output) ? A : B;
	const UEdGraphPin* InputPin = (A->Direction == EGPD_Input) ? A : B;

	if (OutputPin->Direction != EGPD_Output || InputPin->Direction != EGPD_Input)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Must connect output to input"));
	}

	// PatternRoot pin validation
	const bool bOutputIsRoot = (OutputPin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory);
	const bool bInputIsRoot = (InputPin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory);

	if (bOutputIsRoot || bInputIsRoot)
	{
		// Root output → Root input: exact match
		if (bOutputIsRoot && bInputIsRoot)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_AB, TEXT(""));
		}

		// Root output dropped on a non-Root pin of an entry node that has a RootIn pin:
		// Allow it — TryCreateConnection will redirect to RootIn
		if (bOutputIsRoot && !bInputIsRoot)
		{
			if (InputPin->GetOwningNode()->FindPin(TEXT("RootIn"), EGPD_Input))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_AB, TEXT(""));
			}
		}

		// Cross-category disallow (Root input can't connect to non-Root output, etc.)
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Root pin can only connect to an entry node"));
	}

	// "Any" pins can connect to anything (except Root pins, handled above)
	if (OutputPin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::AnyPinCategory ||
		InputPin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::AnyPinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
	}

	// Both are connector type pins — check compatibility if ConnectorSet available
	const UPCGExConnectorPatternGraph* PatternGraph = GetPatternGraph(OutputPin->GetOwningNode()->GetGraph());
	const UPCGExValencyConnectorSet* ConnSet = PatternGraph ? PatternGraph->GetConnectorSet() : nullptr;

	if (ConnSet)
	{
		const FName SourceType = OutputPin->PinType.PinSubCategory;
		const FName TargetType = InputPin->PinType.PinSubCategory;

		const int32 SourceIdx = ConnSet->FindConnectorTypeIndex(SourceType);
		const int32 TargetIdx = ConnSet->FindConnectorTypeIndex(TargetType);

		if (SourceIdx != INDEX_NONE && TargetIdx != INDEX_NONE)
		{
			if (!ConnSet->AreTypesCompatible(SourceIdx, TargetIdx))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Incompatible connector types"));
			}
		}
	}

	// Permissive: allow if no ConnectorSet or types not found
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
}

bool UPCGExConnectorPatternGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	// Redirect: Root output dropped on a non-Root pin → reroute to that node's RootIn pin
	UEdGraphPin* Output = (A->Direction == EGPD_Output) ? A : B;
	UEdGraphPin* Input = (A->Direction == EGPD_Input) ? A : B;

	if (Output->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory &&
		Input->PinType.PinCategory != UPCGExConnectorPatternGraphNode::PatternRootPinCategory)
	{
		if (UEdGraphPin* RootIn = Input->GetOwningNode()->FindPin(TEXT("RootIn"), EGPD_Input))
		{
			Input = RootIn;
		}
	}

	const bool bResult = UEdGraphSchema::TryCreateConnection(Output, Input);
	if (bResult)
	{
		TriggerRecompile(Output->GetOwningNode()->GetGraph());
	}
	return bResult;
}

void UPCGExConnectorPatternGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	UEdGraphSchema::BreakPinLinks(TargetPin, bSendsNodeNotification);
	TriggerRecompile(TargetPin.GetOwningNode()->GetGraph());
}

void UPCGExConnectorPatternGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	UEdGraphSchema::BreakSinglePinLink(SourcePin, TargetPin);
	TriggerRecompile(SourcePin->GetOwningNode()->GetGraph());
}

FLinearColor UPCGExConnectorPatternGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory)
	{
		return FLinearColor(0.6f, 0.3f, 0.9f); // Purple for Root wire
	}

	if (PinType.PinCategory == UPCGExConnectorPatternGraphNode::AnyPinCategory)
	{
		return FLinearColor::White;
	}

	if (PinType.PinCategory == UPCGExConnectorPatternGraphNode::ConnectorPinCategory)
	{
		// Resolve color from ConnectorSet stored in PinSubCategoryObject
		if (const UPCGExValencyConnectorSet* ConnSet = Cast<UPCGExValencyConnectorSet>(PinType.PinSubCategoryObject.Get()))
		{
			const int32 TypeIdx = ConnSet->FindConnectorTypeIndex(PinType.PinSubCategory);
			if (TypeIdx != INDEX_NONE)
			{
				return ConnSet->ConnectorTypes[TypeIdx].DebugColor;
			}
		}

		// Fallback: hash-based color when ConnectorSet unavailable
		const uint32 Hash = GetTypeHash(PinType.PinSubCategory);
		const float H = (Hash % 360) / 360.0f;
		return FLinearColor::MakeFromHSV8(
			static_cast<uint8>(H * 255),
			180,
			220);
	}

	return FLinearColor::Gray;
}

void UPCGExConnectorPatternGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	// Create header node
	FGraphNodeCreator<UPCGExConnectorPatternHeaderNode> HeaderCreator(Graph);
	UPCGExConnectorPatternHeaderNode* HeaderNode = HeaderCreator.CreateNode(true);
	HeaderNode->NodePosX = 0;
	HeaderNode->NodePosY = 0;
	HeaderNode->PatternName = FName("Pattern");
	HeaderCreator.Finalize();

	// Create entry node below header
	FGraphNodeCreator<UPCGExConnectorPatternGraphNode> EntryCreator(Graph);
	UPCGExConnectorPatternGraphNode* EntryNode = EntryCreator.CreateNode(false);
	EntryNode->NodePosX = 0;
	EntryNode->NodePosY = 150;
	EntryCreator.Finalize();

	// Wire header Root output → entry Root input
	UEdGraphPin* HeaderRootOut = HeaderNode->FindPin(TEXT("RootOut"), EGPD_Output);
	UEdGraphPin* EntryRootIn = EntryNode->FindPin(TEXT("RootIn"), EGPD_Input);
	if (HeaderRootOut && EntryRootIn)
	{
		HeaderRootOut->MakeLinkTo(EntryRootIn);
	}
}

TSharedPtr<FEdGraphSchemaAction> UPCGExConnectorPatternGraphSchema::GetCreateCommentAction() const
{
	return MakeShared<FPCGExSchemaAction_AddComment>();
}

FConnectionDrawingPolicy* UPCGExConnectorPatternGraphSchema::CreateConnectionDrawingPolicy(
	int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FPCGExPatternConnectionDrawingPolicy(
		InBackLayerID, InFrontLayerID, InZoomFactor,
		InClippingRect, InDrawElements, this);
}

void UPCGExConnectorPatternGraphSchema::TriggerRecompile(UEdGraph* Graph) const
{
	if (UPCGExConnectorPatternGraph* PatternGraph = Cast<UPCGExConnectorPatternGraph>(Graph))
	{
		PatternGraph->CompileGraphToAsset();
	}
}

UPCGExConnectorPatternGraph* UPCGExConnectorPatternGraphSchema::GetPatternGraph(const UEdGraph* Graph)
{
	return const_cast<UPCGExConnectorPatternGraph*>(Cast<UPCGExConnectorPatternGraph>(Graph));
}

#pragma endregion
