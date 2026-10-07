// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/Geo/PCGExDelaunay.h"

#include "CoreMinimal.h"
#include "PCGExCoreSettingsCache.h"

#include "PCGExSettingsCacheBody.h"
#include "Async/ParallelFor.h"
#include "Core/PCGExMTCommon.h"
#include "Math/PCGExProjectionDetails.h"
#include "Math/Geo/PCGExGeo.h"
#include "CompGeom/ExactPredicates.h"
#include "ThirdParty/Delaunator/include/delaunator.hpp"

namespace PCGExMath::Geo
{
	FDelaunaySite2::FDelaunaySite2(const UE::Geometry::FIndex3i& InVtx, const UE::Geometry::FIndex3i& InAdjacency, const int32 InId)
		: Id(InId)
	{
		for (int i = 0; i < 3; i++)
		{
			Vtx[i] = InVtx[i];
			Neighbors[i] = InAdjacency[i];
		}
	}

	FDelaunaySite2::FDelaunaySite2(const int32 A, const int32 B, const int32 C, const int32 InId)
		: Id(InId)
	{
		Vtx[0] = A;
		Vtx[1] = B;
		Vtx[2] = C;
		for (int i = 0; i < 3; i++)
		{
			Neighbors[i] = -1;
		}
	}

	bool FDelaunaySite2::ContainsEdge(const uint64 Edge) const
	{
		return Edge == PCGEx::H64U(Vtx[0], Vtx[1]) || Edge == PCGEx::H64U(Vtx[0], Vtx[2]) || Edge == PCGEx::H64U(Vtx[1], Vtx[2]);
	}

	uint64 FDelaunaySite2::GetSharedEdge(const FDelaunaySite2* Other) const
	{
		return Other->ContainsEdge(PCGEx::H64U(Vtx[0], Vtx[1])) ? PCGEx::H64U(Vtx[0], Vtx[1]) : Other->ContainsEdge(PCGEx::H64U(Vtx[0], Vtx[2])) ? PCGEx::H64U(Vtx[0], Vtx[2]) : PCGEx::H64U(Vtx[1], Vtx[2]);
	}

	void FDelaunaySite2::PushAdjacency(const int32 SiteId)
	{
		for (int i = 0; i < 3; i++)
		{
			if (Neighbors[i] == -1)
			{
				Neighbors[i] = SiteId;
				// A site is on the hull as long as it has fewer than 3 neighbors,
				// i.e. until the last neighbor slot is filled.
				bOnHull = i != 2;
				break;
			}
		}
	}

	TDelaunay2::~TDelaunay2()
	{
		Clear();
	}

	void TDelaunay2::Clear()
	{
		Sites.Empty();
		DelaunayEdges.Empty();
		DelaunayHull.Empty();

		IsValid = false;
	}

	bool TDelaunay2::Process(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, const bool bComputeDelaunayEdges, const bool bComputeHull)
	{
		Clear();

		if (Positions.IsEmpty() || Positions.Num() <= 2)
		{
			return false;
		}

		if (PCGEX_CORE_SETTINGS.bUseDelaunator)
		{
			std::vector<double> Coords(Positions.Num() * 2);
			ProjectionDetails.Project(Positions, Coords);
			return ProcessDelaunator(Coords, bComputeDelaunayEdges, bComputeHull);
		}

		TArray<FVector2D> Projected;
		ProjectionDetails.Project(Positions, Projected);
		return ProcessFallback(Projected, bComputeDelaunayEdges, bComputeHull);
	}

