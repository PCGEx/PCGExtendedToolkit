// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExMT.h"

struct FPCGExTransformDetails;
struct FPCGExAttributeToTagDetails;

namespace PCGExData
{
	class FDataForwardHandler;
	class FPointIO;
	class FPointIOCollection;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExGraphTask
{
	/**
	 * Duplicates a COMPILED graph builder's cluster (its vtx data + every edges data) onto one target
	 * point, re-tagging the copy with a fresh pair id and transforming both halves with the same target
	 * index. The shared tail of every "spawn this cluster at N points" node.
	 *
	 * The source builder is only ever read, so one builder feeds any number of these concurrently.
	 */
	class PCGEXGRAPHS_API FCopyGraphToPoint final : public PCGExMT::FPCGExIndexedTask
	{
	public:
		FCopyGraphToPoint(
			const int32 InTaskIndex,
			const TSharedPtr<PCGExData::FPointIO>& InPointIO,
			const TSharedPtr<PCGExGraphs::FGraphBuilder>& InGraphBuilder,
			const TSharedPtr<PCGExData::FPointIOCollection>& InVtxCollection,
			const TSharedPtr<PCGExData::FPointIOCollection>& InEdgeCollection,
			FPCGExTransformDetails* InTransformDetails,
			const FPCGExAttributeToTagDetails* InAttributesToTags = nullptr,
			const TSharedPtr<PCGExData::FDataForwardHandler>& InForwardHandler = nullptr)
			: FPCGExIndexedTask(InTaskIndex)
			  , PointIO(InPointIO)
			  , GraphBuilder(InGraphBuilder)
			  , VtxCollection(InVtxCollection)
			  , EdgeCollection(InEdgeCollection)
			  , TransformDetails(InTransformDetails)
			  , AttributesToTags(InAttributesToTags)
			  , ForwardHandler(InForwardHandler)
		{
		}

		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		TSharedPtr<PCGExData::FPointIOCollection> VtxCollection;
		TSharedPtr<PCGExData::FPointIOCollection> EdgeCollection;

		FPCGExTransformDetails* TransformDetails = nullptr;

		/** Optional: stamps target attributes onto the copies as tags. Read-only, shared across tasks. */
		const FPCGExAttributeToTagDetails* AttributesToTags = nullptr;

		/** Optional: forwards target attributes onto the copied vtx metadata. */
		TSharedPtr<PCGExData::FDataForwardHandler> ForwardHandler;


		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;
	};
}
