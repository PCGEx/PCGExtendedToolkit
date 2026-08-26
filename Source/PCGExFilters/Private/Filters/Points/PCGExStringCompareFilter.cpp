// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExStringCompareFilter.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExStringCompareFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) && (Config.CompareAgainst == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandB));
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExStringCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FStringCompareFilter>(this);
}

bool UPCGExStringCompareFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	InContext->AddConsumableAttributeName(Config.OperandA);
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		InContext->AddConsumableAttributeName(Config.OperandB);
	}

	return true;
}

bool PCGExPointFilter::FStringCompareFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	// Equality comparisons read operands as FName (cheaper; see IsStringEqualityComparison for caveats).
	bUseNameComparison = PCGExCompare::IsStringEqualityComparison(TypedFilterFactory->Config.Comparison);

	if (bUseNameComparison)
	{
		OperandAName = MakeShared<PCGExData::TAttributeBroadcaster<FName>>();
		if (!OperandAName->Prepare(TypedFilterFactory->Config.OperandA, PointDataFacade->Source))
		{
			PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
			return false;
		}

		if (TypedFilterFactory->Config.CompareAgainst == EPCGExInputValueType::Attribute)
		{
			OperandBName = MakeShared<PCGExData::TAttributeBroadcaster<FName>>();
			if (!OperandBName->Prepare(TypedFilterFactory->Config.OperandB, PointDataFacade->Source))
			{
				PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Operand B, TypedFilterFactory->Config.OperandB)
				return false;
			}
		}
		else
		{
			OperandBConstantName = FName(TypedFilterFactory->Config.OperandBConstant);
		}

		return true;
	}

	OperandA = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();
	if (!OperandA->Prepare(TypedFilterFactory->Config.OperandA, PointDataFacade->Source))
	{
		PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	if (TypedFilterFactory->Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		OperandB = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();
		if (!OperandB->Prepare(TypedFilterFactory->Config.OperandB, PointDataFacade->Source))
		{
			PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Operand B, TypedFilterFactory->Config.OperandB)
			return false;
		}
	}

	return true;
}

bool PCGExPointFilter::FStringCompareFilter::Test(const int32 PointIndex) const
{
	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);

	if (bUseNameComparison)
	{
		const FName A = OperandAName->FetchSingle(Point, NAME_None);
		const FName B = TypedFilterFactory->Config.CompareAgainst == EPCGExInputValueType::Attribute ? OperandBName->FetchSingle(Point, NAME_None) : OperandBConstantName;
		// Equality is symmetric, so bSwapOperands is a no-op here.
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
	}

	const FString A = OperandA->FetchSingle(Point, TEXT(""));
	const FString B = TypedFilterFactory->Config.CompareAgainst == EPCGExInputValueType::Attribute ? OperandB->FetchSingle(Point, TEXT("")) : TypedFilterFactory->Config.OperandBConstant;
	return TypedFilterFactory->Config.bSwapOperands ? PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, B, A) : PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
}

bool PCGExPointFilter::FStringCompareFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	FString A = TEXT("");
	FString B = TEXT("");

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	if (!PCGExData::Helpers::TryGetSettingDataValue(IO, TypedFilterFactory->Config.CompareAgainst, TypedFilterFactory->Config.OperandB, TypedFilterFactory->Config.OperandBConstant, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	// Mirror the per-point path: equality uses FName so both domains agree (see Test(int32)).
	if (PCGExCompare::IsStringEqualityComparison(TypedFilterFactory->Config.Comparison))
	{
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, FName(A), FName(B));
	}

	return TypedFilterFactory->Config.bSwapOperands ? PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, B, A) : PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
}

PCGEX_CREATE_FILTER_FACTORY(StringCompare)

#if WITH_EDITOR
FString UPCGExStringCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = Config.OperandA.ToString();
	DisplayName += PCGExCompare::ToString(Config.Comparison);
	DisplayName += Config.CompareAgainst == EPCGExInputValueType::Constant ? Config.OperandBConstant : Config.OperandB.ToString();
	return DisplayName;
}
#endif


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
