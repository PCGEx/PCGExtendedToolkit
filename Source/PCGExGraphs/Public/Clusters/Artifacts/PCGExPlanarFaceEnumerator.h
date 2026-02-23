// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExH.h"

struct FPCGExGeo2DProjectionDetails;

namespace PCGExMath
{
	struct FBestFitPlane;
}

namespace PCGExClusters
{
	class FCluster;
	class FCell;
	class FCellConstraints;
	enum class ECellResult : uint8;

	/**
	 * Half-edge structure for DCEL-based planar face enumeration.
	 * Each undirected edge becomes two half-edges pointing in opposite directions.
	 */
	struct PCGEXGRAPHS_API FHalfEdge
	{
		int32 OriginNode = -1;    // Node index where this half-edge starts
		int32 TargetNode = -1;    // Node index where this half-edge ends
		int32 TwinIndex = -1;     // Index of the opposite half-edge
		int32 NextIndex = -1;     // Index of the next half-edge in the face (CCW)
		int32 FaceIndex = -1;     // Index of the face this half-edge bounds (-1 if not yet assigned)
		double Angle = 0;         // Angle of this half-edge from origin (for sorting)

		FHalfEdge() = default;
		FHalfEdge(int32 InOrigin, int32 InTarget, double InAngle)
			: OriginNode(InOrigin), TargetNode(InTarget), Angle(InAngle)
		{
		}
	};

	/**
	 * Raw face data - lightweight structure for parallel cell building
	 */
	struct PCGEXGRAPHS_API FRawFace
	{
		TArray<int32> Nodes;
		int32 FaceIndex = -1;
		FBox Bounds3D = FBox(ForceInit);  // Lightweight bounds for early culling

		FRawFace() = default;
		explicit FRawFace(int32 InFaceIndex) : FaceIndex(InFaceIndex) {}
	};

	/**
	 * DCEL-based planar face enumerator.
	 * Builds a proper half-edge structure and enumerates all faces by following next pointers.
	 */
	class PCGEXGRAPHS_API FPlanarFaceEnumerator : public TSharedFromThis<FPlanarFaceEnumerator>
	{
	protected:
		TArray<FHalfEdge> HalfEdges;
		TMap<uint64, int32> HalfEdgeMap;  // Maps H64(origin, target) to half-edge index

		const FCluster* Cluster = nullptr;

		/** Node-indexed projected positions (size = NumNodes, access via NodeIndex). nullptr when bIsLocalTangent. */
		TSharedPtr<TArray<FVector2D>> ProjectedPositions;

		/** Per-node tangent frames for LocalTangent mode (node-indexed). nullptr otherwise. */
		TSharedPtr<TArray<FQuat>> NodeTangentFrames;

		/** True when built with local tangent projection (per-node frames instead of global 2D space) */
		bool bIsLocalTangent = false;

		int32 NumFaces = 0;

		// Cached raw faces for reuse
		TArray<FRawFace> CachedRawFaces;
		bool bRawFacesEnumerated = false;

		// Cached adjacency map (lazy-computed, thread-safe)
		mutable FRWLock AdjacencyMapLock;
		mutable TMap<int32, TSet<int32>> CachedAdjacencyMap;
		mutable int32 CachedAdjacencyWrapperIndex = INDEX_NONE;
		mutable bool bAdjacencyMapCached = false;

	public:
		FPlanarFaceEnumerator() = default;

		/**
		 * Build the DCEL structure from a cluster using projection settings.
		 * Internally builds node-indexed projected positions.
		 * @param InCluster The cluster to build from
		 * @param InProjection Projection settings for 2D transformation
		 */
		void Build(const TSharedRef<FCluster>& InCluster, const FPCGExGeo2DProjectionDetails& InProjection);

		/**
		 * Build the DCEL structure from a cluster with pre-computed node-indexed positions.
		 * @param InCluster The cluster to build from
		 * @param InNodeIndexedPositions Pre-computed 2D positions indexed by node index (size must equal cluster node count)
		 */
		void Build(const TSharedRef<FCluster>& InCluster, const TSharedPtr<TArray<FVector2D>>& InNodeIndexedPositions);

		/**
		 * Build the DCEL structure using per-node local tangent frames for non-planar clusters.
		 * Half-edge angles are computed in each origin node's local tangent frame.
		 * @param InCluster The cluster to build from
		 * @param InNodeTangentFrames Per-node tangent frame quaternions (node-indexed)
		 */
		void Build(const TSharedRef<FCluster>& InCluster, const TSharedPtr<TArray<FQuat>>& InNodeTangentFrames);

		/**
		 * Enumerate raw faces (serial operation).
		 * Call this once, then use BuildCellsFromRawFaces for parallel cell building.
		 * @return Reference to cached raw faces
		 */
		const TArray<FRawFace>& EnumerateRawFaces();

		/**
		 * Build cells from raw faces. Can be called in parallel per-face.
		 * @param InRawFace The raw face data
		 * @param OutCell Output cell (caller should allocate)
		 * @param Constraints Cell constraints for filtering
		 * @return Cell result status
		 */
		ECellResult BuildCellFromRawFace(
			const FRawFace& InRawFace,
			TSharedPtr<FCell>& OutCell,
			const TSharedRef<FCellConstraints>& Constraints) const;

