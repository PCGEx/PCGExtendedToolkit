// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExBooleanCompareFilter.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExBooleanCompareFilterConfig, OperandB, bool, CompareAgainst, OperandB, OperandBConstant)

bool UPCGExBooleanCompareFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) &&
		(Config.CompareAgainst == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandB));
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExBooleanCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FBooleanCompareFilter>(this);
}

void UPCGExBooleanCompareFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<bool>(InContext, Config.OperandA);
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<bool>(InContext, Config.OperandB);
	}
}

bool UPCGExBooleanCompareFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)

	return true;
}

bool PCGExPointFilter::FBooleanCompareFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}
	
	OperandA = PointDataFacade->GetBroadcaster<bool>(TypedFilterFactory->Config.OperandA, true, false, PCGEX_QUIET_HANDLING);

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

bool PCGExPointFilter::FBooleanCompareFilter::Test(const int32 PointIndex) const
{
	const bool A = OperandA->Read(PointIndex);
	const bool B = OperandB->Read(PointIndex);
	return TypedFilterFactory->Config.Comparison == EPCGExEquality::Equal ? A == B : A != B;
}

bool PCGExPointFilter::FBooleanCompareFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	bool A = false;
	bool B = false;

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	if (!PCGExData::Helpers::TryGetSettingDataValue(IO, TypedFilterFactory->Config.CompareAgainst, TypedFilterFactory->Config.OperandB, TypedFilterFactory->Config.OperandBConstant, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return TypedFilterFactory->Config.Comparison == EPCGExEquality::Equal ? A == B : A != B;
}

PCGEX_CREATE_FILTER_FACTORY(BooleanCompare)

#if WITH_EDITOR
FString UPCGExBooleanCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + (Config.Comparison == EPCGExEquality::Equal ? TEXT(" == ") : TEXT(" != "));

	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%s"), Config.OperandBConstant ? TEXT("true") : TEXT("false"));
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
