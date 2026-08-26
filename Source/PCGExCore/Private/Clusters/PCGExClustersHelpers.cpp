// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/PCGExClustersHelpers.h"

#include "PCGExCoreSettingsCache.h"
#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Paths/PCGExPathsCommon.h"

namespace PCGExClusters::Helpers
{
	void SetClusterVtx(const TSharedPtr<PCGExData::FPointIO>& IO, PCGExDataId& OutId)
	{
		OutId = IO->Tags->Set<int64>(Labels::TagStr_PCGExCluster, IO->GetOutIn()->GetUniqueID());
		IO->Tags->AddRaw(Labels::TagStr_PCGExVtx);
		IO->Tags->Remove(Labels::TagStr_PCGExEdges);
	}

	void MarkClusterVtx(const TSharedPtr<PCGExData::FPointIO>& IO, const PCGExDataId& Id)
	{
		if (!Id)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Missing or malformed '%s' pair tag on input #%d ('%s') -- vtx left unmarked."), *Labels::TagStr_PCGExCluster, IO->IOIndex, *GetNameSafe(IO->GetOutIn()));
			return;
		}

		IO->Tags->Set(Labels::TagStr_PCGExCluster, Id);
		IO->Tags->AddRaw(Labels::TagStr_PCGExVtx);
		IO->Tags->Remove(Labels::TagStr_PCGExEdges);
		IO->DeleteAttribute(PCGExPaths::Labels::ClosedLoopIdentifier);
	}

	void MarkClusterEdges(const TSharedPtr<PCGExData::FPointIO>& IO, const PCGExDataId& Id)
	{
		if (!Id)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Missing or malformed '%s' pair tag on input #%d ('%s') -- edges left unmarked."), *Labels::TagStr_PCGExCluster, IO->IOIndex, *GetNameSafe(IO->GetOutIn()));
			return;
		}

		IO->Tags->Set(Labels::TagStr_PCGExCluster, Id);
		IO->Tags->AddRaw(Labels::TagStr_PCGExEdges);
		IO->Tags->Remove(Labels::TagStr_PCGExVtx);
		IO->DeleteAttribute(PCGExPaths::Labels::ClosedLoopIdentifier);
	}

	void MarkClusterEdges(const TArrayView<TSharedRef<PCGExData::FPointIO>> Edges, const PCGExDataId& Id)
	{
		for (const TSharedRef<PCGExData::FPointIO>& IO : Edges)
		{
			MarkClusterEdges(IO, Id);
		}
	}

	bool IsPointDataVtxReady(const UPCGMetadata* Metadata)
	{
		return PCGExMetaHelpers::TryGetConstAttribute<int64>(Metadata, Labels::Attr_PCGExVtxIdx) ? true : false;
	}

	bool IsPointDataEdgeReady(const UPCGMetadata* Metadata)
	{
		return PCGExMetaHelpers::TryGetConstAttribute<int64>(Metadata, Labels::Attr_PCGExEdgeIdx) ? true : false;
	}

	void CleanupVtxData(const TSharedPtr<PCGExData::FPointIO>& PointIO)
	{
		if (!PointIO->GetOut())
		{
			return;
		}
		UPCGMetadata* Metadata = PointIO->GetOut()->MutableMetadata();
		PointIO->Tags->Remove(Labels::TagStr_PCGExCluster);
		PointIO->Tags->Remove(Labels::TagStr_PCGExVtx);
		Metadata->DeleteAttribute(Labels::Attr_PCGExVtxIdx);
		Metadata->DeleteAttribute(Labels::Attr_PCGExEdgeIdx);
	}

	void CleanupEdgeData(const TSharedPtr<PCGExData::FPointIO>& PointIO)
	{
		if (!PointIO->GetOut())
		{
			return;
		}
		UPCGMetadata* Metadata = PointIO->GetOut()->MutableMetadata();
		PointIO->Tags->Remove(Labels::TagStr_PCGExCluster);
		PointIO->Tags->Remove(Labels::TagStr_PCGExEdges);
		Metadata->DeleteAttribute(Labels::Attr_PCGExVtxIdx);
		Metadata->DeleteAttribute(Labels::Attr_PCGExEdgeIdx);
	}

	void CleanupClusterData(const TSharedPtr<PCGExData::FPointIO>& PointIO)
	{
		CleanupVtxData(PointIO);
		CleanupEdgeData(PointIO);
		CleanupClusterTags(PointIO);
	}

	void CleanupClusterTags(const TSharedPtr<PCGExData::FPointIO>& IO, const bool bKeepPairTag)
	{
		IO->Tags->Remove(Labels::TagStr_PCGExVtx);
		IO->Tags->Remove(Labels::TagStr_PCGExEdges);
		if (!bKeepPairTag)
		{
			IO->Tags->Remove(Labels::TagStr_PCGExCluster);
		}
	}

	void GetAdjacencyData(const FCluster* InCluster, FNode& InNode, TArray<FAdjacencyData>& OutData)
	{
		const int32 NumAdjacency = InNode.Num();
		const FVector NodePosition = InCluster->GetPos(InNode);
		OutData.Reserve(NumAdjacency);
		for (int i = 0; i < NumAdjacency; i++)
		{
			const FLink Lk = InNode.Links[i];

			const FNode* OtherNode = InCluster->NodesDataPtr + Lk.Node;
			const FVector OtherPosition = InCluster->GetPos(OtherNode);

			FAdjacencyData& Data = OutData.Emplace_GetRef();
			Data.NodeIndex = Lk.Node;
			Data.NodePointIndex = OtherNode->PointIndex;
			Data.EdgeIndex = Lk.Edge;
			(NodePosition - OtherPosition).ToDirectionAndLength(Data.Direction, Data.Length);
		}
	}

	TSharedPtr<FCluster> TryGetCachedCluster(const TSharedRef<PCGExData::FPointIO>& VtxIO, const TSharedRef<PCGExData::FPointIO>& EdgeIO)
	{
		if (PCGEX_CORE_SETTINGS.bCacheClusters)
		{
			if (const UPCGExClusterEdgesData* ClusterEdgesData = Cast<UPCGExClusterEdgesData>(EdgeIO->GetIn()))
			{
				//Try to fetch cached cluster
				if (const TSharedPtr<FCluster>& CachedCluster = ClusterEdgesData->GetBoundCluster())
				{
					// Cheap validation -- if there are artifact use SanitizeCluster node, it's still incredibly cheaper.
					if (CachedCluster->IsValidWith(VtxIO, EdgeIO))
					{
						return CachedCluster;
					}
				}
			}
		}

		return nullptr;
	}
}
