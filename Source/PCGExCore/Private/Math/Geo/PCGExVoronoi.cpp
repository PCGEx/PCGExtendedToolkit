// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/Geo/PCGExVoronoi.h"

#include "CoreMinimal.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExArrayHelpers.h"

#include "Math/PCGExProjectionDetails.h"
#include "Math/Geo/PCGExDelaunay.h"
#include "Math/Geo/PCGExGeo.h"

namespace PCGExMath::Geo
{
#pragma region TVoronoi2

	TVoronoi2::~TVoronoi2()
	{
		Clear();
	}

	void TVoronoi2::Clear()
	{
		Delaunay.Reset();
		VoronoiEdges.Empty();
		OutputVertices.Empty();
		OutputEdges.Empty();
		NumCellCenters = 0;
		Metric = EPCGExVoronoiMetric::Euclidean;
		IsValid = false;
	}

	bool TVoronoi2::Process(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, EPCGExVoronoiMetric InMetric, EPCGExCellCenter CellCenterMethod)
	{
		Clear();
		Metric = InMetric;
		return ProcessInternal(Positions, ProjectionDetails, CellCenterMethod, nullptr, nullptr);
	}

	bool TVoronoi2::Process(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, const FBox& Bounds, TArray<int8>& WithinBounds, EPCGExVoronoiMetric InMetric, EPCGExCellCenter CellCenterMethod)
	{
		Clear();
		Metric = InMetric;
		return ProcessInternal(Positions, ProjectionDetails, CellCenterMethod, &Bounds, &WithinBounds);
	}

	bool TVoronoi2::ProcessInternal(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, EPCGExCellCenter CellCenterMethod, const FBox* Bounds, TArray<int8>* WithinBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TVoronoi2::Process);

		// Project once; the same projected positions feed the triangulation and the
		// cell center computation in BuildMetricOutput.
		TArray<FVector> ProjectedPositions;
		ProjectionDetails.Project(Positions, ProjectedPositions);

		Delaunay = MakeShared<TDelaunay2>();

		// Voronoi construction needs sites/adjacency + hull, never the Delaunay edge set
		if (!Delaunay->ProcessProjected(ProjectedPositions, false, true))
		{
			Clear();
			return IsValid;
		}

		BuildVoronoiEdges();

