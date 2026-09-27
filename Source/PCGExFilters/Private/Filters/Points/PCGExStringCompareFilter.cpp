// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExStringCompareFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"


#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

#if WITH_EDITOR
void FPCGExStringCompareFilterConfig::ApplyDeprecation()
{
	// A legacy FName tag loads through SetAttributeName; re-parse it with Update, as FName-based reads do.
	if (OperandA.GetSelection() == EPCGAttributePropertySelection::Attribute) { OperandA.Update(OperandA.GetAttributeName().ToString()); }
	OperandBValue.Update(CompareAgainst_DEPRECATED, OperandB_DEPRECATED, OperandBConstant_DEPRECATED);
}

void FPCGExStringCompareFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandB")), FName(TEXT("OperandBValue")), FName(TEXT("Attribute")), FName(TEXT("Operand B (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandBConstant")), FName(TEXT("OperandBValue")), FName(TEXT("Constant")), FName(TEXT("Operand B")));
}
#endif

bool UPCGExStringCompareFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) && Config.OperandBValue.CanSupportDataOnly();
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

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)

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
			PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
			return false;
		}

		// Direct MakeSettingValue bypasses the shorthand getter: apply both the per-operand and factory gates.
		const FPCGExInputShorthandSelectorString& OperandBValue = TypedFilterFactory->Config.OperandBValue;
		OperandBName = PCGExDetails::MakeSettingValue<FName>(OperandBValue.Input, OperandBValue.Attribute, FName(OperandBValue.Constant));
		OperandBName->bRegisterConsumable = OperandBValue.bCleanupAttribute && TypedFilterFactory->bCleanupConsumableAttributes;
		OperandBName->bQuiet = PCGEX_QUIET_HANDLING;
		return OperandBName->Init(PointDataFacade, false);
	}

	OperandA = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();
	if (!OperandA->Prepare(TypedFilterFactory->Config.OperandA, PointDataFacade->Source))
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	OperandB = TypedFilterFactory->Config.OperandBValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	OperandB->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	return OperandB->Init(PointDataFacade, false);
}

bool PCGExPointFilter::FStringCompareFilter::Test(const int32 PointIndex) const
{
	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);

	if (bUseNameComparison)
	{
		const FName A = OperandAName->FetchSingle(Point, NAME_None);
		const FName B = OperandBName->Read(PointIndex);
		// Equality is symmetric, so bSwapOperands is a no-op here.
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
	}

	const FString A = OperandA->FetchSingle(Point, TEXT(""));
	const FString B = OperandB->Read(PointIndex);
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

	if (!TypedFilterFactory->Config.OperandBValue.TryReadDataValue(IO, B, PCGEX_QUIET_HANDLING))
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

#if WITH_EDITOR
TArray<FText> UPCGExStringCompareFilterProviderSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Filter : by Attribute (string/name)")};
}
#endif

PCGEX_CREATE_FILTER_FACTORY(StringCompare)

#if WITH_EDITOR
void UPCGExStringCompareFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExStringCompareFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExStringCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA);
	DisplayName += PCGExCompare::ToString(Config.Comparison);
	DisplayName += Config.OperandBValue.Input == EPCGExInputValueType::Constant ? Config.OperandBValue.Constant : PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandBValue.Attribute);
	return DisplayName;
}
#endif


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
