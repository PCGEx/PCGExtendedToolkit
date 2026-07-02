// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExNearestFilter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactories.h"
#include "Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "PCGExMatching/Public/Helpers/PCGExTargetsHandler.h"


#define LOCTEXT_NAMESPACE "PCGExNearestFilter"
#define PCGEX_NAMESPACE NearestFilter

#pragma region UPCGExNearestFilterFactoryData

bool UPCGExNearestFilterFactoryData::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	check(NearestConfig);
	if (NearestConfig->DataMatching.IsEnabled())
	{
		PCGExFactories::GetInputFactories(InContext, PCGExMatching::Labels::SourceMatchRulesLabel, MatchRuleFactories, {FPCGExDataTypeInfoMatchRule::AsId()});
	}
	return true;
}

PCGExFactories::EPreparationResult UPCGExNearestFilterFactoryData::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	check(NearestConfig);
	const FPCGExNearestFilterConfigBase& Cfg = *NearestConfig;

	TargetsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	if (!TargetsHandler->Init(InContext, PCGExCommon::Labels::SourceTargetsLabel))
	{
		return PCGExFactories::EPreparationResult::MissingData;
	}

	TargetsHandler->SetDistances(Cfg.DistanceDetails);
	TargetsHandler->SetMatchingDetails(InContext, &Cfg.DataMatching);

	TargetsHandler->ForEachPreloader([&](PCGExData::FFacadePreloader& Preloader)
	{
		RegisterTargetDependencies(InContext, Preloader);
	});

	TWeakPtr<FPCGContextHandle> WeakHandle = InContext->GetWeakSelfHandle();
	TargetsHandler->TargetsPreloader->OnCompleteCallback = [this, WeakHandle]()
	{
		PCGEX_SHARED_CONTEXT_VOID(WeakHandle)
		PrepResult = BuildTargetCaches(SharedContext.Get())
			            ? PCGExFactories::EPreparationResult::Success
			            : PCGExFactories::EPreparationResult::Fail;
	};

	TargetsHandler->StartLoading(TaskManager);
	return Super::Prepare(InContext, TaskManager);
}

void UPCGExNearestFilterFactoryData::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	check(NearestConfig);
	// MaxDistance is read per-point on the source, so preload it.
	NearestConfig->MaxDistance.RegisterBufferDependencies(InContext, FacadePreloader);
}

bool UPCGExNearestFilterFactoryData::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	check(NearestConfig);
	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_CONDITIONAL(NearestConfig->MaxDistance.Input == EPCGExInputValueType::Attribute, NearestConfig->MaxDistance.Attribute, Consumable)

	return true;
}

void UPCGExNearestFilterFactoryData::BeginDestroy()
{
	TargetsHandler.Reset();
	Super::BeginDestroy();
}

#pragma endregion

#pragma region FNearestFilter

bool PCGExPointFilter::FNearestFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	if (!TargetsHandler || TargetsHandler->IsEmpty())
	{
		return false;
	}

	const FPCGExNearestFilterConfigBase& Cfg = NearestFactory->GetNearestConfig();

	MaxDistance = Cfg.MaxDistance.GetValueSetting(PCGEX_QUIET_HANDLING);
	if (!MaxDistance->Init(PointDataFacade, false))
	{
		return false;
	}
	DistanceScale = Cfg.DistanceScale;

	// Constant Max Distance -> resolve the scalar once and skip the per-point virtual Read.
	bConstantMaxDistance = MaxDistance->IsConstant();
	if (bConstantMaxDistance) { ConstantMaxDistance = MaxDistance->Read(0) * DistanceScale; }

	// Only bounds-based source distance modes need the query box padded by the source extents.
	bInflateQueryBounds = Cfg.DistanceDetails.Source != EPCGExDistance::Center;

	if (Cfg.bIgnoreSelf)
	{
		IgnoreList.Add(InPointDataFacade->GetIn());
	}

	const bool bMatchingEnabled = Cfg.DataMatching.IsEnabled() && !NearestFactory->MatchRuleFactories.IsEmpty();

	// This family only supports collection-level (data/tag) matching: the ignore list is identical for every point,
	// so it is built once here (PopulateIgnoreListInverse, data-vs-data) instead of per-point in Test(). It is never
	// collection-evaluated, so a rule that reads a per-point attribute is always rejected.
	if (bMatchingEnabled)
	{
		bNoMatchResult = (Cfg.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);

		bool bWantsPoints = false;
		PCGExMatching::FScope MatchingScope(TargetsHandler->Num(), true);
		const bool bMatched = TargetsHandler->PopulateIgnoreListInverse(NearestFactory->MatchRuleFactories, InPointDataFacade, &Cfg.DataMatching, MatchingScope, IgnoreList, bWantsPoints);
		if (PCGExPointFilter::RejectPerPointMatchRule(InContext, TEXT("Nearest"), bWantsPoints, false))
		{
			return false;
		}
		if (!bMatched)
		{
			bMatchingFailed = true;
			bCollectionTestResult = bNoMatchResult;
		}
	}

	return InitNearest(InContext, InPointDataFacade);
}

const TSet<const UPCGData*>* PCGExPointFilter::FNearestFilter::ResolveExclude(const int32 PointIndex, TSet<const UPCGData*>& Scratch, bool& bShortCircuit, bool& bShortCircuitResult) const
{
	bShortCircuit = false;
	bShortCircuitResult = false;

	// Matching failed for the whole collection (no candidate matched) -> every point takes the fallback result.
	if (bMatchingFailed)
	{
		bShortCircuit = true;
		bShortCircuitResult = bCollectionTestResult;
		return nullptr;
	}

	// Collection-level ignore list (self-ignore + non-matching targets), built once in Init(). Scratch is unused
	// now that there is no per-point exclude set; it is kept in the signature for call-site stability.
	return &IgnoreList;
}

PCGExData::FConstPoint PCGExPointFilter::FNearestFilter::FindNearestInRange(const PCGExData::FConstPoint& SourcePt, const double MaxDist, const TSet<const UPCGData*>* ExcludePtr) const
{
	PCGExData::FConstPoint TargetPt = PCGExData::FConstPoint();
	const bool bBounded = MaxDist > 0;

	double BestDist = TNumericLimits<double>::Max();
	if (bBounded)
	{
		// Bounds-based source modes offset the measured center up to GetScaledExtents().Length(), so pad the
		// query box by that reach (Center mode needs none); the metric trim below still rejects out-of-range hits.
		const double QueryExtent = bInflateQueryBounds ? MaxDist + SourcePt.GetScaledExtents().Length() : MaxDist;
		const FBoxCenterAndExtent QueryBounds(SourcePt.GetLocation(), FVector(QueryExtent));
		TargetsHandler->FindClosestTarget(SourcePt, QueryBounds, TargetPt, BestDist, ExcludePtr);
	}
	else
	{
		TargetsHandler->FindClosestTarget(SourcePt, TargetPt, BestDist, ExcludePtr);
	}

	if (!TargetPt.IsValid() || (bBounded && BestDist > MaxDist * MaxDist))
	{
		return PCGExData::FConstPoint();
	}

	return TargetPt;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
