// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchModel.h"

#include "PCGExH.h"
#include "Clusters/PCGExClusterCommon.h"

#pragma region FPCGExClusterDataChannel

int32 FPCGExClusterDataChannel::Num() const
{
	switch (Type)
	{
	case EPCGExClusterDataChannelType::Double: return DoubleValues.Num();
	case EPCGExClusterDataChannelType::Integer: return IntegerValues.Num();
	case EPCGExClusterDataChannelType::Name: return NameValues.Num();
	case EPCGExClusterDataChannelType::Vector: return VectorValues.Num();
	default: checkNoEntry();
		return 0;
	}
}

void FPCGExClusterDataChannel::SetNumDefaulted(const int32 InNum)
{
	DoubleValues.Empty();
	IntegerValues.Empty();
	NameValues.Empty();
	VectorValues.Empty();
	switch (Type)
	{
	case EPCGExClusterDataChannelType::Double: DoubleValues.SetNumZeroed(InNum);
		break;
	case EPCGExClusterDataChannelType::Integer: IntegerValues.SetNumZeroed(InNum);
		break;
	case EPCGExClusterDataChannelType::Name: NameValues.SetNum(InNum);
		break;
	case EPCGExClusterDataChannelType::Vector: VectorValues.SetNumZeroed(InNum);
		break;
	default: checkNoEntry();
		break;
	}
}

void FPCGExClusterDataChannel::RemoveAt(const int32 Index)
{
	switch (Type)
	{
	case EPCGExClusterDataChannelType::Double: if (DoubleValues.IsValidIndex(Index)) { DoubleValues.RemoveAt(Index); }
		break;
	case EPCGExClusterDataChannelType::Integer: if (IntegerValues.IsValidIndex(Index)) { IntegerValues.RemoveAt(Index); }
		break;
	case EPCGExClusterDataChannelType::Name: if (NameValues.IsValidIndex(Index)) { NameValues.RemoveAt(Index); }
		break;
	case EPCGExClusterDataChannelType::Vector: if (VectorValues.IsValidIndex(Index)) { VectorValues.RemoveAt(Index); }
		break;
	default: checkNoEntry();
		break;
	}
}

void FPCGExClusterDataChannel::InsertDefaulted(const int32 Index)
{
	switch (Type)
	{
	case EPCGExClusterDataChannelType::Double: DoubleValues.Insert(0.0, FMath::Clamp(Index, 0, DoubleValues.Num()));
		break;
	case EPCGExClusterDataChannelType::Integer: IntegerValues.Insert(0, FMath::Clamp(Index, 0, IntegerValues.Num()));
		break;
	case EPCGExClusterDataChannelType::Name: NameValues.Insert(NAME_None, FMath::Clamp(Index, 0, NameValues.Num()));
		break;
	case EPCGExClusterDataChannelType::Vector: VectorValues.Insert(FVector::ZeroVector, FMath::Clamp(Index, 0, VectorValues.Num()));
		break;
	default: checkNoEntry();
		break;
	}
}

#pragma endregion

namespace PCGExClusterSketchModel
{
	/** One captured edge-channel row, so split segments can inherit the parent edge's values. */
	struct FChannelValueSnapshot
	{
		double Double = 0.0;
		int64 Integer = 0;
		FName Name = NAME_None;
		FVector Vector = FVector::ZeroVector;
	};

	void CaptureEdgeChannelValues(const TArray<FPCGExClusterDataChannel>& Channels, const int32 EdgeIndex, TArray<FChannelValueSnapshot>& OutValues)
	{
		OutValues.Reset();
		OutValues.Reserve(Channels.Num());
		for (const FPCGExClusterDataChannel& Channel : Channels)
		{
			FChannelValueSnapshot& Value = OutValues.AddDefaulted_GetRef();
			switch (Channel.Type)
			{
			case EPCGExClusterDataChannelType::Double: if (Channel.DoubleValues.IsValidIndex(EdgeIndex)) { Value.Double = Channel.DoubleValues[EdgeIndex]; }
				break;
			case EPCGExClusterDataChannelType::Integer: if (Channel.IntegerValues.IsValidIndex(EdgeIndex)) { Value.Integer = Channel.IntegerValues[EdgeIndex]; }
				break;
			case EPCGExClusterDataChannelType::Name: if (Channel.NameValues.IsValidIndex(EdgeIndex)) { Value.Name = Channel.NameValues[EdgeIndex]; }
				break;
			case EPCGExClusterDataChannelType::Vector: if (Channel.VectorValues.IsValidIndex(EdgeIndex)) { Value.Vector = Channel.VectorValues[EdgeIndex]; }
				break;
			default: checkNoEntry();
				break;
			}
		}
	}

