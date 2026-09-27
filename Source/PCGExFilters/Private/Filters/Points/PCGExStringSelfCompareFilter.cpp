// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExStringSelfCompareFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

#if WITH_EDITOR
void FPCGExStringSelfCompareFilterConfig::ApplyDeprecation()
{
	// A legacy FName tag loads through SetAttributeName; re-parse it with Update, as FName-based reads do.
	if (OperandA.GetSelection() == EPCGAttributePropertySelection::Attribute) { OperandA.Update(OperandA.GetAttributeName().ToString()); }
	Index.Update(CompareAgainst_DEPRECATED, IndexAttribute_DEPRECATED, IndexConstant_DEPRECATED);
}

void FPCGExStringSelfCompareFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("IndexAttribute")), FName(TEXT("Index")), FName(TEXT("Attribute")), FName(TEXT("Index (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("IndexConstant")), FName(TEXT("Index")), FName(TEXT("Constant")), FName(TEXT("Index")));
}
#endif

TSharedPtr<PCGExPointFilter::IFilter> UPCGExStringSelfCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FStringSelfCompareFilter>(this);
}

void UPCGExStringSelfCompareFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.Index.RegisterBufferDependencies(InContext, FacadePreloader);
}

bool UPCGExStringSelfCompareFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)

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
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	Index = TypedFilterFactory->Config.Index.GetValueSetting(PCGEX_QUIET_HANDLING);
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
		return TypedFilterFactory->Config.InvalidIndexFallback == EPCGExFilterFallback::Pass;
	}

	const FString A = OperandA->FetchSingle(PointDataFacade->Source->GetInPoint(PointIndex), TEXT(""));
	const FString B = OperandA->FetchSingle(PointDataFacade->Source->GetInPoint(TargetIndex), TEXT(""));
	return TypedFilterFactory->Config.bSwapOperands ? PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, B, A) : PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
}

PCGEX_CREATE_FILTER_FACTORY(StringSelfCompare)

#if WITH_EDITOR
void UPCGExStringSelfCompareFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExStringSelfCompareFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExStringSelfCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + PCGExCompare::ToString(Config.Comparison);

	if (Config.IndexMode == EPCGExIndexMode::Pick)
	{
		DisplayName += TEXT(" @ ");
	}
	else
	{
		DisplayName += TEXT(" i+ ");
	}

	if (Config.Index.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.Index.Attribute);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.Index.Constant);
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
