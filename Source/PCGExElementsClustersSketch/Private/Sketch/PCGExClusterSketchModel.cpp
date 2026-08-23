// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchModel.h"

#include "PCGExH.h"
#include "Sketch/PCGExClusterSketchConstraint.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Helpers/PCGExMetaHelpers.h"

namespace PCGExClusterSketchModel
{
	/** Stable identity of an edge while enforcement runs: indices shift on every split, but vertex
	 *  indices do not (enforcement only appends vertices). */
	FORCEINLINE uint64 EdgeKey(const FPCGExClusterSketchEdge& E)
	{
		return PCGEx::H64U(static_cast<uint32>(E.A), static_cast<uint32>(E.B));
	}

	/** Insert a crossing vertex at Point (side-effect provenance) and split both edges through it.
	 *  Higher edge index splits first so the lower index survives the RemoveAt.
	 *  OutChainA/B receive each edge's resulting segment keys, so a caller tracking gesture scope can
	 *  follow its edge through the cut. */
	int32 InsertCrossingVertex(FPCGExClusterSketchModel& Model, const int32 EdgeA, const int32 EdgeB, const FVector& Point, const FPCGExLatticeBasis* Basis,
	                           TArray<uint64>* OutChainA = nullptr, TArray<uint64>* OutChainB = nullptr)
	{
		const int32 NewVertex = Model.AddVertex(FTransform(Point));
#if WITH_EDITORONLY_DATA
		Model.Vertices[NewVertex].bSideEffect = true;
#endif
		if (EdgeA > EdgeB)
		{
			Model.SplitEdgeByContainedVertices(EdgeA, Basis, OutChainA);
			Model.SplitEdgeByContainedVertices(EdgeB, Basis, OutChainB);
		}
		else
		{
			Model.SplitEdgeByContainedVertices(EdgeB, Basis, OutChainB);
			Model.SplitEdgeByContainedVertices(EdgeA, Basis, OutChainA);
		}
		// A third edge concurrent at this point would still run through the new vertex: same separation
		// rule every other vertex-inserting path applies. Chain keys are endpoint pairs, so the extra
		// splits cannot invalidate them.
		Model.EnforceSeparationAroundVertex(NewVertex, Basis);
		return NewVertex;
	}

	/** Interior test: on the segment within the coincidence tolerance, strictly between the endpoints
	 *  (endpoint-coincident is the collocation domain, not overlap). @return the segment parameter, or
	 *  a negative value when not interior. */
	double SegmentInteriorParameter(const FVector& P, const FVector& A, const FVector& B)
	{
		constexpr double TolSq = PCGExSketch::CoincidenceTolerance * PCGExSketch::CoincidenceTolerance;
		if (FVector::DistSquared(P, A) <= TolSq || FVector::DistSquared(P, B) <= TolSq)
		{
			return -1.0;
		}
		const FVector AB = B - A;
		const double LengthSq = AB.SizeSquared();
		if (LengthSq <= TolSq)
		{
			return -1.0;
		}
		const double T = FVector::DotProduct(P - A, AB) / LengthSq;
		if (T <= 0.0 || T >= 1.0)
		{
			return -1.0;
		}
		return FVector::DistSquared(A + AB * T, P) <= TolSq ? T : -1.0;
	}
}

namespace PCGExSketch
{
	bool SegmentsCross(const FVector& A1, const FVector& B1, const FVector& A2, const FVector& B2, FVector& OutPoint)
	{
		constexpr double TolSq = CoincidenceTolerance * CoincidenceTolerance;
		FVector P1, P2;
		FMath::SegmentDistToSegmentSafe(A1, B1, A2, B2, P1, P2);
		if (FVector::DistSquared(P1, P2) > TolSq)
		{
			return false;
		}
		// Strictly interior on BOTH segments: endpoint contact is the vertex-on-edge / collocation domain.
		if (FVector::DistSquared(P1, A1) <= TolSq || FVector::DistSquared(P1, B1) <= TolSq ||
			FVector::DistSquared(P2, A2) <= TolSq || FVector::DistSquared(P2, B2) <= TolSq)
		{
			return false;
		}
		OutPoint = (P1 + P2) * 0.5;
		return true;
	}
}

#pragma region FPCGExClusterSketchModel

FVector FPCGExClusterSketchModel::ResolvedLocation(const FPCGExClusterSketchVertex& V, const FPCGExLatticeBasis* Basis)
{
	return (V.bLatticeBound && Basis) ? Basis->CoordToWorld(V.LatticeCoord) : V.Transform.GetLocation();
}

FBox FPCGExClusterSketchModel::GetBounds(const FPCGExLatticeBasis* Basis) const
{
	FBox Bounds(ForceInit);
	for (const FPCGExClusterSketchVertex& V : Vertices)
	{
		Bounds += ResolvedLocation(V, Basis);
	}
	return Bounds;
}

uint32 FPCGExClusterSketchModel::MintElementId()
{
	// Wrapping back to the invalid value is unreachable in practice; guarded so it can never alias.
	if (NextElementId == PCGExSketch::InvalidElementId)
	{
		++NextElementId;
	}
	return NextElementId++;
}

int32 FPCGExClusterSketchModel::FindVertexIndex(const uint32 InId) const
{
	if (InId == PCGExSketch::InvalidElementId)
	{
		return INDEX_NONE;
	}
	return Vertices.IndexOfByPredicate([InId](const FPCGExClusterSketchVertex& V) { return V.Id == InId; });
}

int32 FPCGExClusterSketchModel::FindEdgeIndex(const uint32 InId) const
{
	if (InId == PCGExSketch::InvalidElementId)
	{
		return INDEX_NONE;
	}
	return Edges.IndexOfByPredicate([InId](const FPCGExClusterSketchEdge& E) { return E.Id == InId; });
}

