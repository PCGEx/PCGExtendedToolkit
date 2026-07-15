// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExStringCompareNearestFilter.h"

#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "PCGExMatching/Public/Helpers/PCGExTargetsHandler.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExStringCompareNearestFilterFactory::Init(FPCGExContext* InContext)
{
	NearestConfig = &Config;

	// Equality comparisons read operands as FName (cheaper; see IsStringEqualityComparison for caveats).
	bUseNameComparison = PCGExCompare::IsStringEqualityComparison(Config.Comparison);

	return Super::Init(InContext);
}

void UPCGExStringCompareNearestFilterFactory::RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	if (bUseNameComparison) { FacadePreloader.Register<FName>(InContext, Config.OperandA); }
	else { FacadePreloader.Register<FString>(InContext, Config.OperandA); }
}

bool UPCGExStringCompareNearestFilterFactory::BuildTargetCaches(FPCGExContext* InContext)
{
	if (bUseNameComparison)
	{
		OperandAName = MakeShared<TArray<TSharedPtr<PCGExData::TBuffer<FName>>>>();
		OperandAName->Reserve(TargetsHandler->Num());
	}
	else
	{
		OperandAString = MakeShared<TArray<TSharedPtr<PCGExData::TBuffer<FString>>>>();
		OperandAString->Reserve(TargetsHandler->Num());
	}

	const bool bError = TargetsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& Target, const int32 TargetIndex, bool& bBreak)
	{
		// Always add (even on failure) to keep index alignment with TargetPt.IO.
		if (bUseNameComparison)
		{
			TSharedPtr<PCGExData::TBuffer<FName>> LocalOperandA = Target->GetBroadcaster<FName>(Config.OperandA, true);
			OperandAName->Add(LocalOperandA);
			if (!LocalOperandA)
			{
				bBreak = true;
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Operand A, Config.OperandA)
			}
		}
		else
		{
			TSharedPtr<PCGExData::TBuffer<FString>> LocalOperandA = Target->GetBroadcaster<FString>(Config.OperandA, true);
			OperandAString->Add(LocalOperandA);
			if (!LocalOperandA)
			{
				bBreak = true;
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Operand A, Config.OperandA)
			}
		}
	});

	return !bError;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExStringCompareNearestFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FStringCompareNearestFilter>(this);
}

void UPCGExStringCompareNearestFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	// Operand B is read on the source (Operand A is a target read, see RegisterTargetDependencies).
	Config.OperandBValue.RegisterBufferDependencies(InContext, FacadePreloader);
}

bool PCGExPointFilter::FStringCompareNearestFilter::InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (bUseNameComparison)
	{
		// Build the source-side operand B as FName directly.
		OperandBName = PCGExDetails::MakeSettingValue<FName>(
			TypedFilterFactory->Config.OperandBValue.Input,
			TypedFilterFactory->Config.OperandBValue.Attribute,
			FName(TypedFilterFactory->Config.OperandBValue.Constant));
		OperandBName->bRegisterConsumable = TypedFilterFactory->Config.OperandBValue.bCleanupAttribute && TypedFilterFactory->bCleanupConsumableAttributes; // direct MakeSettingValue bypasses the shorthand getter: apply both the per-operand and factory gates
		OperandBName->bQuiet = PCGEX_QUIET_HANDLING;
		if (!OperandBName->Init(PointDataFacade, false))
		{
			return false;
		}
	}
	else
	{
		OperandBString = TypedFilterFactory->Config.OperandBValue.GetValueSetting(PCGEX_QUIET_HANDLING);
		OperandBString->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!OperandBString->Init(PointDataFacade, false))
		{
			return false;
		}
	}

	return true;
}

bool PCGExPointFilter::FStringCompareNearestFilter::Test(const int32 PointIndex) const
{
	TSet<const UPCGData*> Scratch;
	bool bShortCircuit = false;
	bool bShortCircuitResult = false;
	const TSet<const UPCGData*>* ExcludePtr = ResolveExclude(PointIndex, Scratch, bShortCircuit, bShortCircuitResult);
	if (bShortCircuit)
	{
		return bShortCircuitResult;
	}

	const PCGExData::FConstPoint SourcePt = PointDataFacade->GetInPoint(PointIndex);
	const PCGExData::FConstPoint TargetPt = FindNearestInRange(SourcePt, GetMaxDistance(PointIndex), ExcludePtr);
	if (!TargetPt.IsValid())
	{
		return false;
	}

	// OperandA: closest target's cached buffer via TargetPt.IO; OperandB: the source point.
	if (bUseNameComparison)
	{
		const FName A = (OperandAName->GetData() + TargetPt.IO)->Get()->Read(TargetPt.Index);
		const FName B = OperandBName->Read(PointIndex);
		// Equality is symmetric, so bSwapOperands is a no-op here.
		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
	}

	const FString A = (OperandAString->GetData() + TargetPt.IO)->Get()->Read(TargetPt.Index);
	const FString B = OperandBString->Read(PointIndex);
	return TypedFilterFactory->Config.bSwapOperands
		       ? PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, B, A)
		       : PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, A, B);
}

TArray<FPCGPinProperties> UPCGExStringCompareNearestFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Target points to read operand A from"), Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(StringCompareNearest)

#if WITH_EDITOR
void UPCGExStringCompareNearestFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExStringCompareNearestFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExStringCompareNearestFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + PCGExCompare::ToString(Config.Comparison);

	if (Config.OperandBValue.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandBValue.Attribute);
	}
	else
	{
		DisplayName += Config.OperandBValue.Constant;
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
