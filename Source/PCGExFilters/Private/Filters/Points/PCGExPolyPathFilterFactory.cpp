// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExPolyPathFilterFactory.h"

#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "PCGExMatching/Public/Helpers/PCGExDataMatcher.h"
#include "PCGExMatching/Public/Helpers/PCGExMatchingHelpers.h"
#if PCGEX_ENGINE_VERSION > 506
#include "Data/PCGPolygon2DData.h"
#endif
#include "Data/PCGSplineData.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Paths/PCGExPolyPath.h"


#define LOCTEXT_NAMESPACE "PCGExPathInclusionFilterDefinition"
#define PCGEX_NAMESPACE PCGExPathInclusionFilterDefinition

bool UPCGExPolyPathFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext)) { return false; }
	if (DataMatching.IsEnabled()) { PCGExFactories::GetInputFactories(InContext, PCGExMatching::Labels::SourceMatchRulesLabel, MatchRuleFactories, {PCGExFactories::EType::MatchRule}); }
	return true;
}

bool UPCGExPolyPathFilterFactory::WantsPreparation(FPCGExContext* InContext)
{
	return true;
}

PCGExFactories::EPreparationResult UPCGExPolyPathFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success) { return Result; }

	TempTargets = InContext->InputData.GetInputsByPin(GetInputLabel());

	if (TempTargets.IsEmpty())
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error) { PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("No targets (no input matches criteria or empty dataset)")) }
		return PCGExFactories::EPreparationResult::MissingData;
	}

	TempTaggedData.Init(FPCGExTaggedData(), TempTargets.Num());
	TempPolyPaths.Init(nullptr, TempTargets.Num());
	PolyPaths.Reserve(TempTargets.Num());

	Datas = MakeShared<TArray<FPCGExTaggedData>>();
	Datas->Reserve(TempTargets.Num());

	TWeakPtr<FPCGContextHandle> CtxHandle = InContext->GetOrCreateHandle();

	InitConfig_Internal();

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

			if (!Path || !Path.IsValid()) { continue; }

			const UPCGSpatialData* Data = Cast<UPCGSpatialData>(TempTargets[i].Data);
			FBox DataBounds = Data->GetBounds().ExpandBy((LocalExpansion + 1 + FMath::Max(0, InclusionOffset)) * 2);
			if (bScaleTolerance) { DataBounds = DataBounds.ExpandBy((DataBounds.GetSize().Length() + 1) * 10); }

			if (LocalExpansionZ < 0)
			{
				DataBounds.Max.Z = MAX_dbl * 0.5;
				DataBounds.Min.Z = MAX_dbl * -0.5;
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
		}

		if (PolyPaths.IsEmpty())
		{
			PrepResult = PCGExFactories::EPreparationResult::MissingData;
			PCGEX_LOG_MISSING_INPUT(SharedContext.Get(), FTEXT("No polypaths to work with (no input matches criteria or empty dataset)"))
			return;
		}

		TempTaggedData.Empty();
		TempPolyPaths.Empty();
		TempTargets.Empty();

		Octree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
		for (int i = 0; i < BoundsList.Num(); i++) { Octree->AddElement(PCGExOctree::FItem(i, BoundsList[i])); }

	};

	CreatePolyPaths->OnIterationCallback = [CtxHandle, this](const int32 Index, const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExPolyPathFilterFactory::CreatePolyPath);

		PCGEX_SHARED_CONTEXT_VOID(CtxHandle)

		const UPCGData* Data = TempTargets[Index].Data;
		if (!Data) { return; }

		const bool bIsClosedLoop = PCGExPaths::Helpers::GetClosedLoop(Data);
		if (LocalSampleInputs == EPCGExSplineSamplingIncludeMode::ClosedLoopOnly && !bIsClosedLoop) { return; }
		if (LocalSampleInputs == EPCGExSplineSamplingIncludeMode::OpenSplineOnly && bIsClosedLoop) { return; }

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
#if PCGEX_ENGINE_VERSION > 506
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
#endif

		if (Path)
		{
			if (bBuildEdgeOctree) { Path->BuildEdgeOctree(); }
			TempPolyPaths[Index] = Path;
			TSharedPtr<PCGExData::FTags> Tags = MakeShared<PCGExData::FTags>(TempTargets[Index].Tags);
			TempTaggedData[Index] = FPCGExTaggedData(Data, Index, Tags, nullptr);
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

// Static matching path: uses Test(UPCGData*, ...) which reads MatchableSourceFirstElements[0] -- always the
// first point of the input. Suitable for collection-level proxy evaluation (bCheckAgainstDataBounds) or when
// no matching is configured. For per-point evaluation with attribute-based rules, filters create their own
// FDataMatcher and call Test(FConstPoint, ...) per-point instead of using this method.
bool UPCGExPolyPathFilterFactory::PopulateMatchIgnoreList(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade, TSet<const UPCGData*>& OutIgnoreList) const
{
	if (!DataMatching.IsEnabled()) { return true; }
	if (MatchRuleFactories.IsEmpty()) { return true; }

	auto InverseMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	InverseMatcher->SetDetails(&DataMatching);

	TArray<TSharedPtr<PCGExData::FFacade>> SingleSource;
	SingleSource.Add(InFacade);
	if (!InverseMatcher->Init(MatchRuleFactories, SingleSource, false)) { return true; }

	PCGExMatching::FScope Scope(1, true);
	return InverseMatcher->PopulateIgnoreListFromCandidates(*Datas, Scope, OutIgnoreList);
}

void UPCGExPolyPathFilterFactory::BeginDestroy()
{
	PolyPaths.Reset();
	Octree.Reset();
	Super::BeginDestroy();
}

#if PCGEX_ENGINE_VERSION > 506
FPCGDataTypeIdentifier PCGExPathInclusion::GetInclusionIdentifier()
{
	return FPCGDataTypeIdentifier::Construct({FPCGDataTypeInfoSpline::AsId(), FPCGDataTypeInfoPolyline::AsId(), FPCGDataTypeInfoPolygon2D::AsId(), FPCGDataTypeInfoPoint::AsId()});
}
#endif

namespace PCGExPathInclusion
{
	void DeclareInclusionPin(TArray<FPCGPinProperties>& PinProperties)
	{
#if PCGEX_ENGINE_VERSION < 507
		PCGEX_PIN_ANY(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Path, splines, polygons, ... will be used for testing"), Required)
#else
		PCGEX_PIN_FACTORIES(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Path, splines, polygons, ... will be used for testing"), Required, PCGExPathInclusion::GetInclusionIdentifier())
#endif
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

	void FHandler::Init(const EPCGExSplineCheckType InCheckType)
	{
		Check = InCheckType;

		switch (Check)
		{
		case EPCGExSplineCheckType::IsInside: GoodFlags = Inside;

			if (Tolerance <= 0)
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
		case EPCGExSplineCheckType::IsInsideOrOn: GoodFlags = static_cast<EFlags>(Inside | On);
			FlagScope = Any;
			break;
		case EPCGExSplineCheckType::IsInsideAndOn: GoodFlags = static_cast<EFlags>(Inside | On);
			FlagScope = All;
			break;
		case EPCGExSplineCheckType::IsOutside: GoodFlags = Outside;

			if (Tolerance <= 0)
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
		case EPCGExSplineCheckType::IsOutsideOrOn: GoodFlags = static_cast<EFlags>(Outside | On);
			FlagScope = Any;
			break;
		case EPCGExSplineCheckType::IsOutsideAndOn: GoodFlags = static_cast<EFlags>(Outside | On);
			FlagScope = All;
			break;
		case EPCGExSplineCheckType::IsOn: GoodFlags = On;
			FlagScope = Any;
			bDistanceCheckOnly = true;
			break;
		case EPCGExSplineCheckType::IsNotOn: BadFlags = On;
			FlagScope = Skip;
			bDistanceCheckOnly = true;
			break;
		}
	}

	EFlags FHandler::GetInclusionFlags(const FVector& WorldPosition, int32& InclusionCount, const bool bClosestOnly, const UPCGData* InParentData, const TSet<const UPCGData*>* InAdditionalExclude) const
	{
		uint8 OutFlags = None;
		bool bIsOn = false;

		const auto* DataArray = Datas->GetData();
		const auto* PathArray = Paths->GetData();

		if (bFastCheck)
		{
			if (bClosestOnly)
			{
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), [&](const PCGExOctree::FItem& Item)
				{
					if (bIgnoreSelf && DataArray[Item.Index].Data == InParentData) { return; }
					if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data)) { return; }
					if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data)) { return; }

					const bool bInside = PathArray[Item.Index]->IsInsideProjection(WorldPosition);
					InclusionCount += bInside;
					OutFlags = bInside ? Inside : Outside;
				});
			}
			else
			{
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), [&](const PCGExOctree::FItem& Item)
				{
					if (bIgnoreSelf && DataArray[Item.Index].Data == InParentData) { return; }
					if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data)) { return; }
					if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data)) { return; }

					const bool bInside = PathArray[Item.Index]->IsInsideProjection(WorldPosition);
					InclusionCount += bInside;
					OutFlags |= bInside ? Inside : Outside;
				});
			}
		}
		else
		{
			if (bClosestOnly)
			{
				double BestDist = MAX_dbl;

				if (Check == EPCGExSplineCheckType::IsOn)
				{
				}

				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), [&](const PCGExOctree::FItem& Item)
				{
					if (bIgnoreSelf && DataArray[Item.Index].Data == InParentData) { return; }
					if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data)) { return; }
					if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data)) { return; }

					bool bLocalIsInside = false;
					const FTransform Closest = PathArray[Item.Index]->GetClosestTransform(WorldPosition, bLocalIsInside, bScaleTolerance);
					InclusionCount += bLocalIsInside;
					OutFlags |= bLocalIsInside ? Inside : Outside;

					if (const double Dist = FVector::DistSquared(WorldPosition, Closest.GetLocation()); Dist < BestDist)
					{
						BestDist = Dist;
						const double Tol = bScaleTolerance ? FMath::Square(Tolerance * (Closest.GetScale3D() * ToleranceScaleFactor).Length()) : ToleranceSquared;
						bIsOn = Dist < Tol;
					}
				});
			}
			else
			{
				Octree->FindElementsWithBoundsTest(FBoxCenterAndExtent(WorldPosition, FVector::OneVector), [&](const PCGExOctree::FItem& Item)
				{
					if (bIgnoreSelf && DataArray[Item.Index].Data == InParentData) { return; }
					if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data)) { return; }
					if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data)) { return; }

					bool bLocalIsInside = false;
					const FTransform Closest = PathArray[Item.Index]->GetClosestTransform(WorldPosition, bLocalIsInside, bScaleTolerance);
					InclusionCount += bLocalIsInside;
					OutFlags |= bLocalIsInside ? Inside : Outside;

					const double Tol = bScaleTolerance ? FMath::Square(Tolerance * (Closest.GetScale3D() * ToleranceScaleFactor).Length()) : ToleranceSquared;
					if (FVector::DistSquared(WorldPosition, Closest.GetLocation()) < Tol) { bIsOn = true; }
				});
			}
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
			if (bIgnoreSelf && InParentData != nullptr) { if (InParentData == DataArray[Item.Index].Data) { return true; } }
			if (!MatchIgnoreList.IsEmpty() && MatchIgnoreList.Contains(DataArray[Item.Index].Data)) { return true; }
			if (InAdditionalExclude && InAdditionalExclude->Contains(DataArray[Item.Index].Data)) { return true; }
			ClosestIntersection = PathArray[Item.Index]->FindClosestIntersection(InDetails, Segment);
			return !ClosestIntersection.bValid;
		});

		return ClosestIntersection;
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
