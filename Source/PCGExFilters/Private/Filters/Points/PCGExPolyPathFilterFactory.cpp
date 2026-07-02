// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExPolyPathFilterFactory.h"

#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPolygon2DData.h"
#include "Data/PCGSplineData.h"
#include "PCGExMatching/Public/Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Paths/PCGExPolyPath.h"


#define LOCTEXT_NAMESPACE "PCGExPathInclusionFilterDefinition"
#define PCGEX_NAMESPACE PCGExPathInclusionFilterDefinition

bool UPCGExPolyPathFilterFactory::Init(FPCGExContext* InContext)
{
	// Match-rule factories are loaded in Prepare(), not here. At Init() time the DataMatching member
	// is still default-constructed (Mode=Disabled) because it is only populated from the derived Config
	// by InitConfig_Internal(), which runs inside Prepare(). Gating the load on DataMatching here would
	// therefore always skip it and silently disable data-matching.
	return Super::Init(InContext);
}

bool UPCGExPolyPathFilterFactory::WantsPreparation(FPCGExContext* InContext)
{
	return true;
}

PCGExFactories::EPreparationResult UPCGExPolyPathFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	TempTargets = InContext->InputData.GetInputsByPin(GetInputLabel());

	if (TempTargets.IsEmpty())
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error)
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("No targets (no input matches criteria or empty dataset)"))
		}
		return PCGExFactories::EPreparationResult::MissingData;
	}

	TempTaggedData.Init(FPCGExTaggedData(), TempTargets.Num());
	TempPolyPaths.Init(nullptr, TempTargets.Num());
	TempTags.Init(nullptr, TempTargets.Num());
	PolyPaths.Reserve(TempTargets.Num());

	Datas = MakeShared<TArray<FPCGExTaggedData>>();
	Datas->Reserve(TempTargets.Num());
	OwnedTags.Reserve(TempTargets.Num());

	TWeakPtr<FPCGContextHandle> CtxHandle = InContext->GetWeakSelfHandle();

	InitConfig_Internal();

	// DataMatching is now populated from the derived Config, so the match-rule factories can be loaded.
	// They are consumed later (when filter instances are created), which always happens after Prepare().
	if (DataMatching.IsEnabled())
	{
		PCGExFactories::GetInputFactories(InContext, PCGExMatching::Labels::SourceMatchRulesLabel, MatchRuleFactories, {FPCGExDataTypeInfoMatchRule::AsId()});
	}

	PCGEX_ASYNC_GROUP_CHKD_RET(TaskManager, CreatePolyPaths, PCGExFactories::EPreparationResult::Fail)

	CreatePolyPaths->OnCompleteCallback = [CtxHandle, this]()
	{
		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		FBox OctreeBounds = FBox(ForceInit);

		TArray<FBox> BoundsList;
		BoundsList.Reserve(TempTargets.Num());

		for (int i = 0; i < TempTargets.Num(); i++)
		{
			const TSharedPtr<PCGExPaths::FPolyPath>& Path = TempPolyPaths[i];

			if (!Path || !Path.IsValid())
			{
				continue;
			}

			const UPCGSpatialData* Data = Cast<UPCGSpatialData>(TempTargets[i].Data);
			FBox DataBounds = Data->GetBounds().ExpandBy((LocalExpansion + 1 + FMath::Max(0, InclusionOffset)) * 2);
			if (bScaleTolerance)
			{
				DataBounds = DataBounds.ExpandBy((DataBounds.GetSize().Length() + 1) * 10);
			}

			if (LocalExpansionZ < 0)
			{
				DataBounds.Max.Z = TNumericLimits<double>::Max() * 0.5;
				DataBounds.Min.Z = TNumericLimits<double>::Max() * -0.5;
			}
			else
			{
				DataBounds.Max.Z += LocalExpansionZ;
				DataBounds.Min.Z -= LocalExpansionZ;
			}

			BoundsList.Add(DataBounds);
			OctreeBounds += DataBounds;

			PolyPaths.Add(Path);
			Datas->Add(TempTaggedData[i]);
			OwnedTags.Add(TempTags[i]); // strong owner -- keeps Datas[k].Tags (TWeakPtr) alive
		}

		if (PolyPaths.IsEmpty())
		{
			PrepResult = PCGExFactories::EPreparationResult::MissingData;
			PCGEX_LOG_MISSING_INPUT(SharedContext.Get(), FTEXT("No polypaths to work with (no input matches criteria or empty dataset)"))
			return;
		}

		TempTaggedData.Empty();
		TempPolyPaths.Empty();
		TempTags.Empty();
		TempTargets.Empty();

		Octree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
		for (int i = 0; i < BoundsList.Num(); i++)
		{
			Octree->AddElement(PCGExOctree::FItem(i, BoundsList[i]));
		}
	};

	CreatePolyPaths->OnIterationCallback = [CtxHandle, this](const int32 Index, const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExPolyPathFilterFactory::CreatePolyPath);

		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		const UPCGData* Data = TempTargets[Index].Data;
		if (!Data)
		{
			return;
		}

		const bool bIsClosedLoop = PCGExPaths::Helpers::GetClosedLoop(Data);
		if (LocalSampleInputs == EPCGExSplineSamplingIncludeMode::ClosedLoopOnly && !bIsClosedLoop)
		{
			return;
		}
		if (LocalSampleInputs == EPCGExSplineSamplingIncludeMode::OpenSplineOnly && bIsClosedLoop)
		{
			return;
		}

		TSharedPtr<PCGExPaths::FPolyPath> Path = nullptr;

		double SafeExpansion = FMath::Max(LocalExpansion, 1);

		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Data))
		{
			if (PointData->GetNumPoints() < 2)
			{
				PCGE_LOG_C(Warning, GraphAndLog, SharedContext.Get(), FTEXT("Some targets have less than 2 points and will be ignored."));
				return;
			}

			const TSharedPtr<PCGExData::FPointIO> PointIO = MakeShared<PCGExData::FPointIO>(CtxHandle, PointData);
			Path = MakeShared<PCGExPaths::FPolyPath>(PointIO, LocalProjection, SafeExpansion, LocalExpansionZ, WindingMutation);
			Path->OffsetProjection(InclusionOffset);
		}
		else if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(Data))
		{
			if (SplineData->GetNumSegments() < 1)
			{
				PCGE_LOG_C(Warning, GraphAndLog, SharedContext.Get(), FTEXT("Some targets splines are invalid (less than one segment)."));
				return;
			}

			Path = MakeShared<PCGExPaths::FPolyPath>(SplineData, LocalFidelity, LocalProjection, SafeExpansion, LocalExpansionZ, WindingMutation);
			Path->OffsetProjection(InclusionOffset);
		}
		else if (const UPCGPolygon2DData* PolygonData = Cast<UPCGPolygon2DData>(Data))
		{
			if (PolygonData->GetNumSegments() < 1)
			{
				PCGE_LOG_C(Warning, GraphAndLog, SharedContext.Get(), FTEXT("Some targets splines are invalid (less than one segment)."));
				return;
			}

			Path = MakeShared<PCGExPaths::FPolyPath>(PolygonData, LocalProjection, SafeExpansion, LocalExpansionZ, WindingMutation);
			Path->OffsetProjection(InclusionOffset);
		}

		if (Path)
		{
			if (bBuildEdgeOctree)
			{
				Path->BuildEdgeOctree();
			}
			TempPolyPaths[Index] = Path;
			TSharedPtr<PCGExData::FTags> Tags = MakeShared<PCGExData::FTags>(TempTargets[Index].Tags);
			TempTaggedData[Index] = FPCGExTaggedData(Data, Index, Tags, nullptr);
			TempTags[Index] = Tags; // retain a strong ref; FPCGExTaggedData only holds Tags weakly
		}
	};

	CreatePolyPaths->StartIterations(TempTargets.Num(), 1);
	return Result;
}

