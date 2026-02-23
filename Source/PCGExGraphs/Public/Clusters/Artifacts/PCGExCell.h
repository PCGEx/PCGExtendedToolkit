// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/PCGExScopedContainers.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Clusters/PCGExEdge.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Math/PCGExProjectionDetails.h"
#include "Math/PCGExWinding.h"

class UPCGBasePointData;
struct FPCGExCellConstraintsDetails;

namespace PCGExData
{
	struct FMutablePoint;
}

struct FPCGExNodeSelectionDetails;

namespace PCGExMath
{
	struct FTriangle;
}

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExClusters
{
	namespace Labels
	{
		const FName SourceEdgeConstrainsFiltersLabel = FName("ConstrainedEdgeFilters");
		const FName SourceHolesLabel = FName("Holes");
	}

	PCGEXGRAPHS_API
	void SetPointProperty(PCGExData::FMutablePoint& InPoint, const double InValue, const EPCGExPointPropertyOutput InProperty);

#pragma region Cell

	enum class ECellResult : uint8
	{
		Unknown = 0,
		Success,
		Duplicate,
		Leaf,
		Hole,
		WrongAspect,
		OutsidePointsLimit,
		OutsideBoundsLimit,
		OutsideAreaLimit,
		OutsidePerimeterLimit,
		OutsideCompactnessLimit,
		OutsideSegmentsLimit,
		OpenCell,
		WrapperCell,
		MalformedCluster,
	};

	class FCell;
	class FCellConstraints;
	struct FNode;

	/**
	 * Unified point set for Seeds/Holes - projects points to 2D and provides AABB-optimized overlap checks.
	 * Thread-safe lazy projection with coarse AABB culling before fine polygon checks.
	 */
	class PCGEXGRAPHS_API FProjectedPointSet : public TSharedFromThis<FProjectedPointSet>
	{
	protected:
		mutable FRWLock ProjectionLock;

		TSharedRef<PCGExData::FFacade> PointDataFacade;
		FPCGExGeo2DProjectionDetails ProjectionDetails;
		TArray<FVector2D> ProjectedPoints;
		FBox2D TightBounds;

		/** 3D AABB of source points (computed alongside 2D projection) */
		FBox TightBounds3D;

	public:
		explicit FProjectedPointSet(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const FPCGExGeo2DProjectionDetails& InProjectionDetails)
			: PointDataFacade(InPointDataFacade), ProjectionDetails(InProjectionDetails), TightBounds(ForceInit), TightBounds3D(ForceInit)
		{
			if (ProjectionDetails.Method == EPCGExProjectionMethod::Normal) { ProjectionDetails.Init(PointDataFacade); }
		}

		/** Lazy project all points + compute tight AABB (thread-safe) */
		void EnsureProjected();

		/** Check if any point overlaps polygon (with AABB early-out) */
		bool OverlapsPolygon(const TArray<FVector2D>& Polygon, const FBox2D& PolygonBounds) const;

		/** Check if any source point overlaps polygon using a per-face local projection (for LocalTangent mode) */
		bool OverlapsPolygonLocal(
			const TArray<FVector2D>& Polygon,
			const FBox2D& PolygonBounds,
			const FBox& FaceBounds3D,
			const FPCGExGeo2DProjectionDetails& FaceProjection) const;

		/** Get projected point by index (for FindCells seed tracking). Caller must call EnsureProjected() before using in loops. */
		FORCEINLINE const FVector2D& GetProjected(int32 Index) const
		{
			return ProjectedPoints[Index];
		}

		int32 Num() const;
		FORCEINLINE const FBox2D& GetBounds() const { return TightBounds; }
	};

	class PCGEXGRAPHS_API FCellConstraints : public TSharedFromThis<FCellConstraints>
	{
	protected:
		PCGExMT::TH64SetShards<> UniquePathsHashSet;
		PCGExMT::TH64SetShards<> UniqueStartHalfEdgesHash;

	public:
		EPCGExWinding Winding = EPCGExWinding::CounterClockwise;

		bool bConcaveOnly = false;
		bool bConvexOnly = false;
		bool bKeepCellsWithLeaves = true;
		bool bDuplicateLeafPoints = false;

		int32 MaxPointCount = MAX_int32;
		int32 MinPointCount = MIN_int32;

		double MaxBoundsSize = MAX_dbl;
		double MinBoundsSize = MIN_dbl_neg;

		double MaxArea = MAX_dbl;
		double MinArea = MIN_dbl_neg;

		double MaxPerimeter = MAX_dbl;
		double MinPerimeter = MIN_dbl_neg;

		double MaxSegmentLength = MAX_dbl;
		double MinSegmentLength = MIN_dbl_neg;

		double MaxCompactness = MAX_dbl;
		double MinCompactness = MIN_dbl_neg;

		bool bBuildWrapper = true;

		TSharedPtr<FCell> WrapperCell;
		TSharedPtr<FProjectedPointSet> Holes;
		TSharedPtr<FPlanarFaceEnumerator> Enumerator;

		FCellConstraints()
		{
		}

		explicit FCellConstraints(const FPCGExCellConstraintsDetails& InDetails);

		void Reserve(const int32 InCellHashReserve);
		bool ContainsSignedEdgeHash(const uint64 Hash);
		bool IsUniqueStartHalfEdge(const uint64 Hash);
		bool IsUniqueCellHash(const TSharedPtr<FCell>& InCell);

		/**
		 * Build or get the shared enumerator. Call this once to build the DCEL, then reuse.
		 * @param InCluster The cluster to build from
		 * @param ProjectionDetails Projection settings for building node-indexed positions and cache lookup/storage
		 */
		TSharedPtr<FPlanarFaceEnumerator> GetOrBuildEnumerator(
			const TSharedRef<FCluster>& InCluster,
			const FPCGExGeo2DProjectionDetails& ProjectionDetails);

		/** Build wrapper cell using the shared enumerator */
		void BuildWrapperCell(const TSharedPtr<FCellConstraints>& InConstraints = nullptr);

		/** Convenience method - builds enumerator internally if needed */
		void BuildWrapperCell(const TSharedRef<FCluster>& InCluster, const FPCGExGeo2DProjectionDetails& ProjectionDetails);

		void Cleanup();
	};

	struct PCGEXGRAPHS_API FCellData
	{
		int8 bIsValid = 0;
		uint32 CellHash = 0;
		FBox Bounds = FBox(ForceInit);
		FVector Centroid = FVector::ZeroVector;
		double Area = 0;
		double Perimeter = 0;
		double Compactness = 0;
		bool bIsConvex = true;
		bool bIsClockwise = false;
		bool bIsClosedLoop = false;

		FCellData() = default;
	};

	/**
	 * Expansion tracking data for a cell.
	 * Used when seeds/holes expand to adjacent cells via growth.
	 */
	struct PCGEXGRAPHS_API FCellExpansionData
	{
		int32 PickCount = 0;        // How many times this cell was selected
		int32 MinDepth = MAX_int32; // Minimum depth at which selected (0 = direct match)
		TSet<int32> SourceIndices;  // Which source indices (seed/hole) selected this cell

		FORCEINLINE void RecordPick(const int32 SourceIndex, const int32 Depth)
		{
			PickCount++;
			MinDepth = FMath::Min(MinDepth, Depth);
			SourceIndices.Add(SourceIndex);
		}

		FORCEINLINE void Reset()
		{
			PickCount = 0;
			MinDepth = MAX_int32;
			SourceIndices.Reset();
		}
	};

	class PCGEXGRAPHS_API FCell : public TSharedFromThis<FCell>
	{
	protected:
		int32 Sign = 0;
		uint64 CellHash = 0;

	public:
		FBox2D Bounds2D;
		TArray<int32> Nodes;
		TSharedRef<FCellConstraints> Constraints;

		FCellData Data = FCellData();

		PCGExGraphs::FLink Seed = PCGExGraphs::FLink(-1, -1);

		bool bBuiltSuccessfully = false;

		TArray<FVector2D> Polygon;

		int32 CustomIndex = -1;
		int32 FaceIndex = -1;  // Index from the planar face enumerator (for adjacency lookups)

		// Expansion tracking (populated when seeds grow)
		int32 ExpansionPickCount = 0;  // Number of times this cell was selected (by seeds/growth)
		int32 ExpansionMinDepth = 0;   // Minimum depth at which cell was picked (0 = direct seed)

		explicit FCell(const TSharedRef<FCellConstraints>& InConstraints)
			: Constraints(InConstraints)
		{
			Data.bIsValid = true;
		}

		~FCell() = default;

		uint64 GetCellHash();
		void PostProcessPoints(UPCGBasePointData* InMutablePoints);
	};

#pragma endregion
}
