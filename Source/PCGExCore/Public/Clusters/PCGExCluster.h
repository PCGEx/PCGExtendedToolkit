// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExClusterCache.h"
#include "PCGExClusterCommon.h"
#include "PCGExEdge.h"
#include "PCGExNode.h"
#include "PCGExOctree.h"
#include "Containers/PCGExIndexLookup.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Utils/PCGValueRange.h"

namespace PCGExGraphs
{
	class FSubGraph;
}

class UPCGBasePointData;
struct FPCGExContext;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FPointIO;
	class FFacade;
	class FFacadePreloader;
}

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExClusters
{
	struct FBoundedEdge;
	class ICachedClusterData;
}

namespace PCGExClusters
{
	using PCGExGraphs::FLink;
	using PCGExGraphs::FEdge;

	class PCGEXCORE_API FCluster : public TSharedFromThis<FCluster>
	{
	protected:
		bool bIsMirror = false;

		TSharedPtr<FCluster> OriginalCluster = nullptr;

		mutable FRWLock ClusterLock;

		TMap<FName, TSharedPtr<ICachedClusterData>> CachedData;

		// Internal helpers for O(1) visited tracking (uses TBitArray instead of TArray::Contains)
		void GetConnectedNodesInternal(const int32 FromIndex, TArray<int32>& OutIndices, TBitArray<>& Visited, const int32 SearchDepth) const;
		void GetConnectedNodesInternal(const int32 FromIndex, TArray<int32>& OutIndices, TBitArray<>& Visited, const int32 SearchDepth, const TSet<int32>& Skip) const;
		void GetConnectedEdgesInternal(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, TBitArray<>& VisitedNodes, TBitArray<>& VisitedEdges, const int32 SearchDepth) const;
		void GetConnectedEdgesInternal(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, TBitArray<>& VisitedNodes, TBitArray<>& VisitedEdges, const int32 SearchDepth, const TSet<int32>& SkipNodes, const TSet<int32>& SkipEdges) const;

	public:
		int32 NumRawVtx = 0;
		int32 NumRawEdges = 0;

		FNode* NodesDataPtr = nullptr;
		FEdge* EdgesDataPtr = nullptr;

		bool bValid = false;
		bool bIsOneToOne = false; // Whether the input data has a single set of edges for a single set of vtx

		int32 ClusterID = -1;
		TSharedPtr<PCGEx::FIndexLookup> NodeIndexLookup; // Point Index -> Node index
		//TMap<uint64, int32> EdgeIndexLookup;   // Edge Hash -> Edge Index
		TSharedPtr<TArray<FNode>> Nodes;
		TSharedPtr<TArray<FBoundedEdge>> BoundedEdges;
		TSharedPtr<TArray<FEdge>> Edges;
		/** Raw world-space edge lengths, built by ComputeEdgeLengths. Consumers needing a
		 * normalized value scale explicitly (MinEdgeLength/MaxEdgeLength are kept alongside). */
		TSharedPtr<TArray<double>> EdgeLengths;
		double MinEdgeLength = 0;
		double MaxEdgeLength = 0;
		TConstPCGValueRange<FTransform> VtxTransforms;

		FBox Bounds = FBox(NoInit);
		FVector2D ProjectedCentroid = FVector2D::ZeroVector;

		const UPCGBasePointData* VtxPoints = nullptr;

		TWeakPtr<PCGExData::FPointIO> VtxIO;
		TWeakPtr<PCGExData::FPointIO> EdgesIO;

		TSharedPtr<PCGExOctree::FItemOctree> NodeOctree;
		TSharedPtr<PCGExOctree::FItemOctree> EdgeOctree;

		/**
		 * Get cached data by key, optionally validating context hash.
		 * @param Key Cache key (e.g., "FaceEnumerator")
		 * @param ExpectedContextHash Expected hash, 0 = don't care
		 * @return Cached data, nullptr if not found or hash mismatch
		 */
		template <typename T>
		TSharedPtr<T> GetCachedData(FName Key, uint32 ExpectedContextHash = 0) const
		{
			FReadScopeLock ReadLock(ClusterLock);
			if (const TSharedPtr<ICachedClusterData>* Entry = CachedData.Find(Key))
			{
				if (ExpectedContextHash == 0 || (*Entry)->ContextHash == ExpectedContextHash)
				{
					return StaticCastSharedPtr<T>(*Entry);
				}
			}
			return nullptr;
		}

