// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExGraphCommon.h"
#include "PCGExH.h"
#include "Clusters/PCGExEdge.h"

namespace PCGExBlending
{
	class FUnionBlender;
}

namespace PCGExMT
{
	struct FScope;
	class FTaskManager;
	class IAsyncHandleGroup;
}

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExData
{
	class FFacade;

	template <typename T>
	class TBuffer;
}

namespace PCGExGraphs
{
	struct FNode;
	struct FGraphMetadataDetails;
	class FGraph;

	class PCGEXGRAPHS_API FSubGraph : public TSharedFromThis<FSubGraph>
	{
	public:
		TWeakPtr<FGraph> WeakParentGraph;
		TArray<int32> Nodes;
		TArray<PCGEx::FIndexKey> Edges;
		TSet<int32> EdgesInIOIndices;
		TSharedPtr<PCGExData::FFacade> VtxDataFacade;
		TSharedPtr<PCGExData::FFacade> EdgesDataFacade;

		/**
		 * Output-space edges built by Compile (1:1 with Edges). Start/End are OUTPUT vtx
		 * point indices (post-prune/reorder); IOIndex is the parent-graph edge index.
		 * Immutable once Compile has run -- read concurrently by cluster build and callbacks.
		 */
		TArray<FEdge> FlattenedEdges;
		int32 UID = 0;
		int32 MinPointIndex = MAX_int32;

		/** Legacy callback - prefer context-based callbacks for new code */
		FSubGraphPostProcessCallback OnSubGraphPostProcess;

		/** Context-based callbacks for advanced subgraph processing */
		FCreateSubGraphContextCallback OnCreateContext;
		FSubGraphPreCompileCallback OnPreCompile;
		FSubGraphPostCompileCallback OnPostCompile;


		FSubGraph() = default;

		~FSubGraph() = default;

		void Add(const FEdge& Edge);
		void Shrink();
		void ComputeMinPointIndex(const TArray<FNode>& ParentNodes);

		void BuildCluster(const TSharedRef<PCGExClusters::FCluster>& InCluster);
		int32 GetFirstInIOIndex();

		void Compile(const TWeakPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<FGraphBuilder>& InBuilder);

		TSharedPtr<FGraphBuilder> GetBuilder() const
		{
			return WeakBuilder.Pin();
		}

	protected:
		TWeakPtr<PCGExMT::FTaskManager> WeakTaskManager;
		TWeakPtr<FGraphBuilder> WeakBuilder;

		const FGraphMetadataDetails* MetadataDetails = nullptr;

		TSharedPtr<PCGExBlending::FUnionBlender> UnionBlender;

		/** User-defined context created by OnCreateContext, shared between PreCompile and PostCompile */
		TSharedPtr<FSubGraphUserContext> UserContext;

		// Edge metadata
#define PCGEX_FOREACH_EDGE_METADATA(MACRO)\
MACRO(IsEdgeUnion, bool, false, IsUnion()) \
MACRO(IsSubEdge, bool, false, bIsSubEdge) \
MACRO(EdgeUnionSize, int32, 0, UnionSize)

#define PCGEX_EDGE_METADATA_DECL(_NAME, _TYPE, _DEFAULT, _ACCESSOR) TSharedPtr<PCGExData::TBuffer<_TYPE>> _NAME##Buffer;
		PCGEX_FOREACH_EDGE_METADATA(PCGEX_EDGE_METADATA_DECL)
#undef PCGEX_EDGE_METADATA_DECL

#undef PCGEX_FOREACH_EDGE_METADATA

		// Extra edge data
		TSharedPtr<PCGExData::TBuffer<double>> EdgeLength;
		TSharedPtr<PCGExData::TBuffer<int64>> EdgeEndpointsWriter;

		void CompileRange(const PCGExMT::FScope& Scope);
		void CompilationComplete();
	};
}