		/**
		 * Enumerate all faces and create cells (convenience method, combines EnumerateRawFaces + BuildCellFromRawFace).
		 * @param OutCells Output array of cells (faces that pass constraints)
		 * @param Constraints Cell constraints for filtering
		 * @param OutFailedCells Optional output array of cells that failed constraints (but have valid polygons for containment testing)
		 * @param bDetectWrapper If true, detects wrapper by winding (CW face), stores in Constraints->WrapperCell, and excludes from OutCells
		 */
		void EnumerateAllFaces(TArray<TSharedPtr<FCell>>& OutCells, const TSharedRef<FCellConstraints>& Constraints, TArray<TSharedPtr<FCell>>* OutFailedCells = nullptr, bool bDetectWrapper = false);

		/**
		 * Enumerate faces that potentially match the bounds filter (skip definitely-Outside faces).
		 * Uses early AABB culling to skip building full FCell objects for faces outside the bounds.
		 * @param OutCells Output array of cells that pass constraints AND might be Inside/Touching
		 * @param Constraints Cell constraints for filtering
		 * @param BoundsFilter 3D bounds filter for early culling
		 * @param bIncludeOutside If true, don't skip Outside faces (disables optimization, same as EnumerateAllFaces)
		 * @param OutFailedCells Optional output for cells that failed constraints
		 * @param bDetectWrapper If true, detect wrapper cell
		 */
		void EnumerateFacesWithinBounds(
			TArray<TSharedPtr<FCell>>& OutCells,
			const TSharedRef<FCellConstraints>& Constraints,
			const FBox& BoundsFilter,
			bool bIncludeOutside = false,
			TArray<TSharedPtr<FCell>>* OutFailedCells = nullptr,
			bool bDetectWrapper = false);
		
		/**
		 * Find the face containing a given 2D point.
		 * @param Point The 2D point to test
		 * @return Face index, or -1 if not found
		 */
		int32 FindFaceContaining(const FVector2D& Point) const;

		/**
		 * Get the outer (wrapper) face index.
		 * This is the unbounded face surrounding the entire graph.
		 * @return Index of the wrapper face, or -1 if not found
		 */
		int32 GetWrapperFaceIndex() const;

		FORCEINLINE bool IsBuilt() const { return !HalfEdges.IsEmpty(); }
		FORCEINLINE bool IsLocalTangent() const { return bIsLocalTangent; }
		FORCEINLINE int32 GetNumHalfEdges() const { return HalfEdges.Num(); }
		FORCEINLINE int32 GetNumFaces() const { return NumFaces; }
		FORCEINLINE const FCluster* GetCluster() const { return Cluster; }

		/** Get a half-edge by index */
		FORCEINLINE const FHalfEdge& GetHalfEdge(int32 Index) const { return HalfEdges[Index]; }

		/** Get read-only access to all half-edges */
		FORCEINLINE const TArray<FHalfEdge>& GetHalfEdges() const { return HalfEdges; }

		/** Get node-indexed projected positions (access via NodeIndex, not PointIndex) */
		FORCEINLINE const TSharedPtr<TArray<FVector2D>>& GetProjectedPositions() const { return ProjectedPositions; }

		/**
		 * Get half-edge index for a directed edge.
		 * @return Half-edge index, or -1 if not found
		 */
		FORCEINLINE int32 GetHalfEdgeIndex(int32 FromNode, int32 ToNode) const
		{
			const int32* Found = HalfEdgeMap.Find(PCGEx::H64(FromNode, ToNode));
			return Found ? *Found : -1;
		}

		/**
		 * Build adjacency map for all faces.
		 * Uses twin half-edges: if HalfEdge[i].FaceIndex = A and HalfEdge[HalfEdge[i].TwinIndex].FaceIndex = B,
		 * then faces A and B are adjacent.
		 * @param WrapperFaceIndex Optional face index to exclude from adjacency (typically the unbounded exterior face)
		 * @return Map of FaceIndex -> Set of adjacent FaceIndices
		 */
		TMap<int32, TSet<int32>> BuildCellAdjacencyMap(int32 WrapperFaceIndex = -1) const;

		/**
		 * Get or build cached adjacency map for all faces.
		 * Lazy-computes on first call, returns cached result on subsequent calls.
		 * @param WrapperFaceIndex Optional face index to exclude from adjacency (typically the unbounded exterior face)
		 * @return Reference to cached map of FaceIndex -> Set of adjacent FaceIndices
		 */
		const TMap<int32, TSet<int32>>& GetOrBuildAdjacencyMap(int32 WrapperFaceIndex = -1) const;

		/**
		 * Get adjacent face indices for a specific face.
		 * Requires EnumerateRawFaces() to have been called first.
		 * @param FaceIndex The face to query
		 * @param OutAdjacentFaces Output array of adjacent face indices
		 * @param WrapperFaceIndex Optional face index to exclude from results
		 */
		void GetAdjacentFaces(int32 FaceIndex, TArray<int32>& OutAdjacentFaces, int32 WrapperFaceIndex = -1) const;

		/**
		 * Get the half-edges that belong to a specific face.
		 * @param FaceIndex The face to query
		 * @param OutHalfEdgeIndices Output array of half-edge indices belonging to this face
		 */
		void GetFaceHalfEdges(int32 FaceIndex, TArray<int32>& OutHalfEdgeIndices) const;

	protected:
		/** Build a cell from a face (list of node indices) - internal use */
		ECellResult BuildCellFromFace(
			const TArray<int32>& FaceNodes,
			TSharedPtr<FCell>& OutCell,
			const TSharedRef<FCellConstraints>& Constraints) const;
	};
}