		void SetCachedData(FName Key, const TSharedPtr<ICachedClusterData>& Data);
		void ClearCachedData();

		FCluster(const TSharedPtr<PCGExData::FPointIO>& InVtxIO, const TSharedPtr<PCGExData::FPointIO>& InEdgesIO, const TSharedPtr<PCGEx::FIndexLookup>& InNodeIndexLookup);
		FCluster(const TSharedRef<FCluster>& OtherCluster, const TSharedPtr<PCGExData::FPointIO>& InVtxIO, const TSharedPtr<PCGExData::FPointIO>& InEdgesIO, const TSharedPtr<PCGEx::FIndexLookup>& InNodeIndexLookup, bool bCopyNodes, bool bCopyEdges, bool bCopyLookup);

		class PCGEXCORE_API TConstVtxLookup
		{
		public:
			TConstVtxLookup(const TSharedPtr<FCluster>& InCluster)
				: NodesArray(*InCluster->Nodes.Get())
			{
			}

			int32 Num() const
			{
				return NodesArray.Num();
			}

			const int32& operator[](const int32 Index) const
			{
				return NodesArray[Index].PointIndex;
			}

			void Dump(TArray<int32>& OutIndices) const;

		private:
			const TArray<FNode>& NodesArray;
		};

		class TVtxLookup
		{
		public:
			TVtxLookup(const TSharedPtr<FCluster>& InCluster)
				: NodesArray(*InCluster->Nodes.Get())
			{
			}

			int32 Num() const
			{
				return NodesArray.Num();
			}

			int32& operator[](const int32 Index)
			{
				return NodesArray[Index].PointIndex;
			}

			const int32& operator[](const int32 Index) const
			{
				return NodesArray[Index].PointIndex;
			}

			void Dump(TArray<int32>& OutIndices) const;

		private:
			TArray<FNode>& NodesArray;
		};

		void ClearInheritedForChanges(const bool bClearOwned = false);
		void WillModifyVtxIO(const bool bClearOwned = false);
		void WillModifyVtxPositions(const bool bClearOwned = false);

		~FCluster();

		bool BuildFrom(const TMap<uint32, int32>& InEndpointsLookup, const TArray<int32>* InExpectedAdjacency);

		/**
		 * Builds this cluster from pre-flattened subgraph edges.
		 * Preconditions (satisfied by FSubGraph::BuildCluster, the only intended call path):
		 * - NodeIndexLookup is non-null and holds -1 for every point index appearing in
		 *   InEdges (it is created fresh per graph compile); entries are written during
		 *   the build and become the lookup's final Point Index -> Node Index content.
		 * - Subgraphs partition vtx points, so concurrent builds sharing the lookup
		 *   write disjoint entries.
		 * - InNumNodes is the exact number of unique endpoints in InEdges.
		 */
		void BuildFromSubgraphData(const TSharedPtr<PCGExData::FFacade>& InVtxFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeFacade, const TArray<FEdge>& InEdges, const int32 InNumNodes);

		bool IsValidWith(const TSharedRef<PCGExData::FPointIO>& InVtxIO, const TSharedRef<PCGExData::FPointIO>& InEdgesIO) const;
		bool HasTag(const FString& InTag);

		FORCEINLINE FNode* GetNode(const int32 Index) const
		{
			return (NodesDataPtr + Index);
		}

		FORCEINLINE FNode* GetNode(const FLink Lk) const
		{
			return (NodesDataPtr + Lk.Node);
		}

		FORCEINLINE int32 GetNodePointIndex(const int32 Index) const
		{
			return (NodesDataPtr + Index)->PointIndex;
		}

		FORCEINLINE int32 GetNodePointIndex(const FLink Lk) const
		{
			return (NodesDataPtr + Lk.Node)->PointIndex;
		}