		IsValid = true;
		// BuildMetricOutput computes final positions and checks bounds after unprojection
		BuildMetricOutput(ProjectedPositions, ProjectionDetails, CellCenterMethod, Bounds, WithinBounds);
		return IsValid;
	}

	void TVoronoi2::BuildVoronoiEdges()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TVoronoi2::BuildVoronoiEdges);

		const TArray<FDelaunaySite2>& Sites = Delaunay->Sites;

		// ~1.5 unique edges per site for a typical triangulation
		VoronoiEdges.Reserve(Sites.Num() * 2);

		for (const FDelaunaySite2& Site : Sites)
		{
			for (int i = 0; i < 3; i++)
			{
				// Each unordered pair is claimed by its lower-id site, so every edge is
				// appended exactly once - no dedup required. Also filters out -1.
				const int32 AdjacentIdx = Site.Neighbors[i];
				if (AdjacentIdx > Site.Id)
				{
					VoronoiEdges.Add(PCGEx::H64U(Site.Id, AdjacentIdx));
				}
			}
		}
	}

	void TVoronoi2::BuildMetricOutput(const TArrayView<FVector>& ProjectedPositions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, EPCGExCellCenter CellCenterMethod, const FBox* Bounds, TArray<int8>* WithinBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TVoronoi2::BuildMetricOutput);

		const int32 NumSites = Delaunay->Sites.Num();
		NumCellCenters = NumSites;

		const bool bEuclidean = Metric == EPCGExVoronoiMetric::Euclidean;

		if (WithinBounds)
		{
			WithinBounds->Init(1, NumSites);
		}

		// Cell centers first; bend points are appended after them for L1/Linf
		OutputVertices.Reserve(bEuclidean ? NumSites : NumSites + VoronoiEdges.Num());
		OutputVertices.SetNumUninitialized(NumSites);

		// Projected centers are cached for the L1/Linf bend pass below, so they are
		// never recomputed per edge.
		TArray<FVector> ProjectedCenters;
		if (!bEuclidean)
		{
			ProjectedCenters.SetNumUninitialized(NumSites);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TVoronoi2::CellCenters);

			const TArray<FDelaunaySite2>& Sites = Delaunay->Sites;

			PCGEX_PARALLEL_FOR(
				NumSites,
				const FDelaunaySite2& Site = Sites[i];

				FVector ProjectedCenter;
				if (CellCenterMethod == EPCGExCellCenter::Centroid)
				{
					GetCentroid(ProjectedPositions, Site.Vtx, ProjectedCenter);
				}
				else if (CellCenterMethod == EPCGExCellCenter::Circumcenter)
				{
					GetCircumcenter2D(ProjectedPositions, Site.Vtx, ProjectedCenter);
				}
				else // Balanced
				{
					GetCircumcenter2D(ProjectedPositions, Site.Vtx, ProjectedCenter);
					if (Bounds && !Bounds->IsInside(ProjectionDetails.Unproject(ProjectedCenter)))
					{
						GetCentroid(ProjectedPositions, Site.Vtx, ProjectedCenter);
					}
				}

				const FVector Unprojected = ProjectionDetails.Unproject(ProjectedCenter);
				OutputVertices[i] = Unprojected;

				if (!bEuclidean)
				{
					ProjectedCenters[i] = ProjectedCenter;
				}

				if (Bounds && WithinBounds)
				{
					(*WithinBounds)[i] = Bounds->IsInside(Unprojected) ? 1 : 0;
				}
				)
		}

		if (bEuclidean)
		{
			// Direct edges only. VoronoiEdges hashes already use the same (H64A, H64B)
			// encoding OutputEdges expects, so this is a plain copy.
			OutputEdges = VoronoiEdges;
			return;
		}

		// L1/Linf: subdivide each edge with at most one bend point
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TVoronoi2::MetricEdges);

			const bool bManhattan = Metric == EPCGExVoronoiMetric::Manhattan;

			OutputEdges.Reserve(VoronoiEdges.Num() * 2);

			for (const uint64 EdgeHash : VoronoiEdges)
			{
				const int32 SiteA = PCGEx::H64A(EdgeHash);
				const int32 SiteB = PCGEx::H64B(EdgeHash);

				const FVector& ProjectedCenterA = ProjectedCenters[SiteA];
				const FVector& ProjectedCenterB = ProjectedCenters[SiteB];

				const FVector2D Start2D(ProjectedCenterA.X, ProjectedCenterA.Y);
				const FVector2D End2D(ProjectedCenterB.X, ProjectedCenterB.Y);

				FVector2D Bend2D;
				const bool bHasBend = bManhattan ? ComputeL1Bend(Start2D, End2D, Bend2D) : ComputeLInfBend(Start2D, End2D, Bend2D);

				if (!bHasBend)
				{
					// No bend point, direct edge
					OutputEdges.Add(PCGEx::H64(SiteA, SiteB));
					continue;
				}

				// Interpolate Z halfway in projected space, then unproject to get the bend on the plane
				const FVector ProjectedBend(Bend2D.X, Bend2D.Y, (ProjectedCenterA.Z + ProjectedCenterB.Z) * 0.5);
				const FVector BendPoint3D = ProjectionDetails.Unproject(ProjectedBend);

				const int32 BendIdx = OutputVertices.Add(BendPoint3D);

				// If the bend point is out of bounds, mark both connected sites as out of bounds
				if (Bounds && WithinBounds && !Bounds->IsInside(BendPoint3D))
				{
					(*WithinBounds)[SiteA] = 0;
					(*WithinBounds)[SiteB] = 0;
				}

				// Previous-to-bend, then bend-to-end
				OutputEdges.Add(PCGEx::H64(SiteA, BendIdx));
				OutputEdges.Add(PCGEx::H64(BendIdx, SiteB));
			}
		}
	}

#pragma endregion

#pragma region TVoronoi3

	TVoronoi3::~TVoronoi3()
	{
		Clear();
	}

	void TVoronoi3::Clear()
	{
		Delaunay.Reset();
		Centroids.Empty();
		IsValid = false;
	}

	bool TVoronoi3::Process(const TArrayView<FVector>& Positions)
	{
		IsValid = false;
		Delaunay = MakeShared<TDelaunay3>();

		if (!Delaunay->Process<true, false>(Positions))
		{
			Clear();
			return IsValid;
		}

		const int32 NumSites = Delaunay->Sites.Num();
		PCGExArrayHelpers::InitArray(Circumspheres, NumSites);
		PCGExArrayHelpers::InitArray(Centroids, NumSites);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GeoVoronoi::FindVoronoiEdges);

			for (FDelaunaySite3& Site : Delaunay->Sites)
			{
				FindSphereFrom4Points(Positions, Site.Vtx, Circumspheres[Site.Id]);
				GetCentroid(Positions, Site.Vtx, Centroids[Site.Id]);
			}

			for (const TPair<uint64, uint64>& AdjacencyPair : Delaunay->Adjacency)
			{
				int32 A = -1;
				int32 B = -1;
				PCGEx::NH64(AdjacencyPair.Value, A, B);

				if (A == -1 || B == -1)
				{
					continue;
				}

				VoronoiEdges.Add(PCGEx::H64U(A, B));
			}
		}

		IsValid = true;
		return IsValid;
	}

#pragma endregion
}
