// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "PCGExGraphCommon.h"
#include "Data/PCGExDataCommon.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Utils/PCGValueRange.h"

namespace PCGEx
{
	class FIndexLookup;
}

namespace PCGExData
{
	class FPointIOCollection;
	class FFacade;
}

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExGraphs
{
	struct FGraphMetadataDetails;
	class FGraph;

	class PCGEXGRAPHS_API FGraphBuilder : public TSharedFromThis<FGraphBuilder>
	{
	protected:
		TSharedPtr<PCGExMT::FTaskManager> TaskManager;
		const FGraphMetadataDetails* MetadataDetailsPtr = nullptr;
		bool bWriteVtxDataFacadeWithCompile = false;
		bool bCompiling = false;

	public:
		const FPCGExGraphBuilderDetails* OutputDetails = nullptr;

		FGraphCompilationEndCallback OnCompilationEndCallback;

		/** Legacy callback - prefer context-based callbacks for new code */
		FSubGraphPostProcessCallback OnSubGraphPostProcess;

		/** Context-based callbacks for advanced subgraph processing */
		FCreateSubGraphContextCallback OnCreateContext;
		FSubGraphPreCompileCallback OnPreCompile;
		FSubGraphPostCompileCallback OnPostCompile;

		PCGExDataId PairId;
		TSharedPtr<FGraph> Graph;

		TSharedRef<PCGExData::FFacade> NodeDataFacade;
		TSharedPtr<PCGEx::FIndexLookup> NodeIndexLookup;

		// The collection of edges given to the node
		// We need the full collection even if unrelated, because we track data by index
		// and those indices are relative to the input data, not the graph context
		TSharedPtr<PCGExData::FPointIOCollection> EdgesIO;
		const TArray<TSharedRef<PCGExData::FFacade>>* SourceEdgeFacades = nullptr;

		// Used exclusively by the custom graph builder.
		// Otherwise a transient array is allocated for the duration of the graph compilation
		TSharedPtr<TArray<int32>> OutputNodeIndices;
		TSharedPtr<TArray<int32>> OutputPointIndices;

		// A value range we will fetch positions from during compilation
		// It must have a valid range for Node.PointIndex
		TConstPCGValueRange<FTransform> NodePointsTransforms;

		// This is true by default, but should be disabled for edge cases where we create new points from scratch
		// especially if the final amount of points is greater than the number of points we're trying to inherit from.
		bool bInheritNodeData = true;

		// This will be set to true post-graph compilation, if compilation was a success
		bool bCompiledSuccessfully = false;

		// When true, edge sort keys will be recomputed using remapped node indices before sorting.
		// Required for union graphs where node indices are non-deterministic due to parallel insertion.
		bool bRequiresEdgeResort = false;

		// Subgraph edge keys are sorted before compilation so the output edge order is
		// deterministic. Nodes whose edge insertion order is itself deterministic (e.g.
		// sequential, single-threaded inserts) can disable this and keep insertion order,
		// skipping the sort entirely. Ignored when bRequiresEdgeResort is set.
		bool bSortEdgeKeys = true;

		// When true, nodes have been pre-sorted spatially at Collapse() time.
		// Skip the Morton sort in Compile() since spatial ordering is already established.
		bool bNodesPreSorted = false;

		FGraphBuilder(const TSharedRef<PCGExData::FFacade>& InNodeDataFacade, const FPCGExGraphBuilderDetails* InDetails);

		const FGraphMetadataDetails* GetMetadataDetails() const
		{
			return MetadataDetailsPtr;
		}

		void CompileAsync(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const bool bWriteNodeFacade, const FGraphMetadataDetails* MetadataDetails = nullptr);
		void Compile(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager, const bool bWriteNodeFacade, const FGraphMetadataDetails* MetadataDetails = nullptr);

	protected:
		void OnCompilationEnd();

	public:
		void StageEdgesOutputs() const;
		void MoveEdgesOutputs(const TSharedPtr<PCGExData::FPointIOCollection>& To) const;

		~FGraphBuilder() = default;
	};
}
