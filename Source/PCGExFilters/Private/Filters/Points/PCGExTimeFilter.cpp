// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExTimeFilter.h"


#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPolyPath.h"
#include "PCGExMatching/Public/Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExTimeFilterDefinition"
#define PCGEX_NAMESPACE PCGExTimeFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExTimeFilterConfig, OperandB, float, CompareAgainst, OperandB, OperandBConstant)

bool UPCGExTimeFilterFactory::SupportsCollectionEvaluation() const
{
	return Config.bCheckAgainstDataBounds;
}

bool UPCGExTimeFilterFactory::SupportsProxyEvaluation() const
{
	return Config.CompareAgainst == EPCGExInputValueType::Constant;
}

void UPCGExTimeFilterFactory::InitConfig_Internal()
{
	Super::InitConfig_Internal();
	LocalFidelity = Config.Fidelity;
	LocalExpansion = Config.Tolerance;
	LocalExpansionZ = -1;
	LocalSampleInputs = Config.SampleInputs;
	WindingMutation = Config.WindingMutation;
	bScaleTolerance = false;
	bUsedForInclusion = false;
	bIgnoreSelf = Config.bIgnoreSelf;
	DataMatching = Config.DataMatching;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExTimeFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FTimeFilter>(this);
}

void UPCGExTimeFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute) { FacadePreloader.Register<double>(InContext, Config.OperandB); }
}

bool UPCGExTimeFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData)) { return false; }

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_CONDITIONAL(Config.CompareAgainst == EPCGExInputValueType::Attribute, Config.OperandB, Consumable)

	return true;
}

FName UPCGExTimeFilterFactory::GetInputLabel() const { return PCGExCommon::Labels::SourceTargetsLabel; }

namespace PCGExPointFilter
{
	bool FTimeFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
	{
		if (!IFilter::Init(InContext, InPointDataFacade)) { return false; }

		const bool bMatchingEnabled = TypedFilterFactory->Config.DataMatching.IsEnabled()
			&& TypedFilterFactory->HasMatchRuleFactories();

		// See FDistanceFilter::Init for the full per-point vs static matching explanation.

		if (bMatchingEnabled && !TypedFilterFactory->Config.bCheckAgainstDataBounds)
		{
			InverseMatcher = MakeShared<PCGExMatching::FDataMatcher>();
			InverseMatcher->SetDetails(&TypedFilterFactory->Config.DataMatching);

			TArray<TSharedPtr<PCGExData::FFacade>> SingleSource;
			SingleSource.Add(InPointDataFacade);
			if (InverseMatcher->Init(TypedFilterFactory->GetMatchRuleFactories(), SingleSource, false))
			{
				bNoMatchResult = (TypedFilterFactory->Config.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);
			}
			else { InverseMatcher.Reset(); }
		}
		else
		{
			// Static matching or no matching
			if (!TypedFilterFactory->PopulateMatchIgnoreList(InContext, InPointDataFacade, Handler->MatchIgnoreList))
			{
				bCheckAgainstDataBounds = true;
				bCollectionTestResult = (TypedFilterFactory->Config.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);
				return true;
			}
		}

		OperandB = TypedFilterFactory->Config.GetValueSettingOperandB();
		if (!OperandB->Init(PointDataFacade)) { return false; }

		InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();
		return true;
	}

