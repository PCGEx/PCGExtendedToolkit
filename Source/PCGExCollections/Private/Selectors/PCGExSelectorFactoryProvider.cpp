// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorFactoryProvider.h"

#include "Selectors/PCGExBuiltinPickerOperations.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExMicroEntryPickerOperation.h"

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoSelector, UPCGExSelectorFactoryData)

#pragma region UPCGExSelectorFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	// Abstract -- concrete subclasses (Index / Random / WeightedRandom / user) override.
	return nullptr;
}

TSharedPtr<FPCGExMicroEntryPickerOperation> UPCGExSelectorFactoryData::CreateMicroOperation(FPCGExContext* InContext) const
{
	// Default dispatch on BaseConfig.EntrySelector.Distribution -- concrete main-mode
	// factories rarely need to override this; it's shared across all built-in modes and
	// user-authored factories that configure micro via the standard BaseConfig path.
	switch (BaseConfig.SubDistribution.Distribution)
	{
	case EPCGExDistribution::Index:
	{
		TSharedPtr<FPCGExMicroIndexPickerOp> Op = MakeShared<FPCGExMicroIndexPickerOp>();
		Op->IndexConfig = BaseConfig.SubDistribution.IndexSettings;
		return Op;
	}
	case EPCGExDistribution::Random:
		return MakeShared<FPCGExMicroRandomPickerOp>();
	case EPCGExDistribution::WeightedRandom:
	default:
		return MakeShared<FPCGExMicroWeightedRandomPickerOp>();
	}
}

#pragma endregion

#pragma region UPCGExSelectorFactoryProviderSettings

UPCGExFactoryData* UPCGExSelectorFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	InFactory->Priority = Priority;
	return Super::CreateFactory(InContext, InFactory);
}

#pragma endregion
