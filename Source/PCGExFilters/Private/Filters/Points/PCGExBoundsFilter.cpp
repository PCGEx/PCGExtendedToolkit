// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExBoundsFilter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExMathBounds.h"  // For PCGExMath::GetLocalBounds
#include "Math/OBB/PCGExOBBCollection.h"
#include "PCGExMatching/Public/Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExBoundsFilterDefinition"
#define PCGEX_NAMESPACE PCGExBoundsFilterDefinition


TSharedPtr<PCGExPointFilter::IFilter> UPCGExBoundsFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FBoundsFilter>(this);
}

bool UPCGExBoundsFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext)) { return false; }
	if (Config.DataMatching.IsEnabled()) { PCGExFactories::GetInputFactories(InContext, PCGExMatching::Labels::SourceMatchRulesLabel, MatchRuleFactories, {PCGExFactories::EType::MatchRule}); }
	return true;
}

PCGExFactories::EPreparationResult UPCGExBoundsFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success) { return Result; }

	if (!PCGExData::TryGetFacades(InContext, FName("Bounds"), BoundsDataFacades, false))
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error) { PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing bounds data.")) }
		return PCGExFactories::EPreparationResult::MissingData;
	}

	// Expansion is doubled for expanded modes (legacy behavior)
	const float Expansion =
		(Config.TestMode == EPCGExBoxCheckMode::ExpandedBox || Config.TestMode == EPCGExBoxCheckMode::ExpandedSphere)
			? Config.Expansion * 2.0f
			: Config.Expansion;

	// Build OBB collections from bounds data
	Collections.Reserve(BoundsDataFacades.Num());
	for (const TSharedPtr<PCGExData::FFacade>& Facade : BoundsDataFacades)
	{
		auto Collection = MakeShared<PCGExMath::OBB::FCollection>();
		const int32 NumPoints = Facade->GetNum();

		Collection->Reserve(NumPoints);

		for (int32 i = 0; i < NumPoints; i++)
		{
			const PCGExData::FConstPoint Point = Facade->Source->GetInPoint(i);
			const FTransform Transform = Point.GetTransform();
			FBox LocalBox = PCGExMath::GetLocalBounds(Point, Config.BoundsTarget);

			// Apply expansion if needed
			if (Expansion > 0) { LocalBox = LocalBox.ExpandBy(Expansion); }

			Collection->Add(Transform, LocalBox, i);
		}

		Collection->BuildOctree();
		Collections.Add(Collection);
	}

	return Result;
}

void UPCGExBoundsFilterFactory::BeginDestroy()
{
	BoundsDataFacades.Empty();
	Collections.Empty();
	Super::BeginDestroy();
}

