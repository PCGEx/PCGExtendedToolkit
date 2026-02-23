// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExSegmentCrossFilter.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"
#include "PCGExMatching/Public/Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExSegmentCrossFilterDefinition"
#define PCGEX_NAMESPACE PCGExSegmentCrossFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExSegmentCrossFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FSegmentCrossFilter>(this);
}

FName UPCGExSegmentCrossFilterFactory::GetInputLabel() const
{
	return PCGExCommon::Labels::SourceTargetsLabel;
}

void UPCGExSegmentCrossFilterFactory::InitConfig_Internal()
{
	Super::InitConfig_Internal();

	Config.IntersectionSettings.Init();

	LocalFidelity = Config.Fidelity;
	LocalExpansion = Config.IntersectionSettings.Tolerance;
	LocalExpansionZ = -1;
	//LocalProjection = Config.ProjectionDetails;
	LocalSampleInputs = Config.SampleInputs;
	WindingMutation = EPCGExWindingMutation::Unchanged;
	bScaleTolerance = false;
	bIgnoreSelf = Config.bIgnoreSelf;
	DataMatching = Config.DataMatching;
	bBuildEdgeOctree = true;
}

namespace PCGExPointFilter
{
	bool FSegmentCrossFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
	{
		if (!IFilter::Init(InContext, InPointDataFacade)) { return false; }

		const bool bMatchingEnabled = TypedFilterFactory->Config.DataMatching.IsEnabled()
			&& TypedFilterFactory->HasMatchRuleFactories();

		// See FDistanceFilter::Init for the full per-point vs static matching explanation.
		// Always per-point when matching is enabled (no bCheckAgainstDataBounds on this filter).

		if (bMatchingEnabled)
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

		if (!InverseMatcher)
		{
			// Static matching or no matching
			if (!TypedFilterFactory->PopulateMatchIgnoreList(InContext, InPointDataFacade, Handler->MatchIgnoreList))
			{
				bMatchingFailed = true;
				bCollectionTestResult = (TypedFilterFactory->Config.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);
				return true;
			}
		}

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(InPointDataFacade->Source->GetIn());
		LastIndex = InPointDataFacade->GetNum() - 1;

		InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();
		return true;
	}

	bool FSegmentCrossFilter::Test(const int32 PointIndex) const
	{
		if (bMatchingFailed) { return bCollectionTestResult; }

		// Build segment from current point to neighbor. For open paths, endpoints have no valid
		// neighbor segment, so return the default (no-intersection) result immediately.
		int32 NextIndex = PointIndex;

		if (TypedFilterFactory->Config.Direction == EPCGExSegmentCrossWinding::ToNext)
		{
			NextIndex++;
			if (NextIndex > LastIndex)
			{
				if (!bClosedLoop) { return TypedFilterFactory->Config.bInvert; }
				NextIndex = 0;
			}
		}
		else
		{
			NextIndex--;
			if (NextIndex < 0)
			{
				if (!bClosedLoop) { return TypedFilterFactory->Config.bInvert; }
				NextIndex = LastIndex;
			}
		}

		const TSet<const UPCGData*>* AdditionalExclude = nullptr;
		TSet<const UPCGData*> PerPointExclude;

		if (InverseMatcher)
		{
			if (!InverseMatcher->BuildPerPointExclude(PointDataFacade->Source->GetInPoint(PointIndex), *TypedFilterFactory->Datas, PerPointExclude))
			{ return bNoMatchResult; }
			AdditionalExclude = &PerPointExclude;
		}

		const PCGExMath::FSegment Segment(InTransforms[PointIndex].GetLocation(), InTransforms[NextIndex].GetLocation(), Handler->Tolerance);
		const PCGExMath::FClosestPosition ClosestPosition = Handler->FindClosestIntersection(Segment, TypedFilterFactory->Config.IntersectionSettings, PointDataFacade->Source->GetIn(), AdditionalExclude);
		return TypedFilterFactory->Config.bInvert ? !ClosestPosition.bValid : ClosestPosition.bValid;
	}
}

TArray<FPCGPinProperties> UPCGExSegmentCrossFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExPathInclusion::DeclareInclusionPin(PinProperties);
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(SegmentCross)

#if WITH_EDITOR
FString UPCGExSegmentCrossFilterProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString();
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