int32 FPCGExClusterSketchModel::RepairElementIds()
{
	// Counter first: it must clear every id already in use before any re-mint draws from it.
	TSet<uint32> Seen;
	Seen.Reserve(Vertices.Num() + Edges.Num());
	auto Scan = [&](const uint32 Id)
	{
		if (Id != PCGExSketch::InvalidElementId && Id >= NextElementId)
		{
			NextElementId = Id + 1;
		}
	};
	for (const FPCGExClusterSketchVertex& V : Vertices) { Scan(V.Id); }
	for (const FPCGExClusterSketchEdge& E : Edges) { Scan(E.Id); }
#if WITH_EDITORONLY_DATA
	for (const FInstancedStruct& Entry : Constraints)
	{
		if (const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>()) { Scan(C->Id); }
	}
#endif

	int32 NumRepaired = 0;
	auto Repair = [&](uint32& Id)
	{
		bool bAlreadySeen = false;
		if (Id != PCGExSketch::InvalidElementId)
		{
			Seen.Add(Id, &bAlreadySeen);
		}
		if (Id == PCGExSketch::InvalidElementId || bAlreadySeen)
		{
			Id = MintElementId();
			Seen.Add(Id);
			++NumRepaired;
		}
	};
	for (FPCGExClusterSketchVertex& V : Vertices) { Repair(V.Id); }
	for (FPCGExClusterSketchEdge& E : Edges) { Repair(E.Id); }
#if WITH_EDITORONLY_DATA
	for (FInstancedStruct& Entry : Constraints)
	{
		if (FPCGExSketchConstraint* C = Entry.GetMutablePtr<FPCGExSketchConstraint>()) { Repair(C->Id); }
	}
#endif
	return NumRepaired;
}

int32 FPCGExClusterSketchModel::AddVertex(const FTransform& InTransform, const uint32 InDataId)
{
	const int32 Index = Vertices.Num();
	FPCGExClusterSketchVertex& V = Vertices.AddDefaulted_GetRef();
	V.Id = MintElementId();
	V.Transform = InTransform;
	V.DataId = InDataId;
	return Index;
}

int32 FPCGExClusterSketchModel::AddLatticeVertex(const FIntVector& InCoord, const FPCGExLatticeBasis& InBasis, const uint32 InDataId)
{
	const int32 Index = AddVertex(FTransform(InBasis.CoordToWorld(InCoord)), InDataId);
	FPCGExClusterSketchVertex& V = Vertices[Index];
	V.bLatticeBound = true;
	V.LatticeCoord = InCoord;
	return Index;
}

bool FPCGExClusterSketchModel::RemoveVertex(const int32 Index)
{
	if (!Vertices.IsValidIndex(Index))
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	TArray<uint32> RemovedIds;
	RemovedIds.Add(Vertices[Index].Id);
#endif

	// Drop touching edges (descending, so earlier indices stay valid), then remap the survivors.
	for (int32 e = Edges.Num() - 1; e >= 0; --e)
	{
		if (Edges[e].A == Index || Edges[e].B == Index)
		{
#if WITH_EDITORONLY_DATA
			RemovedIds.Add(Edges[e].Id);
#endif
			Edges.RemoveAt(e);
		}
	}
	for (FPCGExClusterSketchEdge& E : Edges)
	{
		if (E.A > Index)
		{
			--E.A;
		}
		if (E.B > Index)
		{
			--E.B;
		}
	}

	Vertices.RemoveAt(Index);
#if WITH_EDITORONLY_DATA
	OnElementsRemoved(RemovedIds);
#endif
	return true;
}

int32 FPCGExClusterSketchModel::Connect(const int32 A, const int32 B, bool* bOutCreated)
{
	if (bOutCreated)
	{
		*bOutCreated = false;
	}
	if (A == B || !Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B))
	{
		return INDEX_NONE;
	}

	const int32 Existing = FindEdge(A, B);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	if (bOutCreated)
	{
		*bOutCreated = true;
	}
	const int32 Index = Edges.Num();
	FPCGExClusterSketchEdge& E = Edges.AddDefaulted_GetRef();
	E.Id = MintElementId();
	E.A = A;
	E.B = B;
	return Index;
}

bool FPCGExClusterSketchModel::Disconnect(const int32 A, const int32 B)
{
	return RemoveEdgeAt(FindEdge(A, B));
}

bool FPCGExClusterSketchModel::RemoveEdgeAt(const int32 EdgeIndex)
{
	if (!Edges.IsValidIndex(EdgeIndex))
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	const uint32 RemovedId = Edges[EdgeIndex].Id;
#endif
	Edges.RemoveAt(EdgeIndex);
#if WITH_EDITORONLY_DATA
	OnElementsRemoved(MakeArrayView(&RemovedId, 1));
#endif
	return true;
}

int32 FPCGExClusterSketchModel::MergeVertices(const int32 InAbsorbed, const int32 InSurvivor)
{
	if (InAbsorbed == InSurvivor || !Vertices.IsValidIndex(InAbsorbed) || !Vertices.IsValidIndex(InSurvivor))
	{
		return INDEX_NONE;
	}

#if WITH_EDITORONLY_DATA
	// Absorbed subject slots retarget to the survivor; a constraint that would then name the survivor
	// twice has lost its meaning and goes. Done BEFORE the edge pass so dropped edges clean up after.
	{
		const uint32 AbsorbedId = Vertices[InAbsorbed].Id;
		const uint32 SurvivorId = Vertices[InSurvivor].Id;
		for (int32 c = Constraints.Num() - 1; c >= 0; --c)
		{
			FPCGExSketchConstraint* C = Constraints[c].GetMutablePtr<FPCGExSketchConstraint>();
			if (!C || !C->Subjects.Contains(AbsorbedId))
			{
				continue;
			}
			if (C->Subjects.Contains(SurvivorId))
			{
				Constraints.RemoveAt(c);
				continue;
			}
			for (uint32& Subject : C->Subjects)
			{
				if (Subject == AbsorbedId)
				{
					Subject = SurvivorId;
				}
			}
		}
	}
	TArray<uint32> RemovedEdgeIds;
#endif

	// Descending, in place: FindEdge sees already-retargeted edges, so two absorbed edges toward the
	// same far vertex cannot both retarget and mint the duplicate this merge exists to prevent.
	for (int32 e = Edges.Num() - 1; e >= 0; --e)
	{
		FPCGExClusterSketchEdge& E = Edges[e];
		const bool bTouchA = E.A == InAbsorbed;
		const bool bTouchB = E.B == InAbsorbed;
		if (!bTouchA && !bTouchB)
		{
			continue;
		}

		bool bDrop = bTouchA && bTouchB; // self-loop remnant from a raw edit
		if (!bDrop)
		{
			const int32 Other = bTouchA ? E.B : E.A;
			if (Other == InSurvivor || FindEdge(Other, InSurvivor) != INDEX_NONE)
			{
				bDrop = true; // would become a self-loop / a duplicate of a surviving edge
			}
			else
			{
				(bTouchA ? E.A : E.B) = InSurvivor;
				continue;
			}
		}

#if WITH_EDITORONLY_DATA
		RemovedEdgeIds.Add(E.Id);
#endif
		Edges.RemoveAt(e);
	}
#if WITH_EDITORONLY_DATA
	OnElementsRemoved(RemovedEdgeIds);
#endif

	// Survivor inherits only when it holds none: records are sparse, dropping the annotation is the worse failure.
	if (Vertices[InSurvivor].DataId == PCGExSketch::InvalidRecordId)
	{
		Vertices[InSurvivor].DataId = Vertices[InAbsorbed].DataId;
	}

#if WITH_EDITORONLY_DATA
	// Authorship unions: the survivor is tool residue only if BOTH sides were.
	Vertices[InSurvivor].bSideEffect &= Vertices[InAbsorbed].bSideEffect;
#endif

	const int32 SurvivorAfterRemoval = InSurvivor > InAbsorbed ? InSurvivor - 1 : InSurvivor;
	RemoveVertex(InAbsorbed); // no edges touch it anymore -- pure vertex removal + index remap
	return SurvivorAfterRemoval;
}