bool PCGExPointFilter::FBoundsFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade)) { return false; }

	Collections = &TypedFilterFactory->Collections;
	if (!Collections || Collections->IsEmpty()) { return false; }

	const bool bMatchingEnabled = TypedFilterFactory->Config.DataMatching.IsEnabled()
		&& !TypedFilterFactory->MatchRuleFactories.IsEmpty();

	// See FDistanceFilter::Init for the full per-point vs static matching explanation.

	if (bMatchingEnabled && !TypedFilterFactory->Config.bCheckAgainstDataBounds)
	{
		// Per-point: store matcher + candidates now, build per-point collections in Test()
		InverseMatcher = MakeShared<PCGExMatching::FDataMatcher>();
		InverseMatcher->SetDetails(&TypedFilterFactory->Config.DataMatching);

		TArray<TSharedPtr<PCGExData::FFacade>> SingleSource;
		SingleSource.Add(InPointDataFacade);
		if (InverseMatcher->Init(TypedFilterFactory->MatchRuleFactories, SingleSource, false))
		{
			BoundsCandidates.Reserve(TypedFilterFactory->BoundsDataFacades.Num());
			for (int32 i = 0; i < TypedFilterFactory->BoundsDataFacades.Num(); i++)
			{
				BoundsCandidates.Add(TypedFilterFactory->BoundsDataFacades[i]->Source->GetTaggedData(PCGExData::EIOSide::In, i));
			}
			bNoMatchResult = (TypedFilterFactory->Config.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);
		}
		else { InverseMatcher.Reset(); }
	}
	else if (bMatchingEnabled)
	{
		// Static matching (collection-level with bCheckAgainstDataBounds)
		auto StaticMatcher = MakeShared<PCGExMatching::FDataMatcher>();
		StaticMatcher->SetDetails(&TypedFilterFactory->Config.DataMatching);

		TArray<TSharedPtr<PCGExData::FFacade>> SingleSource;
		SingleSource.Add(InPointDataFacade);
		if (StaticMatcher->Init(TypedFilterFactory->MatchRuleFactories, SingleSource, false))
		{
			TArray<FPCGExTaggedData> StaticCandidates;
			StaticCandidates.Reserve(TypedFilterFactory->BoundsDataFacades.Num());
			for (int32 i = 0; i < TypedFilterFactory->BoundsDataFacades.Num(); i++)
			{
				StaticCandidates.Add(TypedFilterFactory->BoundsDataFacades[i]->Source->GetTaggedData(PCGExData::EIOSide::In, i));
			}

			TSet<const UPCGData*> IgnoreList;
			PCGExMatching::FScope MatchingScope(1, true);
			if (!StaticMatcher->PopulateIgnoreListFromCandidates(StaticCandidates, MatchingScope, IgnoreList))
			{
				bCheckAgainstDataBounds = true;
				bCollectionTestResult = (TypedFilterFactory->Config.DataMatching.NoMatchFallback == EPCGExFilterFallback::Pass);
				return true;
			}

			if (!IgnoreList.IsEmpty())
			{
				FilteredCollections.Reserve(TypedFilterFactory->Collections.Num());
				for (int32 i = 0; i < TypedFilterFactory->Collections.Num(); i++)
				{
					if (!IgnoreList.Contains(TypedFilterFactory->BoundsDataFacades[i]->GetIn()))
					{
						FilteredCollections.Add(TypedFilterFactory->Collections[i]);
					}
				}
				Collections = &FilteredCollections;

				if (Collections->IsEmpty())
				{
					bCollectionTestResult = TypedFilterFactory->Config.bInvert;
					return true;
				}
			}
		}
	}

	// Cache config for fast access during tests
	const FPCGExBoundsFilterConfig& Config = TypedFilterFactory->Config;
	BoundsSource = Config.BoundsSource;
	CheckType = Config.CheckType;
	CheckMode = Config.TestMode;
	Expansion = Config.Expansion;
	bInvert = Config.bInvert;
	bIgnoreSelf = Config.bIgnoreSelf;
	bCheckAgainstDataBounds = Config.bCheckAgainstDataBounds;
	bUseCollectionBounds = Config.Mode == EPCGExBoundsFilterCompareMode::CollectionBounds;

	// Pre-compute collection test result if using data bounds mode
	if (bCheckAgainstDataBounds)
	{
		PCGExData::FProxyPoint ProxyPoint;
		InPointDataFacade->Source->GetDataAsProxyPoint(ProxyPoint);
		bCollectionTestResult = Test(ProxyPoint);
	}

	return true;
}