		FORCEINLINE FEdge* GetEdge(const int32 Index) const
		{
			return (EdgesDataPtr + Index);
		}

		FORCEINLINE FEdge* GetEdge(const FLink Lk) const
		{
			return (EdgesDataPtr + Lk.Edge);
		}

		FORCEINLINE FNode* GetEdgeStart(const FEdge* InEdge) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get(InEdge->Start));
		}

		FORCEINLINE FNode* GetEdgeStart(const FEdge& InEdge) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get(InEdge.Start));
		}

		FORCEINLINE FNode* GetEdgeStart(const int32 InEdgeIndex) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get((EdgesDataPtr + InEdgeIndex)->Start));
		}

		FORCEINLINE FNode* GetEdgeEnd(const FEdge* InEdge) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get(InEdge->End));
		}

		FORCEINLINE FNode* GetEdgeEnd(const FEdge& InEdge) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get(InEdge.End));
		}

		FORCEINLINE FNode* GetEdgeEnd(const int32 InEdgeIndex) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get((EdgesDataPtr + InEdgeIndex)->End));
		}

		FORCEINLINE FNode* GetEdgeOtherNode(const int32 InEdgeIndex, const int32 InNodeIndex) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get((EdgesDataPtr + InEdgeIndex)->Other((NodesDataPtr + InNodeIndex)->PointIndex)));
		}

		FORCEINLINE FNode* GetEdgeOtherNode(const FLink Lk) const
		{
			return (NodesDataPtr + NodeIndexLookup->Get((EdgesDataPtr + Lk.Edge)->Other((NodesDataPtr + Lk.Node)->PointIndex)));
		}

		// Returns true if at least one endpoint has >= MinNeighbors links (caches both node lookups)
		FORCEINLINE bool EdgeHasMinNeighbors(const int32 InEdgeIndex, const int32 MinNeighbors) const
		{
			const FEdge* Edge = EdgesDataPtr + InEdgeIndex;
			const FNode* Start = NodesDataPtr + NodeIndexLookup->Get(Edge->Start);
			if (Start->Links.Num() >= MinNeighbors)
			{
				return true;
			}
			const FNode* End = NodesDataPtr + NodeIndexLookup->Get(Edge->End);
			return End->Links.Num() >= MinNeighbors;
		}

		// Returns true if edge and both endpoints are valid (caches both node lookups)
		FORCEINLINE bool IsEdgeFullyValid(const FEdge& InEdge) const
		{
			if (!InEdge.bValid)
			{
				return false;
			}
			const FNode* Start = NodesDataPtr + NodeIndexLookup->Get(InEdge.Start);
			if (!Start->bValid)
			{
				return false;
			}
			const FNode* End = NodesDataPtr + NodeIndexLookup->Get(InEdge.End);
			return End->bValid != 0;
		}

		FORCEINLINE FVector GetStartPos(const FEdge& InEdge) const
		{
			return VtxTransforms[InEdge.Start].GetLocation();
		}

		FORCEINLINE FVector GetStartPos(const FEdge* InEdge) const
		{
			return VtxTransforms[InEdge->Start].GetLocation();
		}

		FORCEINLINE FVector GetStartPos(const int32 InEdgeIndex) const
		{
			return VtxTransforms[(EdgesDataPtr + InEdgeIndex)->Start].GetLocation();
		}

		FORCEINLINE FVector GetEndPos(const FEdge& InEdge) const
		{
			return VtxTransforms[InEdge.End].GetLocation();
		}

		FORCEINLINE FVector GetEndPos(const FEdge* InEdge) const
		{
			return VtxTransforms[InEdge->End].GetLocation();
		}

		FORCEINLINE FVector GetEndPos(const int32 InEdgeIndex) const
		{
			return VtxTransforms[(EdgesDataPtr + InEdgeIndex)->End].GetLocation();
		}

		FORCEINLINE FVector GetPos(const FNode& InNode) const
		{
			return VtxTransforms[InNode.PointIndex].GetLocation();
		}

		FORCEINLINE FVector GetPos(const FNode* InNode) const
		{
			return VtxTransforms[InNode->PointIndex].GetLocation();
		}

		FORCEINLINE FVector GetPos(const int32 Index) const
		{
			return VtxTransforms[(NodesDataPtr + Index)->PointIndex].GetLocation();
		}

		FORCEINLINE FVector GetPos(const FLink Lk) const
		{
			return VtxTransforms[(NodesDataPtr + Lk.Node)->PointIndex].GetLocation();
		}

		double GetDist(const FEdge* InEdge) const
		{
			return FVector::Dist(VtxTransforms[InEdge->Start].GetLocation(), VtxTransforms[InEdge->End].GetLocation());
		}

		double GetDist(const FEdge& InEdge) const
		{
			return FVector::Dist(VtxTransforms[InEdge.Start].GetLocation(), VtxTransforms[InEdge.End].GetLocation());
		}

		double GetDist(const int32 InEdgeIndex) const
		{
			return GetDist(*(EdgesDataPtr + InEdgeIndex));
		}

		double GetDist(const int32 NodeA, const int32 NodeB) const
		{
			return FVector::Dist(GetPos(NodeA), GetPos(NodeB));
		}

		double GetDist(const FNode& A, const FNode& B) const
		{
			return FVector::Dist(VtxTransforms[A.PointIndex].GetLocation(), VtxTransforms[B.PointIndex].GetLocation());
		}

		double GetDistSquared(const FEdge& InEdge) const
		{
			return FVector::DistSquared(VtxTransforms[InEdge.Start].GetLocation(), VtxTransforms[InEdge.End].GetLocation());
		}

		double GetDistSquared(const int32 InEdgeIndex) const
		{
			return GetDist(*(EdgesDataPtr + InEdgeIndex));
		}

		double GetDistSquared(const int32 NodeA, const int32 NodeB) const
		{
			return FVector::DistSquared(GetPos(NodeA), GetPos(NodeB));
		}

		double GetDistSquared(const FNode& A, const FNode& B) const
		{
			return FVector::DistSquared(VtxTransforms[A.PointIndex].GetLocation(), VtxTransforms[B.PointIndex].GetLocation());
		}

		FNode* GetGuidedHalfEdge(const int32 Edge, const FVector& Guide, const FVector& Up = FVector::UpVector) const;

		FNode* GetRoamingNode(const FVector& UVW) const
		{
			return GetNode(FindClosestNode(Bounds.GetCenter() + Bounds.GetExtent() * UVW, EPCGExClusterClosestSearchMode::Edge));
		}

		double EdgeDistToEdge(const FEdge* A, const FEdge* B, FVector& OutP1, FVector& OutP2) const;
		double EdgeDistToEdge(const int32 EdgeA, const int32 EdgeB, FVector& OutP1, FVector& OutP2) const;
		double EdgeDistToEdgeSquared(const FEdge* A, const FEdge* B, FVector& OutP1, FVector& OutP2) const;
		double EdgeDistToEdgeSquared(const int32 EdgeA, const int32 EdgeB, FVector& OutP1, FVector& OutP2) const;
		FVector GetDir(const int32 FromNode, const int32 ToNode) const;
		FVector GetDir(const FNode& From, const FNode& To) const;
		double GetEdgeLength(const FEdge& InEdge) const;
		double GetEdgeLengthSquared(const FEdge& InEdge) const;
		FVector GetEdgeDir(const FEdge& InEdge) const;
		FVector GetEdgeDir(const int32 InEdgeIndex) const;
		FVector GetEdgeDir(const FLink Lk) const;
		FVector GetEdgeDir(const int32 InEdgeIndex, const int32 InStartPtIndex) const;
		FVector GetEdgeDir(const FLink Lk, const int32 InStartPtIndex) const;

		TSharedPtr<PCGExOctree::FItemOctree> GetNodeOctree();
		TSharedPtr<PCGExOctree::FItemOctree> GetEdgeOctree();

		void RebuildNodeOctree();
		void RebuildEdgeOctree();
		void RebuildOctree(EPCGExClusterClosestSearchMode Mode, const bool bForceRebuild = false);

		void GatherNodesPointIndices(TArray<int32>& OutValidNodesPointIndices, const bool bValidity) const;

		int32 FindClosestNode(const FVector& Position, EPCGExClusterClosestSearchMode Mode, const int32 MinNeighbors = 0) const;
		int32 FindClosestNode(const FVector& Position, const int32 MinNeighbors = 0) const;
		int32 FindClosestNodeFromEdge(const FVector& Position, const int32 MinNeighbors = 0) const;

		int32 FindClosestEdge(const int32 InNodeIndex, const FVector& InPosition, const int32 MinNeighbors = 0) const;
		int32 FindClosestNeighbor(const int32 NodeIndex, const FVector& Position, const int32 MinNeighborCount = 1) const;
		int32 FindClosestNeighbor(const int32 NodeIndex, const FVector& Position, const TSet<int32>& Exclusion, const int32 MinNeighborCount = 1) const;

		void ComputeEdgeLengths();

		void GetConnectedNodes(const int32 FromIndex, TArray<int32>& OutIndices, const int32 SearchDepth) const;
		void GetConnectedNodes(const int32 FromIndex, TArray<int32>& OutIndices, const int32 SearchDepth, const TSet<int32>& Skip) const;

		void GetConnectedEdges(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, const int32 SearchDepth) const;
		void GetConnectedEdges(const int32 FromNodeIndex, TArray<int32>& OutNodeIndices, TArray<int32>& OutEdgeIndices, const int32 SearchDepth, const TSet<int32>& SkipNodes, const TSet<int32>& SkipEdges) const;

		FVector GetClosestPointOnEdge(const int32 FromIndex, const int32 ToIndex, const FVector& Position) const;
		FVector GetClosestPointOnEdge(const FEdge& InEdge, const FVector& Position) const;
		FVector GetClosestPointOnEdge(const int32 EdgeIndex, const FVector& Position) const;
		double GetPointDistToEdgeSquared(const FEdge& InEdge, const FVector& Position) const;
		double GetPointDistToEdgeSquared(const int32 EdgeIndex, const FVector& Position) const;

		FVector GetCentroid(const int32 NodeIndex) const;

		void GetValidEdges(TArray<FEdge>& OutValidEdges) const;

		int32 FindClosestNeighborInDirection(const int32 NodeIndex, const FVector& Direction, int32 MinNeighborCount = 1) const;

		TSharedPtr<TArray<FBoundedEdge>> GetBoundedEdges(const bool bBuild);
		void ExpandEdges(PCGExMT::FTaskManager* TaskManager);

		template <typename T, class MakeFunc>
		void GrabNeighbors(const int32 NodeIndex, TArray<T>& OutNeighbors, const MakeFunc&& Make) const
		{
			FNode* Node = (NodesDataPtr + NodeIndex);
			PCGExArrayHelpers::InitArray(OutNeighbors, Node->Num());
			for (int i = 0; i < Node->Num(); i++)
			{
				const FLink Lk = Node->Links[i];
				OutNeighbors[i] = Make(Node, (NodesDataPtr + Lk.Node), (EdgesDataPtr + Lk.Edge));
			}
		}

		template <typename T, class MakeFunc>
		void GrabNeighbors(const FNode& Node, TArray<T>& OutNeighbors, const MakeFunc&& Make) const
		{
			PCGExArrayHelpers::InitArray(OutNeighbors, Node.Num());
			for (int i = 0; i < Node.Num(); i++)
			{
				const FLink Lk = Node.Links[i];
				OutNeighbors[i] = Make((NodesDataPtr + Lk.Node), (EdgesDataPtr + Lk.Edge));
			}
		}

	protected:
		/** Builds Nodes, their links and the node bounds from the already-populated Edges
		 * array, writing NodeIndexLookup along the way. Requires the lookup to hold -1 for
		 * every point index the edges reference; InNumNodesHint must be >= the number of
		 * unique edge endpoints (it is used as allocation size by the parallel path). */
		void BuildAdjacency_Unsafe(const int32 InNumNodesHint);

		int32 GetOrCreateNode_Unsafe(const int32 PointIndex);
	};
}