	bool TDelaunay2::ProcessProjected(const TArrayView<FVector>& ProjectedPositions, const bool bComputeDelaunayEdges, const bool bComputeHull)
	{
		Clear();

		const int32 NumPositions = ProjectedPositions.Num();
		if (NumPositions <= 2)
		{
			return false;
		}

		if (PCGEX_CORE_SETTINGS.bUseDelaunator)
		{
			std::vector<double> Coords(NumPositions * 2);
			PCGEX_PARALLEL_FOR(
				NumPositions,
				const FVector& P = ProjectedPositions[i];
				const int32 ii = i * 2;
				Coords[ii] = P.X;
				Coords[ii + 1] = P.Y;
				)
			return ProcessDelaunator(Coords, bComputeDelaunayEdges, bComputeHull);
		}

		TArray<FVector2D> Projected;
		Projected.SetNumUninitialized(NumPositions);
		PCGEX_PARALLEL_FOR(
			NumPositions,
			Projected[i] = FVector2D(ProjectedPositions[i].X, ProjectedPositions[i].Y);
			)
		return ProcessFallback(Projected, bComputeDelaunayEdges, bComputeHull);
	}

	bool TDelaunay2::ProcessDelaunator(const std::vector<double>& Coords, const bool bComputeDelaunayEdges, const bool bComputeHull)
	{
		// delaunator now routes its predicates through UE's exact predicates (see delaunator.hpp),
		// which require a one-time GlobalInit() before use. GeometryAlgorithms' module startup already
		// calls it; this guarded call makes the dependency explicit and is a thread-safe no-op after
		// the first invocation (C++ guarantees once-only init of a function-local static).
		static const bool bExactPredicatesReady = []()
		{
			UE::Geometry::ExactPredicates::GlobalInit();
			return true;
		}();
		(void)bExactPredicatesReady;

		// NOTE: delaunator keeps a reference to Coords; it must stay alive for the
		// lifetime of the triangulation object.
		TUniquePtr<delaunator::Delaunator> Triangulation;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Delaunator::Triangulate);
			Triangulation = MakeUnique<delaunator::Delaunator>(Coords);
		}

		const delaunator::Delaunator& D = *Triangulation;

		if (D.runtime_error)
		{
			return false;
		}

		const std::size_t NumHalfedges = D.triangles.size();
		if (!NumHalfedges)
		{
			return false;
		}

		const int32 NumSites = static_cast<int32>(NumHalfedges / 3);
		const std::size_t* Triangles = D.triangles.data();
		const std::size_t* Halfedges = D.halfedges.data();

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Delaunay2D::BuildSites);

			// Adjacency comes for free from the half-edge structure: halfedge e belongs to
			// triangle e/3, and Halfedges[e] is its twin in the adjacent triangle
			// (INVALID_INDEX when the edge lies on the convex hull).
			Sites.SetNumUninitialized(NumSites);

			PCGEX_PARALLEL_FOR(
				NumSites,
				const int32 Base = i * 3;
				FDelaunaySite2& Site = Sites[i];
				Site.Id = i;
				bool bTouchesHull = false;
				for (int k = 0; k < 3; k++)
				{
					Site.Vtx[k] = static_cast<int32>(Triangles[Base + k]);
					const std::size_t Opposite = Halfedges[Base + k];
					if (Opposite == delaunator::INVALID_INDEX)
					{
						Site.Neighbors[k] = -1;
						bTouchesHull = true;
					}
					else
					{
						Site.Neighbors[k] = static_cast<int32>(Opposite / 3);
					}
				}
				Site.bOnHull = bTouchesHull;
				)
		}

		if (bComputeDelaunayEdges || bComputeHull)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Delaunay2D::BuildEdgesAndHull);

			std::size_t NumHullEdges = 0;
			for (std::size_t e = 0; e < NumHalfedges; e++)
			{
				if (Halfedges[e] == delaunator::INVALID_INDEX)
				{
					NumHullEdges++;
				}
			}

			if (bComputeDelaunayEdges)
			{
				// Interior edges are shared by two halfedges, hull edges by one.
				DelaunayEdges.Reserve(static_cast<int32>((NumHalfedges - NumHullEdges) / 2 + NumHullEdges));
			}
			if (bComputeHull)
			{
				DelaunayHull.Reserve(static_cast<int32>(NumHullEdges));
			}

			// Each undirected edge is visited exactly once: hull halfedges have no twin,
			// interior pairs are claimed by the smaller halfedge index. No dedup required.
			for (std::size_t e = 0; e < NumHalfedges; e++)
			{
				const std::size_t Opposite = Halfedges[e];
				const bool bHullEdge = Opposite == delaunator::INVALID_INDEX;

				if (!bHullEdge && Opposite < e)
				{
					continue;
				}

				const std::size_t Next = (e % 3 == 2) ? e - 2 : e + 1;
				const int32 A = static_cast<int32>(Triangles[e]);
				const int32 B = static_cast<int32>(Triangles[Next]);

				if (bComputeDelaunayEdges)
				{
					DelaunayEdges.Add(PCGEx::H64U(A, B));
				}

				if (bHullEdge && bComputeHull)
				{
					DelaunayHull.Add(A);
					DelaunayHull.Add(B);
				}
			}
		}

		IsValid = true;
		return IsValid;
	}

	bool TDelaunay2::ProcessFallback(const TArray<FVector2D>& ProjectedPositions, const bool bComputeDelaunayEdges, const bool bComputeHull)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TDelaunay2::ProcessFallback);

		UE::Geometry::FDelaunay2 Delaunay2;
		if (!Delaunay2.Triangulate(ProjectedPositions))
		{
			return false;
		}

		TArray<UE::Geometry::FIndex3i> Triangles = Delaunay2.GetTriangles();
		const int32 NumTriangles = Triangles.Num();

		if (!NumTriangles)
		{
			return false;
		}

		// The engine triangulation does not expose adjacency, so it is rebuilt by matching
		// shared edges between triangles. First occurrence of an edge records the owning
		// site in EdgeMap; the second occurrence links the two owning sites as neighbors.
		// Edges left unmatched once all sites are processed lie on the convex hull.
		TMap<uint64, int32> EdgeMap;
		TSet<uint64> SeenEdges;

		const int32 NumSites = NumTriangles;
		SeenEdges.Reserve(NumSites * 2);
		Sites.Reserve(NumSites);
		EdgeMap.Reserve(NumSites);

		auto PushEdge = [&](FDelaunaySite2& Site, const uint64 Edge)
		{
			bool bIsAlreadySet = false;
			SeenEdges.Add(Edge, &bIsAlreadySet);
			if (bIsAlreadySet)
			{
				if (int32 Idx = -1;
					EdgeMap.RemoveAndCopyValue(Edge, Idx))
				{
					FDelaunaySite2& OtherSite = Sites[Idx];
					OtherSite.PushAdjacency(Site.Id);
					Site.PushAdjacency(OtherSite.Id);
				}
			}
			else
			{
				EdgeMap.Add(Edge, Site.Id);
			}
		};

		int32 s = 0;
		for (const UE::Geometry::FIndex3i& T : Triangles)
		{
			FDelaunaySite2& Site = Sites.Emplace_GetRef(T.A, T.B, T.C, s++);
			PushEdge(Site, Site.AB());
			PushEdge(Site, Site.BC());
			PushEdge(Site, Site.AC());
		}

		if (bComputeHull)
		{
			// Edges still in EdgeMap were never matched to a second triangle: hull edges.
			DelaunayHull.Reserve(EdgeMap.Num());
			for (const TPair<uint64, int32>& HullEdge : EdgeMap)
			{
				DelaunayHull.Add(static_cast<int32>(PCGEx::H64A(HullEdge.Key)));
				DelaunayHull.Add(static_cast<int32>(PCGEx::H64B(HullEdge.Key)));
			}
		}

		if (bComputeDelaunayEdges)
		{
			DelaunayEdges = MoveTemp(SeenEdges);
		}

		IsValid = true;
		return IsValid;
	}

	void TDelaunay2::RemoveLongestEdges(const TArrayView<FVector>& Positions)
	{
		uint64 Edge;
		for (const FDelaunaySite2& Site : Sites)
		{
			GetLongestEdge(Positions, Site.Vtx, Edge);
			DelaunayEdges.Remove(Edge);
		}
	}

	void TDelaunay2::RemoveLongestEdges(const TArrayView<FVector>& Positions, TSet<uint64>& LongestEdges)
	{
		uint64 Edge;
		for (const FDelaunaySite2& Site : Sites)
		{
			GetLongestEdge(Positions, Site.Vtx, Edge);
			DelaunayEdges.Remove(Edge);
			LongestEdges.Add(Edge);
		}
	}

	void TDelaunay2::GetMergedSites(const int32 SiteIndex, const TSet<uint64>& EdgeConnectors, TSet<int32>& OutMerged, TSet<uint64>& OutUEdges, TBitArray<>& VisitedSites)

	{
		// Flood-fill from SiteIndex through adjacent sites connected by edges in EdgeConnectors.
		// This groups Delaunay triangles into merged "super-cells" (e.g. for Urquhart graph construction),
		// collecting the set of connector edges traversed (OutUEdges) and the merged site indices.
		TArray<int32> Stack;

		VisitedSites[SiteIndex] = false;
		Stack.Add(SiteIndex);

		while (!Stack.IsEmpty())
		{
			const int32 NextIndex = Stack.Pop(EAllowShrinking::No);

			if (VisitedSites[NextIndex])
			{
				continue;
			}

			OutMerged.Add(NextIndex);
			VisitedSites[NextIndex] = true;

			const FDelaunaySite2* Site = (Sites.GetData() + NextIndex);

			for (int i = 0; i < 3; i++)
			{
				const int32 OtherIndex = Site->Neighbors[i];
				if (OtherIndex == -1 || VisitedSites[OtherIndex])
				{
					continue;
				}
				const FDelaunaySite2* NeighborSite = Sites.GetData() + OtherIndex;
				if (const uint64 SharedEdge = Site->GetSharedEdge(NeighborSite);
					EdgeConnectors.Contains(SharedEdge))
				{
					OutUEdges.Add(SharedEdge);
					Stack.Add(OtherIndex);
				}
			}
		}

		VisitedSites[SiteIndex] = true;
	}

	FDelaunaySite3::FDelaunaySite3(const FIntVector4& InVtx, const int32 InId)
		: Id(InId)
	{
		for (int i = 0; i < 4; i++)
		{
			Vtx[i] = InVtx[i];
			Faces[i] = 0;
		}

		Algo::Sort(Vtx);
	}

	void FDelaunaySite3::ComputeFaces()
	{
		for (int i = 0; i < 4; i++)
		{
			Faces[i] = PCGEx::UH3(Vtx[MTX[i][0]], Vtx[MTX[i][1]], Vtx[MTX[i][2]]);
		}
	}

	TDelaunay3::~TDelaunay3()
	{
		Clear();
	}

	void TDelaunay3::Clear()
	{
		Sites.Empty();
		DelaunayEdges.Empty();
		DelaunayHull.Empty();

		IsValid = false;
	}

	void TDelaunay3::RemoveLongestEdges(const TArrayView<FVector>& Positions)
	{
		uint64 Edge;
		for (const FDelaunaySite3& Site : Sites)
		{
			GetLongestEdge(Positions, Site.Vtx, Edge);
			DelaunayEdges.Remove(Edge);
		}
	}

	void TDelaunay3::RemoveLongestEdges(const TArrayView<FVector>& Positions, TSet<uint64>& LongestEdges)
	{
		uint64 Edge;
		for (const FDelaunaySite3& Site : Sites)
		{
			GetLongestEdge(Positions, Site.Vtx, Edge);
			DelaunayEdges.Remove(Edge);
			LongestEdges.Add(Edge);
		}
	}
}