int32 FPCGExClusterSketchModel::FindEdge(const int32 A, const int32 B) const
{
	for (int32 e = 0; e < Edges.Num(); ++e)
	{
		const FPCGExClusterSketchEdge& E = Edges[e];
		if ((E.A == A && E.B == B) || (E.A == B && E.B == A))
		{
			return e;
		}
	}
	return INDEX_NONE;
}

bool FPCGExClusterSketchModel::SetLatticeBound(const int32 Index, const bool bBound, const FPCGExLatticeBasis& InBasis)
{
	if (!Vertices.IsValidIndex(Index))
	{
		return false;
	}

	FPCGExClusterSketchVertex& V = Vertices[Index];
	if (V.bLatticeBound == bBound)
	{
		return true;
	}

	V.bLatticeBound = bBound;
	if (bBound)
	{
		// Preserving: re-binding restores whatever unspanned components the coord still stashes.
		V.LatticeCoord = InBasis.SnapWorldToCoordPreserving(V.Transform.GetLocation(), V.LatticeCoord);
		V.Transform.SetLocation(InBasis.CoordToWorld(V.LatticeCoord));
	}
	// Unbinding keeps the current derived location as the free position -- nothing to do.
	return true;
}

void FPCGExClusterSketchModel::SyncBoundVertices(const FPCGExLatticeBasis& InBasis, const bool bResnapFromLocation)
{
	for (FPCGExClusterSketchVertex& V : Vertices)
	{
		if (!V.bLatticeBound)
		{
			continue;
		}
		if (bResnapFromLocation)
		{
			// Preserving, or a rank-collapsed basis would wipe the stashed components on EVERY model
			// edit (locations carry no information along unspanned directions to re-snap from).
			V.LatticeCoord = InBasis.SnapWorldToCoordPreserving(V.Transform.GetLocation(), V.LatticeCoord);
		}
		V.Transform.SetLocation(InBasis.CoordToWorld(V.LatticeCoord));
	}
}

