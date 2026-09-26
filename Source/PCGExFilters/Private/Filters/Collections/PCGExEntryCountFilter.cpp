// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Collections/PCGExEntryCountFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

#if WITH_EDITOR
void FPCGExEntryCountFilterConfig::ApplyDeprecation()
{
	OperandBValue.Update(CompareAgainst_DEPRECATED, OperandBAttr_DEPRECATED, OperandB_DEPRECATED);
}

void FPCGExEntryCountFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandBAttr")), FName(TEXT("OperandBValue")), FName(TEXT("Attribute")), FName(TEXT("Operand B (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandB")), FName(TEXT("OperandBValue")), FName(TEXT("Constant")), FName(TEXT("Operand B")));
}
#endif

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEntryCountFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FEntryCountFilter>(this);
}

bool PCGExPointFilter::FEntryCountFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	int32 B = 0;
	if (!TypedFilterFactory->Config.OperandBValue.TryReadDataValue(IO->GetContext(), IO->GetIn(), B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, IO->GetNum(), B, TypedFilterFactory->Config.Tolerance);
}

PCGEX_CREATE_FILTER_FACTORY(EntryCount)

#if WITH_EDITOR
void UPCGExEntryCountFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExEntryCountFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExEntryCountFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Entry Count ") + PCGExCompare::ToString(Config.Comparison);
	if (Config.OperandBValue.Input == EPCGExInputValueType::Constant)
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.OperandBValue.Constant);
	}
	else
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandBValue.Attribute);
	}
	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
