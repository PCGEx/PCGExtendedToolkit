// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternGraphSchema.h"

#include "ConnectionDrawingPolicy.h"
#include "EdGraphNode_Comment.h"
#include "ScopedTransaction.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternConstraintNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
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

#pragma region FPCGExConnectorPatternGraphSchemaAction_NewNode

UEdGraphNode* FPCGExConnectorPatternGraphSchemaAction_NewNode::PerformAction(
	UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	FGraphNodeCreator<UPCGExConnectorPatternGraphNode> NodeCreator(*ParentGraph);
	UPCGExConnectorPatternGraphNode* NewNode = NodeCreator.CreateNode(bSelectNewNode);
	NewNode->NodePosX = static_cast<int32>(Location.X);
	NewNode->NodePosY = static_cast<int32>(Location.Y);
	NewNode->bIsPatternRoot = bCreateAsRoot;

	if (bCreateAsRoot)
	{
		NewNode->PatternName = FName("NewPattern");
	}

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

#pragma region UPCGExConnectorPatternGraphSchema

void UPCGExConnectorPatternGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// "Add Pattern Root" action
	{
		TSharedPtr<FPCGExConnectorPatternGraphSchemaAction_NewNode> Action = MakeShared<FPCGExConnectorPatternGraphSchemaAction_NewNode>(
			FText::FromString(TEXT("Pattern")),
			FText::FromString(TEXT("Add Pattern Root")),
			FText::FromString(TEXT("Create a new pattern root node (entry 0)")),
			0);
		Action->bCreateAsRoot = true;
		ContextMenuBuilder.AddAction(Action);
	}

	// "Add Pattern Entry" action
	{
		TSharedPtr<FPCGExConnectorPatternGraphSchemaAction_NewNode> Action = MakeShared<FPCGExConnectorPatternGraphSchemaAction_NewNode>(
			FText::FromString(TEXT("Pattern")),
			FText::FromString(TEXT("Add Pattern Entry")),
			FText::FromString(TEXT("Create a new pattern entry node")),
			0);
		Action->bCreateAsRoot = false;
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

	// "Add Output Pin" submenu
	if (ConnSet && ConnSet->ConnectorTypes.Num() > 0)
	{
		FToolMenuSection& AddOutputSection = Menu->AddSection("AddOutputPins", INVTEXT("Add Output Pin"));

		for (int32 i = 0; i < ConnSet->ConnectorTypes.Num(); i++)
		{
			const FPCGExValencyConnectorEntry& ConnEntry = ConnSet->ConnectorTypes[i];

			if (PatternNode->HasConnectorPin(ConnEntry.TypeId, EGPD_Output)) { continue; }

			const int32 TypeId = ConnEntry.TypeId;
			const FName TypeName = ConnEntry.ConnectorType;

			AddOutputSection.AddMenuEntry(
				FName(*FString::Printf(TEXT("AddOutPin_%s"), *TypeName.ToString())),
				FText::FromString(FString::Printf(TEXT("'%s' Output"), *TypeName.ToString())),
				FText::FromString(FString::Printf(TEXT("Add output pin for type '%s'"), *TypeName.ToString())),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[PatternNode, TypeId, TypeName, PatternGraph]()
					{
						const FScopedTransaction Transaction(FText::FromString(TEXT("Add Output Pin")));
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

	// "Add Input Pin" submenu
	if (ConnSet && ConnSet->ConnectorTypes.Num() > 0)
	{
		FToolMenuSection& AddInputSection = Menu->AddSection("AddInputPins", INVTEXT("Add Input Pin"));

		for (int32 i = 0; i < ConnSet->ConnectorTypes.Num(); i++)
		{
			const FPCGExValencyConnectorEntry& ConnEntry = ConnSet->ConnectorTypes[i];

			if (PatternNode->HasConnectorPin(ConnEntry.TypeId, EGPD_Input)) { continue; }

			const int32 TypeId = ConnEntry.TypeId;
			const FName TypeName = ConnEntry.ConnectorType;

			AddInputSection.AddMenuEntry(
				FName(*FString::Printf(TEXT("AddInPin_%s"), *TypeName.ToString())),
				FText::FromString(FString::Printf(TEXT("'%s' Input"), *TypeName.ToString())),
				FText::FromString(FString::Printf(TEXT("Add input pin for type '%s'"), *TypeName.ToString())),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[PatternNode, TypeId, TypeName, PatternGraph]()
					{
						const FScopedTransaction Transaction(FText::FromString(TEXT("Add Input Pin")));
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
	}

	// "Remove Connector Pin" submenu
	if (PatternNode->ConnectorPins.Num() > 0)
	{
		FToolMenuSection& RemoveSection = Menu->AddSection("RemoveConnectorPins", INVTEXT("Remove Pins"));

		for (const FPCGExConnectorPinEntry& PinEntry : PatternNode->ConnectorPins)
		{
			const int32 TypeId = PinEntry.StoredTypeId;
			const FName TypeName = PinEntry.StoredTypeName;

			if (PinEntry.bOutput)
			{
				RemoveSection.AddMenuEntry(
					FName(*FString::Printf(TEXT("RemoveOutPin_%s"), *TypeName.ToString())),
					FText::FromString(FString::Printf(TEXT("Remove '%s' Output"), *TypeName.ToString())),
					FText::FromString(FString::Printf(TEXT("Remove output pin for type '%s'"), *TypeName.ToString())),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda(
						[PatternNode, TypeId, PatternGraph]()
						{
							const FScopedTransaction Transaction(FText::FromString(TEXT("Remove Output Pin")));
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

			if (PinEntry.bInput)
			{
				RemoveSection.AddMenuEntry(
					FName(*FString::Printf(TEXT("RemoveInPin_%s"), *TypeName.ToString())),
					FText::FromString(FString::Printf(TEXT("Remove '%s' Input"), *TypeName.ToString())),
					FText::FromString(FString::Printf(TEXT("Remove input pin for type '%s'"), *TypeName.ToString())),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda(
						[PatternNode, TypeId, PatternGraph]()
						{
							const FScopedTransaction Transaction(FText::FromString(TEXT("Remove Input Pin")));
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

	// "Any" pins can connect to anything
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
	const bool bResult = UEdGraphSchema::TryCreateConnection(A, B);
	if (bResult)
	{
		TriggerRecompile(A->GetOwningNode()->GetGraph());
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
	// Create a single root node
	FGraphNodeCreator<UPCGExConnectorPatternGraphNode> NodeCreator(Graph);
	UPCGExConnectorPatternGraphNode* RootNode = NodeCreator.CreateNode(true);
	RootNode->NodePosX = 0;
	RootNode->NodePosY = 0;
	RootNode->bIsPatternRoot = true;
	RootNode->PatternName = FName("Pattern");
	NodeCreator.Finalize();
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