int32 FPCGExClusterSketchModel::FindVertexOnEdgeInterior(const int32 EdgeIndex, const TConstArrayView<FVector> InLocations) const
{
	if (!Edges.IsValidIndex(EdgeIndex))
	{
		return INDEX_NONE;
	}
	const FPCGExClusterSketchEdge& E = Edges[EdgeIndex];
	if (!InLocations.IsValidIndex(E.A) || !InLocations.IsValidIndex(E.B))
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < InLocations.Num(); ++i)
	{
		if (i == E.A || i == E.B)
		{
			continue;
		}
		if (PCGExClusterSketchModel::SegmentInteriorParameter(InLocations[i], InLocations[E.A], InLocations[E.B]) >= 0.0)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FPCGExClusterSketchModel::FindVertexOnEdgeInterior(const int32 EdgeIndex, const FPCGExLatticeBasis* Basis) const
{
	if (!Edges.IsValidIndex(EdgeIndex))
	{
		return INDEX_NONE;
	}
	const FPCGExClusterSketchEdge& E = Edges[EdgeIndex];
	if (!Vertices.IsValidIndex(E.A) || !Vertices.IsValidIndex(E.B))
	{
		return INDEX_NONE;
	}
	const FVector A = ResolvedLocation(Vertices[E.A], Basis);
	const FVector B = ResolvedLocation(Vertices[E.B], Basis);
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		if (i == E.A || i == E.B)
		{
			continue;
		}
		if (PCGExClusterSketchModel::SegmentInteriorParameter(ResolvedLocation(Vertices[i], Basis), A, B) >= 0.0)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FPCGExClusterSketchModel::SplitEdgeByContainedVertices(const int32 EdgeIndex, const FPCGExLatticeBasis* Basis, TArray<uint64>* OutSegmentKeys)
{
	if (!Edges.IsValidIndex(EdgeIndex))
	{
		return 0;
	}
	const FPCGExClusterSketchEdge Edge = Edges[EdgeIndex];
	if (!Vertices.IsValidIndex(Edge.A) || !Vertices.IsValidIndex(Edge.B))
	{
		return 0;
	}

	const FVector A = ResolvedLocation(Vertices[Edge.A], Basis);
	const FVector B = ResolvedLocation(Vertices[Edge.B], Basis);

	TArray<TPair<double, int32>> Contained;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		if (i == Edge.A || i == Edge.B)
		{
			continue;
		}
		const double T = PCGExClusterSketchModel::SegmentInteriorParameter(ResolvedLocation(Vertices[i], Basis), A, B);
		if (T >= 0.0)
		{
			Contained.Emplace(T, i);
		}
	}
	if (Contained.IsEmpty())
	{
		return 0;
	}
	Contained.Sort([](const TPair<double, int32>& Lhs, const TPair<double, int32>& Rhs)
	{
		return Lhs.Key < Rhs.Key;
	});

	// Only newly created segments take the parent's record; one deduped onto existing connectivity keeps its own.
	const uint32 ParentDataId = Edge.DataId;

	Edges.RemoveAt(EdgeIndex);
#if WITH_EDITORONLY_DATA
	// A constraint on the parent (a Length, say) is undefined over a chain: it goes with the edge.
	OnElementsRemoved(MakeArrayView(&Edge.Id, 1));
#endif

	int32 NumSegments = 0;
	int32 Prev = Edge.A;
	auto Link = [&](const int32 From, const int32 To)
	{
		bool bCreated = false;
		const int32 NewIndex = Connect(From, To, &bCreated);
		if (bCreated)
		{
			Edges[NewIndex].DataId = ParentDataId;
		}
		if (NewIndex != INDEX_NONE)
		{
			++NumSegments;
			if (OutSegmentKeys)
			{
				// Deduped edges report too: the chain occupies that geometry either way.
				OutSegmentKeys->Add(PCGExClusterSketchModel::EdgeKey(Edges[NewIndex]));
			}
		}
	};
	for (const TPair<double, int32>& Entry : Contained)
	{
		Link(Prev, Entry.Value);
		Prev = Entry.Value;
	}
	Link(Prev, Edge.B);
	return NumSegments;
}

int32 FPCGExClusterSketchModel::EnforceSeparationAroundVertex(const int32 VertexIndex, const FPCGExLatticeBasis* Basis)
{
	if (!Vertices.IsValidIndex(VertexIndex))
	{
		return 0;
	}

	// Gesture scope: the vertex's incident edges AND every segment they are later split into. Cutting an
	// edge leaves only one half incident to the vertex, so an incidence test abandons the far half and
	// resolves some crossings while merely flagging others.
	TSet<uint64> Touched;
	Touched.Reserve(Edges.Num());
	for (const FPCGExClusterSketchEdge& E : Edges)
	{
		if ((E.A == VertexIndex || E.B == VertexIndex) && Vertices.IsValidIndex(E.A) && Vertices.IsValidIndex(E.B))
		{
			Touched.Add(PCGExClusterSketchModel::EdgeKey(E));
		}
	}

	// Restart-scan to a fixed point: every split/insertion invalidates edge indices, and chain-split
	// output never contains interior vertices, so the degeneracy count strictly decreases. The guard is
	// a belt-and-braces bound far above any real model.
	int32 TotalChanges = 0;
	int32 Guard = (Edges.Num() + Vertices.Num()) * 4 + 64;
	bool bChanged = true;
	TArray<uint64> ChainKeys;
	while (bChanged && Guard-- > 0)
	{
		bChanged = false;

		// Vertex-on-edge splits first: a touched edge passing through any vertex, or any edge passing
		// through the gesture's own vertex.
		for (int32 e = 0; e < Edges.Num(); ++e)
		{
			const int32 EA = Edges[e].A;
			const int32 EB = Edges[e].B;
			if (!Vertices.IsValidIndex(EA) || !Vertices.IsValidIndex(EB))
			{
				continue;
			}

			bool bAffected = Touched.Contains(PCGExClusterSketchModel::EdgeKey(Edges[e])) && FindVertexOnEdgeInterior(e, Basis) != INDEX_NONE;
			if (!bAffected && EA != VertexIndex && EB != VertexIndex)
			{
				const FVector A = ResolvedLocation(Vertices[EA], Basis);
				const FVector B = ResolvedLocation(Vertices[EB], Basis);
				bAffected = PCGExClusterSketchModel::SegmentInteriorParameter(ResolvedLocation(Vertices[VertexIndex], Basis), A, B) >= 0.0;
			}

			if (bAffected)
			{
				ChainKeys.Reset();
				TotalChanges += SplitEdgeByContainedVertices(e, Basis, &ChainKeys);
				for (const uint64 Key : ChainKeys)
				{
					Touched.Add(Key);
				} // the chain inherits the parent's scope
				bChanged = true;
				break;
			}
		}
	}
	return TotalChanges;
}

int32 FPCGExClusterSketchModel::SplitAllOverlappingEdges(const FPCGExLatticeBasis* Basis)
{
	int32 TotalSplits = 0;
	// RemoveAt shifts the next edge into the split slot, so stay on the index after a split; freshly
	// appended chain segments carry no interior vertices and pass the scan when it reaches them.
	for (int32 e = 0; e < Edges.Num();)
	{
		if (FindVertexOnEdgeInterior(e, Basis) != INDEX_NONE)
		{
			TotalSplits += SplitEdgeByContainedVertices(e, Basis);
		}
		else
		{
			++e;
		}
	}
	return TotalSplits;
}

void FPCGExClusterSketchModel::FindEdgeCrossings(TArray<FPCGExClusterSketchCrossing>& OutCrossings, const FPCGExLatticeBasis* Basis) const
{
	OutCrossings.Reset();
	for (int32 e1 = 0; e1 < Edges.Num(); ++e1)
	{
		const FPCGExClusterSketchEdge& E1 = Edges[e1];
		if (!Vertices.IsValidIndex(E1.A) || !Vertices.IsValidIndex(E1.B))
		{
			continue;
		}
		const FVector A1 = ResolvedLocation(Vertices[E1.A], Basis);
		const FVector B1 = ResolvedLocation(Vertices[E1.B], Basis);
		for (int32 e2 = e1 + 1; e2 < Edges.Num(); ++e2)
		{
			const FPCGExClusterSketchEdge& E2 = Edges[e2];
			if (E2.A == E1.A || E2.A == E1.B || E2.B == E1.A || E2.B == E1.B ||
				!Vertices.IsValidIndex(E2.A) || !Vertices.IsValidIndex(E2.B))
			{
				continue;
			}
			FVector CrossingPoint;
			if (PCGExSketch::SegmentsCross(A1, B1, ResolvedLocation(Vertices[E2.A], Basis), ResolvedLocation(Vertices[E2.B], Basis), CrossingPoint))
			{
				FPCGExClusterSketchCrossing& Crossing = OutCrossings.AddDefaulted_GetRef();
				Crossing.EdgeA = e1;
				Crossing.EdgeB = e2;
				Crossing.Location = CrossingPoint;
			}
		}
	}
}

int32 FPCGExClusterSketchModel::MaterializeCrossing(const int32 EdgeA, const int32 EdgeB, const FVector& Location, const FPCGExLatticeBasis* Basis)
{
	if (EdgeA == EdgeB || !Edges.IsValidIndex(EdgeA) || !Edges.IsValidIndex(EdgeB))
	{
		return INDEX_NONE;
	}

	const FPCGExClusterSketchEdge& EA = Edges[EdgeA];
	const FPCGExClusterSketchEdge& EB = Edges[EdgeB];
	if (!Vertices.IsValidIndex(EA.A) || !Vertices.IsValidIndex(EA.B) || !Vertices.IsValidIndex(EB.A) || !Vertices.IsValidIndex(EB.B))
	{
		return INDEX_NONE;
	}

	// A cached crossing goes stale whenever geometry moves under it (an undo, a details-panel edit)
	// while both indices stay in range -- re-derive it, or the vertex is added and neither split fires.
	FVector Point = Location;
	if (!PCGExSketch::SegmentsCross(
		ResolvedLocation(Vertices[EA.A], Basis), ResolvedLocation(Vertices[EA.B], Basis),
		ResolvedLocation(Vertices[EB.A], Basis), ResolvedLocation(Vertices[EB.B], Basis), Point))
	{
		return INDEX_NONE;
	}

	return PCGExClusterSketchModel::InsertCrossingVertex(*this, EdgeA, EdgeB, Point, Basis);
}

int32 FPCGExClusterSketchModel::InsertCrossingVertices(const FPCGExLatticeBasis* Basis)
{
	int32 Inserted = 0;
	int32 Guard = Edges.Num() * Edges.Num() + 64;
	TArray<FPCGExClusterSketchCrossing> Crossings;
	while (Guard-- > 0)
	{
		// Rescan every round: materializing one crossing splits two edges, shifting every index after
		// them -- and the two new chains can cross things the originals did not reach.
		FindEdgeCrossings(Crossings, Basis);
		if (Crossings.IsEmpty())
		{
			break;
		}
		if (MaterializeCrossing(Crossings[0].EdgeA, Crossings[0].EdgeB, Crossings[0].Location, Basis) == INDEX_NONE)
		{
			break;
		}
		++Inserted;
	}
	return Inserted;
}

#if WITH_EDITORONLY_DATA
int32 FPCGExClusterSketchModel::RemoveOrphanSideEffectVertices()
{
	TArray<int32> Degree;
	Degree.SetNumZeroed(Vertices.Num());
	for (const FPCGExClusterSketchEdge& E : Edges)
	{
		if (Vertices.IsValidIndex(E.A) && Vertices.IsValidIndex(E.B) && E.A != E.B)
		{
			++Degree[E.A];
			++Degree[E.B];
		}
	}

	int32 Removed = 0;
	// Descending: removals only shift HIGHER indices, so earlier Degree entries stay aligned.
	for (int32 i = Vertices.Num() - 1; i >= 0; --i)
	{
		if (Vertices[i].bSideEffect && Degree[i] == 0)
		{
			RemoveVertex(i);
			++Removed;
		}
	}
	return Removed;
}

void FPCGExClusterSketchModel::MarkVertexAuthored(const int32 VertexIndex)
{
	if (Vertices.IsValidIndex(VertexIndex))
	{
		Vertices[VertexIndex].bSideEffect = false;
	}
}
#endif

void FPCGExClusterSketchModel::GatherVertexRemovalCascade(const int32 InVertexIndex, TSet<int32>& OutVertices, TSet<int32>& OutEdges) const
{
	if (!Vertices.IsValidIndex(InVertexIndex))
	{
		return;
	}

	OutVertices.Add(InVertexIndex);

	TArray<int32> Degree;
	Degree.SetNumZeroed(Vertices.Num());
	for (int32 e = 0; e < Edges.Num(); ++e)
	{
		const FPCGExClusterSketchEdge& E = Edges[e];
		if (!Vertices.IsValidIndex(E.A) || !Vertices.IsValidIndex(E.B) || E.A == E.B)
		{
			continue; // dormant or degenerate: not this operation's business
		}
		++Degree[E.A];
		++Degree[E.B];
		if (E.A == InVertexIndex || E.B == InVertexIndex)
		{
			OutEdges.Add(e);
		}
	}

#if WITH_EDITORONLY_DATA
	// Mirrors RemoveOrphanSideEffectVertices: a tool-inserted vertex whose ONLY edge was to this one
	// goes with it. Degree is pre-removal, so "only edge" reads as degree 1.
	for (const int32 EdgeIndex : OutEdges)
	{
		const FPCGExClusterSketchEdge& E = Edges[EdgeIndex];
		const int32 Other = E.A == InVertexIndex ? E.B : E.A;
		if (Other != InVertexIndex && Vertices[Other].bSideEffect && Degree[Other] <= 1)
		{
			OutVertices.Add(Other);
		}
	}
#endif
}

int32 FPCGExClusterSketchModel::RemoveInvalidEdges()
{
	const int32 NumVtx = Vertices.Num();
	int32 NumRemoved = 0;
	TSet<uint64> Seen;
	Seen.Reserve(Edges.Num());
	// Ascending scan with in-place removal would skip; descending keeps earlier indices stable -- but
	// duplicate detection must keep the FIRST occurrence, so collect keys ascending, remove descending.
	TArray<bool> bRemove;
	bRemove.SetNumZeroed(Edges.Num());
	for (int32 e = 0; e < Edges.Num(); ++e)
	{
		const FPCGExClusterSketchEdge& E = Edges[e];
		if (E.A < 0 || E.B < 0 || E.A >= NumVtx || E.B >= NumVtx || E.A == E.B)
		{
			bRemove[e] = true;
			continue;
		}
		bool bAlreadySeen = false;
		Seen.Add(PCGEx::H64U(static_cast<uint32>(E.A), static_cast<uint32>(E.B)), &bAlreadySeen);
		bRemove[e] = bAlreadySeen;
	}
	for (int32 e = Edges.Num() - 1; e >= 0; --e)
	{
		if (!bRemove[e])
		{
			continue;
		}
		Edges.RemoveAt(e);
		++NumRemoved;
	}

#if WITH_EDITORONLY_DATA
	// Dropping edges can strand tool residue, same as every other edge-removing operation.
	RemoveOrphanSideEffectVertices();
#endif
	return NumRemoved;
}

int32 FPCGExClusterSketchModel::MergeCollocatedVertices(const FPCGExLatticeBasis* Basis)
{
	int32 NumMerged = 0;

	// Every merge remaps indices, so rescan from scratch after each one; the guard bounds the loop by the
	// only thing it can shrink.
	bool bMergedAny = true;
	int32 Guard = Vertices.Num() + 1;
	while (bMergedAny && Guard-- > 0)
	{
		bMergedAny = false;
		TMap<FVector, int32> FirstAtLocation;
		FirstAtLocation.Reserve(Vertices.Num());
		for (int32 i = 0; i < Vertices.Num(); ++i)
		{
			const FVector Key = PCGExSketch::QuantizedLocationKey(ResolvedLocation(Vertices[i], Basis));
			if (const int32* First = FirstAtLocation.Find(Key))
			{
				MergeVertices(i, *First);
				++NumMerged;
				bMergedAny = true;
				break;
			}
			FirstAtLocation.Add(Key, i);
		}
	}

	// Merging retargets edges, which can leave them passing THROUGH vertices (the collinear D-onto-A
	// case) -- genuinely degenerate, so the cleanup must resolve it. Crossings it may also create are
	// left alone: they are legitimate geometry, offered as ghosts and materialized on demand.
	SplitAllOverlappingEdges(Basis);

#if WITH_EDITORONLY_DATA
	// Merges can also strand tool residue.
	RemoveOrphanSideEffectVertices();
#endif
	return NumMerged;
}

int32 FPCGExClusterSketchModel::SplitOverlappingEdges(const FPCGExLatticeBasis* Basis)
{
	// Pass order is free: materializing a crossing enforces separation around its inserted vertex, so no
	// containment residue survives. Sequenced through a local: both calls mutate, evaluation order unspecified.
	const int32 NumSplits = SplitAllOverlappingEdges(Basis);
	return NumSplits + InsertCrossingVertices(Basis);
}

void FPCGExClusterSketchModel::Validate(FPCGExClusterSketchValidation& OutSummary) const
{
	OutSummary = FPCGExClusterSketchValidation();

	const int32 NumVtx = Vertices.Num();

	TSet<uint64> Seen;
	TArray<int32> Degree;
	Degree.SetNumZeroed(NumVtx);
	for (const FPCGExClusterSketchEdge& E : Edges)
	{
		if (E.A < 0 || E.B < 0 || E.A >= NumVtx || E.B >= NumVtx)
		{
			++OutSummary.InvalidEdges;
			continue;
		}
		if (E.A == E.B)
		{
			++OutSummary.SelfLoops;
			continue;
		}
		bool bAlreadySeen = false;
		Seen.Add(PCGEx::H64U(static_cast<uint32>(E.A), static_cast<uint32>(E.B)), &bAlreadySeen);
		if (bAlreadySeen)
		{
			++OutSummary.DuplicateEdges;
			continue;
		}
		++Degree[E.A];
		++Degree[E.B];
	}

	for (int32 i = 0; i < NumVtx; ++i)
	{
		if (Degree[i] == 0)
		{
			++OutSummary.IsolatedVertices;
		}
	}

	// Collocation: bound-vs-bound by exact coord, free-vs-free by position. (Cross-kind collisions need
	// a basis to resolve bound locations and are the editor's job to prevent, not this basis-less scan's.)
	{
		constexpr double PositionEpsilonSq = PCGExSketch::CoincidenceTolerance * PCGExSketch::CoincidenceTolerance;
		TSet<FIntVector> SeenCoords;
		TArray<FVector> FreeLocations;
		for (const FPCGExClusterSketchVertex& V : Vertices)
		{
			if (V.bLatticeBound)
			{
				bool bAlreadySeen = false;
				SeenCoords.Add(V.LatticeCoord, &bAlreadySeen);
				if (bAlreadySeen)
				{
					++OutSummary.CollocatedVertices;
				}
			}
			else
			{
				const FVector Location = V.Transform.GetLocation();
				for (const FVector& Earlier : FreeLocations)
				{
					if (FVector::DistSquared(Location, Earlier) <= PositionEpsilonSq)
					{
						++OutSummary.CollocatedVertices;
						break;
					}
				}
				FreeLocations.Add(Location);
			}
		}
	}

	// Names are validated on the SANITIZED output name -- that is what actually reaches the attribute.
	// "My.Prop" and "My_Prop" collide only after sanitization, and a literal "PCGEx/VData" survives it
	// intact, so a check on the raw schema name would miss both.
	auto ValidateLayerNames = [](const FPCGExPropertySchemaCollection& InSchema, FPCGExClusterSketchValidation::FLayerIssues& OutIssues)
	{
		TArray<FPCGExPropertyResolved> Resolved;
		InSchema.Resolve(Resolved);

		TSet<FName> SanitizedNames;
		SanitizedNames.Reserve(Resolved.Num());
		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			const FName Sanitized = PCGExMetaHelpers::SanitizeAttributeName(Entry.Source->Name);
			bool bAlreadySeen = false;
			SanitizedNames.Add(Sanitized, &bAlreadySeen);
			if (Sanitized.IsNone() || bAlreadySeen || PCGExClusters::Labels::ProtectedClusterAttributes.Contains(Sanitized))
			{
				// Keyed by the SCHEMA name: that is what the print path holds when it asks Rejects().
				OutIssues.InvalidNames.AddUnique(Entry.Source->Name);
			}
		}
	};

	auto ValidateLayerRecords = [](const FPCGExSketchDataLayer& InLayer, const TArray<uint32>& InLiveIds, FPCGExClusterSketchValidation::FLayerIssues& OutIssues)
	{
		TSet<uint32> RecordIds;
		RecordIds.Reserve(InLayer.Records.Num());
		for (const FPCGExSketchDataRecord& Record : InLayer.Records)
		{
			bool bAlreadySeen = false;
			RecordIds.Add(Record.Id, &bAlreadySeen);
			if (bAlreadySeen || Record.Id == PCGExSketch::InvalidRecordId)
			{
				++OutIssues.DuplicateRecordIds;
			}
		}
		for (const uint32 Id : InLiveIds)
		{
			if (!RecordIds.Contains(Id))
			{
				++OutIssues.DanglingRefs;
			}
		}
	};

	TArray<uint32> LiveVertexIds;
	TArray<uint32> LiveEdgeIds;
	GatherLiveDataIds(LiveVertexIds, LiveEdgeIds);

	ValidateLayerNames(Data.SketchProperties, OutSummary.SketchLayerIssues);
	ValidateLayerNames(Data.VertexLayer.Schema, OutSummary.VertexLayerIssues);
	ValidateLayerNames(Data.EdgeLayer.Schema, OutSummary.EdgeLayerIssues);
	ValidateLayerRecords(Data.VertexLayer, LiveVertexIds, OutSummary.VertexLayerIssues);
	ValidateLayerRecords(Data.EdgeLayer, LiveEdgeIds, OutSummary.EdgeLayerIssues);

#if WITH_EDITORONLY_DATA
	// Basis-less on purpose, like the collocation scan: bound vertices measure through their cached
	// transform here, which only ever drifts by the last solve's own snap.
	TArray<FPCGExSketchConstraintResidual> Residuals;
	EvaluateConstraints(nullptr, Residuals);
	for (const FPCGExSketchConstraintResidual& R : Residuals)
	{
		if (R.bDangling)
		{
			++OutSummary.ConstraintIssues.Dangling;
		}
		else if (!R.IsSatisfied())
		{
			++OutSummary.ConstraintIssues.Unsatisfied;
		}
	}
#endif
}

