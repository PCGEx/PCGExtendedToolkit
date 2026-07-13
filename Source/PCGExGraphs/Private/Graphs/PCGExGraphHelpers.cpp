// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Graphs/PCGExGraphHelpers.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExEdge.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExBufferHelper.h"

namespace PCGExGraphs::Helpers
{
	bool BuildIndexedEdges(const TSharedPtr<PCGExData::FPointIO>& EdgeIO, const TMap<uint32, int32>& EndpointsLookup, TArray<FEdge>& OutEdges, const bool bStopOnError)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExEdge::BuildIndexedEdges-Vanilla);

		const TUniquePtr<PCGExData::TArrayBuffer<int64>> EndpointsBuffer = MakeUnique<PCGExData::TArrayBuffer<int64>>(EdgeIO.ToSharedRef(), PCGExClusters::Labels::Attr_PCGExEdgeIdx);
		if (!EndpointsBuffer->InitForRead())
		{
			return false;
		}

		const TArray<int64>& Endpoints = *EndpointsBuffer->GetInValues().Get();
		const int32 EdgeIOIndex = EdgeIO->IOIndex;

		int8 bValid = 1;
		const int32 NumEdges = EdgeIO->GetNum();

		PCGExArrayHelpers::InitArray(OutEdges, NumEdges);

		if (!bStopOnError)
		{
			int32 EdgeIndex = 0;

			for (int i = 0; i < NumEdges; i++)
			{
				uint32 A;
				uint32 B;
				PCGEx::H64(Endpoints[i], A, B);

				const int32* StartPointIndexPtr = EndpointsLookup.Find(A);
				const int32* EndPointIndexPtr = EndpointsLookup.Find(B);

				if ((!StartPointIndexPtr || !EndPointIndexPtr))
				{
					continue;
				}

				OutEdges[EdgeIndex] = FEdge(EdgeIndex, *StartPointIndexPtr, *EndPointIndexPtr, i, EdgeIOIndex);
				EdgeIndex++;
			}

			PCGExArrayHelpers::InitArray(OutEdges, EdgeIndex);
		}
		else
		{
			PCGExMT::ParallelOrSequential(
				NumEdges,
				[&](const int32 i)
				{
					if (!bValid)
					{
						return;
					}

					uint32 A;
					uint32 B;
					PCGEx::H64(Endpoints[i], A, B);

					const int32* StartPointIndexPtr = EndpointsLookup.Find(A);
					const int32* EndPointIndexPtr = EndpointsLookup.Find(B);

					if ((!StartPointIndexPtr || !EndPointIndexPtr))
					{
						FPlatformAtomics::InterlockedExchange(&bValid, 0);
						return;
					}

					OutEdges[i] = FEdge(i, *StartPointIndexPtr, *EndPointIndexPtr, i, EdgeIOIndex);
				});
		}

		return static_cast<bool>(bValid);
	}

	bool BuildEndpointsLookup(const TSharedPtr<PCGExData::FPointIO>& InPointIO, TMap<uint32, int32>& OutIndices, TArray<int32>& OutAdjacency)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGraph::BuildLookupTable);

		PCGExArrayHelpers::InitArray(OutAdjacency, InPointIO->GetNum());
		OutIndices.Empty();

		const TUniquePtr<PCGExData::TArrayBuffer<int64>> IndexBuffer = MakeUnique<PCGExData::TArrayBuffer<int64>>(InPointIO.ToSharedRef(), PCGExClusters::Labels::Attr_PCGExVtxIdx);
		if (!IndexBuffer->InitForRead())
		{
			return false;
		}

		const TArray<int64>& Indices = *IndexBuffer->GetInValues().Get();

		OutIndices.Reserve(Indices.Num());
		for (int i = 0; i < Indices.Num(); i++)
		{
			uint32 A;
			uint32 B;
			PCGEx::H64(Indices[i], A, B);

			OutIndices.Add(A, i);
			OutAdjacency[i] = B;
		}

		return true;
	}
}