	void ApplyEdgeChannelValues(TArray<FPCGExClusterDataChannel>& Channels, const int32 EdgeIndex, const TArray<FChannelValueSnapshot>& Values)
	{
		for (int32 c = 0; c < Channels.Num() && c < Values.Num(); ++c)
		{
			FPCGExClusterDataChannel& Channel = Channels[c];
			switch (Channel.Type)
			{
			case EPCGExClusterDataChannelType::Double: if (Channel.DoubleValues.IsValidIndex(EdgeIndex)) { Channel.DoubleValues[EdgeIndex] = Values[c].Double; }
				break;
			case EPCGExClusterDataChannelType::Integer: if (Channel.IntegerValues.IsValidIndex(EdgeIndex)) { Channel.IntegerValues[EdgeIndex] = Values[c].Integer; }
				break;
			case EPCGExClusterDataChannelType::Name: if (Channel.NameValues.IsValidIndex(EdgeIndex)) { Channel.NameValues[EdgeIndex] = Values[c].Name; }
				break;
			case EPCGExClusterDataChannelType::Vector: if (Channel.VectorValues.IsValidIndex(EdgeIndex)) { Channel.VectorValues[EdgeIndex] = Values[c].Vector; }
				break;
			default: checkNoEntry();
				break;
			}
		}
	}

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

int32 FPCGExClusterSketchModel::AddVertex(const FTransform& InTransform)
{
	const int32 Index = Vertices.Num();
	FPCGExClusterSketchVertex& V = Vertices.AddDefaulted_GetRef();
	V.Transform = InTransform;
	for (FPCGExClusterDataChannel& Channel : VertexChannels)
	{
		Channel.InsertDefaulted(Index);
	}
	return Index;
}

int32 FPCGExClusterSketchModel::AddLatticeVertex(const FIntVector& InCoord, const FPCGExLatticeBasis& InBasis)
{
	const int32 Index = AddVertex(FTransform(InBasis.CoordToWorld(InCoord)));
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

	// Drop touching edges (descending, so earlier indices stay valid), then remap the survivors.
	for (int32 e = Edges.Num() - 1; e >= 0; --e)
	{
		if (Edges[e].A == Index || Edges[e].B == Index)
		{
			Edges.RemoveAt(e);
			for (FPCGExClusterDataChannel& Channel : EdgeChannels)
			{
				Channel.RemoveAt(e);
			}
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
	for (FPCGExClusterDataChannel& Channel : VertexChannels)
	{
		Channel.RemoveAt(Index);
	}
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
	E.A = A;
	E.B = B;
	for (FPCGExClusterDataChannel& Channel : EdgeChannels)
	{
		Channel.InsertDefaulted(Index);
	}
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

	Edges.RemoveAt(EdgeIndex);
	for (FPCGExClusterDataChannel& Channel : EdgeChannels)
	{
		Channel.RemoveAt(EdgeIndex);
	}
	return true;
}

int32 FPCGExClusterSketchModel::MergeVertices(const int32 InAbsorbed, const int32 InSurvivor)
{
	if (InAbsorbed == InSurvivor || !Vertices.IsValidIndex(InAbsorbed) || !Vertices.IsValidIndex(InSurvivor))
	{
		return INDEX_NONE;
	}

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

		Edges.RemoveAt(e);
		for (FPCGExClusterDataChannel& Channel : EdgeChannels)
		{
			Channel.RemoveAt(e);
		}
	}

#if WITH_EDITORONLY_DATA
	// Authorship unions: the survivor is tool residue only if BOTH sides were.
	Vertices[InSurvivor].bSideEffect &= Vertices[InAbsorbed].bSideEffect;
#endif

	const int32 SurvivorAfterRemoval = InSurvivor > InAbsorbed ? InSurvivor - 1 : InSurvivor;
	RemoveVertex(InAbsorbed); // no edges touch it anymore -- pure vertex/channel removal + index remap
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
	Contained.Sort([](const TPair<double, int32>& Lhs, const TPair<double, int32>& Rhs) { return Lhs.Key < Rhs.Key; });

	// New chain segments inherit the parent edge's channel values.
	TArray<PCGExClusterSketchModel::FChannelValueSnapshot> ParentValues;
	PCGExClusterSketchModel::CaptureEdgeChannelValues(EdgeChannels, EdgeIndex, ParentValues);

	Edges.RemoveAt(EdgeIndex);
	for (FPCGExClusterDataChannel& Channel : EdgeChannels)
	{
		Channel.RemoveAt(EdgeIndex);
	}

	int32 NumSegments = 0;
	int32 Prev = Edge.A;
	auto Link = [&](const int32 From, const int32 To)
	{
		bool bCreated = false;
		const int32 NewIndex = Connect(From, To, &bCreated);
		if (bCreated)
		{
			PCGExClusterSketchModel::ApplyEdgeChannelValues(EdgeChannels, NewIndex, ParentValues);
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
				for (const uint64 Key : ChainKeys) { Touched.Add(Key); } // the chain inherits the parent's scope
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
		for (FPCGExClusterDataChannel& Channel : EdgeChannels)
		{
			Channel.RemoveAt(e);
		}
		++NumRemoved;
	}
	return NumRemoved;
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
		constexpr double PositionEpsilonSq = 0.01 * 0.01;
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

	auto ValidateChannels = [](const TArray<FPCGExClusterDataChannel>& Channels, const int32 DomainCount, FPCGExClusterSketchValidation::FChannelIssues& OutIssues)
	{
		TSet<FName> Names;
		for (const FPCGExClusterDataChannel& Channel : Channels)
		{
			bool bAlreadySeen = false;
			Names.Add(Channel.Name, &bAlreadySeen);
			if (Channel.Name.IsNone() || bAlreadySeen || PCGExClusters::Labels::ProtectedClusterAttributes.Contains(Channel.Name))
			{
				OutIssues.InvalidNames.AddUnique(Channel.Name);
			}
			if (Channel.Num() != DomainCount)
			{
				OutIssues.Misaligned.AddUnique(Channel.Name);
			}
		}
	};
	ValidateChannels(VertexChannels, NumVtx, OutSummary.VertexChannelIssues);
	ValidateChannels(EdgeChannels, Edges.Num(), OutSummary.EdgeChannelIssues);
}

#pragma endregion