uint32 FPCGExClusterSketchModel::ResolveExtrudeEdgeDataId(const int32 InVertexIndex) const
{
	// Guarded, not assumed: an edge defaults to A/B = -1, so INDEX_NONE would match raw-authored blanks.
	if (!Vertices.IsValidIndex(InVertexIndex))
	{
		return PCGExSketch::InvalidRecordId;
	}

	int32 Sole = INDEX_NONE;
	for (int32 e = 0; e < Edges.Num(); ++e)
	{
		const FPCGExClusterSketchEdge& E = Edges[e];
		if (E.A != InVertexIndex && E.B != InVertexIndex)
		{
			continue;
		}
		if (Sole != INDEX_NONE)
		{
			return PCGExSketch::InvalidRecordId; // a junction: no single parent to speak for the extrusion
		}
		Sole = e;
	}
	return Sole == INDEX_NONE ? PCGExSketch::InvalidRecordId : Edges[Sole].DataId;
}

bool FPCGExClusterSketchModel::SetVertexDataId(const int32 Index, const uint32 InDataId)
{
	if (!Vertices.IsValidIndex(Index))
	{
		return false;
	}
	Vertices[Index].DataId = InDataId;
	return true;
}

bool FPCGExClusterSketchModel::SetEdgeDataId(const int32 Index, const uint32 InDataId)
{
	if (!Edges.IsValidIndex(Index))
	{
		return false;
	}
	Edges[Index].DataId = InDataId;
	return true;
}

