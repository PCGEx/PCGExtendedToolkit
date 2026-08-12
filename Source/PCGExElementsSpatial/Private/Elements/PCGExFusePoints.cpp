// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// * 12/08/26 NRG-Nad Write union metadata in Keep Most Central mode

#include "Elements/PCGExFusePoints.h"


#include "Blenders/PCGExUnionBlender.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExUnionRegistry.h"
#include "Core/PCGExUnionTable.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExBestFitPlane.h"

#define LOCTEXT_NAMESPACE "PCGExFusePointsElement"
#define PCGEX_NAMESPACE FusePoints

namespace PCGExFusePoints
{
	// Reorder FUnionTable entries so they appear in ascending order of each group's lowest contributing
	// input-index. Octree mode emits with dense Keys (0..N-1) in creation order, so the table is already
	// in input-traversal order after stable radix sort -- this collapses to an identity check + early-out.
	// Voxel mode emits with uint64 spatial-hash Keys, which scrambles entries vs input order; this pass
	// restores the legacy single-threaded UnionGraph "first-creator-wins" entry ordering.
	void ReorderUnionTableByPrimary(PCGExData::FUnionTable& Table)
	{
		const int32 N = Table.Num();
		if (N <= 1)
		{
			return;
		}

		TArray<int32> Order;
		Order.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
		{
			Order[i] = i;
		}

		Order.Sort([&Table](const int32 A, const int32 B)
		{
			return Table.Get(A)[0].Index < Table.Get(B)[0].Index;
		});

		// Common case (octree mode, or voxel inputs that happen to be hash-sorted) -- skip rewrite.
		bool bAlreadySorted = true;
		for (int32 i = 0; i < N; ++i)
		{
			if (Order[i] != i)
			{
				bAlreadySorted = false;
				break;
			}
		}
		if (bAlreadySorted)
		{
			return;
		}

		TArray<int32> NewOffsets;
		TArray<uint64> NewKeys;
		TArray<PCGExData::FElement> NewElements;

		NewOffsets.SetNumUninitialized(N + 1);
		NewKeys.SetNumUninitialized(N);
		NewElements.SetNumUninitialized(Table.Elements.Num());

		NewOffsets[0] = 0;
		for (int32 NewIdx = 0; NewIdx < N; ++NewIdx)
		{
			const int32 OldIdx = Order[NewIdx];
			const TConstArrayView<PCGExData::FElement> Span = Table.Get(OldIdx);
			const int32 SpanNum = Span.Num();

			FMemory::Memcpy(NewElements.GetData() + NewOffsets[NewIdx], Span.GetData(), SpanNum * sizeof(PCGExData::FElement));
			NewKeys[NewIdx] = Table.GetKey(OldIdx);
			NewOffsets[NewIdx + 1] = NewOffsets[NewIdx] + SpanNum;
		}

		Table.Offsets = MoveTemp(NewOffsets);
		Table.Keys = MoveTemp(NewKeys);
		Table.Elements = MoveTemp(NewElements);
	}

	FVector ComputeSpanCentroid(const TConstArrayView<PCGExData::FElement>& Span, const TConstPCGValueRange<FTransform>& InTransforms)
	{
		FVector Sum = FVector::ZeroVector;
		for (const PCGExData::FElement& E : Span)
		{
			Sum += InTransforms[E.Index].GetLocation();
		}
		return Sum / static_cast<double>(Span.Num());
	}

	void EnforceMinExtent(FBox& Bounds, const double MinExtent)
	{
		if (MinExtent <= 0)
		{
			return;
		}
		const FVector HalfMin(MinExtent);
		const FVector Center = Bounds.GetCenter();
		const FVector HalfSize = Bounds.GetExtent();
		const FVector EnforcedHalf = FVector::Max(HalfSize, HalfMin);
		Bounds = FBox(Center - EnforcedHalf, Center + EnforcedHalf);
	}

