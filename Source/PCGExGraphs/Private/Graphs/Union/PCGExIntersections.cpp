// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/Union/PCGExIntersections.h"

#include "PCGExH.h"

#include "Async/ParallelFor.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Clusters/PCGExEdge.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExIntersectionDetails.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphMetadata.h"
#include "Math/PCGExMath.h"

namespace PCGExGraphs
{
#pragma region FIntersectionAllocations

	FIntersectionAllocations::FIntersectionAllocations(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO)
		: Graph(InGraph)
		  , PointIO(InPointIO)
	{
		NodeTransforms = InPointIO->GetOutIn()->GetConstTransformValueRange();
	}

	void FIntersectionAllocations::Build(const double InTolerance)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FIntersectionAllocations::Build);

		Tolerance = InTolerance;
		ToleranceSquared = FMath::Square(InTolerance);

		const int32 NumNodes = Graph->Nodes.Num();
		const int32 NumEdges = Graph->Edges.Num();

		Positions.SetNumUninitialized(NumNodes);
		for (int32 i = 0; i < NumNodes; i++)
		{
			Positions[i] = NodeTransforms[i].GetLocation();
		}

		ValidEdges.Init(false, NumEdges);
		LengthSquared.SetNumUninitialized(NumEdges);
		Directions.SetNumUninitialized(NumEdges);
		EdgeBoxes.SetNumUninitialized(NumEdges);
		EdgeRootIndex.SetNumUninitialized(NumEdges);

		for (int32 i = 0; i < NumEdges; i++)
		{
			const FEdge& Edge = Graph->Edges[i];

			if (!Edge.bValid)
			{
				EdgeRootIndex[i] = i; // sentinel: Apply skips empty buckets so this is never read
				continue;
			}

			EdgeRootIndex[i] = Graph->FindEdgeMetadataRootIndex_Unsafe(i);

			const FVector A = Positions[Edge.Start];
			const FVector B = Positions[Edge.End];
			const double Len = FVector::DistSquared(A, B);
			if (FMath::IsNearlyZero(Len))
			{
				continue;
			}

			ValidEdges[i] = true;
			LengthSquared[i] = Len;
			Directions[i] = (A - B).GetSafeNormal();
			EdgeBoxes[i] = PCGEX_BOX_TOLERANCE_INLINE(A, B, InTolerance);
		}
	}

	void FIntersectionAllocations::BuildRootIOSets()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FIntersectionAllocations::BuildRootIOSets);

		const int32 NumEdges = Graph->Edges.Num();

		// All entries default to -1 (no IO set). Valid edges get assigned a unique-set index below.
		EdgeRootIOSetIdx.Init(-1, NumEdges);

		const TSharedPtr<PCGExData::IUnionMetadata> EdgesUnion = Graph->EdgesUnion;
		if (!EdgesUnion)
		{
			return;
		}

		// Single pass: for each valid edge, look up or insert into UniqueRootIOSets. This stores one
		// TSet per unique RootIndex (typically O(num IO sources) ≪ NumEdges) rather than one per edge.
		TMap<int32, int32> RootIdxToUniqueIdx;
		for (int32 i = 0; i < NumEdges; i++)
		{
			if (!ValidEdges[i])
			{
				continue;
			}
			const int32 RootIdx = EdgeRootIndex[i];
			if (const int32* ExistingUniqueIdx = RootIdxToUniqueIdx.Find(RootIdx))
			{
				EdgeRootIOSetIdx[i] = *ExistingUniqueIdx;
			}
			else
			{
				const int32 NewUniqueIdx = UniqueRootIOSets.Num();
				UniqueRootIOSets.Add(EdgesUnion->GetIOSet(RootIdx));
				RootIdxToUniqueIdx.Add(RootIdx, NewUniqueIdx);
				EdgeRootIOSetIdx[i] = NewUniqueIdx;
			}
		}
	}

	void FIntersectionAllocations::BuildEdgeOctree(const FBox& InBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FIntersectionAllocations::BuildEdgeOctree);

		EdgeOctree = MakeShared<PCGExOctree::FItemOctree>(InBounds.GetCenter(), InBounds.GetExtent().Length() + (Tolerance * 2));

		const int32 NumEdges = Graph->Edges.Num();
		for (int32 i = 0; i < NumEdges; i++)
		{
			if (!ValidEdges[i])
			{
				continue;
			}
			EdgeOctree->AddElement(PCGExOctree::FItem(i, EdgeBoxes[i]));
		}
	}