TSharedPtr<PCGExPathInclusion::FHandler> UPCGExPolyPathFilterFactory::CreateHandler() const
{
	TSharedPtr<PCGExPathInclusion::FHandler> Handler = MakeShared<PCGExPathInclusion::FHandler>(this);
	Handler->bScaleTolerance = bScaleTolerance;
	return Handler;
}

// Collection-level matching path: uses Test(UPCGData*, ...) which reads MatchableSourceFirstElements[0] -- always
// the first point of the input. These path filters only support collection-level rules, so the ignore list is the
// same for every point and is built a single time here. If a rule wants per-point evaluation, bOutWantsPoints is
// set and the caller fails init (we never build a per-point list in the hot path).
bool UPCGExPolyPathFilterFactory::PopulateMatchIgnoreList(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade, TSet<const UPCGData*>& OutIgnoreList, bool& bOutWantsPoints) const
{
	bOutWantsPoints = false;
	if (!DataMatching.IsEnabled())
	{
		return true;
	}
	if (MatchRuleFactories.IsEmpty())
	{
		return true;
	}

	auto InverseMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	InverseMatcher->SetDetails(&DataMatching);

	TArray<TSharedPtr<PCGExData::FFacade>> SingleSource;
	SingleSource.Add(InFacade);
	if (!InverseMatcher->Init(MatchRuleFactories, SingleSource, false))
	{
		return true;
	}

	// A per-point rule still yields a usable collection-level list (built from the first element); we flag it so
	// per-point callers can reject it, but we always build the list so collection/proxy callers keep matching.
	bOutWantsPoints = InverseMatcher->WantsPoints();

	PCGExMatching::FScope Scope(1, true);
	return InverseMatcher->PopulateIgnoreListFromCandidates(*Datas, Scope, OutIgnoreList);
}