void FPCGExClusterSketchModel::GatherLiveDataIds(TArray<uint32>& OutVertexIds, TArray<uint32>& OutEdgeIds) const
{
	OutVertexIds.Reset();
	OutEdgeIds.Reset();
	for (const FPCGExClusterSketchVertex& V : Vertices)
	{
		if (V.DataId != PCGExSketch::InvalidRecordId)
		{
			OutVertexIds.Add(V.DataId);
		}
	}
	for (const FPCGExClusterSketchEdge& E : Edges)
	{
		if (E.DataId != PCGExSketch::InvalidRecordId)
		{
			OutEdgeIds.Add(E.DataId);
		}
	}
}

int32 FPCGExClusterSketchModel::CountVertexReferences(const uint32 InDataId) const
{
	if (InDataId == PCGExSketch::InvalidRecordId)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FPCGExClusterSketchVertex& V : Vertices)
	{
		if (V.DataId == InDataId)
		{
			++Count;
		}
	}
	return Count;
}

int32 FPCGExClusterSketchModel::CountEdgeReferences(const uint32 InDataId) const
{
	if (InDataId == PCGExSketch::InvalidRecordId)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FPCGExClusterSketchEdge& E : Edges)
	{
		if (E.DataId == InDataId)
		{
			++Count;
		}
	}
	return Count;
}

