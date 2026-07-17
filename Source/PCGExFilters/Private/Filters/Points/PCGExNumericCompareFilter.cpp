// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExNumericCompareFilter.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExNumericCompareFilterConfig, OperandB, double, CompareAgainst, OperandB, OperandBConstant)

bool UPCGExNumericCompareFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) && (Config.CompareAgainst == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandB));
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNumericCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FNumericCompareFilter>(this);
}

void UPCGExNumericCompareFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<double>(InContext, Config.OperandA);
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Config.OperandB);
	}
}

bool UPCGExNumericCompareFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)

	return true;
}

bool PCGExPointFilter::FNumericCompareFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	OperandA = PointDataFacade->GetBroadcaster<double>(TypedFilterFactory->Config.OperandA, true, false, PCGEX_QUIET_HANDLING);

	if (!OperandA)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	OperandB = TypedFilterFactory->Config.GetValueSettingOperandB(PCGEX_QUIET_HANDLING);
	OperandB->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandB->Init(PointDataFacade))
	{
		return false;
	}

	return true;
}

bool PCGExPointFilter::FNumericCompareFilter::Test(const int32 PointIndex) const
{
	const double A = OperandA->Read(PointIndex);
	const double B = OperandB->Read(PointIndex);
	return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
}

bool PCGExPointFilter::FNumericCompareFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	double A = 0;
	double B = 0;

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	if (!PCGExData::Helpers::TryGetSettingDataValue(IO, TypedFilterFactory->Config.CompareAgainst, TypedFilterFactory->Config.OperandB, TypedFilterFactory->Config.OperandBConstant, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B, TypedFilterFactory->Config.Tolerance);
}

PCGEX_CREATE_FILTER_FACTORY(NumericCompare)

#if WITH_EDITOR
FString UPCGExNumericCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + PCGExCompare::ToString(Config.Comparison);

	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.OperandBConstant) / 1000.0));
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
