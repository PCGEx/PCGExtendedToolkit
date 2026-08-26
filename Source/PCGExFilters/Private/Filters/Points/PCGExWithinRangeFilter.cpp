// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExWithinRangeFilter.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Pickers/PCGExPickerAttributeSetRanges.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExWithinRangeFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA);
}

bool UPCGExWithinRangeFilterFactory::Init(FPCGExContext* InContext)
{
	if (Config.Source != EPCGExRangeSource::AttributeSet)
	{
		FPCGExPickerConstantRangeConfig& Range = Ranges.Emplace_GetRef();
		Range.RelativeStartIndex = Config.RangeMin;
		Range.RelativeEndIndex = Config.RangeMax;
		Range.Sanitize();
	}

	return Super::Init(InContext);
}

bool UPCGExWithinRangeFilterFactory::WantsPreparation(FPCGExContext* InContext)
{
	return Config.Source == EPCGExRangeSource::AttributeSet;
}

// Ranges resolve during preparation so an empty pin routes through MissingDataPolicy instead of erroring out.
PCGExFactories::EPreparationResult UPCGExWithinRangeFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	FPCGExPickerAttributeSetRangesConfig DummyConfig;
	DummyConfig.Attributes = Config.Attributes;
	if (!UPCGExPickerAttributeSetRangesFactory::GetUniqueRanges(InContext, FName("Ranges"), DummyConfig, Ranges, MissingDataPolicy != EPCGExFilterNoDataFallback::Error))
	{
		return PCGExFactories::EPreparationResult::MissingData;
	}

	return Result;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExWithinRangeFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FWithinRangeFilter>(this);
}

TArray<FPCGPinProperties> UPCGExWithinRangeFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	if (Config.Source == EPCGExRangeSource::AttributeSet)
	{
		PCGEX_PIN_ANY(FName("Ranges"), "Data to read attribute ranges from", Required)
	}
	else
	{
		PCGEX_PIN_ANY(FName("Ranges"), "Data to read attribute ranges from", Advanced)
	}

	return PinProperties;
}

bool PCGExPointFilter::FWithinRangeFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	// Cache config flags up front so they survive a per-point Init failure: the collection-level
	// Test(IO,...) path reads them and may run even when the broadcaster setup below fails against
	// the seed facade (see IFilter::Test(IO, ParentCollection) contract).
	bInclusive = TypedFilterFactory->Config.bInclusive;
	bInvert = TypedFilterFactory->Config.bInvert;

	OperandA = PointDataFacade->GetBroadcaster<double>(TypedFilterFactory->Config.OperandA, true, false, PCGEX_QUIET_HANDLING);

	if (!OperandA)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	return true;
}

bool PCGExPointFilter::FWithinRangeFilter::Test(const int32 PointIndex) const
{
	const double A = OperandA->Read(PointIndex);

	if (bInclusive)
	{
		for (const FPCGExPickerConstantRangeConfig& Range : Ranges)
		{
			if (Range.IsWithinInclusive(A))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}
	for (const FPCGExPickerConstantRangeConfig& Range : Ranges)
	{
		if (Range.IsWithin(A))
		{
			return !bInvert;
		}
	}
	return bInvert;
}

bool PCGExPointFilter::FWithinRangeFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	double A = 0;
	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	if (bInclusive)
	{
		for (const FPCGExPickerConstantRangeConfig& Range : Ranges)
		{
			if (Range.IsWithinInclusive(A))
			{
				return !bInvert;
			}
		}
		return bInvert;
	}
	for (const FPCGExPickerConstantRangeConfig& Range : Ranges)
	{
		if (Range.IsWithin(A))
		{
			return !bInvert;
		}
	}
	return bInvert;
}

bool UPCGExWithinRangeFilterProviderSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == FName("Ranges"))
	{
		return Config.Source == EPCGExRangeSource::AttributeSet;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGEX_CREATE_FILTER_FACTORY(WithinRange)

#if WITH_EDITOR
FString UPCGExWithinRangeFilterProviderSettings::GetDisplayName() const
{
	if (Config.Source == EPCGExRangeSource::AttributeSet)
	{
		return GetDefaultNodeTitle().ToString();
	}

	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + TEXT("[");

	DisplayName += FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.RangeMin) / 1000.0));
	DisplayName += TEXT(" .. ");
	DisplayName += FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.RangeMax) / 1000.0));

	return PCGExCommon::FlagInvertLabel(DisplayName + TEXT("]"), Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
