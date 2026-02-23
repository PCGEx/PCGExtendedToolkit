// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExOctree.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExOpStats.h"

#include "Data/PCGExPointElements.h"
#include "Clusters/PCGExEdge.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExFuseDetails.h"
#include "Utils/PCGValueRange.h"

struct FPCGExEdgeEdgeIntersectionDetails;
struct FPCGExPointEdgeIntersectionDetails;

namespace PCGExBlending
{
	class FMetadataBlender;
}

namespace PCGExData
{
	class FPointIOCollection;
	class FUnionMetadata;
	class IUnionData;
}

enum class EPCGExCutType : uint8;

namespace PCGExMath
{
	struct FCut;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

namespace PCGExGraphs
{
	class FEdgeProxy;
	class FEdgeEdgeIntersections;
	class FGraph;
	struct FEdge;

#pragma region Compound Graph

	class PCGEXGRAPHS_API FUnionNode : public TSharedFromThis<FUnionNode>
	{
	public:
		const PCGExData::FConstPoint Point;
		FVector Center;
		FBoxSphereBounds Bounds;
		int32 Index;

		FVector CenterAccum;
		int32 FuseCount = 1;

		FUnionNode(const PCGExData::FConstPoint& InPoint, const FVector& InCenter, const int32 InIndex);
		~FUnionNode() = default;

		FORCEINLINE FVector GetCenter() const { return CenterAccum / static_cast<double>(FuseCount); }
		FORCEINLINE void Accumulate(const FVector& Position) { CenterAccum += Position; FuseCount++; }
	};

	PCGEX_OCTREE_SEMANTICS(FUnionNode, { return Element->Bounds;}, { return A->Index == B->Index; })

	class PCGEXGRAPHS_API FUnionGraph : public TSharedFromThis<FUnionGraph>
	{
		int32 NumCollapsedEdges = 0;

	public:
		PCGExMT::TH64MapShards<int32> NodeBinsShards;

		TWeakPtr<PCGExData::FPointIOCollection> SourceCollection = nullptr;
		TSharedPtr<PCGExData::FUnionMetadata> NodesUnion;
		TSharedPtr<PCGExData::FUnionMetadata> EdgesUnion;
		TArray<TSharedPtr<FUnionNode>> Nodes;

		PCGExMT::TH64MapShards<int32> EdgesMapShards;
		TArray<FEdge> Edges;

		FPCGExFuseDetails FuseDetails;

		FBox Bounds;

		bool bNodesSorted = false;

		TUniquePtr<FUnionNodeOctree> Octree;

		mutable FRWLock UnionLock;
		mutable FRWLock EdgesLock;

		explicit FUnionGraph(const FPCGExFuseDetails& InFuseDetails, const FBox& InBounds, const TSharedPtr<PCGExData::FPointIOCollection>& InSourceCollection = nullptr);

		~FUnionGraph() = default;

		bool Init(FPCGExContext* InContext);
		bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InUniqueSourceFacade, const bool SupportScopedGet);

		void Reserve(const int32 NodeReserve, const int32 EdgeReserve);

		FORCEINLINE int32 GetNumCollapsedEdges() const { return NumCollapsedEdges; }
		FORCEINLINE bool RequiresSequentialInsertion() const { return Octree != nullptr; }

		int32 InsertPoint(const PCGExData::FConstPoint& Point);

		void InsertEdge(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, const PCGExData::FConstPoint& Edge = PCGExData::NONE_ConstPoint);

		void WriteNodeMetadata(const TSharedPtr<FGraph>& InGraph) const;
		void WriteEdgeMetadata(const TSharedPtr<FGraph>& InGraph) const;

		void Collapse();

		/** RAII batch inserter for sequential use. Holds both locks for the lifetime,
		 *  avoiding per-element lock overhead when inserting from a single thread. */
		class PCGEXGRAPHS_API FBatchInserter
		{
			FUnionGraph& Graph;
			FWriteScopeLock UnionLk;
			FWriteScopeLock EdgesLk;

		public:
			explicit FBatchInserter(FUnionGraph& InGraph)
				: Graph(InGraph), UnionLk(InGraph.UnionLock), EdgesLk(InGraph.EdgesLock)
			{
			}

			int32 InsertPoint(const PCGExData::FConstPoint& Point);
			void InsertEdge(const PCGExData::FConstPoint& From,
			                const PCGExData::FConstPoint& To,
			                const PCGExData::FConstPoint& Edge = PCGExData::NONE_ConstPoint);
		};
	};

#pragma endregion

	class PCGEXGRAPHS_API FIntersectionCache : public TSharedFromThis<FIntersectionCache>
	{
	public:
		TConstPCGValueRange<FTransform> NodeTransforms;
		const TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<FGraph> Graph;

		TBitArray<> ValidEdges;
		TArray<double> LengthSquared;
		TArray<FVector> Positions;
		TArray<FVector> Directions;
		TSharedPtr<PCGExOctree::FItemOctree> Octree;

		double ToleranceSquared = 10;

		FIntersectionCache(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO);

		bool InitProxy(const TSharedPtr<FEdgeProxy>& Edge, const int32 Index) const;

	protected:
		double Tolerance = 10;
		void BuildCache();
	};

