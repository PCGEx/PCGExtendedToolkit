// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "PCGExSelectorFactoryBaseConfig.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExSelectorFactoryProvider.generated.h"

namespace PCGExAssetCollection
{
	class FCategory;
}

class FPCGExEntryPickerOperation;
class FPCGExMicroEntryPickerOperation;

namespace PCGExCollections
{
	class FSelectorSharedData;
}

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Selector"))
struct FPCGExDataTypeInfoSelector : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXCOLLECTIONS_API)
};

/**
 * Abstract factory data for collection distribution. Flows on the "Selector" pin from
 * palette factory nodes to consuming nodes (Staging Distribute, Spline Mesh, ...).
 *
 * Concrete subclasses override CreateEntryOperation to emit the hot-path picker matching
 * their distribution mode (Index, Random, WeightedRandom, or user-authored).
 * CreateMicroOperation dispatches on BaseConfig.EntryDistribution and is typically not
 * overridden by concrete main-mode factories.
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXCOLLECTIONS_API UPCGExSelectorFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoSelector)

	UPROPERTY()
	FPCGExSelectorFactoryBaseConfig BaseConfig;

	/** Create a hot-path entry picker operation. Concrete subclasses override. */
	virtual TSharedPtr<FPCGExEntryPickerOperation> CreateEntryOperation(FPCGExContext* InContext) const;

	/** Create a hot-path micro picker operation based on BaseConfig.EntryDistribution. */
	virtual TSharedPtr<FPCGExMicroEntryPickerOperation> CreateMicroOperation(FPCGExContext* InContext) const;

	/**
	 * Produce collection-derived shared data for a given (Collection, Category). Invoked by
	 * FSelectorSharedDataCache on cache miss. Selectors that benefit from reusing collection-derived
	 * state across facades override this. Default returns nullptr (no caching, ops self-build).
	 */
	virtual TSharedPtr<PCGExCollections::FSelectorSharedData> BuildSharedData(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target) const
	{
		return nullptr;
	}
};

/**
 * Abstract palette node base for distribution factories. Concrete subclasses
 * (Index / Random / WeightedRandom, plus any user-authored mode) inherit this
 * and fill in CreateFactory to emit their matching UPCGExSelectorFactoryData.
 *
 * Output pin label: "Distribution" (see PCGExCollections::Labels::OutputDistributionLabel).
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class PCGEXCOLLECTIONS_API UPCGExSelectorFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoSelector)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SelectorFactory, "Selector Definition", "Creates a selector factory definition.")

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Selector);
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override
	{
		return PCGExCollections::Labels::OutputSelectorLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
