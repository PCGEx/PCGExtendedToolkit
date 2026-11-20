// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/K2Node_PCGExPropertyAccessBase.h"

#include "K2Node_PCGExPropertyBase.generated.h"

/**
 * The custom-property access nodes over a UPCGExAssetCollection: Get/Set Entry|Category|Collection Property.
 * Leaves declare only scope and direction. Backed by UPCGExCollectionEntryBlueprintLibrary.
 *
 * Scope picks the key pin and the resolution tier: Entry (EntryIndex; override -> category -> default),
 * Category (Category name; row -> default, row minted on write), Collection (no key; schema default, or
 * the import override standing in for an imported property).
 */
UCLASS(Abstract)
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_PCGExPropertyBase : public UK2Node_PCGExPropertyAccessBase
{
	GENERATED_BODY()

public:
	enum class EScope : uint8
	{
		Entry,
		Category,
		Collection
	};

	// UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UK2Node
	virtual FText GetMenuCategory() const override;

protected:
	/** Which resolution tier the node addresses; decides the key pin and the backing functions. */
	virtual EScope GetScope() const PURE_VIRTUAL(UK2Node_PCGExPropertyBase::GetScope, return EScope::Entry;);

	// UK2Node_PCGExPropertyAccessBase
	virtual FName GetTargetPinName() const override;
	virtual UClass* GetTargetPinClass() const override;
	virtual FName GetKeyPinName() const override;
	virtual void CreateKeyPin() override;
	virtual UClass* GetLibraryClass() const override;
	virtual FName GetLibraryFunctionName(bool bSet, EFlavor Flavor) const override;
};
