// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "EdGraph/EdGraphPin.h"

#include "K2Node_SwitchOnCollectionType.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;
class UEdGraphPin;

/**
 * Exec switch over the registered PCGEx collection types (FTypeRegistry). One exec output
 * per registered type, each paired with a pre-cast typed collection output ("As Mesh
 * Collection", ...), plus a Default branch for unregistered types.
 *
 * Matching is EXACT TypeId equality (UPCGExAssetCollection::GetTypeId): Blueprint
 * subclasses of a native collection report their native TypeId and route to that branch;
 * custom types with their own registered TypeId get their own pins.
 *
 * Pins are generated from the registry at node (re)construction time and sorted lexically
 * by TypeId for stable ordering. Pins for types whose module is no longer loaded become
 * orphaned on reconstruction (standard broken-link warnings). A user TypeId literally
 * named "Selection" or "Default" would collide with the internal switch pins -- avoid.
 */
UCLASS(meta=(DisplayName="Switch on Collection Type", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline/switch-on-collection-type"))
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_SwitchOnCollectionType : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FLinearColor GetNodeTitleColor() const override;

	// UK2Node
	virtual bool IsNodePure() const override
	{
		return false;
	}

	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

private:
	UEdGraphPin* GetExecInputPin() const;
	UEdGraphPin* GetCollectionPin() const;

	/** Registered type ids with live collection classes, sorted lexically for stable pin order. */
	static void GetSortedTypeIds(TArray<FName>& OutTypeIds);

	/** Name of the typed collection output pin paired with a type's exec output. */
	static FName MakeTypedOutPinName(FName TypeId);

	static const FName CollectionPinName;
	static const FName DefaultExecPinName;
};
