// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"

#include "PCGExConnectorPatternGraphSchema.generated.h"

class UPCGExConnectorPatternGraph;

/** Action to create a standalone pattern entry node */
USTRUCT()
struct FPCGExConnectorPatternGraphSchemaAction_NewEntry : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	FPCGExConnectorPatternGraphSchemaAction_NewEntry()
	{
	}

	FPCGExConnectorPatternGraphSchemaAction_NewEntry(
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{
	}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
};

/** Action to create a new pattern (header + entry pair, pre-wired) */
USTRUCT()
struct FPCGExSchemaAction_AddPattern : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	FPCGExSchemaAction_AddPattern()
	{
	}

	FPCGExSchemaAction_AddPattern(
		FText InNodeCategory, FText InMenuDesc, FText InToolTip, int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{
	}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
};

/**
 * Schema defining connection rules, context menus, and pin colors for the Connector Pattern graph.
 */
UCLASS()
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExConnectorPatternGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	//~ UEdGraphSchema interface
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual TSharedPtr<FEdGraphSchemaAction> GetCreateCommentAction() const override;
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;

	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;

private:
	/** Trigger recompile on the owning graph */
	void TriggerRecompile(UEdGraph* Graph) const;

	/** Get the pattern graph cast */
	static UPCGExConnectorPatternGraph* GetPatternGraph(const UEdGraph* Graph);
};
