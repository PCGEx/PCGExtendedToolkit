// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExBooleanCompareFilter.h"

#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

#if WITH_EDITOR
void FPCGExBooleanCompareFilterConfig::ApplyDeprecation()
{
	OperandBValue.Update(CompareAgainst_DEPRECATED, OperandB_DEPRECATED, OperandBConstant_DEPRECATED);
}

void FPCGExBooleanCompareFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandB")), FName(TEXT("OperandBValue")), FName(TEXT("Attribute")), FName(TEXT("Operand B (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandBConstant")), FName(TEXT("OperandBValue")), FName(TEXT("Constant")), FName(TEXT("Operand B")));
}
#endif

bool UPCGExBooleanCompareFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) && Config.OperandBValue.CanSupportDataOnly();
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExBooleanCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FBooleanCompareFilter>(this);
}

void UPCGExBooleanCompareFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<bool>(InContext, Config.OperandA);
	Config.OperandBValue.RegisterBufferDependencies(InContext, FacadePreloader);
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

	OperandB = TypedFilterFactory->Config.OperandBValue.GetValueSetting(PCGEX_QUIET_HANDLING);
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
	if (!TypedFilterFactory->Config.OperandBValue.TryReadDataValue(IO, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return TypedFilterFactory->Config.Comparison == EPCGExEquality::Equal ? A == B : A != B;
}

PCGEX_CREATE_FILTER_FACTORY(BooleanCompare)

#if WITH_EDITOR
void UPCGExBooleanCompareFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExBooleanCompareFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExBooleanCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + (Config.Comparison == EPCGExEquality::Equal ? TEXT(" == ") : TEXT(" != "));

	if (Config.OperandBValue.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandBValue.Attribute);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%s"), Config.OperandBValue.Constant ? TEXT("true") : TEXT("false"));
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