	/** Base proxy for edge intersection detection.
	 *  Not polymorphic -- derived types (FPointEdgeProxy, FEdgeEdgeProxy) are always used
	 *  through concrete TSharedPtr, never through base pointers. InitProxy accepts
	 *  TSharedPtr<FEdgeProxy> for convenience but only calls the non-virtual Init(). */
	class PCGEXGRAPHS_API FEdgeProxy
	{
	public:
		~FEdgeProxy() = default;
		int32 Index = -1;
		int32 Start = 0;
		int32 End = 0;
		FBox Box = FBox(NoInit);

		FEdgeProxy() = default;

		void Init(const FEdge& InEdge, const FVector& InStart, const FVector& InEnd, const double Tolerance);
	};

#pragma region Point Edge intersections

	struct PCGEXGRAPHS_API FPESplit
	{
		int32 Index = -1;
		double Time = -1;
		FVector ClosestPoint = FVector::ZeroVector;

		bool operator==(const FPESplit& Other) const { return Index == Other.Index; }
	};

	class PCGEXGRAPHS_API FPointEdgeProxy : public FEdgeProxy
	{
	public:
		TArray<FPESplit, TInlineAllocator<8>> CollinearPoints;

		bool FindSplit(const int32 PointIndex, const TSharedPtr<FIntersectionCache>& Cache, FPESplit& OutSplit) const;
		void Add(const FPESplit& Split);

		bool IsEmpty() const { return CollinearPoints.IsEmpty(); }
	};

	class PCGEXGRAPHS_API FPointEdgeIntersections : public FIntersectionCache
	{
	public:
		const FPCGExPointEdgeIntersectionDetails* Details;
		TSharedPtr<PCGExMT::TScopedArray<TSharedPtr<FPointEdgeProxy>>> ScopedEdges;
		TArray<TSharedPtr<FPointEdgeProxy>> Edges;

		FPointEdgeIntersections(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const FPCGExPointEdgeIntersectionDetails* InDetails);

		void Init(const TArray<PCGExMT::FScope>& Loops);
		void InsertEdges();
		void BlendIntersection(const int32 Index, PCGExBlending::FMetadataBlender* Blender) const;

		~FPointEdgeIntersections() = default;
	};

	/** Find graph nodes that are collinear with (lie on) the given edge within tolerance.
	 *  When bEnableSelfIntersection is false, nodes that share an IO source with the
	 *  edge's root are skipped -- preventing edges from being split by their own cluster's nodes. */
	void FindCollinearNodes(const TSharedPtr<FPointEdgeIntersections>& InIntersections, const TSharedPtr<FPointEdgeProxy>& EdgeProxy, bool bEnableSelfIntersection);

#pragma endregion

#pragma region Edge Edge intersections

	struct PCGEXGRAPHS_API FEESplit
	{
		FEESplit() = default;

		int32 A = -1;
		int32 B = -1;
		double TimeA = -1;
		double TimeB = -1;
		FVector Center = FVector::ZeroVector;

		FORCEINLINE uint64 H64U() const { return PCGEx::H64U(A, B); }
	};

	struct PCGEXGRAPHS_API FEECrossing
	{
		int32 Index = -1;
		FEESplit Split;

		FEECrossing() = default;

		FORCEINLINE double GetTime(const int32 EdgeIndex) const
		{
			return EdgeIndex == Split.A ? Split.TimeA : Split.TimeB;
		}

		bool operator==(const FEECrossing& Other) const { return Index == Other.Index; }
	};

	class PCGEXGRAPHS_API FEdgeEdgeProxy : public FEdgeProxy
	{
	public:
		TArray<FEECrossing> Crossings;

		bool FindSplit(const FEdge& OtherEdge, const TSharedPtr<FIntersectionCache>& Cache);
		bool IsEmpty() const { return Crossings.IsEmpty(); }
	};

	class PCGEXGRAPHS_API FEdgeEdgeIntersections : public FIntersectionCache
	{
	public:
		const FPCGExEdgeEdgeIntersectionDetails* Details;
		TSharedPtr<PCGExMT::TScopedArray<TSharedPtr<FEdgeEdgeProxy>>> ScopedEdges;

		TArray<FEECrossing> UniqueCrossings;
		TArray<TSharedPtr<FEdgeEdgeProxy>> Edges;

		FEdgeEdgeIntersections(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<FUnionGraph>& InUnionGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO, const FPCGExEdgeEdgeIntersectionDetails* InDetails);

		void Init(const TArray<PCGExMT::FScope>& Loops);
		void Collapse(const int32 InReserve);

		bool InsertNodes(const int32 InReserve);
		void InsertEdges();

		void BlendIntersection(const int32 Index, const TSharedRef<PCGExBlending::FMetadataBlender>& Blender, TArray<PCGEx::FOpStats>& Trackers) const;

		~FEdgeEdgeIntersections() = default;
	};

	/** Find edges that cross the given edge within tolerance, recording crossings in the proxy.
	 *  When bEnableSelfIntersection is false, edges whose root metadata shares an IO source
	 *  with this edge's root are skipped -- preventing intra-cluster edge crossings. */
	void FindOverlappingEdges(const TSharedPtr<FEdgeEdgeIntersections>& InIntersections, const TSharedPtr<FEdgeEdgeProxy>& EdgeProxy, bool bEnableSelfIntersection);

#pragma endregion
}