	bool FTimeFilter::Test(const PCGExData::FProxyPoint& Point) const
	{
		const FVector WorldPosition = Point.Transform.GetLocation();
		float Alpha = 0;

		if (TypedFilterFactory->Config.TimeConsolidation == EPCGExSplineTimeConsolidation::Min) { Alpha = MAX_flt; }

		const TSet<const UPCGData*>& MatchIgnore = Handler->MatchIgnoreList;

		if (TypedFilterFactory->Config.Pick == EPCGExSplineFilterPick::Closest)
		{
			double BestDist = MAX_dbl;

			TypedFilterFactory->Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), [&](const PCGExOctree::FItem& Item)
			{
				if (!MatchIgnore.IsEmpty() && MatchIgnore.Contains((*TypedFilterFactory->Datas)[Item.Index].Data)) { return; }

				float LocalAlpha = 0;
				const TSharedPtr<PCGExPaths::FPolyPath> Path = (*(TypedFilterFactory->PolyPaths.GetData() + Item.Index));
				const FTransform Closest = Path->GetClosestTransform(WorldPosition, LocalAlpha, false);

				const double D = FVector::DistSquared(Closest.GetLocation(), WorldPosition);

				if (D < BestDist)
				{
					Alpha = LocalAlpha;
					BestDist = D;
				}
			});
		}
		else
		{
			int32 MatchCount = 0;
			for (int32 i = 0; i < TypedFilterFactory->PolyPaths.Num(); i++)
			{
				if (!MatchIgnore.IsEmpty() && MatchIgnore.Contains((*TypedFilterFactory->Datas)[i].Data)) { continue; }

				float LocalAlpha = 0;
				(void)TypedFilterFactory->PolyPaths[i]->GetClosestTransform(WorldPosition, LocalAlpha, false);
				MatchCount++;

				switch (TypedFilterFactory->Config.TimeConsolidation)
				{
				case EPCGExSplineTimeConsolidation::Min: Alpha = FMath::Min(LocalAlpha, Alpha);
					break;
				case EPCGExSplineTimeConsolidation::Max: Alpha = FMath::Max(LocalAlpha, Alpha);
					break;
				case EPCGExSplineTimeConsolidation::Average: Alpha += LocalAlpha;
					break;
				}
			}

			if (TypedFilterFactory->Config.TimeConsolidation == EPCGExSplineTimeConsolidation::Average && MatchCount > 0)
			{
				Alpha /= MatchCount;
			}
		}

		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, Alpha, TypedFilterFactory->Config.OperandBConstant, TypedFilterFactory->Config.Tolerance);
	}

	// Compare the spline parametric time (alpha) at the closest projection of each point.
	// When multiple splines exist, TimeConsolidation controls how alphas are combined (min/max/avg).
	bool FTimeFilter::Test(const int32 PointIndex) const
	{
		const FVector WorldPosition = InTransforms[PointIndex].GetLocation();
		float Alpha = 0;

		// Pre-seed with MAX_flt so first Min() comparison works correctly.
		if (TypedFilterFactory->Config.TimeConsolidation == EPCGExSplineTimeConsolidation::Min) { Alpha = MAX_flt; }

		const TSet<const UPCGData*>* MatchExclude = &Handler->MatchIgnoreList;
		TSet<const UPCGData*> PerPointExclude;

		if (InverseMatcher)
		{
			if (!InverseMatcher->BuildPerPointExclude(PointDataFacade->Source->GetInPoint(PointIndex), *TypedFilterFactory->Datas, PerPointExclude))
			{ return bNoMatchResult; }
			MatchExclude = &PerPointExclude;
		}

		if (TypedFilterFactory->Config.Pick == EPCGExSplineFilterPick::Closest)
		{
			double BestDist = MAX_dbl;

			TypedFilterFactory->Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), [&](const PCGExOctree::FItem& Item)
			{
				if (!MatchExclude->IsEmpty() && MatchExclude->Contains((*TypedFilterFactory->Datas)[Item.Index].Data)) { return; }

				float LocalAlpha = 0;
				const TSharedPtr<PCGExPaths::FPolyPath> Path = (*(TypedFilterFactory->PolyPaths.GetData() + Item.Index));
				const FTransform Closest = Path->GetClosestTransform(WorldPosition, LocalAlpha, false);

				const double D = FVector::DistSquared(Closest.GetLocation(), WorldPosition);

				if (D < BestDist)
				{
					Alpha = LocalAlpha;
					BestDist = D;
				}
			});
		}
		else
		{
			int32 MatchCount = 0;
			for (int32 i = 0; i < TypedFilterFactory->PolyPaths.Num(); i++)
			{
				if (!MatchExclude->IsEmpty() && MatchExclude->Contains((*TypedFilterFactory->Datas)[i].Data)) { continue; }

				float LocalAlpha = 0;
				(void)TypedFilterFactory->PolyPaths[i]->GetClosestTransform(WorldPosition, LocalAlpha, false);
				MatchCount++;

				switch (TypedFilterFactory->Config.TimeConsolidation)
				{
				case EPCGExSplineTimeConsolidation::Min: Alpha = FMath::Min(LocalAlpha, Alpha);
					break;
				case EPCGExSplineTimeConsolidation::Max: Alpha = FMath::Max(LocalAlpha, Alpha);
					break;
				case EPCGExSplineTimeConsolidation::Average: Alpha += LocalAlpha;
					break;
				}
			}

			if (TypedFilterFactory->Config.TimeConsolidation == EPCGExSplineTimeConsolidation::Average && MatchCount > 0)
			{
				Alpha /= MatchCount;
			}
		}

		return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, Alpha, TypedFilterFactory->Config.OperandBConstant, TypedFilterFactory->Config.Tolerance);
	}

	bool FTimeFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
	{
		PCGExData::FProxyPoint ProxyPoint;
		IO->GetDataAsProxyPoint(ProxyPoint);
		return Test(ProxyPoint);
	}
}

TArray<FPCGPinProperties> UPCGExTimeFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExPathInclusion::DeclareInclusionPin(PinProperties);
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Time)

#if WITH_EDITOR
FString UPCGExTimeFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Time ") + PCGExCompare::ToString(Config.Comparison);

	if (Config.CompareAgainst == EPCGExInputValueType::Attribute) { DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB); }
	else { DisplayName += FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.OperandBConstant) / 1000.0)); }

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