#pragma endregion

#pragma region PointEdgePass

	namespace PointEdgePass
	{
		// Find the perpendicular projection of the candidate node onto the segment, returning a
		// non-degenerate hit when the projected point is strictly interior and within tolerance.
		static bool TryFindSplit(
			const FIntersectionAllocations& Allocations,
			const int32 EdgeIdx,
			const int32 CandidateNodeIdx,
			FVector& OutClosestPoint,
			double& OutTime)
		{
			const FEdge& Edge = Allocations.Graph->Edges[EdgeIdx];
			const FVector& A = Allocations.Positions[Edge.Start];
			const FVector& B = Allocations.Positions[Edge.End];
			const FVector& C = Allocations.Positions[CandidateNodeIdx];

			OutClosestPoint = FMath::ClosestPointOnSegment(C, A, B);
			if ((OutClosestPoint - A).IsNearlyZero() || (OutClosestPoint - B).IsNearlyZero())
			{
				return false;
			}
			if (FVector::DistSquared(OutClosestPoint, C) >= Allocations.ToleranceSquared)
			{
				return false;
			}

			OutTime = FVector::DistSquared(A, OutClosestPoint) / Allocations.LengthSquared[EdgeIdx];
			return true;
		}

		void Emit(
			FIntersectionAllocations& Allocations,
			const FPCGExPointEdgeIntersectionDetails& Details,
			const bool bEnableSelfIntersection,
			const PCGExMT::FScope& Scope,
			TArray<FPECollinear>& OutScopeRecords)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PointEdgePass::Emit);

			const TSharedPtr<FGraph> Graph = Allocations.Graph;
			const PCGPointOctree::FPointOctree& PointOctree = Allocations.PointIO->GetOutIn()->GetPointOctree();
			const TConstPCGValueRange<FTransform>& Transforms = Allocations.NodeTransforms;

			PCGEX_SCOPE_LOOP(EdgeIdx)
			{
				if (!Allocations.ValidEdges[EdgeIdx])
				{
					continue;
				}

				const FEdge& Edge = Graph->Edges[EdgeIdx];
				const FBox& Box = Allocations.EdgeBoxes[EdgeIdx];

				// Use a pointer rather than a const-ref binding so we don't default-construct a
				// TSet per edge when bEnableSelfIntersection is true (const-ref to temporary
				// forces lifetime extension, i.e. 48-byte zero-init per iteration).
				const TSet<int32>* RootIOIndices = nullptr;
				if (!bEnableSelfIntersection && Allocations.EdgeRootIOSetIdx.IsValidIndex(EdgeIdx))
				{
					const int32 SetIdx = Allocations.EdgeRootIOSetIdx[EdgeIdx];
					if (SetIdx >= 0)
					{
						RootIOIndices = &Allocations.UniqueRootIOSets[SetIdx];
					}
				}

				PointOctree.FindElementsWithBoundsTest(Box, [&](const PCGPointOctree::FPointRef& PointRef)
				{
					const int32 PointIndex = PointRef.Index;
					if (!Transforms.IsValidIndex(PointIndex))
					{
						return;
					}

					const FNode& Node = Graph->Nodes[PointIndex];
					if (!Node.bValid)
					{
						return;
					}
					if (Edge.Contains(Node.PointIndex))
					{
						return;
					}

					const FVector Position = Transforms[Node.PointIndex].GetLocation();
					if (!Box.IsInside(Position))
					{
						return;
					}

					FVector ClosestPoint;
					double Time;
					if (!TryFindSplit(Allocations, EdgeIdx, Node.PointIndex, ClosestPoint, Time))
					{
						return;
					}

					if (RootIOIndices && !RootIOIndices->IsEmpty())
					{
						if (Graph->NodesUnion->IOIndexOverlap(Node.Index, *RootIOIndices))
						{
							return;
						}
					}

					OutScopeRecords.Emplace(EdgeIdx, Node.Index, Time, ClosestPoint);
				});
			}
		}

		void Apply(
			FIntersectionAllocations& Allocations,
			const FPCGExPointEdgeIntersectionDetails& Details,
			TArray<FPECollinear>& Records)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PointEdgePass::Apply);

			if (Records.IsEmpty())
			{
				return;
			}

			// CSR-bucket records by edge so per-edge sort can run in parallel. Cheaper than a
			// global O(N log N) sort when typical per-edge collinear count is small (1-8).
			const TSharedPtr<FGraph> Graph = Allocations.Graph;
			const int32 NumEdges = Graph->Edges.Num();
			const int32 NumRecords = Records.Num();

			TArray<int32> EdgeOffsets;
			EdgeOffsets.SetNumZeroed(NumEdges + 1);
			for (const FPECollinear& R : Records)
			{
				EdgeOffsets[R.EdgeIdx + 1]++;
			}
			for (int32 i = 1; i <= NumEdges; i++)
			{
				EdgeOffsets[i] += EdgeOffsets[i - 1];
			}

			// Collect only edges that actually have records. When crossings are sparse (typical),
			// this is far fewer than NumEdges, avoiding millions of no-op ParallelFor tasks.
			TArray<int32> NonEmptyBuckets;
			NonEmptyBuckets.Reserve(NumRecords); // at most one unique edge per record
			for (int32 i = 0; i < NumEdges; i++)
			{
				if (EdgeOffsets[i + 1] > EdgeOffsets[i])
				{
					NonEmptyBuckets.Add(i);
				}
			}

			TArray<FPECollinear> Bucketed;
			Bucketed.SetNumUninitialized(NumRecords);
			TArray<int32> Cursor = EdgeOffsets;
			for (const FPECollinear& R : Records)
			{
				Bucketed[Cursor[R.EdgeIdx]++] = R;
			}

			// Parallel per-edge sort by Time. Time is a ratio of DistSquared so ties are
			// effectively impossible -- unstable sort is sufficient and faster than StableSort.
			ParallelFor(NonEmptyBuckets.Num(), [&](const int32 i)
			{
				const int32 EdgeIdx = NonEmptyBuckets[i];
				const int32 Begin = EdgeOffsets[EdgeIdx];
				const int32 End = EdgeOffsets[EdgeIdx + 1];
				if (End - Begin <= 1)
				{
					return;
				}
				TArrayView<FPECollinear> Slice = MakeArrayView(Bucketed.GetData() + Begin, End - Begin);
				Slice.Sort([](const FPECollinear& A, const FPECollinear& B)
				{
					return A.Time < B.Time;
				});
			});

			// Sequential subdivision.
			TPCGValueRange<FTransform> Transforms = Allocations.PointIO->GetOut()->GetTransformValueRange(false);
			Graph->ReserveForEdges(NumRecords + NumEdges);

			FEdge NewEdge = FEdge{};
			for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; EdgeIdx++)
			{
				const int32 Begin = EdgeOffsets[EdgeIdx];
				const int32 End = EdgeOffsets[EdgeIdx + 1];
				if (Begin == End)
				{
					continue;
				}

				const FEdge& SrcEdge = Graph->Edges[EdgeIdx];
				const int32 RootIndex = Allocations.EdgeRootIndex[EdgeIdx];
				const int32 IOIndex = SrcEdge.IOIndex;

				Graph->Edges[EdgeIdx].bValid = 0;

				int32 Prev = SrcEdge.Start;
				for (int32 k = Begin; k < End; k++)
				{
					const FPECollinear& R = Bucketed[k];
					const int32 Next = R.NodeIdx;

					if (Graph->InsertEdge_Unsafe(Prev, Next, NewEdge, IOIndex))
					{
						Graph->GetOrCreateNodeMetadata_Unsafe(Next).Type = EPCGExIntersectionType::PointEdge;
						FGraphEdgeMetadata& NewMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, RootIndex);
						NewMeta.Type = EPCGExIntersectionType::PointEdge;
						NewMeta.bIsSubEdge = true;
						if (Details.bSnapOnEdge)
						{
							Transforms[Graph->Nodes[Next].PointIndex].SetLocation(R.ClosestPoint);
						}
					}
					else if (FGraphEdgeMetadata* Existing = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
					{
						Existing->UnionSize++;
						Existing->bIsSubEdge = true;
					}

					Prev = Next;
				}

				if (Prev != SrcEdge.End)
				{
					if (Graph->InsertEdge_Unsafe(Prev, SrcEdge.End, NewEdge, IOIndex))
					{
						FGraphEdgeMetadata& NewMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, RootIndex);
						NewMeta.Type = EPCGExIntersectionType::PointEdge;
						NewMeta.bIsSubEdge = true;
					}
					else if (FGraphEdgeMetadata* Existing = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
					{
						Existing->UnionSize++;
						Existing->bIsSubEdge = true;
					}
				}
			}
		}

		void BlendIntersection(
			const FIntersectionAllocations& Allocations,
			PCGExBlending::FMetadataBlender* Blender,
			const FPECollinear& Record)
		{
			// TODO: finish proper P/E blend. Current behavior: blend the two endpoint attributes at 0.5,
			// then restore the pre-blend location.
			const FEdge& SrcEdge = Allocations.Graph->Edges[Record.EdgeIdx];
			const TPCGValueRange<FTransform> Transforms = Allocations.PointIO->GetOut()->GetTransformValueRange(false);

			const int32 TargetIndex = Allocations.Graph->Nodes[Record.NodeIdx].PointIndex;
			const FVector PreBlend = Transforms[TargetIndex].GetLocation();

			Blender->Blend(SrcEdge.Start, SrcEdge.End, TargetIndex, 0.5);

			Transforms[TargetIndex].SetLocation(PreBlend);
		}
	}

