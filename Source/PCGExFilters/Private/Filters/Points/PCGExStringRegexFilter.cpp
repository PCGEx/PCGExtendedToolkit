// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExStringRegexFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExStringRegexFilterDefinition"
#define PCGEX_NAMESPACE StringRegexFilterDefinition

#if WITH_EDITOR
void FPCGExStringRegexFilterConfig::ApplyDeprecation()
{
	// A legacy FName tag loads through SetAttributeName; re-parse it with Update, as FName-based reads do.
	if (OperandA.GetSelection() == EPCGAttributePropertySelection::Attribute) { OperandA.Update(OperandA.GetAttributeName().ToString()); }
}
#endif

bool UPCGExStringRegexFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA);
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExStringRegexFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FStringRegexFilter>(this);
}

bool UPCGExStringRegexFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)

	return true;
}

bool PCGExPointFilter::FStringRegexFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	// Compile the regex up front so the kept collection filter has a valid pattern even when the
	// OperandA prep below fails against the seed facade (see IFilter::Test(IO, ParentCollection)
	// contract). UE's FRegexPattern can't report invalid syntax, so there is nothing to check here
	// -- a malformed pattern simply matches nothing at test time.
	RegexMatcher.Compile(TypedFilterFactory->Config.RegexPattern);

	OperandA = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();
	if (!OperandA->Prepare(TypedFilterFactory->Config.OperandA, PointDataFacade->Source))
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	return true;
}

bool PCGExPointFilter::FStringRegexFilter::Test(const int32 PointIndex) const
{
	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);
	const FString A = OperandA->FetchSingle(Point, TEXT(""));
	const bool bResult = RegexMatcher.Test(A);
	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

bool PCGExPointFilter::FStringRegexFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	FString A = TEXT("");

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	const bool bResult = RegexMatcher.Test(A);
	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

PCGEX_CREATE_FILTER_FACTORY(StringRegex)

#if WITH_EDITOR
void UPCGExStringRegexFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExStringRegexFilterProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + TEXT(" =~ /") + Config.RegexPattern + TEXT("/"), Config.bInvert);
}
#endif


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
