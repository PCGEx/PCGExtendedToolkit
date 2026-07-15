// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExInclusionFilter.h"


#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "PCGExMatching/Public/Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "Paths/PCGExPath.h"


#define LOCTEXT_NAMESPACE "PCGExInclusionFilterDefinition"
#define PCGEX_NAMESPACE PCGExInclusionFilterDefinition

bool UPCGExInclusionFilterFactory::SupportsCollectionEvaluation() const
{
	return Config.bCheckAgainstDataBounds;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExInclusionFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FInclusionFilter>(this);
}

FName UPCGExInclusionFilterFactory::GetInputLabel() const
{
	return PCGExCommon::Labels::SourceTargetsLabel;
}

void UPCGExInclusionFilterFactory::InitConfig_Internal()
{
	Super::InitConfig_Internal();
	LocalFidelity = Config.Fidelity;
	LocalExpansion = Config.Tolerance;
	LocalExpansionZ = Config.ExpandZAxis;
	InclusionOffset = Config.InclusionOffset;
	LocalProjection = Config.ProjectionDetails;
	LocalSampleInputs = Config.SampleInputs;
	WindingMutation = Config.WindingMutation;
	bScaleTolerance = Config.bSplineScalesTolerance;
	bIgnoreSelf = Config.bIgnoreSelf;
	DataMatching = Config.DataMatching;
}

#if WITH_EDITOR
void UPCGExInclusionFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExInclusionFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 73, 4)
	{
		Config.ProjectionDetails.ApplyDeprecation();
	}

	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.DataMatching.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

namespace PCGExPointFilter
{
	bool FInclusionFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
	{
		if (!IFilter::Init(InContext, InPointDataFacade))
		{
			return false;
		}

		const bool bMatchingEnabled = TypedFilterFactory->Config.DataMatching.IsEnabled()
			&& TypedFilterFactory->HasMatchRuleFactories();

		// Inclusion only supports collection-level (data/tag) matching: the ignore list is identical for every point,
		// so it is built once here instead of per-point in Test(). A rule that reads a per-point attribute is rejected
		// under per-point evaluation; under collection/proxy mode (bCheckAgainstDataBounds) it degrades gracefully.
		if (bMatchingEnabled)
		{
			bool bWantsPoints = false;
			const bool bMatched = TypedFilterFactory->PopulateMatchIgnoreList(InContext, InPointDataFacade, Handler->MatchIgnoreList, bWantsPoints);
			if (PCGExPointFilter::RejectPerPointMatchRule(InContext, TEXT("Inclusion"), bWantsPoints, TypedFilterFactory->Config.bCheckAgainstDataBounds))
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

		InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();
		InBoundsMin = InPointDataFacade->GetIn()->GetConstBoundsMinValueRange();
		InBoundsMax = InPointDataFacade->GetIn()->GetConstBoundsMaxValueRange();

		if (bCheckAgainstDataBounds)
		{
			PCGExData::FProxyPoint ProxyPoint;
			InPointDataFacade->Source->GetDataAsProxyPoint(ProxyPoint);
			bCollectionTestResult = Test(ProxyPoint);
		}

		return true;
	}

#define PCGEX_CHECK_MAX if (TypedFilterFactory->Config.bUseMaxInclusionCount && InclusionsCount > TypedFilterFactory->Config.MaxInclusionCount) { return TypedFilterFactory->Config.bInvert; }
#define PCGEX_CHECK_MIN if (TypedFilterFactory->Config.bUseMinInclusionCount && InclusionsCount < TypedFilterFactory->Config.MinInclusionCount) { return TypedFilterFactory->Config.bInvert; }

	bool FInclusionFilter::Test(const PCGExData::FProxyPoint& Point) const
	{
		int32 InclusionsCount = 0;
		PCGExPathInclusion::EFlags Flags = Handler->GetInclusionFlags(Point.GetTransform(), Point.GetBoundsMin(), Point.GetBoundsMax(), InclusionsCount, TypedFilterFactory->Config.Pick == EPCGExSplineFilterPick::Closest);

		PCGEX_CHECK_MAX
		PCGEX_CHECK_MIN

		const bool bPass = Handler->TestFlags(Flags);
		return TypedFilterFactory->Config.bInvert ? !bPass : bPass;
	}

	bool FInclusionFilter::Test(const int32 PointIndex) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FInclusionFilter::Test);

		if (bCheckAgainstDataBounds)
		{
			return bCollectionTestResult;
		}

		int32 InclusionsCount = 0;
		PCGExPathInclusion::EFlags Flags = Handler->GetInclusionFlags(InTransforms[PointIndex], InBoundsMin[PointIndex], InBoundsMax[PointIndex], InclusionsCount, TypedFilterFactory->Config.Pick == EPCGExSplineFilterPick::Closest, PointDataFacade->Source->GetIn());

		PCGEX_CHECK_MAX
		PCGEX_CHECK_MIN

		const bool bPass = Handler->TestFlags(Flags);
		return TypedFilterFactory->Config.bInvert ? !bPass : bPass;
	}

	bool FInclusionFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
	{
		PCGExData::FProxyPoint ProxyPoint;
		IO->GetDataAsProxyPoint(ProxyPoint);

		int32 InclusionsCount = 0;
		PCGExPathInclusion::EFlags Flags = Handler->GetInclusionFlags(ProxyPoint.GetTransform(), ProxyPoint.GetBoundsMin(), ProxyPoint.GetBoundsMax(), InclusionsCount, TypedFilterFactory->Config.Pick == EPCGExSplineFilterPick::Closest, IO->GetInOut());

		PCGEX_CHECK_MAX
		PCGEX_CHECK_MIN

		const bool bPass = Handler->TestFlags(Flags);
		return TypedFilterFactory->Config.bInvert ? !bPass : bPass;
	}

#undef PCGEX_CHECK_MAX
#undef PCGEX_CHECK_MIN
}

TArray<FPCGPinProperties> UPCGExInclusionFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExPathInclusion::DeclareInclusionPin(PinProperties);
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Inclusion)

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExInclusionFilterProviderSettings::GetPreconfiguredInfo() const
{
	TArray<FPCGPreConfiguredSettingsInfo> Infos;

	const TSet<EPCGExSplineCheckType> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExSplineCheckType>(ValuesToSkip, FTEXT("{0}"));
}
#endif

void UPCGExInclusionFilterProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	if (const UEnum* EnumPtr = StaticEnum<EPCGExSplineCheckType>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Config.CheckType = static_cast<EPCGExSplineCheckType>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

#if WITH_EDITOR
FString UPCGExInclusionFilterProviderSettings::GetDisplayName() const
{
	return PCGExPathInclusion::ToString(Config.CheckType);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