#pragma endregion

#pragma region EdgeEdgePass

	namespace EdgeEdgePass
	{
		// Compute crossing geometry between two valid edges. Returns false on miss/endpoint touch.
		static bool TryFindSplit(
			const FIntersectionAllocations& Allocations,
			const int32 EdgeAIdx,
			const int32 EdgeBIdx,
			FVector& OutCenter,
			double& OutTimeA,
			double& OutTimeB)
		{
			const FEdge& EdgeA = Allocations.Graph->Edges[EdgeAIdx];
			const FEdge& EdgeB = Allocations.Graph->Edges[EdgeBIdx];

			const FVector A0 = Allocations.Positions[EdgeA.Start];
			const FVector B0 = Allocations.Positions[EdgeA.End];
			const FVector A1 = Allocations.Positions[EdgeB.Start];
			const FVector B1 = Allocations.Positions[EdgeB.End];

			FVector PointOnA;
			FVector PointOnB;
			FMath::SegmentDistToSegment(A0, B0, A1, B1, PointOnA, PointOnB);

			if (FVector::DistSquared(PointOnA, PointOnB) >= Allocations.ToleranceSquared)
			{
				return false;
			}
			if (PointOnA == A0 || PointOnA == B0 || PointOnB == A1 || PointOnB == B1)
			{
				return false;
			}

			OutCenter = FMath::Lerp(PointOnA, PointOnB, 0.5);
			OutTimeA = FVector::DistSquared(A0, PointOnA) / Allocations.LengthSquared[EdgeAIdx];
			OutTimeB = FVector::DistSquared(A1, PointOnB) / Allocations.LengthSquared[EdgeBIdx];
			return true;
		}

		void Emit(
			FIntersectionAllocations& Allocations,
			const FPCGExEdgeEdgeIntersectionDetails& Details,
			const bool bEnableSelfIntersection,
			const PCGExMT::FScope& Scope,
			TArray<FEECrossing>& OutScopeRecords)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(EdgeEdgePass::Emit);

			const TSharedPtr<FGraph> Graph = Allocations.Graph;
			const TSharedPtr<PCGExOctree::FItemOctree>& Octree = Allocations.EdgeOctree;
			const TArray<FVector>& Directions = Allocations.Directions;

			PCGEX_SCOPE_LOOP(EdgeIdx)
			{
				if (!Allocations.ValidEdges[EdgeIdx])
				{
					continue;
				}

				const FEdge& Edge = Graph->Edges[EdgeIdx];
				const FBox& Box = Allocations.EdgeBoxes[EdgeIdx];

				// Pointer avoids default-constructing a TSet per edge when bEnableSelfIntersection
				// is true (const-ref lifetime extension forces a 48-byte zero-init per iteration).
				const TSet<int32>* RootIOIndices = nullptr;
				if (!bEnableSelfIntersection && Allocations.EdgeRootIOSetIdx.IsValidIndex(EdgeIdx))
				{
					const int32 SetIdx = Allocations.EdgeRootIOSetIdx[EdgeIdx];
					if (SetIdx >= 0)
					{
						RootIOIndices = &Allocations.UniqueRootIOSets[SetIdx];
					}
				}

				Octree->FindElementsWithBoundsTest(Box, [&](const PCGExOctree::FItem& Item)
				{
					const int32 OtherIdx = Item.Index;
					// Canonical ordering: emit each unordered pair exactly once.
					if (OtherIdx <= EdgeIdx)
					{
						return;
					}
					if (!Allocations.ValidEdges[OtherIdx])
					{
						return;
					}

					const FEdge& OtherEdge = Graph->Edges[OtherIdx];
					if (Edge.Start == OtherEdge.Start || Edge.Start == OtherEdge.End ||
						Edge.End == OtherEdge.Start || Edge.End == OtherEdge.End)
					{
						return;
					}

					if (Details.bUseMinAngle || Details.bUseMaxAngle)
					{
						if (!Details.CheckDot(FMath::Abs(FVector::DotProduct(Directions[EdgeIdx], Directions[OtherIdx]))))
						{
							return;
						}
					}

					if (RootIOIndices && !RootIOIndices->IsEmpty())
					{
						const int32 OtherSetIdx = Allocations.EdgeRootIOSetIdx[OtherIdx];
						if (OtherSetIdx >= 0)
						{
							const TSet<int32>& OtherIOs = Allocations.UniqueRootIOSets[OtherSetIdx];
							for (const int32 IO : OtherIOs)
							{
								if (RootIOIndices->Contains(IO))
								{
									return;
								}
							}
						}
					}

					FVector Center;
					double TimeA;
					double TimeB;
					if (!TryFindSplit(Allocations, EdgeIdx, OtherIdx, Center, TimeA, TimeB))
					{
						return;
					}

					FEECrossing& Rec = OutScopeRecords.Emplace_GetRef();
					Rec.EdgeA = EdgeIdx;
					Rec.EdgeB = OtherIdx;
					Rec.TimeA = TimeA;
					Rec.TimeB = TimeB;
					Rec.Center = Center;
				});
			}
		}

		int32 ResolveCrossings(
			FIntersectionAllocations& Allocations,
			TArray<FEECrossing>& Records)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(EdgeEdgePass::ResolveCrossings);

			if (Records.IsEmpty())
			{
				return 0;
			}

			// Canonical-pair filter at emit time guarantees one record per unordered pair, so we
			// don't need a dedup pass. Sort by packed pair key for deterministic node-allocation
			// order; single-uint64 compare keeps the comparator branch-free.
			Records.Sort([](const FEECrossing& A, const FEECrossing& B)
			{
				return A.Key() < B.Key();
			});

			const TSharedPtr<FGraph> Graph = Allocations.Graph;
			const double ToleranceSq = Allocations.ToleranceSquared;
			const double ToleranceVal = Allocations.Tolerance;
			const TSharedPtr<PCGExOctree::FItemOctree>& Octree = Allocations.EdgeOctree;
			const TArray<FVector>& Positions = Allocations.Positions;

			int32 NewlyAllocated = 0;
			const int32 BaseNewNodeIndex = Graph->Nodes.Num();

			// Single pass: every record is its own primary. Try to reuse an existing node within
			// tolerance (a) from the four endpoints of the two crossing edges, then (b) from any
			// node owned by a nearby valid edge via the octree. Else allocate a fresh node index.
			for (FEECrossing& Rec : Records)
			{
				Rec.bIsPrimary = true;

				const FEdge& EdgeA = Graph->Edges[Rec.EdgeA];
				const FEdge& EdgeB = Graph->Edges[Rec.EdgeB];

				int32 Reused = INDEX_NONE;
				const FVector& Center = Rec.Center;

				for (const int32 Endpoint : {static_cast<int32>(EdgeA.Start), static_cast<int32>(EdgeA.End), static_cast<int32>(EdgeB.Start), static_cast<int32>(EdgeB.End)})
				{
					if (FVector::DistSquared(Positions[Endpoint], Center) < ToleranceSq)
					{
						Reused = Endpoint;
						break;
					}
				}

				if (Reused == INDEX_NONE && Octree)
				{
					Octree->FindElementsWithBoundsTest(PCGEX_BOX_TOLERANCE_INLINE(Center, Center, ToleranceVal),
					                                   [&](const PCGExOctree::FItem& Item)
					                                   {
						                                   if (Reused != INDEX_NONE || !Allocations.ValidEdges[Item.Index])
						                                   {
							                                   return;
						                                   }
						                                   const FEdge& Near = Graph->Edges[Item.Index];
						                                   if (FVector::DistSquared(Positions[Near.Start], Center) < ToleranceSq)
						                                   {
							                                   Reused = Near.Start;
						                                   }
						                                   else if (FVector::DistSquared(Positions[Near.End], Center) < ToleranceSq)
						                                   {
							                                   Reused = Near.End;
						                                   }
					                                   });
				}

				if (Reused != INDEX_NONE)
				{
					Rec.ResolvedNodeIdx = Reused;
					Rec.bAllocatedNewNode = false;
				}
				else
				{
					Rec.ResolvedNodeIdx = BaseNewNodeIndex + NewlyAllocated;
					Rec.bAllocatedNewNode = true;
					NewlyAllocated++;
				}
			}

			// Allocate the new graph nodes in one batch + size point data + reserve metadata entries.
			if (NewlyAllocated > 0)
			{
				int32 StartIdx = INDEX_NONE;
				Graph->AddNodes(NewlyAllocated, StartIdx);
				check(StartIdx == BaseNewNodeIndex);

				UPCGBasePointData* MutablePoints = Allocations.PointIO->GetOut();
				const int32 NumPoints = Graph->Nodes.Num();
				MutablePoints->SetNumPoints(NumPoints);

				UPCGMetadata* Metadata = Allocations.PointIO->GetOut()->Metadata;
				TPCGValueRange<int64> MetadataEntries = MutablePoints->GetMetadataEntryValueRange(false);

				TArray<TTuple<int64, int64>> DelayedEntries;
				DelayedEntries.SetNum(NewlyAllocated);
				int32 W = 0;
				for (int32 i = StartIdx; i < NumPoints; i++)
				{
					MetadataEntries[i] = Metadata->AddEntryPlaceholder();
					DelayedEntries[W++] = MakeTuple(MetadataEntries[i], PCGInvalidEntryKey);
				}
				Metadata->AddDelayedEntries(DelayedEntries);
			}

			return NewlyAllocated;
		}

		void Apply(
			FIntersectionAllocations& Allocations,
			TArray<FEECrossing>& Records)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(EdgeEdgePass::Apply);

			if (Records.IsEmpty())
			{
				return;
			}

			// Each crossing splits both EdgeA and EdgeB, so we need 2 (Time, NodeIdx) entries per
			// record bucketed by edge. Build a CSR layout (Offsets[NumEdges+1], flat Splits) so
			// per-edge sort can run in parallel via ParallelFor -- matching the legacy
			// `ParallelFor(Edges) Crossings.Sort(...)` shape, which is much cheaper than a single
			// global O(N log N) sort when typical per-edge crossing count is small.
			struct FEdgeSplit
			{
				double Time;
				int32 NodeIdx;
			};

			const TSharedPtr<FGraph> Graph = Allocations.Graph;
			const int32 NumEdges = Graph->Edges.Num();
			const int32 NumPrimaries = Records.Num();

			// Pass 1: per-edge counts (each primary contributes to 2 edges).
			TArray<int32> EdgeOffsets;
			EdgeOffsets.SetNumZeroed(NumEdges + 1);
			for (const FEECrossing& Rec : Records)
			{
				EdgeOffsets[Rec.EdgeA + 1]++;
				EdgeOffsets[Rec.EdgeB + 1]++;
			}

			// Prefix sum.
			for (int32 i = 1; i <= NumEdges; i++)
			{
				EdgeOffsets[i] += EdgeOffsets[i - 1];
			}
			const int32 TotalSplits = EdgeOffsets[NumEdges];

			// Collect only edges that actually have crossings before spawning parallel tasks.
			// Each crossing touches 2 edges, so at most 2*NumPrimaries edges have non-empty buckets.
			TArray<int32> NonEmptyBuckets;
			NonEmptyBuckets.Reserve(FMath::Min(NumEdges, 2 * NumPrimaries));
			for (int32 i = 0; i < NumEdges; i++)
			{
				if (EdgeOffsets[i + 1] > EdgeOffsets[i])
				{
					NonEmptyBuckets.Add(i);
				}
			}

			TArray<FEdgeSplit> Splits;
			Splits.SetNumUninitialized(TotalSplits);

			// Pass 2: distribute. Cursor tracks per-edge write position.
			TArray<int32> Cursor = EdgeOffsets; // copy
			for (const FEECrossing& Rec : Records)
			{
				Splits[Cursor[Rec.EdgeA]++] = {Rec.TimeA, Rec.ResolvedNodeIdx};
				Splits[Cursor[Rec.EdgeB]++] = {Rec.TimeB, Rec.ResolvedNodeIdx};
			}

			// Pass 3: parallel per-edge sort by Time. Each bucket is independent.
			// Time is a DistSquared ratio so ties are effectively impossible -- Sort is sufficient.
			ParallelFor(NonEmptyBuckets.Num(), [&](const int32 i)
			{
				const int32 EdgeIdx = NonEmptyBuckets[i];
				const int32 Begin = EdgeOffsets[EdgeIdx];
				const int32 End = EdgeOffsets[EdgeIdx + 1];
				if (End - Begin <= 1)
				{
					return;
				}
				TArrayView<FEdgeSplit> Slice = MakeArrayView(Splits.GetData() + Begin, End - Begin);
				Slice.Sort([](const FEdgeSplit& A, const FEdgeSplit& B)
				{
					return A.Time < B.Time;
				});
			});

			// Pass 4: sequential subdivision (graph mutations).
			Graph->ReserveForEdges(TotalSplits + Graph->Edges.Num());

			FEdge NewEdge = FEdge{};
			for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; EdgeIdx++)
			{
				const int32 Begin = EdgeOffsets[EdgeIdx];
				const int32 End = EdgeOffsets[EdgeIdx + 1];
				if (Begin == End)
				{
					continue;
				}

				const FEdge& SrcEdge = Graph->Edges[EdgeIdx];
				const int32 RootIndex = Allocations.EdgeRootIndex[EdgeIdx];
				const int32 IOIndex = SrcEdge.IOIndex;

				Graph->Edges[EdgeIdx].bValid = 0;

				int32 Prev = SrcEdge.Start;
				for (int32 k = Begin; k < End; k++)
				{
					const int32 Next = Splits[k].NodeIdx;
					if (Next == Prev)
					{
						continue;
					}

					if (Graph->InsertEdge_Unsafe(Prev, Next, NewEdge, IOIndex))
					{
						Graph->GetOrCreateNodeMetadata_Unsafe(Next).Type = EPCGExIntersectionType::EdgeEdge;
						FGraphEdgeMetadata& NewMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, RootIndex);
						NewMeta.Type = EPCGExIntersectionType::EdgeEdge;
						NewMeta.bIsSubEdge = true;
					}
					else if (FGraphEdgeMetadata* Existing = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
					{
						Existing->UnionSize++;
						Existing->bIsSubEdge = true;
					}

					Prev = Next;
				}

				if (Prev != SrcEdge.End)
				{
					if (Graph->InsertEdge_Unsafe(Prev, SrcEdge.End, NewEdge, IOIndex))
					{
						FGraphEdgeMetadata& NewMeta = Graph->GetOrCreateEdgeMetadata_Unsafe(NewEdge.Index, RootIndex);
						NewMeta.Type = EPCGExIntersectionType::EdgeEdge;
						NewMeta.bIsSubEdge = true;
					}
					else if (FGraphEdgeMetadata* Existing = Graph->FindEdgeMetadata_Unsafe(NewEdge.Index))
					{
						Existing->UnionSize++;
						Existing->bIsSubEdge = true;
					}
				}
			}
		}

		void BlendIntersection(
			const FIntersectionAllocations& Allocations,
			const TSharedRef<PCGExBlending::FMetadataBlender>& Blender,
			const FEECrossing& Crossing,
			TArray<PCGEx::FOpStats>& Trackers)
		{
			const TSharedPtr<FGraph> Graph = Allocations.Graph;
			const int32 Target = Graph->Nodes[Crossing.ResolvedNodeIdx].PointIndex;

			Blender->BeginMultiBlend(Target, Trackers);

			const int32 A1 = Graph->Nodes[Graph->Edges[Crossing.EdgeA].Start].PointIndex;
			const int32 A2 = Graph->Nodes[Graph->Edges[Crossing.EdgeA].End].PointIndex;
			const int32 B1 = Graph->Nodes[Graph->Edges[Crossing.EdgeB].Start].PointIndex;
			const int32 B2 = Graph->Nodes[Graph->Edges[Crossing.EdgeB].End].PointIndex;

			Blender->MultiBlend(A1, Target, Crossing.TimeA, Trackers);
			Blender->MultiBlend(A2, Target, 1 - Crossing.TimeA, Trackers);
			Blender->MultiBlend(B1, Target, Crossing.TimeB, Trackers);
			Blender->MultiBlend(B2, Target, 1 - Crossing.TimeB, Trackers);

			Blender->EndMultiBlend(Target, Trackers);

			Allocations.PointIO->GetOutPoint(Target).SetLocation(Crossing.Center);
		}
	}

#pragma endregion
}
