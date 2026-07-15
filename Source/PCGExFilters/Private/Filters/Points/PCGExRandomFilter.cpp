// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExRandomFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExRandomHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExRandomFilterConfig, Weight, double, bPerPointWeight ? EPCGExInputValueType::Attribute : EPCGExInputValueType::Constant, Weight, 1)

#if WITH_EDITOR
void FPCGExRandomFilterConfig::ApplyDeprecation()
{
	ThresholdValue.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, Threshold_DEPRECATED);
}

void FPCGExRandomFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Threshold")), FName(TEXT("ThresholdValue")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("ThresholdValue")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}
#endif

bool UPCGExRandomFilterFactory::Init(FPCGExContext* InContext)
{
	Config.WeightLUT = Config.WeightCurveLookup.MakeLookup(Config.bUseLocalCurve, Config.LocalWeightCurve, Config.WeightCurve);
	return Super::Init(InContext);
}

bool UPCGExRandomFilterFactory::SupportsCollectionEvaluation() const
{
	return (!Config.bPerPointWeight && Config.ThresholdValue.Input == EPCGExInputValueType::Constant) || bOnlyUseDataDomain;
}

bool UPCGExRandomFilterFactory::SupportsProxyEvaluation() const
{
	return !Config.bPerPointWeight && Config.ThresholdValue.Input == EPCGExInputValueType::Constant;
}

void UPCGExRandomFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.bPerPointWeight && Config.bRemapWeightInternally)
	{
		FacadePreloader.Register<double>(InContext, Config.Weight);
	}
	if (Config.ThresholdValue.Input != EPCGExInputValueType::Constant && Config.bRemapThresholdInternally)
	{
		FacadePreloader.Register<double>(InContext, Config.ThresholdValue.Attribute);
	}
}

void UPCGExRandomFilterFactory::RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const
{
	Super::RegisterAssetDependencies(InDependencies);
	InDependencies.Add(Config.WeightCurve.ToSoftObjectPath());
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExRandomFilterFactory::CreateFilter() const
{
	PCGEX_MAKE_SHARED(Filter, PCGExPointFilter::FRandomFilter, this)
	Filter->WeightCurve = Config.WeightLUT;
	return Filter;
}

bool PCGExPointFilter::FRandomFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	Threshold = TypedFilterFactory->Config.ThresholdValue.Constant;

	// When remapping internally, track min/max to normalize weight values to [0..WeightRange].
	// If min is negative, WeightOffset shifts values so the effective range starts at zero.
	WeightBuffer = TypedFilterFactory->Config.GetValueSettingWeight(PCGEX_QUIET_HANDLING);
	WeightBuffer->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!WeightBuffer->IsConstant())
	{
		if (TypedFilterFactory->Config.bRemapWeightInternally)
		{
			if (!WeightBuffer->Init(PointDataFacade, false, true))
			{
				return false;
			}
			WeightRange = WeightBuffer->Max();

			if (WeightBuffer->Min() < 0)
			{
				WeightOffset = WeightBuffer->Min();
				WeightRange += WeightOffset;
			}
		}
		else
		{
			if (!WeightBuffer->Init(PointDataFacade))
			{
				return false;
			}
		}
	}

	ThresholdBuffer = TypedFilterFactory->Config.ThresholdValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	ThresholdBuffer->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!ThresholdBuffer->IsConstant())
	{
		if (TypedFilterFactory->Config.bRemapThresholdInternally)
		{
			if (!ThresholdBuffer->Init(PointDataFacade, false, true))
			{
				return false;
			}
			ThresholdRange = ThresholdBuffer->Max();

			if (ThresholdBuffer->Min() < 0)
			{
				ThresholdOffset = ThresholdBuffer->Min();
				ThresholdRange += ThresholdOffset;
			}
		}
		else
		{
			if (!ThresholdBuffer->Init(PointDataFacade))
			{
				return false;
			}
		}
	}

	Seeds = PointDataFacade->GetIn()->GetConstSeedValueRange();

	RandomSeedV = FVector(RandomSeed);

	return true;
}

// Normalize weight and threshold from their raw attribute ranges to [0..1],
// generate a seeded random fraction, scale by weight curve, then compare against threshold.
bool PCGExPointFilter::FRandomFilter::Test(const int32 PointIndex) const
{
	const double LocalWeightRange = WeightOffset + WeightBuffer->Read(PointIndex);
	const double LocalThreshold = ThresholdBuffer ? (ThresholdOffset + ThresholdBuffer->Read(PointIndex)) / ThresholdRange : Threshold;
	const float RandomValue = WeightCurve->Eval((FRandomStream(PCGExRandomHelpers::GetRandomStreamFromPoint(Seeds[PointIndex], RandomSeed)).GetFraction() * LocalWeightRange) / WeightRange);
	return TypedFilterFactory->Config.bInvertResult ? RandomValue <= LocalThreshold : RandomValue >= LocalThreshold;
}

bool PCGExPointFilter::FRandomFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	const float RandomValue = WeightCurve->Eval((FRandomStream(PCGExRandomHelpers::ComputeSpatialSeed(Point.GetLocation(), RandomSeedV)).GetFraction() * WeightRange) / WeightRange);
	return TypedFilterFactory->Config.bInvertResult ? RandomValue <= Threshold : RandomValue >= Threshold;
}

bool PCGExPointFilter::FRandomFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	const float RandomValue = WeightCurve->Eval((FRandomStream(PCGExRandomHelpers::GetRandomStreamFromPoint(IO->GetIn()->GetSeed(0), RandomSeed)).GetFraction() * WeightRange) / WeightRange);
	return TypedFilterFactory->Config.bInvertResult ? RandomValue <= Threshold : RandomValue >= Threshold;
}

PCGEX_CREATE_FILTER_FACTORY(Random)

#if WITH_EDITOR
void UPCGExRandomFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExRandomFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExRandomFilterProviderSettings::GetDisplayName() const
{
	return TEXT("Random");
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