int32 FPCGExClusterSketchModel::PurgeUnreferencedRecords()
{
	TArray<uint32> LiveVertexIds;
	TArray<uint32> LiveEdgeIds;
	GatherLiveDataIds(LiveVertexIds, LiveEdgeIds);
	return Data.VertexLayer.PurgeUnreferenced(LiveVertexIds) + Data.EdgeLayer.PurgeUnreferenced(LiveEdgeIds);
}

#if WITH_EDITORONLY_DATA
uint32 FPCGExClusterSketchModel::AddConstraint(FInstancedStruct&& InConstraint, const FPCGExLatticeBasis* Basis)
{
	FPCGExSketchConstraint* Constraint = InConstraint.GetMutablePtr<FPCGExSketchConstraint>();
	if (!Constraint)
	{
		return PCGExSketch::InvalidElementId;
	}
	Constraint->Id = MintElementId();

	FPCGExSketchSolveContext Ctx;
	Ctx.Build(*this, Basis, {});
	Constraint->InitializeFromGeometry(Ctx);

	const uint32 Id = Constraint->Id;
	Constraints.Add(MoveTemp(InConstraint));
	return Id;
}

bool FPCGExClusterSketchModel::RemoveConstraint(const uint32 InId)
{
	const int32 Index = FindConstraintIndex(InId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Constraints.RemoveAt(Index);
	return true;
}

int32 FPCGExClusterSketchModel::FindConstraintIndex(const uint32 InId) const
{
	if (InId == PCGExSketch::InvalidElementId)
	{
		return INDEX_NONE;
	}
	return Constraints.IndexOfByPredicate([InId](const FInstancedStruct& Entry)
	{
		const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>();
		return C && C->Id == InId;
	});
}

const FPCGExSketchConstraint* FPCGExClusterSketchModel::FindConstraint(const uint32 InId) const
{
	const int32 Index = FindConstraintIndex(InId);
	return Index == INDEX_NONE ? nullptr : Constraints[Index].GetPtr<FPCGExSketchConstraint>();
}

FPCGExSketchConstraint* FPCGExClusterSketchModel::FindConstraintMutable(const uint32 InId)
{
	const int32 Index = FindConstraintIndex(InId);
	return Index == INDEX_NONE ? nullptr : Constraints[Index].GetMutablePtr<FPCGExSketchConstraint>();
}

void FPCGExClusterSketchModel::GatherConstraintsOf(const uint32 InElementId, TArray<int32>& OutConstraintIndices) const
{
	OutConstraintIndices.Reset();
	if (InElementId == PCGExSketch::InvalidElementId)
	{
		return;
	}
	for (int32 c = 0; c < Constraints.Num(); ++c)
	{
		const FPCGExSketchConstraint* C = Constraints[c].GetPtr<FPCGExSketchConstraint>();
		if (C && C->Subjects.Contains(InElementId))
		{
			OutConstraintIndices.Add(c);
		}
	}
}

int32 FPCGExClusterSketchModel::RemoveConstraintsOf(const uint32 InElementId)
{
	const int32 Before = Constraints.Num();
	OnElementsRemoved(MakeArrayView(&InElementId, 1));
	return Before - Constraints.Num();
}

bool FPCGExClusterSketchModel::IsVertexDirectSubject(const uint32 InVertexId) const
{
	for (const FInstancedStruct& Entry : Constraints)
	{
		const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>();
		if (!C || !C->bEnabled)
		{
			continue;
		}
		const int32 Expected = C->GetNumSubjects();
		for (int32 Slot = 0; Slot < C->Subjects.Num(); ++Slot)
		{
			const int32 KindSlot = Expected == PCGExSketch::VariadicSubjects ? 0 : Slot;
			if (C->Subjects[Slot] == InVertexId && C->GetSubjectKind(KindSlot) == EPCGExSketchSubjectKind::Vertex)
			{
				return true;
			}
		}
	}
	return false;
}

bool FPCGExClusterSketchModel::AbsorbProposal(const uint32 InVertexId, const FVector& InProposed, const FPCGExLatticeBasis* Basis)
{
	if (Constraints.IsEmpty() || InVertexId == PCGExSketch::InvalidElementId)
	{
		return false;
	}
	FPCGExSketchSolveContext Ctx;
	Ctx.Build(*this, Basis, {});

	bool bAbsorbed = false;
	for (FInstancedStruct& Entry : Constraints)
	{
		FPCGExSketchConstraint* C = Entry.GetMutablePtr<FPCGExSketchConstraint>();
		if (C && C->bEnabled && C->Subjects.Contains(InVertexId) && C->ResolvesIn(Ctx))
		{
			bAbsorbed |= C->AbsorbProposal(Ctx, InVertexId, InProposed);
		}
	}
	return bAbsorbed;
}

void FPCGExClusterSketchModel::OnElementsRemoved(const TConstArrayView<uint32> InRemovedIds)
{
	if (InRemovedIds.IsEmpty() || Constraints.IsEmpty())
	{
		return;
	}
	Constraints.RemoveAll([&InRemovedIds](const FInstancedStruct& Entry)
	{
		const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>();
		if (!C)
		{
			return true; // a null entry is nothing to keep
		}
		for (const uint32 Subject : C->Subjects)
		{
			if (InRemovedIds.Contains(Subject))
			{
				return true;
			}
		}
		return false;
	});
}

bool FPCGExClusterSketchModel::SolveConstraints(const FPCGExLatticeBasis* Basis, const TConstArrayView<uint32> InPinnedIds, TArray<FPCGExSketchConstraintResidual>* OutResiduals)
{
	if (Constraints.IsEmpty())
	{
		if (OutResiduals)
		{
			OutResiduals->Reset();
		}
		return false;
	}

	FPCGExSketchSolveContext Ctx;
	Ctx.Build(*this, Basis, InPinnedIds);

	// Ordered projection: each pass runs the list in order, so the LAST constraint has the final say on
	// a contested vertex, and a few passes let the earlier ones settle around it.
	constexpr int32 NumPasses = 8;
	for (int32 Pass = 0; Pass < NumPasses; ++Pass)
	{
		for (const FInstancedStruct& Entry : Constraints)
		{
			const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>();
			if (C && C->bEnabled && C->ResolvesIn(Ctx))
			{
				C->Project(Ctx);
			}
		}
	}

	// Write back. Bound vertices re-snap: the lattice is the final projection until it is itself a
	// constraint in this list.
	bool bAnyMoved = false;
	constexpr double MoveTolSq = UE_DOUBLE_SMALL_NUMBER;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		if (!Ctx.bMovable[i])
		{
			continue;
		}
		FPCGExClusterSketchVertex& V = Vertices[i];
		FVector NewLocation = Ctx.Positions[i];
		if (V.bLatticeBound && Basis)
		{
			V.LatticeCoord = Basis->SnapWorldToCoordPreserving(NewLocation, V.LatticeCoord);
			NewLocation = Basis->CoordToWorld(V.LatticeCoord);
		}
		if (FVector::DistSquared(NewLocation, V.Transform.GetLocation()) > MoveTolSq)
		{
			V.Transform.SetLocation(NewLocation);
			bAnyMoved = true;
		}
		Ctx.Positions[i] = NewLocation;
	}

	if (OutResiduals)
	{
		// Against what was actually written -- the snap may have undone part of a projection.
		EvaluateConstraints(Basis, *OutResiduals);
	}
	return bAnyMoved;
}

