// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExGeo.h"

struct FPCGExGeo2DProjectionDetails;

namespace PCGExMath::Geo
{
	class TDelaunay3;
	class TDelaunay2;

	class PCGEXCORE_API TVoronoi2
	{
	public:
		TSharedPtr<TDelaunay2> Delaunay;

		// Unique Delaunay site pairs (H64U hashes), each adjacency listed exactly once
		TArray<uint64> VoronoiEdges;

		// Metric used for this Voronoi diagram
		EPCGExVoronoiMetric Metric = EPCGExVoronoiMetric::Euclidean;

		// For Euclidean, OutputVertices contains cell centers and OutputEdges mirrors VoronoiEdges
		// For L1/Linf, OutputVertices contains [CellCenters..., BendPoints...] and OutputEdges contains subdivided edges
		TArray<FVector> OutputVertices;
		TArray<uint64> OutputEdges;
		int32 NumCellCenters = 0; // Number of cell centers (first N entries in OutputVertices)

		bool IsValid = false;

		TVoronoi2() = default;
		~TVoronoi2();

	protected:
		void Clear();

		bool ProcessInternal(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, EPCGExCellCenter CellCenterMethod, const FBox* Bounds, TArray<int8>* WithinBounds);

		// Derive unique Voronoi edges (cell-to-cell adjacency) from Delaunay site neighbors
		void BuildVoronoiEdges();

		// Compute cell centers in projected space, unproject them, and build OutputVertices/OutputEdges (with bend subdivision for L1/Linf)
		void BuildMetricOutput(const TArrayView<FVector>& ProjectedPositions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, EPCGExCellCenter CellCenterMethod, const FBox* Bounds, TArray<int8>* WithinBounds);

	public:
		bool Process(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, EPCGExVoronoiMetric InMetric, EPCGExCellCenter CellCenterMethod);
		bool Process(const TArrayView<FVector>& Positions, const FPCGExGeo2DProjectionDetails& ProjectionDetails, const FBox& Bounds, TArray<int8>& WithinBounds, EPCGExVoronoiMetric InMetric, EPCGExCellCenter CellCenterMethod);
	};

	class PCGEXCORE_API TVoronoi3
	{
	public:
		TSharedPtr<TDelaunay3> Delaunay;
		TSet<uint64> VoronoiEdges;
		TArray<FSphere> Circumspheres;
		TArray<FVector> Centroids;

		bool IsValid = false;

		TVoronoi3() = default;
		~TVoronoi3();

	protected:
		void Clear();

	public:
		bool Process(const TArrayView<FVector>& Positions);
	};
}
