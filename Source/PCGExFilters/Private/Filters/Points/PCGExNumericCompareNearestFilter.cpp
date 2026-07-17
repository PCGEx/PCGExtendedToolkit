// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExNumericCompareNearestFilter.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "PCGExMatching/Public/Helpers/PCGExTargetsHandler.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExNumericCompareNearestFilterFactory::Init(FPCGExContext* InContext)
{
	NearestConfig = &Config;
	return Super::Init(InContext);
}

void UPCGExNumericCompareNearestFilterFactory::RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	FacadePreloader.Register<double>(InContext, Config.OperandA);
}

bool UPCGExNumericCompareNearestFilterFactory::BuildTargetCaches(FPCGExContext* InContext)
{
	OperandA = MakeShared<TArray<TSharedPtr<PCGExData::TBuffer<double>>>>();
	OperandA->Reserve(TargetsHandler->Num());

	const bool bError = TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& Target, const int32 TargetIndex, bool& bBreak)
	{
		// Always add (even on failure) to keep index alignment with TargetPt.IO.
		TSharedPtr<PCGExData::TBuffer<double>> LocalOperandA = Target->GetBroadcaster<double>(Config.OperandA, true);
		OperandA->Add(LocalOperandA);
		if (!LocalOperandA)
		{
			bBreak = true;
			PCGEX_LOG_INVALID_SELECTOR_C(InContext, Operand A, Config.OperandA)
		}
	});

	return !bError;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExNumericCompareNearestFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FNumericCompareNearestFilter>(this);
}

void UPCGExNumericCompareNearestFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	Config.OperandBValue.RegisterBufferDependencies(InContext, FacadePreloader);
}

#if WITH_EDITOR
void UPCGExNumericCompareNearestFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 20)
	{
		// Rewire the old Operand B override pins onto the new shorthand pins (ApplyDeprecation only migrates the inline value).
		PCGEX_SHORTHAND_RENAME_PIN(OperandB, OperandBConstant, OperandBValue)
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExNumericCompareNearestFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 75, 20)
	{
		Config.OperandBValue.Update(Config.CompareAgainst_DEPRECATED, Config.OperandB_DEPRECATED, Config.OperandBConstant_DEPRECATED);
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool PCGExPointFilter::FNumericCompareNearestFilter::InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	OperandB = TypedFilterFactory->Config.OperandBValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	OperandB->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandB->Init(PointDataFacade, false))
	{
		return false;
	}

	return true;
}

bool PCGExPointFilter::FNumericCompareNearestFilter::Test(const int32 PointIndex) const
{
	TSet<const UPCGData*> Scratch;
	bool bShortCircuit = false;
	bool bShortCircuitResult = false;
	const TSet<const UPCGData*>* ExcludePtr = ResolveExclude(PointIndex, Scratch, bShortCircuit, bShortCircuitResult);
	if (bShortCircuit)
	{
		return bShortCircuitResult;
	}

	const double B = OperandB->Read(PointIndex);
	const PCGExData::FConstPoint SourcePt = PointDataFacade->GetInPoint(PointIndex);

	const PCGExData::FConstPoint TargetPt = FindNearestInRange(SourcePt, GetMaxDistance(PointIndex), ExcludePtr);
	if (!TargetPt.IsValid())
	{
		return false;
	}

	// OperandA: closest target's cached buffer via TargetPt.IO.
	const PCGExData::TBuffer<double>* Buffer = (OperandA->GetData() + TargetPt.IO)->Get();
	if (!Buffer)
	{
		return bNoMatchResult;
	}

	return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, Buffer->Read(TargetPt.Index), B, TypedFilterFactory->Config.Tolerance);
}

TArray<FPCGPinProperties> UPCGExNumericCompareNearestFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Target points to read operand B from"), Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(NumericCompareNearest)

#if WITH_EDITOR
FString UPCGExNumericCompareNearestFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + PCGExCompare::ToString(Config.Comparison);

	if (Config.OperandBValue.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandBValue.Attribute);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.OperandBValue.Constant) / 1000.0));
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