void FPCGExClusterSketchModel::EvaluateConstraints(const FPCGExLatticeBasis* Basis, TArray<FPCGExSketchConstraintResidual>& OutResiduals) const
{
	OutResiduals.Reset();
	if (Constraints.IsEmpty())
	{
		return;
	}

	FPCGExSketchSolveContext Ctx;
	Ctx.Build(*this, Basis, {});

	OutResiduals.Reserve(Constraints.Num());
	for (const FInstancedStruct& Entry : Constraints)
	{
		const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>();
		if (!C || !C->bEnabled)
		{
			continue;
		}
		FPCGExSketchConstraintResidual& R = OutResiduals.AddDefaulted_GetRef();
		R.ConstraintId = C->Id;
		R.bDangling = !C->ResolvesIn(Ctx);
		R.Residual = R.bDangling ? 0.0 : C->Residual(Ctx);
	}
}

bool FPCGExClusterSketchModel::InferAlongAnchors(const uint32 InVertexId, uint32& OutAnchorA, uint32& OutAnchorB) const
{
	const int32 Start = FindVertexIndex(InVertexId);
	if (Start == INDEX_NONE)
	{
		return false;
	}

	// Which vertices already ride an Along: the chain walks THROUGH those and stops at the first that does not.
	TSet<uint32> AlongSubjects;
	for (const FInstancedStruct& Entry : Constraints)
	{
		const FPCGExSketchConstraint_Along* Along = Entry.GetPtr<FPCGExSketchConstraint_Along>();
		if (Along && Along->Subjects.Num() == 3)
		{
			AlongSubjects.Add(Along->Subjects[PCGExSketch::AlongRole::Subject]);
		}
	}

	// Neighbours per vertex, once.
	TArray<TArray<int32, TInlineAllocator<4>>> Adjacency;
	Adjacency.SetNum(Vertices.Num());
	for (const FPCGExClusterSketchEdge& E : Edges)
	{
		if (Vertices.IsValidIndex(E.A) && Vertices.IsValidIndex(E.B) && E.A != E.B)
		{
			Adjacency[E.A].AddUnique(E.B);
			Adjacency[E.B].AddUnique(E.A);
		}
	}
	if (Adjacency[Start].Num() != 2)
	{
		return false; // a junction or a loose end cannot sit "between" anything
	}

	auto Walk = [&](int32 From, int32 Towards, uint32& OutAnchor) -> bool
	{
		TSet<int32> Visited;
		Visited.Add(From);
		int32 Prev = From;
		int32 Current = Towards;
		while (true)
		{
			if (Visited.Contains(Current))
			{
				return false; // a loop with no fixed vertex on it
			}
			Visited.Add(Current);
			if (!AlongSubjects.Contains(Vertices[Current].Id))
			{
				OutAnchor = Vertices[Current].Id;
				return true;
			}
			if (Adjacency[Current].Num() != 2)
			{
				return false; // an Along subject at a junction: no single way through
			}
			const int32 Next = Adjacency[Current][0] == Prev ? Adjacency[Current][1] : Adjacency[Current][0];
			Prev = Current;
			Current = Next;
		}
	};

	return Walk(Start, Adjacency[Start][0], OutAnchorA) && Walk(Start, Adjacency[Start][1], OutAnchorB) && OutAnchorA != OutAnchorB;
}
#endif

#pragma endregion