	void AccumulateBounds(FBox& Bounds, const FTransform& InvTransform, const PCGExData::FElement& Element,
	                      const UPCGBasePointData* InData, const TConstPCGValueRange<FTransform>& InTransforms,
	                      const EPCGExPointBoundsSource BoundsSource)
	{
		const FVector PointPos = InTransforms[Element.Index].GetLocation();
		switch (BoundsSource)
		{
		default:
		case EPCGExPointBoundsSource::ScaledBounds:
			Bounds += FBoxCenterAndExtent(InvTransform.TransformPosition(PointPos), InData->GetScaledExtents(Element.Index)).GetBox();
			break;
		case EPCGExPointBoundsSource::DensityBounds:
			Bounds += InData->GetDensityBounds(Element.Index).GetBox().TransformBy(InvTransform);
			break;
		case EPCGExPointBoundsSource::Bounds:
			Bounds += FBoxCenterAndExtent(InvTransform.TransformPosition(PointPos), InData->GetExtents(Element.Index)).GetBox();
			break;
		case EPCGExPointBoundsSource::Center:
			Bounds += InvTransform.TransformPosition(PointPos);
			break;
		}
	}
}

PCGEX_INITIALIZE_ELEMENT(FusePoints)
PCGEX_ELEMENT_BATCH_POINT_IMPL(FusePoints)

bool FPCGExFusePointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FusePoints)

	Context->Distances = Settings->PointPointIntersectionDetails.FuseDetails.GetDistances();
	if (!Settings->PointPointIntersectionDetails.SanityCheck(Context))
	{
		return false;
	}

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	return true;
}

