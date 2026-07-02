// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExDistanceFilter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Math/PCGExMathDistances.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "PCGExMatching/Public/Helpers/PCGExTargetsHandler.h"


#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExDistanceFilterConfig, DistanceThreshold, double, CompareAgainst, DistanceThreshold, DistanceThresholdConstant)

bool UPCGExDistanceFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}
	if (Config.DataMatching.IsEnabled())
	{
		PCGExFactories::GetInputFactories(InContext, PCGExMatching::Labels::SourceMatchRulesLabel, MatchRuleFactories, {FPCGExDataTypeInfoMatchRule::AsId()});
	}
	return true;
}

bool UPCGExDistanceFilterFactory::SupportsProxyEvaluation() const
{
	return Config.CompareAgainst == EPCGExInputValueType::Constant;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExDistanceFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FDistanceFilter>(this);
}

void UPCGExDistanceFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Config.DistanceThreshold);
	}
}

bool UPCGExDistanceFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_CONDITIONAL(Config.CompareAgainst == EPCGExInputValueType::Attribute, Config.DistanceThreshold, Consumable)

	return true;
}

PCGExFactories::EPreparationResult UPCGExDistanceFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	if (!TargetsHandler->Init(InContext, PCGExCommon::Labels::SourceTargetsLabel))
	{
		return PCGExFactories::EPreparationResult::MissingData;
	}

	TargetsHandler->SetDistances(Config.DistanceDetails);
	TargetsHandler->SetMatchingDetails(InContext, &Config.DataMatching);

	return Super::Prepare(InContext, TaskManager);
}

void UPCGExDistanceFilterFactory::BeginDestroy()
{
	Super::BeginDestroy();
}

bool PCGExPointFilter::FDistanceFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	if (TypedFilterFactory->Config.bIgnoreSelf)
	{
		IgnoreList.Add(InPointDataFacade->GetIn());
	}

	const bool bMatchingEnabled = TypedFilterFactory->Config.DataMatching.IsEnabled()
		&& !TypedFilterFactory->MatchRuleFactories.IsEmpty();

	// Distance only supports collection-level (data/tag) matching: the ignore list is identical for every point,
	// so it is built once here (PopulateIgnoreListInverse, data-vs-data) instead of per-point in Test(). A rule that
	// reads a per-point attribute is rejected under per-point evaluation; under collection/proxy mode it degrades.
	if (bMatchingEnabled)
	{
		bool bWantsPoints = false;
		PCGExMatching::FScope MatchingScope(TargetsHandler->Num(), true);
		const bool bMatched = TargetsHandler->PopulateIgnoreListInverse(TypedFilterFactory->MatchRuleFactories, InPointDataFacade, &TypedFilterFactory->Config.DataMatching, MatchingScope, IgnoreList, bWantsPoints);
		if (PCGExPointFilter::RejectPerPointMatchRule(InContext, TEXT("Distance"), bWantsPoints, TypedFilterFactory->Config.bCheckAgainstDataBounds))
		{
			return false;
		}
		if (!bMatched)
		{
			bCheckAgainstDataBounds = true;
			bCollectionTestResult = (TypedFilterFactory->Config.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);
			return true;
		}
	}

	bCheckAgainstDataBounds = TypedFilterFactory->Config.bCheckAgainstDataBounds;

	if (bCheckAgainstDataBounds)
	{
		PCGExData::FProxyPoint ProxyPoint;
		InPointDataFacade->Source->GetDataAsProxyPoint(ProxyPoint);
		bCollectionTestResult = Test(ProxyPoint);
		return true;
	}

	DistanceThresholdGetter = TypedFilterFactory->Config.GetValueSettingDistanceThreshold(PCGEX_QUIET_HANDLING);
	if (!DistanceThresholdGetter->Init(InPointDataFacade))
	{
		return false;
	}

	InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();

	return true;
}

bool PCGExPointFilter::FDistanceFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	const FVector ProbeLocation = Point.GetLocation();
	const double B = TypedFilterFactory->Config.DistanceThresholdConstant;
	const double SearchExtent = B + TypedFilterFactory->Config.Tolerance;
	const FBoxCenterAndExtent QueryBounds(ProbeLocation, FVector(SearchExtent));

	double BestDist = TNumericLimits<double>::Max();
	const PCGExMath::IDistances* Dist = TargetsHandler->GetDistances();

	TargetsHandler->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExData::FConstPoint& CandidatePoint)
	{
		if (const double D = FVector::DistSquared(Dist->GetTargetCenter(CandidatePoint, CandidatePoint.GetLocation(), ProbeLocation), ProbeLocation);
			BestDist > D)
		{
			BestDist = D;
		}
	}, &IgnoreList);

	return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, FMath::Sqrt(BestDist), B, TypedFilterFactory->Config.Tolerance);
}

bool PCGExPointFilter::FDistanceFilter::Test(const int32 PointIndex) const
{
	if (bCheckAgainstDataBounds)
	{
		return bCollectionTestResult;
	}

	const TSet<const UPCGData*>* ExcludePtr = &IgnoreList;

	const PCGExData::FConstPoint& SourcePt = PointDataFacade->Source->GetInPoint(PointIndex);
	PCGExData::FConstPoint TargetPt;

	const double B = DistanceThresholdGetter->Read(PointIndex);

	// Use bounded search with threshold as extent so FindElementsWithBoundsTest is used
	// instead of FindNearbyElements, which can miss targets when the probe falls outside
	// the octree's root bounds. Targets beyond threshold+tolerance fail the comparison
	// anyway, so the bounded search is sufficient for all comparison types.
	const double SearchExtent = B + TypedFilterFactory->Config.Tolerance;
	const FBoxCenterAndExtent QueryBounds(InTransforms[PointIndex].GetLocation(), FVector(SearchExtent));

	double BestDist = TNumericLimits<double>::Max();
	TargetsHandler->FindClosestTarget(SourcePt, QueryBounds, TargetPt, BestDist, ExcludePtr);

	return PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, FMath::Sqrt(BestDist), B, TypedFilterFactory->Config.Tolerance);
}

bool PCGExPointFilter::FDistanceFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	PCGExData::FProxyPoint ProxyPoint;
	IO->GetDataAsProxyPoint(ProxyPoint);
	return Test(ProxyPoint);
}

TArray<FPCGPinProperties> UPCGExDistanceFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Target points to read operand B from"), Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Distance)

#if WITH_EDITOR
FString UPCGExDistanceFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Distance ") + PCGExCompare::ToString(Config.Comparison);

	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.DistanceThreshold);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.DistanceThresholdConstant) / 1000.0));
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