bool PCGExPointFilter::FBoundsFilter::TestPoint(const FVector& Position, const FTransform& Transform, const FBox& LocalBox) const
{
	// Build query OBB from point (only needed for overlap tests)
	PCGExMath::OBB::FOBB QueryOBB;
	const bool bNeedQueryOBB = (CheckType == EPCGExBoundsCheckType::Intersects || CheckType == EPCGExBoundsCheckType::IsInsideOrIntersects);

	if (bNeedQueryOBB)
	{
		QueryOBB = PCGExMath::OBB::Factory::FromTransform(Transform, LocalBox, -1);
	}

	for (const TSharedPtr<PCGExMath::OBB::FCollection>& Collection : *Collections)
	{
		bool bPass = false;

		if (bUseCollectionBounds)
		{
			// CollectionBounds mode: Test against combined world bounds of target collection
			const FBox& WorldBounds = Collection->GetWorldBounds();
			const FBox ExpandedBounds = Expansion > 0 ? WorldBounds.ExpandBy(Expansion) : WorldBounds;

			switch (CheckType)
			{
			case EPCGExBoundsCheckType::Intersects:
				{
					// Query OBB intersects collection's world bounds
					const FBox QueryWorldBox = LocalBox.TransformBy(Transform);
					bPass = ExpandedBounds.Intersect(QueryWorldBox);
				}
				break;

			case EPCGExBoundsCheckType::IsInside:
				bPass = ExpandedBounds.IsInside(Position);
				break;

			case EPCGExBoundsCheckType::IsInsideOrOn:
				bPass = ExpandedBounds.IsInsideOrOn(Position);
				break;

			case EPCGExBoundsCheckType::IsInsideOrIntersects:
				{
					const FBox QueryWorldBox = LocalBox.TransformBy(Transform);
					bPass = ExpandedBounds.IsInside(Position) || ExpandedBounds.Intersect(QueryWorldBox);
				}
				break;
			}
		}
		else
		{
			// PerPointBounds mode: Test against individual OBBs in collection
			switch (CheckType)
			{
			case EPCGExBoundsCheckType::Intersects:
				// Point's OBB overlaps any target OBB
				bPass = Collection->Overlaps(QueryOBB, CheckMode, Expansion);
				break;

			case EPCGExBoundsCheckType::IsInside:
				// Point center is inside any target OBB
				bPass = Collection->IsPointInside(Position, CheckMode, Expansion);
				break;

			case EPCGExBoundsCheckType::IsInsideOrOn:
				// Point center is inside or on boundary (use small expansion for "on")
				bPass = Collection->IsPointInside(Position, CheckMode, Expansion + KINDA_SMALL_NUMBER);
				break;

			case EPCGExBoundsCheckType::IsInsideOrIntersects:
				// Either point inside OR OBB overlaps
				bPass = Collection->IsPointInside(Position, CheckMode, Expansion) ||
					Collection->Overlaps(QueryOBB, CheckMode, Expansion);
				break;
			}
		}

		if (bPass) { return !bInvert; }
	}

	return bInvert; // No collection matched
}

bool PCGExPointFilter::FBoundsFilter::TestPoint(const FVector& Position, const FTransform& Transform, const FBox& LocalBox, const TArray<TSharedPtr<PCGExMath::OBB::FCollection>>& InCollections) const
{
	PCGExMath::OBB::FOBB QueryOBB;
	const bool bNeedQueryOBB = (CheckType == EPCGExBoundsCheckType::Intersects || CheckType == EPCGExBoundsCheckType::IsInsideOrIntersects);

	if (bNeedQueryOBB)
	{
		QueryOBB = PCGExMath::OBB::Factory::FromTransform(Transform, LocalBox, -1);
	}

	for (const TSharedPtr<PCGExMath::OBB::FCollection>& Collection : InCollections)
	{
		bool bPass = false;

		if (bUseCollectionBounds)
		{
			const FBox& WorldBounds = Collection->GetWorldBounds();
			const FBox ExpandedBounds = Expansion > 0 ? WorldBounds.ExpandBy(Expansion) : WorldBounds;

			switch (CheckType)
			{
			case EPCGExBoundsCheckType::Intersects:
				{
					const FBox QueryWorldBox = LocalBox.TransformBy(Transform);
					bPass = ExpandedBounds.Intersect(QueryWorldBox);
				}
				break;
			case EPCGExBoundsCheckType::IsInside:
				bPass = ExpandedBounds.IsInside(Position);
				break;
			case EPCGExBoundsCheckType::IsInsideOrOn:
				bPass = ExpandedBounds.IsInsideOrOn(Position);
				break;
			case EPCGExBoundsCheckType::IsInsideOrIntersects:
				{
					const FBox QueryWorldBox = LocalBox.TransformBy(Transform);
					bPass = ExpandedBounds.IsInside(Position) || ExpandedBounds.Intersect(QueryWorldBox);
				}
				break;
			}
		}
		else
		{
			switch (CheckType)
			{
			case EPCGExBoundsCheckType::Intersects:
				bPass = Collection->Overlaps(QueryOBB, CheckMode, Expansion);
				break;
			case EPCGExBoundsCheckType::IsInside:
				bPass = Collection->IsPointInside(Position, CheckMode, Expansion);
				break;
			case EPCGExBoundsCheckType::IsInsideOrOn:
				bPass = Collection->IsPointInside(Position, CheckMode, Expansion + KINDA_SMALL_NUMBER);
				break;
			case EPCGExBoundsCheckType::IsInsideOrIntersects:
				bPass = Collection->IsPointInside(Position, CheckMode, Expansion) ||
					Collection->Overlaps(QueryOBB, CheckMode, Expansion);
				break;
			}
		}

		if (bPass) { return !bInvert; }
	}

	return bInvert;
}