bool FPCGExFusePointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFusePointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FusePoints)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = Settings->Mode != EPCGExFusedPointOutput::MostCentral ||
					Settings->PointPointIntersectionDetails.PointUnionData.WriteAny();
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to fuse."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExFusePoints
{
	FProcessor::~FProcessor() = default;

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFusePoints::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::New)

		IOIndex = PointDataFacade->Source->IOIndex;

		// Init a per-processor mutable copy of the FuseDetails -- its ToleranceGetter binds to this facade.
		FuseDetailsCopy = Settings->PointPointIntersectionDetails.FuseDetails;
		if (!FuseDetailsCopy.Init(Context, PointDataFacade))
		{
			return false;
		}

		EffectiveMethod = FuseDetailsCopy.GetEffectiveMethod();

		UnionTable = MakeShared<PCGExData::FUnionTable>();

		// Register fetch-able buffers for chunked reads.
		TArray<PCGExData::FAttributeIdentity> SourceAttributes;
		PCGExBlending::GetFilteredIdentities(PointDataFacade->GetIn()->Metadata, SourceAttributes, &Settings->BlendingDetails, &Context->CarryOverDetails);

		PointDataFacade->CreateReadables(SourceAttributes);

		const int32 NumIn = PointDataFacade->GetNum();

		if (EffectiveMethod == EPCGExFuseMethod::Octree)
		{
			// Octree-mode dedup is order-dependent: FindOrInsert mutates the running octree and
			// running-centers, so concurrent calls would race AND produce non-deterministic insertion order.
			// Sequential single-thread is the only deterministic option with the current FUnionRegistry.
			// (Lifting this requires a separate parallel-octree-merge util.)
			bForceSingleThreadedProcessPoints = true;

			Registry = MakeShared<PCGExData::FUnionRegistry>(PointDataFacade->GetIn()->GetBounds().ExpandBy(10.0));
			Registry->Reserve(NumIn);

			// Single scope, sized for the whole input.
			UnionTableBuilder = MakeShared<PCGExData::FUnionTableBuilder>(1);
			UnionTableBuilder->Reserve(0, NumIn);
		}
		else
		{
			// Voxel mode: keys are pure functions of (location + tolerance + voxel offset), so emission
			// is embarrassingly parallel. FUnionTable's stable LSD radix sort makes the result
			// bit-identical regardless of scope count. Builder is allocated in PrepareLoopScopesForPoints
			// once Loops.Num() is known.
			bForceSingleThreadedProcessPoints = false;
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		if (EffectiveMethod != EPCGExFuseMethod::Voxel)
		{
			return;
		}

		const int32 NumLoops = Loops.Num();
		UnionTableBuilder = MakeShared<PCGExData::FUnionTableBuilder>(NumLoops);
		for (int32 i = 0; i < NumLoops; ++i)
		{
			UnionTableBuilder->Reserve(i, Loops[i].Count);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::FusePoints::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		if (EffectiveMethod == EPCGExFuseMethod::Voxel)
		{
			const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();
			PCGEX_SCOPE_LOOP(Index)
			{
				const uint64 Key = FuseDetailsCopy.GetGridKey(InTransforms[Index].GetLocation(), Index);
				UnionTableBuilder->Emit(Scope.LoopIndex, Key, IOIndex, Index);
			}
		}
		else
		{
			// Octree: single scope, single-threaded by force flag. Sequential dedup against Registry.
			PCGEX_SCOPE_LOOP(Index)
			{
				const PCGExData::FConstPoint P = PointDataFacade->GetInPoint(Index);
				const int32 RepIdx = Registry->FindOrInsert(P, FuseDetailsCopy);
				UnionTableBuilder->Emit(0, static_cast<uint64>(RepIdx), IOIndex, Index);
			}
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TPCGValueRange<FTransform> Transforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		// The "primary" input point for entry i is Get(i)[0] -- the lowest (ScopeIndex, in-scope-position)
		// record in that group after stable radix sort. For octree mode, that's the founding insert; for
		// voxel mode (single-source, contiguous scope partition), that's the lowest input index in the group.
		const EPCGPointNativeProperties InheritProps = PointDataFacade->GetAllocations() & ~EPCGPointNativeProperties::MetadataEntry;
		for (int32 i = 0; i < Scope.Count; ++i)
		{
			const int32 Idx = Scope.Start + i;
			PointDataFacade->Source->InheritProperties(UnionTable->Get(Idx)[0].Index, Idx, 1, InheritProps);
		}

		TArray<PCGExData::FWeightedPoint> WeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;
		UnionBlender->InitTrackers(Trackers);

		const bool bUpdateCenter = Settings->BlendingDetails.PropertiesOverrides.bOverridePosition && Settings->BlendingDetails.PropertiesOverrides.PositionBlending == EPCGExBlendingType::None;

		PCGEX_SHARED_CONTEXT_VOID(Context->GetWeakSelfHandle())

		PCGEX_SCOPE_LOOP(Index)
		{
			const TConstArrayView<PCGExData::FElement> Span = UnionTable->Get(Index);

			// Centroid is the mean of contributors' input positions. Iterating in span (radix-sorted)
			// order keeps the FP sum sequence deterministic regardless of how scopes were partitioned.
			const FVector Center = ComputeSpanCentroid(Span, InTransforms);

			if (bUpdateCenter)
			{
				Transforms[Index].SetLocation(Center);
			}

			UnionBlender->MergeSingle(Index, Span, WeightedPoints, Trackers);
			if (IsUnionWriter)
			{
				IsUnionWriter->SetValue(Index, WeightedPoints.Num() > 1);
			}
			if (UnionSizeWriter)
			{
				UnionSizeWriter->SetValue(Index, WeightedPoints.Num());
			}
		}

		if (Settings->FusedBoundsMode != EPCGExFusedBoundsMode::None)
		{
			UPCGBasePointData* OutData = PointDataFacade->GetOut();
			const UPCGBasePointData* InData = PointDataFacade->GetIn();
			TPCGValueRange<FVector> OutBoundsMin = OutData->GetBoundsMinValueRange(false);
			TPCGValueRange<FVector> OutBoundsMax = OutData->GetBoundsMaxValueRange(false);
			const EPCGExPointBoundsSource BoundsSource = Settings->BoundsSource;
			const double MinExtent = Settings->MinBoundsExtent;

			if (Settings->FusedBoundsMode == EPCGExFusedBoundsMode::AABB)
			{
				const FTransform Identity = FTransform::Identity;

				PCGEX_SCOPE_LOOP(Index)
				{
					const FVector FusedPosition = Transforms[Index].GetLocation();
					const TConstArrayView<PCGExData::FElement> Span = UnionTable->Get(Index);

					FBox Bounds(ForceInit);
					for (const PCGExData::FElement& Element : Span)
					{
						AccumulateBounds(Bounds, Identity, Element, InData, InTransforms, BoundsSource);
					}
					EnforceMinExtent(Bounds, MinExtent);

					OutBoundsMin[Index] = Bounds.Min - FusedPosition;
					OutBoundsMax[Index] = Bounds.Max - FusedPosition;
				}
			}
			else // OBB
			{
				const EPCGExAxisOrder AxisOrder = Settings->AxisOrder;

				PCGEX_SCOPE_LOOP(Index)
				{
					const TConstArrayView<PCGExData::FElement> Span = UnionTable->Get(Index);
					const int32 NumElements = Span.Num();

					const PCGExMath::FBestFitPlane BestFitPlane(NumElements, [&](const int32 e) -> FVector
					{
						return InTransforms[Span[e].Index].GetLocation();
					});

					const FTransform OBBTransform = BestFitPlane.GetTransform(AxisOrder);
					const FTransform InvTransform = OBBTransform.Inverse();

					FBox Bounds(ForceInit);
					for (const PCGExData::FElement& Element : Span)
					{
						AccumulateBounds(Bounds, InvTransform, Element, InData, InTransforms, BoundsSource);
					}
					EnforceMinExtent(Bounds, MinExtent);

					Transforms[Index].SetRotation(OBBTransform.GetRotation());
					Transforms[Index].SetLocation(OBBTransform.GetLocation());
					OutBoundsMin[Index] = Bounds.Min;
					OutBoundsMax[Index] = Bounds.Max;
				}
			}
		}
	}

	void FProcessor::InitUnionMetadataWriters()
	{
		const FPCGExPointUnionMetadataDetails& PtUnionDetails = Settings->PointPointIntersectionDetails.PointUnionData;

		if (PtUnionDetails.bWriteIsUnion)
		{
			IsUnionWriter = PointDataFacade->GetWritable<bool>(PtUnionDetails.IsUnionAttributeName, false, true, PCGExData::EBufferInit::New);
		}

		if (PtUnionDetails.bWriteUnionSize)
		{
			UnionSizeWriter = PointDataFacade->GetWritable<int32>(PtUnionDetails.UnionSizeAttributeName, 1, true, PCGExData::EBufferInit::New);
		}
	}

	void FProcessor::CompleteWork()
	{
		// Finalize the build phase: compile the per-scope records into the immutable, packed FUnionTable.
		check(UnionTableBuilder);
		UnionTableBuilder->Compile(*UnionTable);
		UnionTableBuilder.Reset();
		Registry.Reset();

		if (Settings->bPreserveOrder)
		{
			ReorderUnionTableByPrimary(*UnionTable);
		}

		const int32 NumUnionEntries = UnionTable->Num();

		UPCGBasePointData* OutData = PointDataFacade->GetOut();

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutData, NumUnionEntries, PointDataFacade->GetAllocations());

		if (Settings->Mode == EPCGExFusedPointOutput::MostCentral)
		{
			TArray<int32>& IdxMapping = PointDataFacade->Source->GetIdxMapping(NumUnionEntries);
			const PCGPointOctree::FPointOctree& Octree = PointDataFacade->GetIn()->GetPointOctree();
			const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

			// Local non-null reference for the parallel-for body to capture cleanly.
			const TSharedRef<PCGExData::FUnionTable> Table = UnionTable.ToSharedRef();

			// No blender in this mode, so writables can be created up-front and filled by the mapping pass.
			InitUnionMetadataWriters();

			PCGExMT::ParallelOrSequential(
				NumUnionEntries,
				[&](const int32 i)
				{

					const TConstArrayView<PCGExData::FElement> Span = Table->Get(i);
					const FVector Center = ComputeSpanCentroid(Span, InTransforms);

					double BestDist = TNumericLimits<double>::Max();
					int32 BestIndex = -1;

					Octree.FindNearbyElements(Center, [&](const PCGPointOctree::FPointRef& PointRef)
					{
						const double Dist = FVector::DistSquared(Center, InTransforms[PointRef.Index].GetLocation());
						if (Dist < BestDist)
						{
							BestDist = Dist;
							BestIndex = PointRef.Index;
						}
					});

					if (BestIndex == -1)
					{
						BestIndex = Span[0].Index;
					}
					IdxMapping[i] = BestIndex;

					if (IsUnionWriter)
					{
						IsUnionWriter->SetValue(i, Span.Num() > 1);
					}
					if (UnionSizeWriter)
					{
						UnionSizeWriter->SetValue(i, Span.Num());
					}
				});

			PointDataFacade->Source->ConsumeIdxMapping(PointDataFacade->GetAllocations());

			if (Settings->FusedBoundsMode != EPCGExFusedBoundsMode::None)
			{
				const UPCGBasePointData* InData = PointDataFacade->GetIn();
				TPCGValueRange<FTransform> OutTransformsMut = OutData->GetTransformValueRange(false);
				TPCGValueRange<FVector> OutBoundsMin = OutData->GetBoundsMinValueRange(true);
				TPCGValueRange<FVector> OutBoundsMax = OutData->GetBoundsMaxValueRange(true);
				const EPCGExPointBoundsSource BoundsSource = Settings->BoundsSource;
				const double MinExtent = Settings->MinBoundsExtent;

				if (Settings->FusedBoundsMode == EPCGExFusedBoundsMode::AABB)
				{
					const FTransform Identity = FTransform::Identity;

					PCGExMT::ParallelOrSequential(
						NumUnionEntries,
						[&](const int32 i)
						{
							const FVector FusedPosition = OutTransformsMut[i].GetLocation();
							const TConstArrayView<PCGExData::FElement> Span = Table->Get(i);

							FBox Bounds(ForceInit);
							for (const PCGExData::FElement& Element : Span)
							{
								AccumulateBounds(Bounds, Identity, Element, InData, InTransforms, BoundsSource);
							}
							EnforceMinExtent(Bounds, MinExtent);

							OutBoundsMin[i] = Bounds.Min - FusedPosition;
							OutBoundsMax[i] = Bounds.Max - FusedPosition;
						});
				}
				else // OBB
				{
					const EPCGExAxisOrder AxisOrder = Settings->AxisOrder;

					PCGExMT::ParallelOrSequential(
						NumUnionEntries,
						[&](const int32 i)
						{
							const TConstArrayView<PCGExData::FElement> Span = Table->Get(i);
							const int32 NumElements = Span.Num();

							const PCGExMath::FBestFitPlane BestFitPlane(NumElements, [&](const int32 e) -> FVector
							{
								return InTransforms[Span[e].Index].GetLocation();
							});

							const FTransform OBBTransform = BestFitPlane.GetTransform(AxisOrder);
							const FTransform InvTransform = OBBTransform.Inverse();

							FBox Bounds(ForceInit);
							for (const PCGExData::FElement& Element : Span)
							{
								AccumulateBounds(Bounds, InvTransform, Element, InData, InTransforms, BoundsSource);
							}
							EnforceMinExtent(Bounds, MinExtent);

							OutTransformsMut[i].SetRotation(OBBTransform.GetRotation());
							OutTransformsMut[i].SetLocation(OBBTransform.GetLocation());
							OutBoundsMin[i] = Bounds.Min;
							OutBoundsMax[i] = Bounds.Max;
						});
				}
			}

			return;
		}

		// Blend mode: drive the union blender via the span overload (no FUnionMetadata required).
		const TSharedPtr<PCGExBlending::FUnionBlender> TypedBlender = MakeShared<PCGExBlending::FUnionBlender>(
			&Settings->BlendingDetails, &Context->CarryOverDetails, Context->Distances);
		UnionBlender = TypedBlender;

		TArray<TSharedRef<PCGExData::FFacade>> UnionSources;
		UnionSources.Add(PointDataFacade);

		TypedBlender->AddSources(UnionSources, &PCGExClusters::Labels::ProtectedClusterAttributes);
		if (!TypedBlender->Init(Context, PointDataFacade))
		{
			bIsProcessorValid = false;
			return;
		}

		// Initialize writables AFTER we initialize Union Blender, so these don't get captured in the mix
		InitUnionMetadataWriters();

		StartParallelLoopForRange(NumUnionEntries);
	}

	void FProcessor::Write()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
