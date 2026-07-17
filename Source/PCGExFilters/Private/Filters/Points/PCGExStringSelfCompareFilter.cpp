// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExStringSelfCompareFilter.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExStringSelfCompareFilterConfig, Index, int32, CompareAgainst, IndexAttribute, IndexConstant)

TSharedPtr<PCGExPointFilter::IFilter> UPCGExStringSelfCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FStringSelfCompareFilter>(this);
}

void UPCGExStringSelfCompareFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
}

bool UPCGExStringSelfCompareFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	InContext->AddConsumableAttributeName(Config.OperandA);

	return true;
}

bool PCGExPointFilter::FStringSelfCompareFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	bOffset = TypedFilterFactory->Config.IndexMode == EPCGExIndexMode::Offset;
	MaxIndex = PointDataFacade->Source->GetNum() - 1;

	if (MaxIndex < 0)
	{
		return TypedFilterFactory->Config.InvalidIndexFallback == EPCGExFilterFallback::Pass;
	}

	OperandA = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();
	if (!OperandA->Prepare(TypedFilterFactory->Config.OperandA, PointDataFacade->Source))
	{
		PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	Index = TypedFilterFactory->Config.GetValueSettingIndex(PCGEX_QUIET_HANDLING);
	Index->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!Index->Init(PointDataFacade))
	{
		return false;
	}

	return true;
}

bool PCGExPointFilter::FStringSelfCompareFilter::Test(const int32 PointIndex) const
{
	const int32 IndexValue = Index->Read(PointIndex);
	const int32 TargetIndex = PCGExMath::SanitizeIndex(bOffset ? PointIndex + IndexValue : IndexValue, MaxIndex, TypedFilterFactory->Config.IndexSafety);

	if (TargetIndex == -1)
	{
		return false;
	}

	const FString A = OperandA->FetchSingle(PointDataFacade->Source->GetInPoint(PointIndex), TEXT(""));
	const FString B = OperandA->FetchSingle(PointDataFacade->Source->GetInPoint(TargetIndex), TEXT(""));
	return TypedFilterFactory->Config.bSwapOperands ? PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, B, A) : PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
}

PCGEX_CREATE_FILTER_FACTORY(StringSelfCompare)

#if WITH_EDITOR
FString UPCGExStringSelfCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = Config.OperandA.ToString() + PCGExCompare::ToString(Config.Comparison);

	if (Config.IndexMode == EPCGExIndexMode::Pick)
	{
		DisplayName += TEXT(" @ ");
	}
	else
	{
		DisplayName += TEXT(" i+ ");
	}

	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.IndexAttribute);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.IndexConstant);
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