bool PCGExPointFilter::FBoundsFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	const FTransform Transform = Point.GetTransform();
	const FBox LocalBox = PCGExMath::GetLocalBounds(Point, BoundsSource);
	return TestPoint(Transform.GetLocation(), Transform, LocalBox);
}

bool PCGExPointFilter::FBoundsFilter::Test(const int32 PointIndex) const
{
	if (bCheckAgainstDataBounds) { return bCollectionTestResult; }

	if (InverseMatcher)
	{
		// Per-point matching -- build a filtered collections list for this specific point.
		// Unlike other filters that build an exclude set, bounds uses an include list since
		// TestPoint iterates collections directly (no octree intermediary with data pointers).
		PCGExData::FConstPoint Pt = PointDataFacade->Source->GetInPoint(PointIndex);
		Pt.IO = 0;
		TArray<TSharedPtr<PCGExMath::OBB::FCollection>> PerPointCollections;
		bool bAnyMatch = false;
		for (int32 i = 0; i < BoundsCandidates.Num(); i++)
		{
			PCGExMatching::FScope Scope(1, true);
			if (InverseMatcher->Test(Pt, BoundsCandidates[i], Scope))
			{
				PerPointCollections.Add(TypedFilterFactory->Collections[i]);
				bAnyMatch = true;
			}
		}
		if (!bAnyMatch) { return bNoMatchResult; }

		const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);
		const FTransform Transform = Point.GetTransform();
		const FBox LocalBox = PCGExMath::GetLocalBounds(Point, BoundsSource);
		return TestPoint(Transform.GetLocation(), Transform, LocalBox, PerPointCollections);
	}

	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);
	const FTransform Transform = Point.GetTransform();
	const FBox LocalBox = PCGExMath::GetLocalBounds(Point, BoundsSource);
	return TestPoint(Transform.GetLocation(), Transform, LocalBox);
}

bool PCGExPointFilter::FBoundsFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	PCGExData::FProxyPoint ProxyPoint;
	IO->GetDataAsProxyPoint(ProxyPoint);
	return Test(ProxyPoint);
}

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExBoundsFilterProviderSettings::GetPreconfiguredInfo() const
{
	const TSet<EPCGExBoundsCheckType> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExBoundsCheckType>(ValuesToSkip, FTEXT("{0} (Bounds)"));
}
#endif

void UPCGExBoundsFilterProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	if (const UEnum* EnumPtr = StaticEnum<EPCGExBoundsCheckType>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Config.CheckType = static_cast<EPCGExBoundsCheckType>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

TArray<FPCGPinProperties> UPCGExBoundsFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(FName("Bounds"), TEXT("Points which bounds will be used for testing"), Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(Config.DataMatching, PinProperties);
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Bounds)

#if WITH_EDITOR
FString UPCGExBoundsFilterProviderSettings::GetDisplayName() const
{
	switch (Config.CheckType)
	{
	default:
	case EPCGExBoundsCheckType::Intersects: return TEXT("Intersects");
	case EPCGExBoundsCheckType::IsInside: return TEXT("Is Inside");
	case EPCGExBoundsCheckType::IsInsideOrOn: return TEXT("Is Inside or On");
	case EPCGExBoundsCheckType::IsInsideOrIntersects: return TEXT("Is Inside or Intersects");
	}
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