void UPCGExPolyPathFilterFactory::BeginDestroy()
{
	PolyPaths.Reset();
	OwnedTags.Reset();
	Octree.Reset();
	Super::BeginDestroy();
}

FPCGDataTypeIdentifier PCGExPathInclusion::GetInclusionIdentifier()
{
	return FPCGDataTypeIdentifier::Construct({FPCGDataTypeInfoSpline::AsId(), FPCGDataTypeInfoPolyline::AsId(), FPCGDataTypeInfoPolygon2D::AsId(), FPCGDataTypeInfoPoint::AsId()});
}

namespace PCGExPathInclusion
{
	void DeclareInclusionPin(TArray<FPCGPinProperties>& PinProperties)
	{
		PCGEX_PIN_FACTORIES(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Path, splines, polygons, ... will be used for testing"), Required, PCGExPathInclusion::GetInclusionIdentifier())
	}

	FHandler::FHandler(const UPCGExPolyPathFilterFactory* InFactory)
	{
		Datas = InFactory->Datas;
		Paths = &InFactory->PolyPaths;
		Octree = InFactory->Octree;
		Tolerance = InFactory->LocalExpansion;
		ToleranceSquared = FMath::Square(InFactory->LocalExpansion);
		bIgnoreSelf = InFactory->bIgnoreSelf;
	}

	void FHandler::Init(const EPCGExSplineCheckType InCheckType, const EPCGExDistance InPrecision)
	{
		Check = InCheckType;

		// Failsafe: only Sphere/Box enable precision. Center, the hidden None sentinel, and any out-of-range
		// value (e.g. forced through an override pin) all fall back to Center, so precision is never half-enabled.
		switch (InPrecision)
		{
		case EPCGExDistance::SphereBounds:
		case EPCGExDistance::BoxBounds:
			Precision = InPrecision;
			break;
		default:
			Precision = EPCGExDistance::Center;
			break;
		}

		const bool bUsePrecision = Precision != EPCGExDistance::Center;

		switch (Check)
		{
		case EPCGExSplineCheckType::IsInside:
			GoodFlags = Inside;

			// Precision needs the distance path (the fast path is center-only and can't see the bound).
			if (!bUsePrecision && Tolerance <= 0)
			{
				bFastCheck = true;
			}
			else
			{
				bFastCheck = false;
				BadFlags = On;
			}

			FlagScope = Any;
			break;
		case EPCGExSplineCheckType::IsInsideOrOn:
			GoodFlags = static_cast<EFlags>(Inside | On);
			FlagScope = Any;
			break;
		case EPCGExSplineCheckType::IsInsideAndOn:
			GoodFlags = static_cast<EFlags>(Inside | On);
			FlagScope = All;
			break;
		case EPCGExSplineCheckType::IsOutside:
			GoodFlags = Outside;

			// Precision needs the distance path (the fast path is center-only and can't see the bound).
			if (!bUsePrecision && Tolerance <= 0)
			{
				bFastCheck = true;
			}
			else
			{
				bFastCheck = false;
				BadFlags = On;
			}

			FlagScope = Any;
			break;
		case EPCGExSplineCheckType::IsOutsideOrOn:
			GoodFlags = static_cast<EFlags>(Outside | On);
			FlagScope = Any;
			break;
		case EPCGExSplineCheckType::IsOutsideAndOn:
			GoodFlags = static_cast<EFlags>(Outside | On);
			FlagScope = All;
			break;
		case EPCGExSplineCheckType::IsOn:
			GoodFlags = On;
			FlagScope = Any;
			bDistanceCheckOnly = true;
			break;
		case EPCGExSplineCheckType::IsNotOn:
			BadFlags = On;
			FlagScope = Skip;
			bDistanceCheckOnly = true;
			break;
		}
	}

	EFlags FHandler::GetInclusionFlags(const FTransform& InTransform, const FVector& InBoundsMin, const FVector& InBoundsMax, int32& InclusionCount, const bool bClosestOnly, const UPCGData* InParentData, const TSet<const UPCGData*>* InAdditionalExclude) const
	{
		const FVector WorldPosition = InTransform.GetLocation();
		const bool bUsePrecision = Precision != EPCGExDistance::Center;

		uint8 OutFlags = None;
		bool bIsOn = false;
		bool bAnyShapeAllGood = false; // All-scope (AndOn): a single shape must satisfy every good flag

		// Bounding-sphere (corner) radius of the tested bound. It upper-bounds any box reach too, so it both sizes
		// the octree broad-phase query (below) and serves as the reach for SphereBounds. Only paid under precision.
		const double SphereReach = bUsePrecision ? (((InBoundsMax - InBoundsMin) * 0.5) * InTransform.GetScale3D()).Length() : 0.0;

		const auto* DataArray = Datas->GetData();
		const auto* PathArray = Paths->GetData();

		// The candidate query must cover the bound's reach, or far paths the bound touches are never visited and
		// can't contribute the 'On' flag. Center keeps the cheap 1-unit query (its reach is 0).
		const FVector QueryExtent = bUsePrecision ? FVector(SphereReach + Tolerance) : FVector::OneVector;

		auto ShouldSkip = [&](const PCGExOctree::FItem& Item) -> bool
		{
			if (bIgnoreSelf && DataArray[Item.Index].Data == InParentData) { return true; }
			if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data)) { return true; }
			if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data)) { return true; }
			return false;
		};

		if (bFastCheck)
		{
			Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, QueryExtent), [&](const PCGExOctree::FItem& Item)
			{
				if (ShouldSkip(Item)) { return; }

				const bool bInside = PathArray[Item.Index]->IsInsideProjection(WorldPosition);
				InclusionCount += bInside;
				if (bClosestOnly) { OutFlags = bInside ? Inside : Outside; }
				else { OutFlags |= bInside ? Inside : Outside; }
			});
		}
		else
		{
			// 'On' band: the nearest boundary lies within the bound's reach toward it plus the tolerance band.
			// Center -> reach 0, measured as 3D squared distance (unchanged legacy behaviour). Precision -> reach is
			// the sphere radius / oriented-box silhouette, and the distance is measured IN-PLANE (projected) so it
			// matches the 2D inside test; out-of-plane extent is left to the octree / ExpandZAxis broad-phase.
			auto ComputeIsOn = [&](const PCGExPaths::FPath& Path, const FTransform& Closest, const double DistSquared3D) -> bool
			{
				if (!bUsePrecision)
				{
					const double Tol = bScaleTolerance ? FMath::Square(Tolerance * (Closest.GetScale3D() * ToleranceScaleFactor).Length()) : ToleranceSquared;
					return DistSquared3D < Tol;
				}

				const FPCGExGeo2DProjectionDetails& Projection = Path.GetProjection();
				const FVector ClosestLoc = Closest.GetLocation();
				const FVector FlatPos = Projection.ProjectFlat(WorldPosition);
				const double InPlaneDistSq = FVector::DistSquared(FlatPos, Projection.ProjectFlat(ClosestLoc));

				double Reach = SphereReach;
				if (Precision == EPCGExDistance::BoxBounds)
				{
					// Closest point on the oriented box toward the boundary (same clamp as GetSpatializedCenter<BoxBounds>),
					// projected so the reach is measured in the same plane as InPlaneDistSq.
					const FVector LocalClosest = InTransform.InverseTransformPosition(ClosestLoc).ComponentMax(InBoundsMin).ComponentMin(InBoundsMax);
					Reach = FVector::Dist(FlatPos, Projection.ProjectFlat(InTransform.TransformPosition(LocalClosest)));
				}

				const double LinearTol = bScaleTolerance ? Tolerance * (Closest.GetScale3D() * ToleranceScaleFactor).Length() : Tolerance;
				const double Band = Reach + LinearTol;
				return InPlaneDistSq < Band * Band; // squared compare -- no sqrt of the per-path distance
			};

			double BestDist = TNumericLimits<double>::Max();

			Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, QueryExtent), [&](const PCGExOctree::FItem& Item)
			{
				if (ShouldSkip(Item)) { return; }

				const PCGExPaths::FPath& Path = *PathArray[Item.Index];
				bool bLocalIsInside = false;
				const FTransform Closest = Path.GetClosestTransform(WorldPosition, bLocalIsInside, bScaleTolerance);
				InclusionCount += bLocalIsInside;
				OutFlags |= bLocalIsInside ? Inside : Outside;

				const double DistSquared3D = FVector::DistSquared(WorldPosition, Closest.GetLocation());
				const bool bLocalOn = ComputeIsOn(Path, Closest, DistSquared3D);

				if (FlagScope == All)
				{
					// Require the SAME shape to satisfy every good flag (e.g. inside AND on its own boundary),
					// instead of letting Inside come from one shape and On from another.
					const uint8 ShapeFlags = (bLocalIsInside ? Inside : Outside) | (bLocalOn ? On : None);
					if (EnumHasAllFlags(static_cast<EFlags>(ShapeFlags), GoodFlags)) { bAnyShapeAllGood = true; }
				}
				else if (bClosestOnly && !bUsePrecision)
				{
					// Center 'closest' pick: On reflects the nearest-by-distance shape only.
					if (DistSquared3D < BestDist) { BestDist = DistSquared3D; bIsOn = bLocalOn; }
				}
				else if (bLocalOn)
				{
					// All pick, and every precision pick: On if the bound is on any shape. Precision can't take the
					// nearest-by-center shortcut because the oriented-box reach is direction-dependent.
					bIsOn = true;
				}
			});
		}

		if (FlagScope == All)
		{
			// AndOn passes iff a single shape satisfied every good flag; BadFlags is None for these checks.
			return bAnyShapeAllGood ? GoodFlags : Outside;
		}

		if (OutFlags == None) { OutFlags = Outside; }
		if (bIsOn) { OutFlags |= On; }

		return static_cast<EFlags>(OutFlags);
	}

	PCGExMath::FClosestPosition FHandler::FindClosestIntersection(const PCGExMath::FSegment& Segment, const FPCGExPathIntersectionDetails& InDetails, const UPCGData* InParentData, const TSet<const UPCGData*>* InAdditionalExclude) const
	{
		PCGExMath::FClosestPosition ClosestIntersection;

		const auto* DataArray = Datas->GetData();
		const auto* PathArray = Paths->GetData();

		Octree->FindFirstElementWithBoundsTest(Segment.Bounds, [&](const PCGExOctree::FItem& Item)
		{
			if (bIgnoreSelf && InParentData != nullptr)
			{
				if (InParentData == DataArray[Item.Index].Data)
				{
					return true;
				}
			}
			if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data))
			{
				return true;
			}
			if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data))
			{
				return true;
			}
			ClosestIntersection = PathArray[Item.Index]->FindClosestIntersection(InDetails, Segment);
			return !ClosestIntersection.bValid;
		});

		return ClosestIntersection;
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
