// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "EdGraph/EdGraphPin.h"

#include "K2Node_SetPCGExEntryProperty.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;
class UEdGraphPin;
class UGraphNodeContextMenuContext;
class UToolMenu;

/**
 * Blueprint node that writes a value to an entry's property override slot on a
 * UPCGExAssetCollection (by raw entry index) and reads back the resolved value after any
 * type coercion. The override slot is enabled by the write; a property name that isn't
 * part of the collection schema fails with a Blueprint runtime warning.
 *
 * NewValue (input) and Readback (output) are wildcards locked to the same concrete type:
 * connecting either to a typed pin retypes both. Users can also pick the type explicitly
 * from either pin's right-click menu. Impure -- has exec pins.
 *
 * Compiles down to a CustomThunk call to UPCGExCollectionEntryBlueprintLibrary::
 * TrySetEntryPropertyOverride with the resolved type stamped onto both wildcards.
 */
UCLASS(meta=(DisplayName="Set Entry Property", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline/set-entry-property"))
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_SetPCGExEntryProperty : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// UK2Node
	virtual bool IsNodePure() const override
	{
		return false;
	}

	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

private:
	/**
	 * Persisted resolved pin type for both NewValue and Readback (which always share the
	 * same concrete type). AllocateDefaultPins re-stamps this on graph reload so manually
	 * picked types survive save/reopen even when no connections exist on either pin.
	 */
	UPROPERTY()
	FEdGraphPinType ResolvedPinType;

	UEdGraphPin* GetExecPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetCollectionPin() const;
	UEdGraphPin* GetEntryIndexPin() const;
	UEdGraphPin* GetPropertyNamePin() const;
	UEdGraphPin* GetNewValuePin() const;
	UEdGraphPin* GetReadbackPin() const;
	UEdGraphPin* GetSuccessPin() const;

	void ResetWildcardPinsToWildcard();
	void AdoptTypeFromOther(const UEdGraphPin* OtherPin);
	void SetWildcardPinsType(const FEdGraphPinType& InType);

	static const FName CollectionPinName;
	static const FName EntryIndexPinName;
	static const FName PropertyNamePinName;
	static const FName NewValuePinName;
	static const FName ReadbackPinName;
	static const FName SuccessPinName;
};
